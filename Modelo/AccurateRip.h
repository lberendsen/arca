// ARCA no xOS - Modelo/AccurateRip (de Modelo\AccurateRip.cs).
// TTocAudio, TFaixaAccurateRip, TResultadoAccurateRip, IFonteDisco e AccurateRip.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ARCA::Modelo {

/// Indice de audio de um CD: onde comeca cada faixa de audio (LBA, a partir de 0) e onde o audio
/// acaba. E o que identifica o disco no AccurateRip e diz onde cortar cada faixa.
class TTocAudio
{
public:
    static constexpr int AmostrasPorSetor = 588;          // 2352 bytes / 4 (16 bits, estereo)
    static constexpr int GapSessaoCdExtra = 11400;        // lead-out + lead-in entre as sessoes

    std::vector<int> lstInicios;
    int intLeadOut = 0;                                   // fim do audio (LBA)
    int intPrimeiraFaixa = 1;                             // numero da primeira faixa
    /// Inicio da faixa de dados (CD Extra), ou -1. Entra so no ID do freedb.
    int intInicioDados = -1;
    /// Lead-out fisico do disco, quando ha sessao de dados depois do audio.
    int intLeadOutFisico = -1;
    /// Inicio da faixa de dados que vem ANTES do audio (CD mixed-mode), ou -1.
    int intDadosAntes = -1;

    int QtdFaixas() const { return (int) lstInicios.size(); }

    int FimDaFaixa(int i) const { return i + 1 < (int) lstInicios.size() ? lstInicios[i + 1] : intLeadOut; }

    int64_t AmostrasDaFaixa(int i) const { return (int64_t) (FimDaFaixa(i) - lstInicios[i]) * AmostrasPorSetor; }

    /// O WM/MCDI que o Windows Media Player grava ao ripar: texto como "A+96+5C1C+...+2E55" - a
    /// quantidade de faixas e, em hexadecimal, o endereco de cada uma e o do lead-out, contando os
    /// 150 setores do pre-gap. intFaixasDeAudio diz quantas sao audio (CD Extra tem uma faixa de
    /// dados a mais no fim); 0 = todas. nullptr se o texto nao for um MCDI.
    static std::shared_ptr<TTocAudio> DeMcdi(const std::string &strMcdi, int intFaixasDeAudio);

    /// O indice a partir do AudioCD.cue da extracao: INDEX 01 de cada faixa AUDIO (as posicoes do
    /// CUE contam do LBA 0 do disco), o REM LEAD-OUT da sessao de audio e, em CD Extra, a faixa de
    /// dados e o REM LEAD-OUT-SESSION2. Sem lead-out no CUE, o tamanho do .bin. nullptr se nao der.
    static std::shared_ptr<TTocAudio> DeCue(const std::string &strCue, int64_t lngBytesBin);

    /// O indice no formato WM/MCDI do Windows Media Player ("A+96+5C1C+..."), para gravar nas
    /// faixas: e o que permite conferir o album no AccurateRip anos depois, sem o disco.
    std::string ParaMcdi() const;

private:
    static int Msf(const std::string &s);
};

/// Uma faixa conferida: as duas assinaturas calculadas e o que a base disse delas.
class TFaixaAccurateRip
{
public:
    int intNumero = 0;
    uint32_t uintV1 = 0, uintV2 = 0;
    /// Quantas pessoas riparam a faixa com exatamente estes numeros (soma das prensagens que
    /// conferem). 0 = nao confere.
    int intConfianca = 0;
    /// Quantas submissoes a base tem para esta faixa, conferindo ou nao.
    int intTotalNaBase = 0;
    std::string strVersao;   // "v2", "v1" ou "" (null no original)

    bool Confere() const { return intConfianca > 0; }
};

class TResultadoAccurateRip
{
public:
    /// O disco existe na base?
    bool blnNaBase = false;
    /// Deslocamento, em amostras, em que as faixas foram conferidas (0 = como estao).
    int intOffset = 0;
    std::string strUrl;
    std::string strFalha;     // "" = null no original
    std::vector<TFaixaAccurateRip> lstFaixas;

    int QtdConferem() const
    {
        int n = 0;
        for (auto &f : lstFaixas)
            if (f.Confere()) n++;
        return n;
    }
    bool TodasConferem() const { return !lstFaixas.empty() && QtdConferem() == (int) lstFaixas.size(); }
};

/// Fonte de amostras de um disco inteiro, em sequencia, como uint por amostra estereo (esquerdo
/// nos 16 bits baixos, direito nos altos - a ordem em que os bytes vem do CD). A posicao 0 e o
/// LBA indicado por LbaInicial.
class IFonteDisco
{
public:
    virtual ~IFonteDisco() = default;
    virtual int LbaInicial() const = 0;
    virtual int64_t TotalAmostras() const = 0;
    virtual void Reiniciar() = 0;
    /// Le ate intMax amostras; 0 no fim. Lanca em erro.
    virtual int Ler(uint32_t *arrDestino, int intMax) = 0;
};

/// AccurateRip: a base publica em que milhares de pessoas registraram a assinatura de cada faixa
/// que riparam. Se a sua confere, a leitura do disco esta certa - e a prova que nenhuma checagem
/// local da, porque um drive pode entregar um trecho estimado sem acusar erro.
///
/// As duas assinaturas (v1 e v2) sao somas das amostras ponderadas pela posicao na faixa; as 5
/// primeiras setores da primeira faixa e os 5 ultimos da ultima ficam de fora (drives diferem nas
/// pontas do disco).
///
/// O deslocamento (offset) do drive desloca todas as amostras por um numero fixo; a mesma musica
/// lida por dois drives sai "igual, mas deslocada". Aqui ele e achado em vez de exigido: a v1 de
/// uma faixa do meio e recalculada para cada deslocamento possivel em tempo constante por passo, e
/// onde bate com a base, as outras faixas sao conferidas.
class AccurateRip
{
public:
    static constexpr int AmostrasPontas = 5 * TTocAudio::AmostrasPorSetor;   // 2940
    /// Ate onde procurar o deslocamento, em amostras para cada lado (~0,14 s).
    static constexpr int AlcanceBusca = 6000;

    // ---- Identificacao do disco e consulta a base ----
    static void CalcularIds(const TTocAudio &objToc, uint32_t &id1, uint32_t &id2, uint32_t &idFreedb);
    static std::string Url(const TTocAudio &objToc);

    /// Uma prensagem registrada: confianca e assinatura de cada faixa.
    struct TPrensagem
    {
        std::vector<int> arrConfianca;
        std::vector<uint32_t> arrCrc;
    };

    /// As prensagens que a base tem para o disco. Lista vazia = disco nao esta na base (nao e
    /// erro). nullopt (null no original) = nao consegui consultar (strFalha).
    static std::optional<std::vector<TPrensagem>> Consultar(const TTocAudio &objToc, std::string &strUrl,
                                                            std::string &strFalha);

    // ---- Assinaturas ----
    /// v1 e v2 de todas as faixas, para cada deslocamento pedido, numa passada so pela fonte. A
    /// amostra i da faixa t, no deslocamento k, e a da posicao (inicio de t) + k + i do disco; fora
    /// do disco, zero. arrV1[o][t]: deslocamento o, faixa t. Lanca OperationCanceledException.
    static void Calcular(IFonteDisco &objFonte, const TTocAudio &objToc, const std::vector<int> &arrOffsets,
                         std::vector<std::vector<uint32_t>> &arrV1, std::vector<std::vector<uint32_t>> &arrV2,
                         const std::function<bool()> &cancelou);

    /// Procura o deslocamento: guarda uma faixa do meio com folga de AlcanceBusca de cada lado e
    /// recalcula a v1 dela para cada deslocamento k por recorrencia -
    ///   v1(k+1) = v1(k) - W(k) + n*s[ini+n+k],   W(k+1) = W(k) - s[ini+k] + s[ini+n+k]
    /// (W e a soma simples da janela). Devolve os k em que a v1 bate com alguma prensagem, do mais
    /// proximo de zero ao mais longe.
    static std::vector<int> ProcurarOffsets(IFonteDisco &objFonte, const TTocAudio &objToc,
                                            const std::vector<TPrensagem> *lstBase,
                                            const std::function<bool()> &cancelou);

    /// Confere o disco: primeiro como esta (deslocamento 0); se nao conferir tudo, procura o
    /// deslocamento e confere nele.
    /// arrConhecidos: deslocamentos a tentar ja na primeira passada, alem do 0 - o offset do drive.
    /// A busca acha so o que a base tem em v1; disco registrado so em v2 e encontrado por aqui.
    static TResultadoAccurateRip Conferir(IFonteDisco &objFonte, const TTocAudio &objToc,
                                          const std::vector<int> &arrConhecidos,
                                          const std::function<bool()> &cancelou);

private:
    static std::vector<TPrensagem> Interpretar(const std::vector<uint8_t> &arr, const TTocAudio &objToc,
                                               std::string &strFalha);
    static TResultadoAccurateRip Melhor(const TResultadoAccurateRip &rAtual, const TTocAudio &objToc,
                                        const std::vector<TPrensagem> &lstBase,
                                        const std::vector<std::vector<uint32_t>> &v1,
                                        const std::vector<std::vector<uint32_t>> &v2, const std::vector<int> &arrK,
                                        const std::string &strUrl);
    static void Preencher(TResultadoAccurateRip &r, const TTocAudio &objToc, const std::vector<TPrensagem> &lstBase,
                          const std::vector<std::vector<uint32_t>> &v1, const std::vector<std::vector<uint32_t>> &v2,
                          int o, int k);
};

} // namespace ARCA::Modelo

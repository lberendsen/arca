// ARCA no xOS - Modelo/OndaCache (de Modelo\OndaCache.cs).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ARCA::Modelo {

/// <summary>
/// A onda do arquivo inteiro, pré-calculada em cache piramidal.
///
/// **A ideia que faz caber numa máquina fraca.** Desenhar a onda de um
/// arquivo de horas amostra a amostra é impossível — milhões de amostras
/// por coluna de pixel. A solução profissional é decidir o que se preserva
/// quando milhares de amostras viram um pixel: o **mínimo e o máximo** da
/// região (para nenhum transiente sumir), mais o RMS (energia) e um sinal
/// de clipping. Isso é calculado **uma vez**, num núcleo do processador, e
/// guardado. Depois, desenhar é ler um vetor pequeno e traçar linhas — não
/// usa a placa, não tem gargalo de fill-rate. O peso saiu do caminho quente.
///
/// **Pirâmide.** O nível 0 resume grupos pequenos de amostras; cada nível
/// acima agrega o de baixo preservando os extremos. Ao desenhar num zoom
/// qualquer, escolhe-se o nível cuja densidade casa com a tela — então o
/// custo de desenhar é o mesmo na visão do arquivo inteiro e no detalhe, e
/// os picos não somem nem mudam de lugar ao dar zoom.
///
/// **Sidecar.** O cache vai num arquivo à parte, numa pasta própria, com
/// chave pelo caminho + tamanho + data do original. Não incha o áudio (não
/// vai no atributo ASF), e é reaproveitado entre sessões — reabrir uma
/// faixa já analisada é instantâneo.
/// </summary>
// Guardado por std::shared_ptr (o fio da geração entrega ao fio da tela); nullptr = null.
class OndaCache
{
public:
    // "ANO3": os "ANO2" podem estar truncados — antes de 02/10/2026 um
    // erro de decodificação contava como fim de arquivo. Trocar a mágica
    // faz cada faixa ser reanalisada uma vez, na primeira vez que tocar.
    static constexpr int32_t Magica = 0x334F4E41;   // "ANO3" (inclui loudness de arquivo)
    static constexpr int BinBase = 512;              // amostras por bin no nível 0

    /// <summary>Resumo de um canal num nível: min, máx e RMS normalizados (-1..1), e clipping.</summary>
    struct Faixa
    {
        std::vector<float> Min;
        std::vector<float> Max;
        std::vector<float> Rms;
        std::vector<bool> Clipou;
        int Bins() const { return (int) Min.size(); }
    };

    int Canais = 0;
    int Taxa = 0;
    int Bits = 0;
    int64_t TotalAmostras = 0;

    // Medidas do arquivo inteiro (BS.1770 / R128), computadas na geração.
    /// <summary>Loudness integrado do programa, com gate absoluto e relativo (LUFS).</summary>
    double IntegradoLufs = 0;
    /// <summary>Faixa de loudness (EBU 3342), em LU.</summary>
    double LraLu = 0;
    /// <summary>Maior pico de amostra do arquivo (dBFS).</summary>
    double PicoArquivoDb = 0;

    int Niveis() const { return (int) _niveis.size(); }

    /// <summary>Amostras que um bin cobre no nível indicado.</summary>
    int64_t AmostrasPorBin(int nivel) const;

    /// <summary>
    /// Decodifica o arquivo inteiro e monta a pirâmide. Faz streaming — não
    /// segura o arquivo todo na memória, só o bin corrente de cada canal —,
    /// então funciona em processo de 32 bits mesmo com faixa longa.
    /// `cancelou` é consultado com frequência; devolve null se cancelar.
    /// Também devolve null se a decodificação falhar no meio, com o
    /// motivo em strFalha: uma onda truncada seria salva em cache e
    /// mostrada como se fosse a faixa inteira.
    /// </summary>
    static std::shared_ptr<OndaCache> Gerar(const std::string &arquivo, std::function<void(double)> progresso,
                                            std::function<bool()> cancelou, std::string &strFalha);

    /// <summary>
    /// Monta um cache a partir de um nível 0 já pronto (dados decodificados
    /// por outra via, ou um teste). Gerar() é o caminho normal, do arquivo.
    /// </summary>
    static std::shared_ptr<OndaCache> DeNivel0(int canais, int taxa, int bits, int64_t totalAmostras,
                                               std::vector<Faixa> nivel0);

    /// <summary>Constrói os níveis superiores agregando o nível 0. Usado no teste também.</summary>
    void MontarPiramide(std::vector<Faixa> nivel0);

    /// <summary>
    /// RMS de um trecho, de verdade: raiz da média das energias dos bins do
    /// nível 0 (512 amostras), ponderada pelo que cada bin tem dentro do
    /// trecho. Colunas() devolve o MAIOR RMS de bin por coluna — certo para
    /// desenhar, errado como medida de um trecho.
    /// </summary>
    double RmsDoTrecho(int canal, int64_t iniAmostra, int64_t fimAmostra) const;

    /// <summary>
    /// Resume o trecho [iniAmostra, fimAmostra) do canal em `largura`
    /// colunas, escolhendo o nível da pirâmide adequado à densidade. Cada
    /// coluna recebe min, máx, RMS e clipping. É o que o renderizador
    /// consome — barato, sem tocar no arquivo.
    /// </summary>
    void Colunas(int canal, int64_t iniAmostra, int64_t fimAmostra, int largura, std::vector<float> &outMin,
                 std::vector<float> &outMax, std::vector<float> &outRms, std::vector<bool> &outClip) const;

    /// <summary>Pasta de caches, no perfil local do usuário.</summary>
    // LINUX: %LOCALAPPDATA%\ARCA\Ondas vira <ARCA_CACHE ou /var/cache/arca>/Ondas (PORTE.md, seção 4)
    static std::string PastaCache();
    static std::string CaminhoSidecar(const std::string &arquivo);

    bool Salvar(const std::string &arquivoOrigem) const;

    /// <summary>Carrega o cache se existir e ainda corresponder ao arquivo. Senão null.</summary>
    static std::shared_ptr<OndaCache> Carregar(const std::string &arquivoOrigem);

private:
    static constexpr int Fator = 8;             // agregação entre níveis
    static constexpr int TetoBinsTopo = 2048;   // para de agregar quando cabe nisto
    static constexpr int MaxSubL = 30;          // 3 s de sub-blocos de 100 ms, para o LRA

    /// <summary>niveis[nivel][canal]. O nível 0 é o mais fino.</summary>
    std::vector<std::vector<Faixa>> _niveis;
};

// ---- o que OndaCache e EspectrogramaCache usam igual (no original: código repetido nas duas) ----
namespace CacheSidecar {
// FileInfo.Length e FileInfo.LastWriteTimeUtc.Ticks (os ticks do .NET: 100 ns desde 01/01/0001)
bool Identidade(const std::string &arquivo, int64_t &tamanho, int64_t &ticks);
// SHA1 do caminho em minúsculas, em UTF-16LE (Encoding.Unicode), em hexadecimal
std::string Chave(const std::string &arquivo);
} // namespace CacheSidecar

} // namespace ARCA::Modelo

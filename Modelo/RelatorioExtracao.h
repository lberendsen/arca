// ARCA no xOS - Modelo/RelatorioExtracao (de Modelo\RelatorioExtracao.cs).
// TRelatorioExtracao, NivelBitPerfect e TVerificacaoFaixa.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace ARCA::Modelo {

enum class NivelBitPerfect
{
    /// O AccurateRip confere e o FLAC (WMA no original) e identico ao que se leu: prova externa.
    Comprovada,
    /// Fora da base (ou sem conferir com ela), mas duas leituras deram o mesmo e o arquivo e identico.
    LidaDuasVezes,
    NaoComprovada
};

/// O que se sabe de uma faixa: AccurateRip, releitura, arquivo codificado, setores mascarados.
/// LINUX: os nomes "Wma" ficam (sao os do original e os do ARCA_extracao.txt), mas aqui o
/// arquivo conferido e o FLAC.
class TVerificacaoFaixa
{
public:
    int intFaixa = 0;
    int intConfiancaAr = -1;          // -1 = nao conferido no AccurateRip
    int intTotalAr = 0;
    std::string strVersaoAr;          // "" = null
    int intReleituraDiferentes = -1;  // -1 = sem releitura
    int intReleituraIlegiveis = 0;
    int intMascarados = 0;
    /// nullopt = arquivo nao conferido; senao, se e identico ao audio lido.
    std::optional<bool> blnWmaIdentico;
    std::string strFalhaWma;          // "" = null

    NivelBitPerfect Nivel() const;
    /// Por que nao esta comprovada, em poucas palavras.
    std::string Motivo() const;
    /// Texto para o atributo ARCA/Verification da faixa (ARCA_VERIFICATION no FLAC).
    std::string ParaEtiqueta() const;
    std::string ParaLinha() const;
    /// nullptr se a linha nao for de verificacao
    static std::unique_ptr<TVerificacaoFaixa> DeLinha(const std::string &strLinha);
};

/// O que a extracao fisica nao conseguiu ler, guardado na area de trabalho ao lado do AudioCD.bin.
///
/// Setor irrecuperavel vira silencio para a imagem sair inteira, e isso e escolha legitima - desde
/// que se saiba. Sem este arquivo, a contagem morria na memoria do extrator e um disco riscado
/// entrava no acervo como se fosse copia exata. Em arquivo, ela sobrevive a fechar o ARCA entre
/// extrair e aprovar, chega a confirmacao da aprovacao e ao ledger.
///
/// Formato de texto simples, uma chave por linha, legivel a mao:
///   setoresMascarados=12
///   setoresRecuperados=3
///   faixa=3;mascarados=10
///   faixa=7;mascarados=2
class TRelatorioExtracao
{
public:
    static constexpr const char *NomeArquivo = "ARCA_extracao.txt";

    /// Setores de 2352 bytes num segundo de audio de CD.
    static constexpr int SetoresPorSegundo = 75;

    int intSetoresMascarados = 0;
    int intSetoresRecuperados = 0;

    /// Numero da faixa -> setores substituidos por silencio nela.
    std::map<int, int> dicMascaradosPorFaixa;

    bool Exata() const { return intSetoresMascarados == 0; }

    // ---- Conferencia bit-perfect (preenchida depois da leitura) ----
    std::optional<std::string> strDrive;          // nullopt = null
    /// Correcao de offset aplicada ao cortar as faixas, em amostras.
    int intOffsetAplicado = 0;
    /// De onde veio o offset (tabela do AccurateRip, confirmado aqui...); "" = desconhecido, nenhuma correcao.
    std::string strOrigemOffset;
    /// "na base", "fora da base" ou o motivo de nao ter consultado; nullopt = nao consultado.
    std::optional<std::string> strAccurateRip;
    /// Deslocamento em que o AccurateRip conferiu (!= offset aplicado: outra prensagem).
    int intOffsetAccurateRip = 0;
    std::map<int, TVerificacaoFaixa> dicVerificacao;

    /// A verificacao da faixa, criada se ainda nao houver (a referencia vale enquanto o relatorio existir).
    TVerificacaoFaixa &Verificacao(int intFaixa);

    /// Faixas comprovadas pelo AccurateRip.
    int QtdComprovadas() const;

    /// Uma linha para a tela, a aprovacao e o ledger.
    std::string ResumoBitPerfect() const;

    /// "faixa 3 (10 setores, 0,13 s), faixa 7 (2 setores, 0,03 s)".
    std::string ResumoPorFaixa() const;

    /// Lanca std::runtime_error se nao conseguir gravar.
    void Gravar(const std::string &strPasta) const;

    /// O relatorio da area de trabalho, ou nullptr se nao houver (extracao anterior a este
    /// arquivo, ou falha).
    static std::shared_ptr<TRelatorioExtracao> Ler(const std::string &strPasta);

    static void Apagar(const std::string &strPasta);

    static int Inteiro(const std::string &s);
};

} // namespace ARCA::Modelo

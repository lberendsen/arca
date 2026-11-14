// ARCA no xOS - Interop/SptiImageExtractor (de Interop\SptiImageExtractor.cs).
// LINUX: a "unidade" que o original recebia como letra ("E") aqui e o dispositivo ("/dev/sr0");
// o que nao comecar com "/" vira Base::DispositivoCd(). Por baixo, SptiDiscReader pelo SG_IO.
#pragma once

#include "Interop/CdromTocReader.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ARCA::Modelo {
class AcquisitionLogger;
class TRelatorioExtracao;
}

namespace ARCA::Interop {

class SptiDiscReader;

/// Orquestrador de extracao fisica direta via SPTI (LINUX: SG_IO).
/// Gera os arquivos .BIN/.CUE aplicando resiliencia optica avancada:
/// - Buffer Binary Halving (reducao dinamica ate 1 setor)
/// - Cache-Defeating (purga de RAM interna do drive)
/// - Zero-Masking (preenchimento por silencio em setores irrecuperaveis)
/// - Geracao de CUE compativel com o parser de metadados e conversao do ARCA.
class SptiImageExtractor
{
public:
    /// (percentual, MB lidos, MB total, MB/s, LBA atual, LBA total) - chamado no fio da extracao
    using ProgressoExtracao = std::function<void(double, double, double, double, int64_t, int64_t)>;
    /// (verbo, detalhe) - chamado no fio da extracao
    using NotificacaoFase = std::function<void(const std::string &, const std::string &)>;

    explicit SptiImageExtractor(Modelo::AcquisitionLogger &objLogger);

    /// Por que ExtrairImagem devolveu false, em frase para a tela. "" = null.
    const std::string &MotivoFalha() const { return m_strMotivoFalha; }

    /// Fabricante e modelo do drive, como ele se declara (INQUIRY). E a chave do offset. "" = null.
    const std::string &Fabricante() const { return m_strFabricante; }
    const std::string &Modelo() const { return m_strModelo; }

    /// Resultado da releitura de uma faixa.
    struct TReleitura
    {
        int intFaixa = 0;
        /// Setores que vieram diferentes da primeira leitura.
        int intSetoresDiferentes = 0;
        /// Setores que a releitura nao conseguiu ler.
        int intSetoresIlegiveis = 0;
        bool Identica() const { return intSetoresDiferentes == 0 && intSetoresIlegiveis == 0; }
    };

    /// Le de novo as faixas pedidas ({numero, LBA inicial, LBA final sem incluir}) e compara
    /// setor a setor com o .bin da primeira leitura.
    ///
    /// Nao prova a faixa como o AccurateRip prova - um drive que interpola um trecho ruim pode
    /// interpolar igual duas vezes -, mas pega a leitura instavel, que e o erro mais comum. Antes
    /// de comecar, le SetoresParaEsvaziarCache setores longe da primeira faixa: sem isso a
    /// "releitura" poderia vir do cache do drive, e nao do disco.
    /// nullopt (null) = nao abriu a unidade (strFalha). Lanca OperationCanceledException.
    std::optional<std::vector<TReleitura>> Reler(const std::string &strLetraDrive, const std::string &strBin,
                                                 const std::vector<std::array<int, 3>> &lstFaixas,
                                                 const std::function<void(int, double)> &progresso,
                                                 const std::function<bool()> &cancelou, std::string &strFalha);

    /// O que nao se leu, por faixa. Preenchido quando a extracao conclui (nullptr antes).
    std::shared_ptr<Modelo::TRelatorioExtracao> Relatorio() const { return m_objRelatorio; }

    /// Gera strDestinoArquivo (AudioCD.bin) e, ao lado, o .cue; o relatorio e o log vao para
    /// strPastaDestino. false = MotivoFalha diz por que (e a imagem incompleta foi apagada).
    /// Erro de gravacao do .bin lanca (Base::IOException), como o FileStream do original.
    bool ExtrairImagem(const std::string &strLetraDrive, const std::string &strDestinoArquivo, const std::string &strPastaDestino,
                       bool blnIsAudio, const ProgressoExtracao &delProgresso = nullptr, const NotificacaoFase &delFase = nullptr);

private:
    // Falha de canal: o pedido nem chegou ao disco, ou a unidade deixou de estar pronta. Nao e
    // setor ruim - mascarar seria gravar horas de silencio e chamar isso de extracao.
    static constexpr int intTentativasCanal = 5;
    static constexpr int intEsperaCanalMs = 1000;

    // Tantos setores seguidos irrecuperaveis nao e risco, e regiao ilegivel: 10 s de audio.
    // Parar e melhor que mascarar por horas.
    static constexpr int intLimiteMascaradosSeguidos = 10 * 75;    // 10 * TRelatorioExtracao::SetoresPorSegundo

    static constexpr uint32_t SetoresPorBlocoReleitura = 26;    // 61 KB: cabe em qualquer controladora
    static constexpr uint32_t SetoresParaEsvaziarCache = 4000;  // ~9 MB, mais que o cache de drive de CD

    /// Le n setores; se o bloco falhar, tenta 1 setor so (e devolve n = 1). false se nem esse.
    static bool LerComResgate(SptiDiscReader &objLeitor, uint8_t *hPin, uint32_t lba, uint32_t &n);

    bool Interromper(const std::string &strMotivo);
    /// A falha e do caminho ate o disco, e nao do setor?
    static bool EhFalhaDeCanal(int intWin32Error, uint8_t bSenseKey);
    static std::string DescreverFalhaDeCanal(int intWin32Error, uint8_t bSenseKey, uint8_t bAsc, uint8_t bAscq);
    /// Inicio (LBA) de cada faixa de audio, para atribuir um setor a sua faixa.
    static std::vector<std::pair<int, uint32_t>> InicioDasFaixas(const CdromTocReader::CDROM_TOC &objToc);
    static int FaixaDoSetor(const std::vector<std::pair<int, uint32_t>> &lstInicios, uint32_t dwLba);

    bool ExtrairImagemInterno(const std::string &strLetraDrive, const std::string &strDestinoArquivo, const std::string &strPastaDestino,
                              bool blnIsAudio, const ProgressoExtracao &delProgresso, const NotificacaoFase &delFase);

    void GerarArquivoCue(const std::string &strCaminhoCue, const std::string &strCaminhoBin, const CdromTocReader::CDROM_TOC &objToc,
                         const std::optional<std::string> &strAlbumCdText, const std::optional<std::string> &strArtistaCdText,
                         const std::map<int, std::string> &dicTitulos, const std::map<int, std::string> &dicArtistas,
                         const std::string &strMcn, const std::map<int, std::string> &dicIsrcs, uint32_t dwLbaFimAudio,
                         bool blnLeadOutExato = false);

    Modelo::AcquisitionLogger &m_objLogger;
    std::string m_strMotivoFalha;
    std::string m_strFabricante;
    std::string m_strModelo;
    std::shared_ptr<Modelo::TRelatorioExtracao> m_objRelatorio;
};

} // namespace ARCA::Interop

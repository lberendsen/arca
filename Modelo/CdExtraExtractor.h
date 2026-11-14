// ARCA no xOS - Modelo/CdExtraExtractor (de Modelo\CdExtraExtractor.cs).
// TCdExtraFile, TMetadadosCdExtra e TCdExtraExtractor.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ARCA::Modelo {

/// Representa um arquivo extraido da sessao de dados de um Enhanced CD (CD Extra / Blue Book).
class TCdExtraFile
{
public:
    std::string strNomeArquivo;
    std::string strCaminhoCompleto;
    int64_t lngTamanhoBytes = 0;
    std::string strSha256;
};

/// Encapsula os metadados enriquecidos e a capa oficial de alta resolucao minerados a partir da
/// sessao de dados de um Enhanced CD / Copy Control. Strings vazias valem o null do original.
class TMetadadosCdExtra
{
public:
    std::string strAlbum;
    std::string strArtistaAlbum;
    std::string strAno;
    std::vector<std::string> lstTitulosFaixas;
    std::vector<std::string> lstArtistasFaixas;
    std::vector<std::string> lstCompositoresFaixas;
    std::vector<uint8_t> arrCapaBytes;               // vazio = null
    std::string strCaminhoCapaOriginal;
    int intLarguraCapa = 0;
    int intAlturaCapa = 0;
};

/// Responsavel exclusivamente pela interpretacao do sistema de arquivos ISO 9660 / Joliet contido
/// nos setores da sessao de dados de um Enhanced CD (CD Extra / Blue Book) e pela extracao direta
/// de arquivos de video e multimidia sem necessidade de ferramentas externas.
/// Respeita o Principio de Responsabilidade Unica (SRP) e o desacoplamento da interface.
class TCdExtraExtractor
{
public:
    using TLog = std::function<void(const std::string &)>;

    /// Copia os videos da sessao de dados de um CD Extra a partir da unidade montada (strRaiz,
    /// como "E:\"; LINUX: a pasta onde a sessao de dados do /dev/sr0 esta montada).
    ///
    /// Antes isto se tentava lendo o ISO 9660 de dentro do AudioCD.bin - que nunca funcionou: o
    /// .bin termina no fim do audio, e a sessao de dados nao esta nele. O Windows ja monta essa
    /// sessao como sistema de arquivos (com os nomes longos do Joliet e os acentos); ler por ele
    /// dispensa reimplementar o ISO sobre setor cru.
    ///
    /// Cada video e conferido: o SHA-256 da copia tem de ser igual ao de uma segunda leitura do
    /// disco. Arquivos de mesmo nome em pastas diferentes nao se sobrescrevem - o segundo leva o
    /// nome da pasta.
    static std::vector<TCdExtraFile> CopiarVideosDaUnidade(const std::string &strRaiz, const std::string &strPastaDestino, TLog actLog);

    /// Minera de forma autonoma e resiliente a capa oficial de fabrica e os metadados ricos contidos
    /// nos arquivos da sessao de dados de um CD Extra / Enhanced CD / Copy Control. Avalia imagens
    /// por resolucao e aspect ratio quadrado (~1:1) e le multiplos formatos de metadados (ASX, WAX,
    /// CDS300 trackinfo.xml).
    static bool MinerarSessaoDados(const std::string &strPastaOrigem, TLog actLog, TMetadadosCdExtra &objMetadados);

private:
    static std::string HashDe(const std::string &strArquivo);
    static void ParsearAsxWax(const std::string &strConteudo, TMetadadosCdExtra &objMeta, const TLog &actLog);
    static void ParsearCds300(const std::string &strConteudo, TMetadadosCdExtra &objMeta, const TLog &actLog);
    /// Entidades XML/HTML decodificadas numa passada so: as numericas (&#233; -> e agudo) tambem,
    /// e "&amp;lt;" vira "&lt;", nao "<".
    static std::string LimparTextoXml(const std::string &strTexto);
    /// Poe o valor na posicao da entrada (faixa), preenchendo com vazio o que faltar antes. Antes,
    /// uma entrada sem titulo nao ocupava lugar, e os titulos seguintes caiam uma faixa adiantados.
    /// Nao sobrescreve o que outra fonte ja preencheu.
    static void DefinirNaPosicao(std::vector<std::string> &lst, int intIndice, const std::string &strValor);
    /// Le um arquivo de texto do disco respeitando a codificacao: BOM se houver; senao UTF-8 se o
    /// conteudo for UTF-8 valido; senao a codificacao do Windows (1252). Ler tudo como UTF-8
    /// destruia os acentos dos ASX/WAX gravados em ANSI.
    static std::string LerTextoDetectando(const std::string &strArquivo);
};

} // namespace ARCA::Modelo

// ARCA no xOS - Modelo/DiscogsClient (de Modelo\DiscogsClient.cs).
// TDiscogsReleaseInfo e DiscogsClient: a API REST v2 do Discogs.
#pragma once

#include "Base/Json.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ARCA::Modelo {

// Action<string> do original: o registro de cada passo (pode ser vazio)
using ActLog = std::function<void(const std::string &)>;

/// Estrutura de dados contendo os metadados consolidados obtidos a partir do Discogs.
/// Utiliza estritamente Notacao Hungara para todos os campos.
/// ("" = null no original, nos campos de texto)
class TDiscogsReleaseInfo
{
public:
    std::string strArtist;
    std::string strAlbum;
    std::string strYear;
    std::vector<std::string> lstGenres;
    std::vector<std::string> lstStyles;
    std::vector<std::string> lstTrackTitles;
    std::vector<std::string> lstTrackArtists;
    /// Compositor de cada faixa, alinhado com lstTrackTitles ("" quando nao ha).
    std::vector<std::string> lstTrackComposers;
    /// Duracao de cada faixa em segundos, alinhada com lstTrackTitles (-1 quando o Discogs nao informa).
    std::vector<int> lstTrackDurations;
    /// Gravadora e numero de catalogo ("LaserLight Digital 15 688"): e o que distingue prensagens.
    std::string strGravadora;
    std::string strPais;
    std::string strCoverUrl;
    std::vector<uint8_t> arrCoverBytes;      // vazio = null
    int intReleaseId = 0;
};

/// Cliente isolado responsavel pela comunicacao com a API REST v2 do Discogs.
/// Respeita o Principio de Responsabilidade Unica (SRP) e gerencia autenticacao por token pessoal,
/// cabecalho User-Agent obrigatorio, proxy da rede e download autenticado de imagens.
/// Porta direta do algoritmo de alta precisao comprovado do ARCAwm.
class DiscogsClient
{
public:
    /// Recupera o token de acesso pessoal configurado.
    /// LINUX: no original, o App.config (DiscogsToken), com um token padrao embutido no codigo como
    /// reserva. Aqui, Base::TokenDiscogs() (/var/lib/xos/discogs-token, fora do git); sem reserva
    /// embutida: o token e segredo e nao vai para o codigo-fonte. "" se nao houver.
    static std::string ObterTokenConfigurado();

    /// Efetua a busca e consolidacao de metadados de um album no Discogs.
    /// Prioriza lancamentos cujo numero de faixas coincida com a quantidade esperada do CD fisico.
    /// strArtista: nome do artista candidato obtido pelo MusicBrainz ou GnuDb.
    /// strAlbum: titulo do album candidato.
    /// intQtdFaixasEsperadas: quantidade de faixas do CD fisico lidas no CUE.
    /// actLog: callback opcional para mensagens detalhadas de log.
    /// Devolve o lancamento preenchido, ou nullptr caso nao encontrado.
    static std::shared_ptr<TDiscogsReleaseInfo> ConsultarLancamento(const std::string &strArtista,
                                                                    const std::string &strAlbum,
                                                                    int intQtdFaixasEsperadas, const ActLog &actLog,
                                                                    const std::vector<int> &lstDuracoesDisco = {});
    // DECISAO-LORENZO (08/11/2026): lstDuracoesDisco = a duracao (s) de cada faixa do disco, pelo
    // CUE. Dada, um candidato so e aceito se as duracoes que o Discogs informa baterem com as do
    // disco (ver DuracoesConferem). Vazia = como o original (so a contagem de faixas).
    /// Quantas das duracoes informadas pelo Discogs batem com as do disco (diferenca de ate
    /// INT_TOLERANCIA_DURACAO s). intConhecidas = quantas o Discogs informa (as -1 nao contam).
    static void CompararDuracoes(const std::vector<int> &lstDisco, const std::vector<int> &lstDiscogs, int &intConhecidas,
                                 int &intBatem);
    static constexpr int INT_TOLERANCIA_DURACAO = 10;

    // ===============================================================
    //  LINK EXATO
    //
    //  Quando o usuario cola o link de um lancamento, ele ja fez a
    //  identificacao: o ARCA so tem de ler aquele lancamento, nao buscar
    //  outro parecido. Por isso este caminho nao pontua candidatos nem
    //  compara titulos - vai direto ao /releases/{id}.
    // ===============================================================

    /// O texto e um link de lancamento (/release/123) ou de master (/master/123) do Discogs? Aceita o
    /// prefixo de idioma do site (/pt_BR/release/...) e o lixo de rastreamento depois do numero.
    static bool EhLink(const std::string &strEntrada);

    /// Le exatamente o lancamento do link, com faixas, creditos e capa. Link de master vai ao
    /// lancamento principal dele. Lanca excecao se nao conseguir: um link explicito que falha nao
    /// pode cair calado numa busca por texto, que traria outro disco.
    static std::shared_ptr<TDiscogsReleaseInfo> ConsultarPorLink(const std::string &strEntrada, const ActLog &actLog);

    /// Baixa a imagem de capa do Discogs fornecendo os cabecalhos de autenticacao obrigatorios.
    /// Valida o cabecalho JPEG para evitar arquivos corrompidos. Vazio = null (sem URL).
    static std::vector<uint8_t> BaixarCapaAutenticada(const std::string &strUrlImagem, const std::string &strToken);

    /// Calcula a similaridade semantica entre duas cadeias textuais (retorna valor entre 0.0 e 1.0).
    /// Avalia contencao direta, remocao de stopwords funcionais, cobertura (recall) de palavras-chave
    /// e indice de Jaccard, garantindo que termos nucleares de albuns e artistas coincidam.
    static double CalcularSimilaridadeTexto(const std::string &strTextoA, const std::string &strTextoB);

    /// Normaliza uma cadeia textual para fins de comparacao semantica: remove anotacoes entre
    /// colchetes/parenteses, remove acentos graficos e caracteres nao alfanumericos.
    static std::string NormalizarParaComparacao(const std::string &strTexto);

private:
    static constexpr const char *STR_API_BASE = "https://api.discogs.com";
    // LINUX: o original mandava "ARCA/1.0 ( <e-mail do Lorenzo> )"; o e-mail dele nao vai a
    // servicos externos (regra dele, a mesma do ExternalMetadataClient), entao vai so o programa.
    static constexpr const char *STR_USER_AGENT = "ARCA/1.0 (xOS)";

    static std::shared_ptr<TDiscogsReleaseInfo> ObterDetalhesDoLancamento(const std::string &strResourceUrl,
                                                                          const std::string &strToken,
                                                                          const ActLog &actLog);
    static int LerDuracao(const std::string &strDuracao);
    static bool EhColetanea(const std::string &strArtista);
    static void LerCreditosFaixa(const Base::Json &dicTrack, std::string &strCompositor, std::string &strInterpretes);
    // "" = null no original (resposta que nao e 200)
    static std::string ExecutarRequisicaoJson(const std::string &strUrl, const std::string &strToken);
    static std::string LimparTermoDeBusca(const std::string &strTexto);
    static std::string RemoverAcentos(const std::string &strTexto);
    static const std::set<std::string> SET_STOPWORDS;
    static std::vector<std::string> ExtrairPalavrasChave(const std::string &strTexto);
};

} // namespace ARCA::Modelo

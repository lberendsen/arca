// ARCA no xOS - Modelo/DiscogsClient (de Modelo\DiscogsClient.cs).
#include "DiscogsClient.h"

#include <cstdlib>
#include "ExternalMetadataClient.h"
#include "Formato.h"
#include "GnuDbResolver.h"
#include "TextoIdentificacao.h"

#include "Base/Arquivo.h"
#include "Base/Excecoes.h"
#include "Base/Http.h"
#include "Base/Texto.h"

#include <algorithm>
#include <cmath>
#include <regex>

namespace ARCA::Modelo {

using Base::Json;
namespace TN = TextoNet;

namespace {

std::string Num(int64_t v) { return std::to_string(v); }

// string.Format("{0:F2}") na cultura pt-BR: "0,45"
std::string F2(double v) { return Formato::Num(v, 2); }

// Regex.IsMatch(s, "A|B|C", RegexOptions.IgnoreCase), com palavras literais: alguma aparece, sem caixa
bool TemAlguma(const std::string &s, std::initializer_list<const char *> palavras)
{
    std::string m = TN::Minusculas(s);
    for (const char *p : palavras)
        if (m.find(TN::Minusculas(p)) != std::string::npos) return true;
    return false;
}

// REG_LINK_DISCOGS = new Regex(@"discogs\.com/(?:[\w-]+/)?(release|master)/(\d+)", RegexOptions.IgnoreCase)
// LINUX: sobre code points (\w e \d do .NET sao Unicode). Devolve se casou; grupo 1 e grupo 2.
bool CasaLinkDiscogs(const std::string &s, std::string &strTipo, std::string &strId)
{
    std::vector<uint32_t> c = Base::CodePoints(s);
    auto casa = [&](size_t pos, const char *lit) {
        std::vector<uint32_t> p = Base::CodePoints(lit);
        if (pos + p.size() > c.size()) return false;
        for (size_t i = 0; i < p.size(); i++)
            if (!TN::IgualSemCaixa(c[pos + i], p[i])) return false;
        return true;
    };
    // (release|master)/(\d+) a partir de pos
    auto resto = [&](size_t pos) {
        for (const char *t : {"release", "master"})
        {
            size_t n = std::char_traits<char>::length(t);
            if (!casa(pos, t) || pos + n >= c.size() || c[pos + n] != '/') continue;
            size_t d = pos + n + 1, e = d;
            while (e < c.size() && TN::EhDigito(c[e])) e++;
            if (e == d) continue;
            strTipo.clear();
            strId.clear();
            for (size_t i = pos; i < pos + n; i++) Base::AcrescentaUtf8(strTipo, c[i]);
            for (size_t i = d; i < e; i++) Base::AcrescentaUtf8(strId, c[i]);
            return true;
        }
        return false;
    };
    for (size_t i = 0; i < c.size(); i++)
    {
        if (!casa(i, "discogs.com/")) continue;
        size_t j = i + 12;
        // (?:[\w-]+/)? - guloso: primeiro com o segmento, depois sem
        size_t k = j;
        while (k < c.size() && (TN::EhPalavra(c[k]) || c[k] == '-')) k++;
        if (k > j && k < c.size() && c[k] == '/' && resto(k + 1)) return true;
        if (resto(j)) return true;
    }
    return false;
}

} // namespace

std::string DiscogsClient::ObterTokenConfigurado()
{
    // LINUX: ConfigurationManager.AppSettings["DiscogsToken"] -> Base::TokenDiscogs(); a reserva
    // STR_TOKEN_PADRAO do original (um token embutido no codigo) nao foi portada: e segredo.
    try
    {
        std::string strToken = Base::TokenDiscogs();
        if (!strToken.empty()) return Base::Trim(strToken);
    }
    catch (...) { }
    return "";
}

std::shared_ptr<TDiscogsReleaseInfo> DiscogsClient::ConsultarLancamento(const std::string &strArtista,
                                                                        const std::string &strAlbum,
                                                                        int intQtdFaixasEsperadas,
                                                                        const ActLog &actLogRecebido,
                                                                        const std::vector<int> &lstDuracoesDisco)
{
    ActLog actLog = actLogRecebido ? actLogRecebido : ActLog([](const std::string &) {});

    std::string strToken = ObterTokenConfigurado();
    if (strToken.empty())
    {
        // LINUX: o original dizia "...não configurado no App.config..."; aqui o token mora em outro lugar.
        actLog("Discogs: Token de autenticação não configurado em /var/lib/xos/discogs-token. Pulando consulta ao Discogs.");
        return nullptr;
    }

    // Basta um dos dois. Sem album, busca na discografia do artista e
    // escolhe pela contagem de faixas; sem artista, busca pelo titulo.
    bool blnSemArtista = strArtista.empty() || Base::Trim(strArtista).empty();
    bool blnSemAlbum = strAlbum.empty() || Base::Trim(strAlbum).empty();
    if (blnSemArtista && blnSemAlbum)
    {
        actLog("Discogs: Artista e Álbum vazios. Não é possível realizar a busca.");
        return nullptr;
    }

    try
    {
        // Limpeza suave de termos que costumam poluir a busca no Discogs (ex: "[Live]", "(Disc 1)")
        std::string strArtistaLimpo = blnSemArtista ? "" : LimparTermoDeBusca(strArtista);
        std::string strAlbumLimpo = blnSemAlbum ? "" : LimparTermoDeBusca(strAlbum);
        if (!blnSemAlbum) strAlbumLimpo = GnuDbResolver::HigienizarTituloAlbum(strArtistaLimpo, strAlbumLimpo);

        // Normalizacao para Coletaneas / Varios Interpretes (Discogs cataloga universalmente como "Various")
        std::string strArtistaSemAcentos = RemoverAcentos(strArtistaLimpo);
        // Por expressao, nao por lista de grafias exatas: "Varios Artistas",
        // "Diversos", "Varios Interpretes", "Artistas Variados" ficavam de fora.
        // LINUX: std::regex (ECMAScript, icase) com a mesma expressao; o \s do .NET e Unicode, entao
        // os espacos Unicode viram ' ' antes (a expressao so tem ASCII).
        std::string strAlvo;
        for (uint32_t c : Base::CodePoints(Base::Trim(strArtistaSemAcentos))) Base::AcrescentaUtf8(strAlvo, TN::EhEspaco(c) ? ' ' : c);
        static const std::regex regColetanea(
            R"(^(va|v\.\s*a\.?|various(\s+artists?)?|varios(\s+(artistas|interpretes))?|diversos(\s+(artistas|interpretes))?|)"
            R"(artistas\s+(variados|diversos)|coletanea|compilation)$)",
            std::regex::ECMAScript | std::regex::icase);
        bool blnArtistaBuscaColetanea = std::regex_match(strAlvo, regColetanea);

        if (blnArtistaBuscaColetanea)
        {
            strArtistaLimpo = "Various";
        }

        actLog("Discogs: Consultando API para '" + strArtistaLimpo + " - " + strAlbumLimpo + "' (faixas esperadas: " +
               Num(intQtdFaixasEsperadas) + ")...");

        Json arrResultados;            // null = sem resultados
        std::string strJsonBusca;

        // Tentativa 1: artista + titulo (so com os dois)
        if (!blnSemArtista && !blnSemAlbum)
        {
            std::string strUrlBusca = std::string(STR_API_BASE) + "/database/search?artist=" +
                                      Base::UrlEncode(strArtistaLimpo) + "&release_title=" +
                                      Base::UrlEncode(strAlbumLimpo) + "&type=release";
            strJsonBusca = ExecutarRequisicaoJson(strUrlBusca, strToken);
        }

        if (!strJsonBusca.empty())
        {
            Json dicBusca = TN::Desserializa(strJsonBusca);
            if (!dicBusca.is_null() && TN::Tem(dicBusca, "results"))
            {
                arrResultados = dicBusca["results"].is_array() ? dicBusca["results"] : Json();
            }
        }

        auto vazio = [&]() { return !arrResultados.is_array() || arrResultados.empty(); };

        // Tentativa 2: Busca direta por release_title caso a busca inicial nao tenha achado
        if (vazio() && !blnSemAlbum)
        {
            std::string strUrlPorTitulo = std::string(STR_API_BASE) + "/database/search?release_title=" +
                                          Base::UrlEncode(strAlbumLimpo) + "&type=release";
            strJsonBusca = ExecutarRequisicaoJson(strUrlPorTitulo, strToken);
            if (!strJsonBusca.empty())
            {
                Json dicBuscaTitulo = TN::Desserializa(strJsonBusca);
                if (!dicBuscaTitulo.is_null() && TN::Tem(dicBuscaTitulo, "results"))
                {
                    arrResultados = dicBuscaTitulo["results"].is_array() ? dicBuscaTitulo["results"] : Json();
                }
            }
        }

        // Tentativa 3: Busca ampla com termo geral.
        // Se for coletanea, busca exclusivamente pelo titulo do album para evitar o mar de falsos positivos de "Various".
        if (vazio())
        {
            std::string strTermoAmplo = blnArtistaBuscaColetanea && !blnSemAlbum
                                            ? strAlbumLimpo : Base::Trim(strArtistaLimpo + " " + strAlbumLimpo);
            std::string strUrlAmpla = std::string(STR_API_BASE) + "/database/search?q=" + Base::UrlEncode(strTermoAmplo) +
                                      "&type=release";
            strJsonBusca = ExecutarRequisicaoJson(strUrlAmpla, strToken);
            if (!strJsonBusca.empty())
            {
                Json dicBuscaAmpla = TN::Desserializa(strJsonBusca);
                if (!dicBuscaAmpla.is_null() && TN::Tem(dicBuscaAmpla, "results"))
                {
                    arrResultados = dicBuscaAmpla["results"].is_array() ? dicBuscaAmpla["results"] : Json();
                }
            }
        }

        // Tentativa 4: Busca pela discografia do artista (resiliencia contra titulos transliterados ou variacoes de grafia)
        if (vazio() && !blnArtistaBuscaColetanea && !strArtistaLimpo.empty())
        {
            actLog("Discogs: Nenhuma correspondência direta por título. Consultando lançamentos do artista '" +
                   strArtistaLimpo + "'...");
            std::string strUrlArtista = std::string(STR_API_BASE) + "/database/search?artist=" +
                                        Base::UrlEncode(strArtistaLimpo) + "&type=release";
            strJsonBusca = ExecutarRequisicaoJson(strUrlArtista, strToken);
            if (!strJsonBusca.empty())
            {
                Json dicBuscaArtista = TN::Desserializa(strJsonBusca);
                if (!dicBuscaArtista.is_null() && TN::Tem(dicBuscaArtista, "results"))
                {
                    arrResultados = dicBuscaArtista["results"].is_array() ? dicBuscaArtista["results"] : Json();
                }
            }
        }

        if (vazio())
        {
            actLog("Discogs: Nenhum lançamento encontrado para '" + strArtistaLimpo + " - " + strAlbumLimpo + "'.");
            return nullptr;
        }

        actLog("Discogs: " + Num((int64_t) arrResultados.size()) +
               " lançamento(s) candidato(s) encontrado(s). Analisando compatibilidade e faixas...");

        // Analisa os primeiros candidatos para eleger o melhor lancamento com trava de similaridade
        std::shared_ptr<TDiscogsReleaseInfo> objMelhorCandidato;
        double dblMelhorScore = 0.0;
        int intLimiteAnalise = std::min((int) arrResultados.size(), 5);

        for (int i = 0; i < intLimiteAnalise; i++)
        {
            const Json &dicItem = arrResultados[i];
            if (!dicItem.is_object() || !TN::Tem(dicItem, "resource_url")) continue;

            std::string strResourceUrl = TN::TextoDe(dicItem["resource_url"]);
            std::shared_ptr<TDiscogsReleaseInfo> objInfo = ObterDetalhesDoLancamento(strResourceUrl, strToken, actLog);
            if (!objInfo) continue;

            // 1. Validacao de Similaridade do Titulo do Album (sem album
            //    informado, nao ha com o que comparar: decide a contagem de faixas)
            double dblSimilaridadeAlbum = blnSemAlbum ? 0.5 : CalcularSimilaridadeTexto(strAlbumLimpo, objInfo->strAlbum);
            if (!blnSemAlbum && dblSimilaridadeAlbum < 0.60)
            {
                actLog("Discogs: Candidato '" + (objInfo->strArtist.empty() ? std::string("Desconhecido") : objInfo->strArtist) +
                       " - " + (objInfo->strAlbum.empty() ? std::string("Desconhecido") : objInfo->strAlbum) +
                       "' descartado por divergência de título com '" + strAlbumLimpo + "' (similaridade " +
                       F2(dblSimilaridadeAlbum) + " < 0.60).");
                continue;
            }

            // 2. Validacao de Similaridade do Artista (quando nao for busca de coletanea "Various")
            if (!blnArtistaBuscaColetanea && !blnSemArtista)
            {
                double dblSimilaridadeArtista = CalcularSimilaridadeTexto(strArtistaLimpo, objInfo->strArtist);
                if (dblSimilaridadeArtista < 0.50)
                {
                    actLog("Discogs: Candidato '" + (objInfo->strArtist.empty() ? std::string("Desconhecido") : objInfo->strArtist) +
                           " - " + (objInfo->strAlbum.empty() ? std::string("Desconhecido") : objInfo->strAlbum) +
                           "' descartado por divergência de artista com '" + strArtistaLimpo + "' (similaridade " +
                           F2(dblSimilaridadeArtista) + " < 0.50).");
                    continue;
                }
            }

            // DECISAO-LORENZO (08/11/2026): com as duracoes do disco, o lancamento tem de ter o mesmo
            // repertorio: pelo menos 80% das duracoes que o Discogs informa a ate 10 s das do disco.
            // Foi o que faltou no Glenn Miller de 07/11 (release de 20 faixas com outro repertorio).
            // Se o Discogs informa menos da metade das duracoes, nao da para conferir: o candidato
            // tambem e descartado (a escolha lado a lado, sem duracoes, continua mostrando todos).
            if (!lstDuracoesDisco.empty())
            {
                int intConhecidas = 0, intBatem = 0;
                CompararDuracoes(lstDuracoesDisco, objInfo->lstTrackDurations, intConhecidas, intBatem);
                std::string strNome = (objInfo->strArtist.empty() ? std::string("Desconhecido") : objInfo->strArtist) + " - " +
                                      (objInfo->strAlbum.empty() ? std::string("Desconhecido") : objInfo->strAlbum);
                if (intConhecidas * 2 >= (int) lstDuracoesDisco.size())
                {
                    if (intBatem * 5 < intConhecidas * 4)
                    {
                        actLog("Discogs: Candidato '" + strNome + "' descartado: as durações não batem com as do disco (" +
                               Num(intBatem) + " de " + Num(intConhecidas) + " faixas a até " + Num(INT_TOLERANCIA_DURACAO) + " s).");
                        continue;
                    }
                    actLog("Discogs: Durações conferem com as do disco (" + Num(intBatem) + " de " + Num(intConhecidas) + " faixas).");
                }
                else
                {
                    actLog("Discogs: Candidato '" + strNome + "' descartado: sem durações suficientes para conferir com o disco (" +
                           Num(intConhecidas) + " de " + Num((int64_t) lstDuracoesDisco.size()) + ").");
                    continue;
                }
            }

            // Calcula score composto considerando similaridade e contagem exata de faixas
            bool blnFaixasExatas = (intQtdFaixasEsperadas > 0 && (int) objInfo->lstTrackTitles.size() == intQtdFaixasEsperadas);
            double dblScoreCandidato = dblSimilaridadeAlbum + (blnFaixasExatas ? 1.0 : 0.0);

            // Se a contagem de faixas coincidir perfeitamente E o titulo for altamente compativel, temos a prensagem exata
            // ARCA-ORIGINAL: so a contagem decide (as duracoes nao entram). Em 07/11/2026, o CD de
            // Glenn Miller no drive (GnuDB "GlennMiller- The very best of", 20 faixas) levou a
            // release 7560781, de 20 faixas mas com outro repertorio: 13 das 20 faixas fora de 10 s.
            // Na identificacao automatica pos-extracao, esses titulos iriam para as faixas.
            if (blnFaixasExatas)
            {
                actLog("Discogs: Correspondência exata de faixas (" + Num(intQtdFaixasEsperadas) +
                       ") e título compatível ('" + objInfo->strAlbum + "', similaridade " + F2(dblSimilaridadeAlbum) +
                       "). Prensagem selecionada.");
                objMelhorCandidato = objInfo;
                break;
            }

            if (!objMelhorCandidato || dblScoreCandidato > dblMelhorScore)
            {
                objMelhorCandidato = objInfo;
                dblMelhorScore = dblScoreCandidato;
            }
        }

        if (!objMelhorCandidato)
        {
            actLog("Discogs: Nenhum candidato atingiu os critérios mínimos de similaridade para '" + strArtistaLimpo +
                   " - " + strAlbumLimpo + "'. Mantendo metadados originais.");
            return nullptr;
        }

        // Baixa a imagem da capa em alta resolucao caso disponivel
        if (!objMelhorCandidato->strCoverUrl.empty())
        {
            actLog("Discogs: Baixando capa oficial autenticada...");
            try
            {
                objMelhorCandidato->arrCoverBytes = BaixarCapaAutenticada(objMelhorCandidato->strCoverUrl, strToken);
                if (!objMelhorCandidato->arrCoverBytes.empty())
                {
                    actLog("Discogs: Capa baixada com sucesso (" +
                           Formato::Inteiro((double) objMelhorCandidato->arrCoverBytes.size()) + " bytes).");
                }
            }
            catch (const std::exception &exCapa)
            {
                actLog(std::string("Discogs: Aviso ao baixar capa: ") + exCapa.what());
            }
        }

        return objMelhorCandidato;
    }
    catch (const std::exception &exGeral)
    {
        actLog(std::string("Discogs: Falha na consulta de metadados: ") + exGeral.what());
        return nullptr;
    }
}

void DiscogsClient::CompararDuracoes(const std::vector<int> &lstDisco, const std::vector<int> &lstDiscogs, int &intConhecidas,
                                     int &intBatem)
{
    intConhecidas = 0;
    intBatem = 0;
    size_t n = std::min(lstDisco.size(), lstDiscogs.size());
    for (size_t i = 0; i < n; i++)
    {
        if (lstDiscogs[i] < 0 || lstDisco[i] <= 0) continue;
        intConhecidas++;
        if (std::abs(lstDiscogs[i] - lstDisco[i]) <= INT_TOLERANCIA_DURACAO) intBatem++;
    }
}

/// Obtem o JSON detalhado de um release especifico (/releases/{id}) e extrai a tracklist oficial e estilos.
std::shared_ptr<TDiscogsReleaseInfo> DiscogsClient::ObterDetalhesDoLancamento(const std::string &strResourceUrl,
                                                                              const std::string &strToken,
                                                                              const ActLog &actLog)
{
    try
    {
        std::string strJsonDetalhe = ExecutarRequisicaoJson(strResourceUrl, strToken);
        if (strJsonDetalhe.empty()) return nullptr;

        Json dicDetalhe = TN::Desserializa(strJsonDetalhe);
        if (dicDetalhe.is_null()) return nullptr;

        auto objRelease = std::make_shared<TDiscogsReleaseInfo>();

        if (TN::Tem(dicDetalhe, "id"))
        {
            int intId;
            if (Base::TentaInt(TN::TextoDe(dicDetalhe["id"]), intId))
                objRelease->intReleaseId = intId;
        }

        // Titulo do Album
        if (TN::Tem(dicDetalhe, "title"))
        {
            objRelease->strAlbum = TN::TextoDe(dicDetalhe["title"]);
        }

        // Artista
        if (TN::Tem(dicDetalhe, "artists"))
        {
            const Json &arrArtistas = dicDetalhe["artists"];
            if (arrArtistas.is_array() && !arrArtistas.empty())
            {
                const Json &dicPrimeiroArtista = arrArtistas[0];
                if (dicPrimeiroArtista.is_object() && TN::Tem(dicPrimeiroArtista, "name"))
                {
                    std::string strNomeBruto = TN::TextoDe(dicPrimeiroArtista["name"]);
                    // Discogs usa notacao como "Artista (2)" para desambiguacao; removemos o sufixo numerico
                    objRelease->strArtist = Base::Trim(TN::TiraSufixoNumerico(strNomeBruto));
                }
            }
        }

        // Gravadora + catalogo, e pais: o que separa uma prensagem da outra
        if (TN::Tem(dicDetalhe, "labels"))
        {
            const Json &arrSelos = dicDetalhe["labels"];
            const Json *dicSelo = arrSelos.is_array() && !arrSelos.empty() && arrSelos[0].is_object() ? &arrSelos[0] : nullptr;
            if (dicSelo && TN::Tem(*dicSelo, "name"))
            {
                std::string strSelo = Base::Trim(TN::TiraSufixoNumerico(TN::TextoDe((*dicSelo)["name"])));
                std::string strCatalogo = TN::Tem(*dicSelo, "catno") && !(*dicSelo)["catno"].is_null()
                                              ? Base::Trim(TN::TextoDe((*dicSelo)["catno"])) : "";
                if (TN::IgualSemCaixa(strCatalogo, "none")) strCatalogo = "";
                objRelease->strGravadora = Base::Trim(strSelo + " " + strCatalogo);
            }
        }
        if (TN::Tem(dicDetalhe, "country") && !dicDetalhe["country"].is_null())
        {
            objRelease->strPais = TN::TextoDe(dicDetalhe["country"]);
        }

        // Ano de Lancamento
        if (TN::Tem(dicDetalhe, "year"))
        {
            std::string strAno = TN::TextoDe(dicDetalhe["year"]);
            if (strAno != "0" && !strAno.empty())
            {
                objRelease->strYear = strAno;
            }
        }

        // Generos
        if (TN::Tem(dicDetalhe, "genres"))
        {
            const Json &arrGeneros = dicDetalhe["genres"];
            if (arrGeneros.is_array())
            {
                for (const Json &objG : arrGeneros)
                {
                    if (!objG.is_null()) objRelease->lstGenres.push_back(TN::TextoDe(objG));
                }
            }
        }

        // Estilos Refinados (o ponto forte do Discogs: ex: MPB, Samba-Cancao, Ambient, etc.)
        if (TN::Tem(dicDetalhe, "styles"))
        {
            const Json &arrEstilos = dicDetalhe["styles"];
            if (arrEstilos.is_array())
            {
                for (const Json &objE : arrEstilos)
                {
                    if (!objE.is_null()) objRelease->lstStyles.push_back(TN::TextoDe(objE));
                }
            }
        }

        // Tracklist oficial
        if (TN::Tem(dicDetalhe, "tracklist"))
        {
            const Json &arrTracklist = dicDetalhe["tracklist"];
            if (arrTracklist.is_array())
            {
                for (const Json &dicTrack : arrTracklist)
                {
                    if (!dicTrack.is_object()) continue;

                    // Ignora cabecalhos ou divisoes de atos
                    if (TN::Tem(dicTrack, "type_") && TN::IgualSemCaixa(TN::TextoDe(dicTrack["type_"]), "heading"))
                        continue;

                    if (TN::Tem(dicTrack, "title"))
                    {
                        std::string strTituloFaixa = TN::TextoDe(dicTrack["title"]);
                        if (!strTituloFaixa.empty())
                        {
                            objRelease->lstTrackTitles.push_back(strTituloFaixa);

                            // Extrai o artista especifico desta faixa (crucial para Coletaneas / Various Artists)
                            std::string strArtistaFaixa;
                            if (TN::Tem(dicTrack, "artists"))
                            {
                                const Json &arrArtistasFaixa = dicTrack["artists"];
                                if (arrArtistasFaixa.is_array() && !arrArtistasFaixa.empty())
                                {
                                    std::vector<std::string> lstNomes;
                                    for (const Json &dicA : arrArtistasFaixa)
                                    {
                                        if (dicA.is_object() && TN::Tem(dicA, "name"))
                                        {
                                            std::string strNome = Base::Trim(TN::TiraSufixoNumerico(TN::TextoDe(dicA["name"])));
                                            if (!strNome.empty()) lstNomes.push_back(strNome);
                                        }
                                    }
                                    if (!lstNomes.empty())
                                    {
                                        strArtistaFaixa = Base::Junta(lstNomes, ", ");
                                    }
                                }
                            }

                            // Creditos da faixa: compositor e, em coletanea, os interpretes.
                            // Em coletanea de musica classica a faixa costuma nao ter
                            // "artists" - quem toca vem em "extraartists" (orquestra,
                            // regente, solista), junto com o compositor.
                            std::string strCompositor;
                            std::string strInterpretes;
                            LerCreditosFaixa(dicTrack, strCompositor, strInterpretes);
                            objRelease->lstTrackComposers.push_back(strCompositor);
                            objRelease->lstTrackDurations.push_back(
                                LerDuracao(TN::Tem(dicTrack, "duration") && !dicTrack["duration"].is_null()
                                               ? TN::TextoDe(dicTrack["duration"]) : ""));

                            if (strArtistaFaixa.empty() && EhColetanea(objRelease->strArtist) && !strInterpretes.empty())
                            {
                                strArtistaFaixa = strInterpretes;
                            }

                            if (strArtistaFaixa.empty())
                            {
                                strArtistaFaixa = objRelease->strArtist.empty() ? "Coletânea" : objRelease->strArtist;
                            }

                            objRelease->lstTrackArtists.push_back(strArtistaFaixa);
                        }
                    }
                }
            }
        }

        // Imagem de Capa Principal
        if (TN::Tem(dicDetalhe, "images"))
        {
            const Json &arrImagens = dicDetalhe["images"];
            if (arrImagens.is_array() && !arrImagens.empty())
            {
                std::string strCandidataSecundaria;
                bool blnTemSecundaria = false;
                for (const Json &dicImg : arrImagens)
                {
                    if (!dicImg.is_object()) continue;

                    std::string strUri = TN::Tem(dicImg, "uri") ? TN::TextoDe(dicImg["uri"]) : "";
                    bool blnTemTipo = TN::Tem(dicImg, "type");
                    std::string strTipo = blnTemTipo ? TN::TextoDe(dicImg["type"]) : "";

                    if (!strUri.empty())
                    {
                        if (blnTemTipo && TN::IgualSemCaixa(strTipo, "primary"))
                        {
                            objRelease->strCoverUrl = strUri;
                            break;
                        }
                        if (!blnTemSecundaria)
                        {
                            strCandidataSecundaria = strUri;
                            blnTemSecundaria = true;
                        }
                    }
                }

                if (objRelease->strCoverUrl.empty() && !strCandidataSecundaria.empty())
                {
                    objRelease->strCoverUrl = strCandidataSecundaria;
                }
            }
        }

        return objRelease;
    }
    catch (const std::exception &ex)
    {
        if (actLog) actLog(std::string("Discogs: Falha ao obter detalhes do lançamento: ") + ex.what());
        return nullptr;
    }
}

bool DiscogsClient::EhLink(const std::string &strEntrada)
{
    std::string t, id;
    return !strEntrada.empty() && CasaLinkDiscogs(strEntrada, t, id);
}

std::shared_ptr<TDiscogsReleaseInfo> DiscogsClient::ConsultarPorLink(const std::string &strEntrada,
                                                                     const ActLog &actLogRecebido)
{
    ActLog actLog = actLogRecebido ? actLogRecebido : ActLog([](const std::string &) {});

    std::string strTipo, strId;
    if (!CasaLinkDiscogs(strEntrada, strTipo, strId))
        throw Base::ArgumentException("não é um link de lançamento do Discogs.");

    // TLS 1.2 e certificado do proxy: sem isto, se o Discogs for a
    // primeira chamada HTTPS do processo, o handshake falha.
    NetworkConfiguration::Initialize();
    std::string strToken = ObterTokenConfigurado();

    bool blnMaster = TN::IgualSemCaixa(strTipo, "master");

    if (blnMaster)
    {
        actLog("Discogs: link de master " + strId + "; buscando o lançamento principal...");
        std::string strJsonMaster = ExecutarRequisicaoJson(std::string(STR_API_BASE) + "/masters/" + strId, strToken);
        Json dicMaster = strJsonMaster.empty() ? Json() : TN::Desserializa(strJsonMaster);
        if (dicMaster.is_null() || !TN::Tem(dicMaster, "main_release") || dicMaster["main_release"].is_null())
            throw Base::InvalidOperationException("o master " + strId + " do Discogs não indica lançamento principal.");
        strId = TN::TextoDe(dicMaster["main_release"]);
    }

    actLog("Discogs: lendo o lançamento " + strId + " indicado pelo link...");
    std::shared_ptr<TDiscogsReleaseInfo> objRelease =
        ObterDetalhesDoLancamento(std::string(STR_API_BASE) + "/releases/" + strId, strToken, actLog);
    if (!objRelease || objRelease->lstTrackTitles.empty())
        throw Base::InvalidOperationException("não consegui ler o lançamento " + strId + " do Discogs (veja o registro).");

    if (!objRelease->strCoverUrl.empty())
    {
        try { objRelease->arrCoverBytes = BaixarCapaAutenticada(objRelease->strCoverUrl, strToken); }
        catch (const std::exception &ex) { actLog(std::string("Discogs: falha ao baixar a capa: ") + ex.what()); }
    }
    if (objRelease->arrCoverBytes.empty())
        actLog("Discogs: o lançamento não tem capa disponível.");

    actLog("Discogs: " + objRelease->strArtist + " — " + objRelease->strAlbum + ", " +
           Num((int64_t) objRelease->lstTrackTitles.size()) + " faixas.");
    return objRelease;
}

/// "9:48" ou "1:02:15" em segundos; -1 quando vazio ou ilegivel (o Discogs deixa a duracao em
/// branco em muitos lancamentos).
int DiscogsClient::LerDuracao(const std::string &strDuracao)
{
    if (strDuracao.empty()) return -1;
    std::vector<std::string> arrPartes = Base::Divide(Base::Trim(strDuracao), ":");
    int intTotal = 0;
    for (const std::string &strParte : arrPartes)
    {
        int intValor;
        if (!Base::TentaInt(strParte, intValor)) return -1;
        intTotal = intTotal * 60 + intValor;
    }
    return intTotal > 0 ? intTotal : -1;
}

/// "Various" e equivalentes: o disco nao tem um artista so, e o artista de cada faixa tem de vir
/// dos creditos dela.
bool DiscogsClient::EhColetanea(const std::string &strArtista)
{
    if (strArtista.empty()) return false;
    std::string strNorm = NormalizarParaComparacao(strArtista);
    return Base::ComecaCom(strNorm, "various") || Base::ComecaCom(strNorm, "vario")
           || Base::ComecaCom(strNorm, "diversos") || Base::ComecaCom(strNorm, "coletanea");
}

/// Creditos de uma faixa em "extraartists": quem compos, e quem interpreta (orquestra, regente,
/// solista, grupo). Papeis tecnicos e de producao ficam de fora. Nomes canonicos do Discogs, sem o
/// sufixo de desambiguacao "(2)". "" = null no original.
void DiscogsClient::LerCreditosFaixa(const Json &dicTrack, std::string &strCompositor, std::string &strInterpretes)
{
    strCompositor.clear();
    strInterpretes.clear();
    if (!TN::Tem(dicTrack, "extraartists")) return;
    const Json &arrCreditos = dicTrack["extraartists"];
    if (!arrCreditos.is_array()) return;

    std::vector<std::string> lstCompositores;
    std::vector<std::string> lstInterpretes;
    for (const Json &dicC : arrCreditos)
    {
        if (!dicC.is_object() || !TN::Tem(dicC, "name")) continue;

        std::string strNome = Base::Trim(TN::TiraSufixoNumerico(TN::TextoDe(dicC["name"])));
        std::string strPapel = TN::Tem(dicC, "role") && !dicC["role"].is_null() ? TN::TextoDe(dicC["role"]) : "";
        if (strNome.empty()) continue;

        if (TemAlguma(strPapel, {"Composed", "Written", "Music By", "Songwriter"}))
        {
            if (std::find(lstCompositores.begin(), lstCompositores.end(), strNome) == lstCompositores.end())
                lstCompositores.push_back(strNome);
        }
        else if (!TemAlguma(strPapel, {"Lyrics", "Arranged", "Produc", "Engineer", "Master", "Mix", "Record", "Design",
                                       "Photo", "Liner", "Artwork", "Edit", "Translat", "Publish", "Copyright", "Licens",
                                       "Executive"}))
        {
            if (std::find(lstInterpretes.begin(), lstInterpretes.end(), strNome) == lstInterpretes.end())
                lstInterpretes.push_back(strNome);
        }
    }

    if (!lstCompositores.empty()) strCompositor = Base::Junta(lstCompositores, ", ");
    if (!lstInterpretes.empty()) strInterpretes = Base::Junta(lstInterpretes, ", ");
}

std::vector<uint8_t> DiscogsClient::BaixarCapaAutenticada(const std::string &strUrlImagem, const std::string &strToken)
{
    if (strUrlImagem.empty()) return {};

    // LINUX: HttpWebRequest com proxy -> NetworkConfiguration::Responder (direto, libcurl). Timeout
    // 20 s igual; o original limitava a 4 redirecionamentos, a libcurl do Base/Http segue ate 8.
    NetworkConfiguration::CreateProxy();
    std::vector<std::string> lstCab;
    if (!strToken.empty())
    {
        lstCab.push_back("Authorization: Discogs token=" + strToken);
    }

    Base::RespostaHttp resp = NetworkConfiguration::Responder(strUrlImagem, lstCab, 20000, STR_USER_AGENT);
    {
        if (resp.status != 200)
        {
            // LINUX: o .NET escreve o nome do HttpStatusCode ("NoContent"); aqui vai o numero
            throw Base::WebException("Status HTTP inválido ao baixar capa: " + Num(resp.status), (int) resp.status);
        }

        // Teto de 8 MB tambem para resposta sem tamanho declarado.
        std::vector<uint8_t> arrBytes = NetworkConfiguration::LerCorpo(resp, 8 * 1024 * 1024);
        {
            // Validacao de integridade do cabecalho JPEG (FF D8 FF) ou PNG (89 50 4E 47)
            if (arrBytes.size() >= 3 && arrBytes[0] == 0xFF && arrBytes[1] == 0xD8 && arrBytes[2] == 0xFF)
            {
                return arrBytes;
            }

            if (arrBytes.size() >= 8 && arrBytes[0] == 0x89 && arrBytes[1] == 0x50 && arrBytes[2] == 0x4E && arrBytes[3] == 0x47)
            {
                return arrBytes;
            }

            // Sem assinatura de imagem, nao e imagem - mesmo que seja grande:
            // uma pagina HTML de bloqueio passava por "capa" pelo tamanho.
            throw Base::InvalidDataException("Arquivo de imagem retornado não possui cabeçalho de imagem válido.");
        }
    }
}

/// Executa requisicao GET autenticada na API do Discogs e retorna o corpo como string.
std::string DiscogsClient::ExecutarRequisicaoJson(const std::string &strUrl, const std::string &strToken)
{
    // LINUX: HttpWebRequest com proxy -> NetworkConfiguration::Responder (direto, libcurl), que lanca
    // WebException em erro HTTP ou de rede, como o GetResponse do original. Timeout 15 s igual; o
    // original limitava a 3 redirecionamentos, a libcurl do Base/Http segue ate 8.
    NetworkConfiguration::CreateProxy();
    std::vector<std::string> lstCab;
    if (!strToken.empty())
    {
        lstCab.push_back("Authorization: Discogs token=" + strToken);
    }

    Base::RespostaHttp resp = NetworkConfiguration::Responder(strUrl, lstCab, 15000, STR_USER_AGENT);
    {
        if (resp.status != 200) return "";
        // LINUX: StreamReader em UTF-8 troca byte invalido por U+FFFD; Base::Utf8Valido le como
        // latim-1 (o nlohmann::json recusaria UTF-8 invalido).
        return Base::Utf8Valido(resp.corpo);
    }
}

/// Normaliza termos de busca removendo anotacoes de edicoes, discos multiplos, caracteres especiais
/// e anotacoes/gravadoras entre parenteses no final.
std::string DiscogsClient::LimparTermoDeBusca(const std::string &strTexto)
{
    if (strTexto.empty()) return "";
    std::string strResultado = strTexto;
    // Remove "(Disc 1)", "[Live]", "(Remastered)", etc.
    // Regex.Replace(.., @"\s*[\(\[][^\]\)]*(disc|cd|live|remaster|deluxe|edition|vers)[^\]\)]*[\)\]]", "", IgnoreCase)
    strResultado = TN::TiraAnotacoes(strResultado, "", {"disc", "cd", "live", "remaster", "deluxe", "edition", "vers"});
    // Remove anotacoes e selos entre parenteses no final do titulo (exceto termos legitimos como "Ao Vivo", "Live", "Acustico")
    strResultado = TN::TiraParentesesDoFim(strResultado);
    strResultado = Base::Trim(TN::TrocaEspacos(strResultado, " "));
    return strResultado;
}

/// Remove acentos graficos de uma cadeia de caracteres para comparacoes textuais robustas.
std::string DiscogsClient::RemoverAcentos(const std::string &strTexto)
{
    if (strTexto.empty()) return "";
    // LINUX: Normalize(FormD), fora os NonSpacingMark, Normalize(FormC) - TextoNet::SemMarcas
    return TN::SemMarcas(strTexto);
}

// StringComparer.OrdinalIgnoreCase no original; as palavras comparadas ja vem em minusculas
// (NormalizarParaComparacao), entao o conjunto em minusculas basta.
const std::set<std::string> DiscogsClient::SET_STOPWORDS = {
    "o", "a", "os", "as", "um", "uma", "uns", "umas",
    "de", "do", "da", "dos", "das", "em", "no", "na", "nos", "nas",
    "e", "ou", "para", "por", "com",
    "the", "a", "an", "of", "in", "on", "at", "to", "for", "with", "and", "or"};

double DiscogsClient::CalcularSimilaridadeTexto(const std::string &strTextoA, const std::string &strTextoB)
{
    if (strTextoA.empty() && strTextoB.empty()) return 1.0;
    if (strTextoA.empty() || strTextoB.empty()) return 0.0;

    std::string strNormA = NormalizarParaComparacao(strTextoA);
    std::string strNormB = NormalizarParaComparacao(strTextoB);

    if (TN::IgualSemCaixa(strNormA, strNormB)) return 1.0;
    if (strNormA.empty() || strNormB.empty()) return 0.0;

    // Contencao direta de substring caso uma englobe a outra
    if (Base::Contem(strNormA, strNormB) || Base::Contem(strNormB, strNormA))
    {
        double nA = (double) Base::Caracteres(strNormA), nB = (double) Base::Caracteres(strNormB);
        double dblRatio = std::min(nA, nB) / std::max(nA, nB);
        if (dblRatio >= 0.70)
        {
            return std::max(0.85, dblRatio);
        }
    }

    std::vector<std::string> lstPalavrasA = ExtrairPalavrasChave(strNormA);
    std::vector<std::string> lstPalavrasB = ExtrairPalavrasChave(strNormB);

    if (lstPalavrasA.empty() || lstPalavrasB.empty()) return 0.0;

    // OrdinalIgnoreCase: as chaves vao em minusculas
    std::set<std::string> setA, setB;
    for (auto &p : lstPalavrasA) setA.insert(TN::Minusculas(p));
    for (auto &p : lstPalavrasB) setB.insert(TN::Minusculas(p));

    std::set<std::string> setIntersecao;
    for (auto &p : setA)
        if (setB.count(p)) setIntersecao.insert(p);

    // Cobertura (recall) do termo pesquisado (A) em relacao ao candidato (B)
    double dblCoberturaAlvo = (double) setIntersecao.size() / setA.size();

    // Jaccard das palavras-chave
    std::set<std::string> setUniao(setA);
    setUniao.insert(setB.begin(), setB.end());
    double dblJaccard = !setUniao.empty() ? (double) setIntersecao.size() / setUniao.size() : 0.0;

    // Se o candidato nao cobrir ao menos 70% das palavras-chave essenciais do termo alvo, rejeita
    if (dblCoberturaAlvo < 0.70)
    {
        return std::min(dblCoberturaAlvo, dblJaccard);
    }

    return (dblCoberturaAlvo * 0.6) + (dblJaccard * 0.4);
}

/// Extrai tokens de palavras ignorando stopwords funcionais comuns que causam ruido na correspondencia.
std::vector<std::string> DiscogsClient::ExtrairPalavrasChave(const std::string &strTexto)
{
    std::vector<std::string> arrTokens = Base::Divide(strTexto, " ", true);
    std::vector<std::string> lstResultado;
    for (const std::string &strT : arrTokens)
    {
        if (!SET_STOPWORDS.count(TN::Minusculas(strT)))
        {
            lstResultado.push_back(strT);
        }
    }
    if (lstResultado.empty())
    {
        lstResultado.insert(lstResultado.end(), arrTokens.begin(), arrTokens.end());
    }
    return lstResultado;
}

std::string DiscogsClient::NormalizarParaComparacao(const std::string &strTexto)
{
    if (strTexto.empty()) return "";

    // Remove anotacoes como (Disc 1), [Live], etc.
    // Regex.Replace(strTexto, @"\s*[\(\[][^\]\)]*[\)\]]", " ")
    std::string strLimpo = TN::TiraAnotacoes(strTexto, " ");

    // Remove acentos
    std::string strSemAcento = RemoverAcentos(strLimpo);

    // Converte pontuacoes para espacos e passa para minusculas
    // Regex.Replace(strSemAcento, @"[^\w\s]", " ").ToLowerInvariant()
    std::string strAlfanumerico;
    for (uint32_t c : Base::CodePoints(strSemAcento))
    {
        if (TN::EhPalavra(c) || TN::EhEspaco(c)) Base::AcrescentaUtf8(strAlfanumerico, c);
        else strAlfanumerico += ' ';
    }
    strAlfanumerico = TN::Minusculas(strAlfanumerico);

    // Condensa espacos consecutivos
    strAlfanumerico = Base::Trim(TN::TrocaEspacos(strAlfanumerico, " "));

    return strAlfanumerico;
}

} // namespace ARCA::Modelo

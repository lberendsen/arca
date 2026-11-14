// ARCA no xOS - Modelo/ItunesResolver (de Modelo\ItunesResolver.cs).
#include "ItunesResolver.h"
#include "ExternalMetadataClient.h"
#include "TextoIdentificacao.h"

#include "Base/Http.h"
#include "Base/Json.h"
#include "Base/Texto.h"

#include <algorithm>
#include <cstdlib>
#include <map>

namespace ARCA::Modelo {

using Base::Json;
namespace TN = TextoNet;

namespace {

// LINUX: as expressoes regulares do original, uma a uma, sobre code points (o std::regex nao
// conhece as classes Unicode do .NET nem o IgnoreCase fora do ASCII).

// os digitos (\d+) a partir de pos; "" se nao houver nenhum
std::string Digitos(const std::vector<uint32_t> &c, size_t pos)
{
    std::string r;
    while (pos < c.size() && TN::EhDigito(c[pos])) Base::AcrescentaUtf8(r, c[pos++]);
    return r;
}

bool CasaSemCaixa(const std::vector<uint32_t> &c, size_t pos, const char *lit)
{
    std::vector<uint32_t> p = Base::CodePoints(lit);
    if (pos + p.size() > c.size()) return false;
    for (size_t i = 0; i < p.size(); i++)
        if (!TN::IgualSemCaixa(c[pos + i], p[i])) return false;
    return true;
}

// Regex.Match(s, @"/album/[^/]+/(\d+)", RegexOptions.IgnoreCase): o grupo 1, ou "" se nao casa
std::string CasaAlbum(const std::string &s)
{
    std::vector<uint32_t> c = Base::CodePoints(s);
    for (size_t i = 0; i < c.size(); i++)
    {
        if (!CasaSemCaixa(c, i, "/album/")) continue;
        size_t j = i + 7, k = j;
        while (k < c.size() && c[k] != '/') k++;
        if (k == j || k >= c.size()) continue;            // [^/]+ e depois "/"
        std::string d = Digitos(c, k + 1);
        if (!d.empty()) return d;
    }
    return "";
}

// Regex.Match(s, @"id=(\d+)", RegexOptions.IgnoreCase)
std::string CasaIdIgual(const std::string &s)
{
    std::vector<uint32_t> c = Base::CodePoints(s);
    for (size_t i = 0; i < c.size(); i++)
    {
        if (!CasaSemCaixa(c, i, "id=")) continue;
        std::string d = Digitos(c, i + 3);
        if (!d.empty()) return d;
    }
    return "";
}

// Regex.Match(s, @"^(\d{6,15})$")
std::string CasaSoDigitos(const std::string &s)
{
    std::vector<uint32_t> c = Base::CodePoints(s);
    size_t n = c.size();
    if (n > 0 && c[n - 1] == '\n') n--;                   // o "$" do .NET aceita um \n no fim
    if (n < 6 || n > 15) return "";
    for (size_t i = 0; i < n; i++)
        if (!TN::EhDigito(c[i])) return "";
    std::string r;
    for (size_t i = 0; i < n; i++) Base::AcrescentaUtf8(r, c[i]);
    return r;
}

// os casamentos de @"\s+X\s+" (X = "e", sem caixa, ou "&"), da esquerda para a direita
std::vector<std::pair<size_t, size_t>> Conectivos(const std::vector<uint32_t> &c, uint32_t x)
{
    std::vector<std::pair<size_t, size_t>> r;
    size_t i = 0;
    while (i < c.size())
    {
        if (TN::EhEspaco(c[i]))
        {
            size_t j = i;
            while (j < c.size() && TN::EhEspaco(c[j])) j++;
            if (j < c.size() && TN::IgualSemCaixa(c[j], x) && j + 1 < c.size() && TN::EhEspaco(c[j + 1]))
            {
                size_t k = j + 1;
                while (k < c.size() && TN::EhEspaco(c[k])) k++;
                r.push_back({i, k});
                i = k;
                continue;
            }
        }
        i++;
    }
    return r;
}

std::string Trecho(const std::vector<uint32_t> &c, size_t de, size_t ate)
{
    std::string r;
    for (size_t i = de; i < ate; i++) Base::AcrescentaUtf8(r, c[i]);
    return r;
}

// Regex.Replace(s, @"\s+X\s+", substituto, RegexOptions.IgnoreCase)
std::string TrocaConectivo(const std::string &s, uint32_t x, const std::string &substituto)
{
    std::vector<uint32_t> c = Base::CodePoints(s);
    std::string r;
    size_t ant = 0;
    for (auto &m : Conectivos(c, x))
    {
        r += Trecho(c, ant, m.first) + substituto;
        ant = m.second;
    }
    return r + Trecho(c, ant, c.size());
}

// Regex.Split(s, @"\s+X\s+", RegexOptions.IgnoreCase)
std::vector<std::string> DivideConectivo(const std::string &s, uint32_t x)
{
    std::vector<uint32_t> c = Base::CodePoints(s);
    std::vector<std::string> r;
    size_t ant = 0;
    for (auto &m : Conectivos(c, x))
    {
        r.push_back(Trecho(c, ant, m.first));
        ant = m.second;
    }
    r.push_back(Trecho(c, ant, c.size()));
    return r;
}

std::string Num(int64_t v) { return std::to_string(v); }

} // namespace

std::shared_ptr<TItunesAlbumInfo> ItunesResolver::ConsultarPorUpc(const std::string &strUpc, const ActLog &actLog)
{
    if (strUpc.empty()) return nullptr;

    std::string strUrl = "https://itunes.apple.com/lookup?upc=" + Base::UrlEncode(Base::Trim(strUpc)) + "&entity=song";
    if (actLog) actLog("iTunes: Consultando por UPC/EAN '" + strUpc + "'...");

    return ExecutarConsultaItunes(strUrl, actLog);
}

std::shared_ptr<TItunesAlbumInfo> ItunesResolver::ConsultarPorUrlOuId(const std::string &strUrlOuId, const ActLog &actLog)
{
    return ConsultarPorUrlOuId(strUrlOuId, 0, actLog);
}

bool ItunesResolver::EhLinkOuId(const std::string &strEntrada)
{
    if (strEntrada.empty()) return false;
    std::string strTexto = Base::Trim(strEntrada);
    return !CasaAlbum(strTexto).empty()
           || !CasaIdIgual(strTexto).empty()
           || !CasaSoDigitos(strTexto).empty();
}

std::shared_ptr<TItunesAlbumInfo> ItunesResolver::ConsultarPorUrlOuId(const std::string &strUrlOuId,
                                                                      int intQtdFaixasEsperadas, const ActLog &actLog)
{
    if (strUrlOuId.empty()) return nullptr;

    std::string strEntrada = Base::Trim(strUrlOuId);

    std::string matchUrl = CasaAlbum(strEntrada);
    if (matchUrl.empty())
    {
        matchUrl = CasaIdIgual(strEntrada);
    }
    if (matchUrl.empty())
    {
        matchUrl = CasaSoDigitos(strEntrada);
    }

    if (!matchUrl.empty())
    {
        std::string strCollectionId = matchUrl;
        if (actLog) actLog("iTunes: ID de álbum detectado: " + strCollectionId + ". Consultando via lookup...");

        std::string strUrl = "https://itunes.apple.com/lookup?id=" + strCollectionId + "&entity=song";
        // ARCA-ORIGINAL: o link exato vai com blnExigirContagem = true (o padrao): album do link com
        // contagem diferente da do CD e recusado ("nao e este disco"), enquanto o link do Discogs so
        // avisa ("ATENCAO: o lancamento do link tem N faixas"). Visto em 07/11/2026 com um album de
        // 19 faixas para um CD de 20.
        return ExecutarConsultaItunes(strUrl, actLog, intQtdFaixasEsperadas);
    }

    if (actLog) actLog("iTunes: Buscando álbum por termo '" + strEntrada + "'...");
    return ConsultarAlbum("", strEntrada, intQtdFaixasEsperadas, actLog);
}

std::shared_ptr<TItunesAlbumInfo> ItunesResolver::ConsultarPorTermo(const std::string &strTermo, const ActLog &actLog)
{
    return ConsultarAlbum("", strTermo, 0, actLog);
}

std::shared_ptr<TItunesAlbumInfo> ItunesResolver::ConsultarAlbum(const std::string &strArtista, const std::string &strAlbum,
                                                                 int intQtdFaixasEsperadas, const ActLog &actLog)
{
    return ConsultarAlbum(strArtista, strAlbum, intQtdFaixasEsperadas, true, actLog);
}

std::shared_ptr<TItunesAlbumInfo> ItunesResolver::ConsultarAlbum(const std::string &strArtista, const std::string &strAlbum,
                                                                 int intQtdFaixasEsperadas, bool blnExigirContagem,
                                                                 const ActLog &actLogRecebido)
{
    ActLog actLog = actLogRecebido ? actLogRecebido : ActLog([](const std::string &) {});

    std::string strArtistaLimpo = Base::Trim(strArtista);
    std::string strAlbumLimpo = Base::Trim(strAlbum);

    if (strArtistaLimpo.empty() && strAlbumLimpo.empty())
        return nullptr;

    std::vector<std::string> lstConsultas;

    // 1. Busca combinada: Artista + Album
    if (!strArtistaLimpo.empty() && !strAlbumLimpo.empty() && strArtistaLimpo != "Desconhecido")
    {
        lstConsultas.push_back(strArtistaLimpo + " " + strAlbumLimpo);

        // Normalizacoes de conectivos: ex: " e " -> " & ", ou remocao de " e "
        if (Base::Indice(strArtistaLimpo, " e ", true) >= 0)
        {
            lstConsultas.push_back(TrocaConectivo(strArtistaLimpo, 'e', " & ") + " " + strAlbumLimpo);
            lstConsultas.push_back(TrocaConectivo(strArtistaLimpo, 'e', " ") + " " + strAlbumLimpo);

            // Primeiro artista individual antes de " e "
            std::vector<std::string> arrPartes = DivideConectivo(strArtistaLimpo, 'e');
            if (!arrPartes.empty() && !Base::Trim(arrPartes[0]).empty())
            {
                lstConsultas.push_back(Base::Trim(arrPartes[0]) + " " + strAlbumLimpo);
            }
        }
        else if (Base::Indice(strArtistaLimpo, " & ", true) >= 0)
        {
            std::vector<std::string> arrPartes = DivideConectivo(strArtistaLimpo, '&');
            if (!arrPartes.empty() && !Base::Trim(arrPartes[0]).empty())
            {
                lstConsultas.push_back(Base::Trim(arrPartes[0]) + " " + strAlbumLimpo);
            }
        }
    }

    // 2. Busca por Album isolado se informado
    if (!strAlbumLimpo.empty())
    {
        if (std::find(lstConsultas.begin(), lstConsultas.end(), strAlbumLimpo) == lstConsultas.end())
            lstConsultas.push_back(strAlbumLimpo);
    }
    else if (!strArtistaLimpo.empty() && strArtistaLimpo != "Desconhecido")
    {
        lstConsultas.push_back(strArtistaLimpo);
    }

    std::vector<TItunesAlbumCandidato> lstTodosCandidatos;
    std::set<int64_t> setIdsVistos;

    for (const std::string &strTermoBusca : lstConsultas)
    {
        actLog("iTunes: Buscando candidatos por termo '" + strTermoBusca + "'...");
        std::vector<TItunesAlbumCandidato> lstParcial = BuscarCandidatos(strTermoBusca, 15);
        for (TItunesAlbumCandidato &cand : lstParcial)
        {
            if (setIdsVistos.insert(cand.lngCollectionId).second)
            {
                cand.intScore = CalcularPontuacao(cand, strArtistaLimpo, strAlbumLimpo, intQtdFaixasEsperadas);
                lstTodosCandidatos.push_back(cand);
            }
        }

        // Se ja achou um candidato com pontuacao de correspondencia perfeita, nao precisa consultar termos mais vagos
        bool blnAchouExcelente = false;
        for (const TItunesAlbumCandidato &c : lstTodosCandidatos)
        {
            if (c.intScore >= 180) { blnAchouExcelente = true; break; }
        }
        if (blnAchouExcelente) break;
    }

    if (lstTodosCandidatos.empty())
    {
        actLog("iTunes: Nenhum candidato a álbum encontrado.");
        return nullptr;
    }

    // Ordena os candidatos pela maior pontuacao. Se o melhor for recusado
    // (nenhum disco dele tem as faixas deste CD), tenta os seguintes -
    // antes, desistia no primeiro.
    // LINUX: List<T>.Sort do .NET 4.0 (nao estavel), para o empate sair na mesma ordem.
    TN::OrdenaComoNet(lstTodosCandidatos, [](const TItunesAlbumCandidato &a, const TItunesAlbumCandidato &b) {
        return b.intScore < a.intScore ? -1 : (b.intScore > a.intScore ? 1 : 0);
    });
    for (size_t i = 0; i < lstTodosCandidatos.size() && i < 4; i++)
    {
        const TItunesAlbumCandidato &candEleito = lstTodosCandidatos[i];
        actLog("iTunes: Candidato eleito: '" + candEleito.strCollectionName + "' de '" + candEleito.strArtistName +
               "' (Faixas: " + Num(candEleito.intTrackCount) + ", ID: " + Num(candEleito.lngCollectionId) +
               ", Score: " + Num(candEleito.intScore) + "). Carregando faixas...");

        std::string strUrlLookup = "https://itunes.apple.com/lookup?id=" + Num(candEleito.lngCollectionId) + "&entity=song";
        std::shared_ptr<TItunesAlbumInfo> objAlbum =
            ExecutarConsultaItunes(strUrlLookup, actLog, intQtdFaixasEsperadas, blnExigirContagem);
        if (objAlbum) return objAlbum;
    }
    actLog("iTunes: nenhum candidato tem um disco com as faixas deste CD.");
    return nullptr;
}

std::vector<ItunesResolver::TItunesAlbumCandidato> ItunesResolver::BuscarCandidatos(const std::string &strTermo,
                                                                                   int intLimit)
{
    std::vector<TItunesAlbumCandidato> lstCandidatos;
    if (strTermo.empty()) return lstCandidatos;

    std::string strUrlBusca = "https://itunes.apple.com/search?term=" + Base::UrlEncode(Base::Trim(strTermo)) +
                              "&entity=album&limit=" + Num(intLimit);

    try
    {
        std::string strJsonBusca = ExternalMetadataClient::FetchString(strUrlBusca);
        if (strJsonBusca.empty()) return lstCandidatos;

        Json dicBusca = TN::Desserializa(strJsonBusca);
        if (dicBusca.is_null() || !TN::Tem(dicBusca, "results")) return lstCandidatos;

        const Json &lstResultados = dicBusca["results"];
        if (!lstResultados.is_array()) return lstCandidatos;

        for (const Json &d : lstResultados)
        {
            if (!d.is_object() || !TN::Tem(d, "collectionId")) continue;

            TItunesAlbumCandidato cand;
            cand.lngCollectionId = TN::ParaInt64(d["collectionId"]);
            cand.strArtistName = TN::Tem(d, "artistName") ? TN::TextoDe(d["artistName"]) : "";
            cand.strCollectionName = TN::Tem(d, "collectionName") ? TN::TextoDe(d["collectionName"]) : "";
            cand.intTrackCount = TN::Tem(d, "trackCount") ? TN::ParaInt32(d["trackCount"]) : 0;
            cand.strArtworkUrl100 = TN::Tem(d, "artworkUrl100") ? TN::TextoDe(d["artworkUrl100"]) : "";
            lstCandidatos.push_back(cand);
        }
    }
    catch (...) { }

    return lstCandidatos;
}

std::set<std::string> ItunesResolver::Palavras(const std::string &strTexto)
{
    // new HashSet<string>(Regex.Split(strTexto ?? "", @"[^\p{L}\p{N}]+"), StringComparer.Ordinal)
    std::set<std::string> r;
    std::vector<uint32_t> c = Base::CodePoints(strTexto);
    std::string atual;
    size_t i = 0;
    while (i < c.size())
    {
        if (TN::EhLetra(c[i]) || TN::EhNumero(c[i])) Base::AcrescentaUtf8(atual, c[i++]);
        else
        {
            while (i < c.size() && !(TN::EhLetra(c[i]) || TN::EhNumero(c[i]))) i++;
            r.insert(atual);
            atual.clear();
        }
    }
    r.insert(atual);
    return r;
}

int ItunesResolver::CalcularPontuacao(const TItunesAlbumCandidato &cand, const std::string &strArtistaEsperado,
                                      const std::string &strAlbumEsperado, int intQtdFaixasEsperadas)
{
    int intPontos = 0;

    std::string strAlbumNormalizadoEsperado = NormalizarTextoParaComparacao(strAlbumEsperado);
    std::string strAlbumNormalizadoCand = NormalizarTextoParaComparacao(cand.strCollectionName);

    // 1. Comparacao de Titulo do Album
    if (!strAlbumNormalizadoEsperado.empty() && !strAlbumNormalizadoCand.empty())
    {
        if (strAlbumNormalizadoCand == strAlbumNormalizadoEsperado)
        {
            intPontos += 100;
        }
        else if (Base::ComecaCom(strAlbumNormalizadoCand, strAlbumNormalizadoEsperado) ||
                 Base::ComecaCom(strAlbumNormalizadoEsperado, strAlbumNormalizadoCand))
        {
            intPontos += 60;
        }
        else if (Base::Contem(strAlbumNormalizadoCand, strAlbumNormalizadoEsperado))
        {
            intPontos += 40;
        }
        else
        {
            // Titulo sem relacao nenhuma com o buscado: nao e o album, por
            // mais que o numero de faixas coincida (Abbey Road nao e o
            // White Album so por ter 17 faixas).
            intPontos -= 80;
        }

        // Penalidade para "II", "Vol. 2", "Single", "EP" quando o esperado nao
        // os tem - por PALAVRA: por pedaco de texto, "ep" pegava "deep" e
        // " 2" penalizava "Tropicalia 2" quando era o proprio album.
        std::set<std::string> setEsperado = Palavras(strAlbumNormalizadoEsperado), setCand = Palavras(strAlbumNormalizadoCand);
        if (!setEsperado.count("ii") && !setEsperado.count("2") && (setCand.count("ii") || setCand.count("2")))
        {
            intPontos -= 40;
        }
        if (!setEsperado.count("single") && setCand.count("single"))
        {
            intPontos -= 50;
        }
        if (!setEsperado.count("ep") && setCand.count("ep"))
        {
            intPontos -= 30;
        }
    }

    // 2. Comparacao de Quantidade de Faixas
    if (intQtdFaixasEsperadas > 0 && cand.intTrackCount > 0)
    {
        if (cand.intTrackCount == intQtdFaixasEsperadas)
        {
            intPontos += 100;
        }
        else if (std::abs(cand.intTrackCount - intQtdFaixasEsperadas) == 1)
        {
            intPontos += 30;
        }
        else if (cand.intTrackCount > intQtdFaixasEsperadas)
        {
            // Mais faixas que o CD: pode ser um album de varios discos, e
            // este CD, um deles. Nao penaliza - quem decide e a escolha do
            // disco, depois de carregar as faixas (EscolherDisco).
        }
        else
        {
            // Penaliza albuns cuja quantidade de faixas divirja muito
            intPontos -= std::min(50, std::abs(cand.intTrackCount - intQtdFaixasEsperadas) * 5);
        }
    }

    // 3. Comparacao de Artista
    std::string strArtistaNormalizadoEsperado = NormalizarTextoParaComparacao(strArtistaEsperado);
    std::string strArtistaNormalizadoCand = NormalizarTextoParaComparacao(cand.strArtistName);

    if (!strArtistaNormalizadoEsperado.empty() && !strArtistaNormalizadoCand.empty() &&
        strArtistaNormalizadoEsperado != "desconhecido")
    {
        if (strArtistaNormalizadoCand == strArtistaNormalizadoEsperado)
        {
            intPontos += 50;
        }
        else
        {
            std::vector<std::string> arrPalavras = Base::Divide(strArtistaNormalizadoEsperado, " ", true);
            int intPalavrasCasadas = 0;
            for (const std::string &p : arrPalavras)
            {
                if (Base::Caracteres(p) >= 3 && Base::Contem(strArtistaNormalizadoCand, p))
                {
                    intPalavrasCasadas++;
                }
            }
            intPontos += intPalavrasCasadas * 15;
            // Nenhuma palavra do artista em comum: e outro artista com um
            // album de mesmo nome (o "Back in Black" do Whodini, de 10
            // faixas, passava a frente do AC/DC).
            if (intPalavrasCasadas == 0) intPontos -= 60;
        }
    }

    return intPontos;
}

std::string ItunesResolver::NormalizarTextoParaComparacao(const std::string &strTexto)
{
    if (strTexto.empty()) return "";
    std::string strLimpo = Base::Trim(TN::Minusculas(strTexto));

    // Remove acentos
    // LINUX: Normalize(FormD) e o filtro por UnicodeCategory/IsLetterOrDigit/IsWhiteSpace, com as
    // tabelas de TextoIdentificacao.
    std::string strDecomposto = TN::SemMarcas(strLimpo);
    std::string sb;
    for (uint32_t c : Base::CodePoints(strDecomposto))
    {
        if (!TN::EhMarca(c))
        {
            if (TN::EhLetraOuDigito(c) || TN::EhEspaco(c))
            {
                Base::AcrescentaUtf8(sb, c);
            }
        }
    }
    return Base::Trim(TN::TrocaEspacos(sb, " "));
}

bool ItunesResolver::EscolherDisco(TItunesAlbumInfo &objAlbum, int intQtdFaixasEsperadas, bool blnExigirContagem,
                                   const ActLog &actLog)
{
    std::map<int, std::vector<TItunesTrackInfo>> dicDiscos;
    for (const TItunesTrackInfo &t : objAlbum.lstFaixas)
        dicDiscos[t.intDiscNumber].push_back(t);
    if (intQtdFaixasEsperadas <= 0) return true;
    int intQtd = (int) objAlbum.lstFaixas.size();
    if (dicDiscos.size() == 1)
    {
        if (intQtd == intQtdFaixasEsperadas) return true;
        if (!blnExigirContagem)
        {
            if (actLog) actLog("iTunes: o álbum tem " + Num(intQtd) + " faixas e o CD tem " + Num(intQtdFaixasEsperadas) +
                               " — confira as durações antes de escolher.");
            return true;
        }
        if (actLog) actLog("iTunes: o álbum tem " + Num(intQtd) + " faixas e o CD tem " + Num(intQtdFaixasEsperadas) +
                           " — não é este disco.");
        return false;
    }
    std::vector<int> lstQueBatem;
    for (auto &kv : dicDiscos)
        if ((int) kv.second.size() == intQtdFaixasEsperadas) lstQueBatem.push_back(kv.first);
    if (lstQueBatem.empty() && !blnExigirContagem)
    {
        // A pessoa vai conferir: o disco de contagem mais proxima.
        int intMelhor = -1;
        for (auto &kv : dicDiscos)
            if (intMelhor < 0 || std::abs((int) kv.second.size() - intQtdFaixasEsperadas) <
                                     std::abs((int) dicDiscos[intMelhor].size() - intQtdFaixasEsperadas))
                intMelhor = kv.first;
        lstQueBatem.push_back(intMelhor);
    }
    if (lstQueBatem.empty())
    {
        if (actLog) actLog("iTunes: o álbum tem " + Num((int64_t) dicDiscos.size()) + " discos e nenhum tem as " +
                           Num(intQtdFaixasEsperadas) + " faixas deste CD.");
        return false;
    }
    if (lstQueBatem.size() > 1 && actLog)
        actLog("iTunes: " + Num((int64_t) lstQueBatem.size()) + " discos do álbum têm " + Num(intQtdFaixasEsperadas) +
               " faixas; usando o disco " + Num(lstQueBatem[0]) + " — confira os títulos.");
    else if (actLog)
        actLog("iTunes: álbum de " + Num((int64_t) dicDiscos.size()) + " discos; este CD é o disco " +
               Num(lstQueBatem[0]) + ".");
    objAlbum.lstFaixas = dicDiscos[lstQueBatem[0]];
    return true;
}

std::shared_ptr<TItunesAlbumInfo> ItunesResolver::ExecutarConsultaItunes(const std::string &strUrl, const ActLog &actLog,
                                                                         int intQtdFaixasEsperadas, bool blnExigirContagem)
{
    try
    {
        std::string strJson = ExternalMetadataClient::FetchString(strUrl);
        if (strJson.empty()) return nullptr;

        Json dicResposta = TN::Desserializa(strJson);
        if (dicResposta.is_null() || !TN::Tem(dicResposta, "results")) return nullptr;

        const Json &lstResultados = dicResposta["results"];
        if (!lstResultados.is_array() || lstResultados.empty()) return nullptr;

        std::shared_ptr<TItunesAlbumInfo> objAlbum;

        for (const Json &dicItem : lstResultados)
        {
            if (!dicItem.is_object()) continue;

            std::string strWrapperType = TN::Tem(dicItem, "wrapperType") ? TN::TextoDe(dicItem["wrapperType"]) : "";

            if (strWrapperType == "collection")
            {
                objAlbum = std::make_shared<TItunesAlbumInfo>();
                if (TN::Tem(dicItem, "collectionId")) objAlbum->lngCollectionId = TN::ParaInt64(dicItem["collectionId"]);
                if (TN::Tem(dicItem, "artistName")) objAlbum->strArtistName = TN::TextoDe(dicItem["artistName"]);
                if (TN::Tem(dicItem, "collectionName")) objAlbum->strCollectionName = TN::TextoDe(dicItem["collectionName"]);
                if (TN::Tem(dicItem, "primaryGenreName")) objAlbum->strPrimaryGenreName = TN::TextoDe(dicItem["primaryGenreName"]);
                if (TN::Tem(dicItem, "trackCount")) objAlbum->intTrackCount = TN::ParaInt32(dicItem["trackCount"]);

                if (TN::Tem(dicItem, "releaseDate"))
                {
                    std::string strRelDate = TN::TextoDe(dicItem["releaseDate"]);
                    objAlbum->strReleaseDate = strRelDate;
                    if (Base::Caracteres(strRelDate) >= 4) objAlbum->strReleaseYear = Base::Sub(strRelDate, 0, 4);
                }

                if (TN::Tem(dicItem, "artworkUrl100"))
                {
                    std::string strArt100 = TN::TextoDe(dicItem["artworkUrl100"]);
                    objAlbum->strArtworkUrl100 = strArt100;
                    objAlbum->strArtworkUrl600 = Base::Substitui(strArt100, "100x100bb", "600x600bb");
                    objAlbum->strArtworkUrl1400 = Base::Substitui(strArt100, "100x100bb", "1400x1400bb");
                }
            }
            else if (strWrapperType == "track" &&
                     (!TN::Tem(dicItem, "kind") || dicItem["kind"].is_null() || TN::TextoDe(dicItem["kind"]) == "song"))
            {
                // So musica: edicoes deluxe listam videoclipes e encartes
                // digitais como "faixas", e eles desmontariam a escolha do disco.
                if (!objAlbum) objAlbum = std::make_shared<TItunesAlbumInfo>();

                TItunesTrackInfo objTrack;
                if (TN::Tem(dicItem, "trackNumber")) objTrack.intTrackNumber = TN::ParaInt32(dicItem["trackNumber"]);
                objTrack.intDiscNumber = TN::Tem(dicItem, "discNumber") ? TN::ParaInt32(dicItem["discNumber"]) : 1;
                if (TN::Tem(dicItem, "trackName")) objTrack.strTrackName = TN::TextoDe(dicItem["trackName"]);
                if (TN::Tem(dicItem, "artistName")) objTrack.strArtistName = TN::TextoDe(dicItem["artistName"]);
                if (TN::Tem(dicItem, "trackTimeMillis")) objTrack.intTrackTimeMillis = TN::ParaInt32(dicItem["trackTimeMillis"]);

                objAlbum->lstFaixas.push_back(objTrack);
            }
        }

        if (objAlbum && !objAlbum->lstFaixas.empty())
        {
            // Disco e depois faixa: ordenar so pela faixa intercalava os
            // discos de um album duplo (1, 1, 2, 2...).
            // LINUX: List<T>.Sort do .NET 4.0 (nao estavel), como no original.
            TN::OrdenaComoNet(objAlbum->lstFaixas, [](const TItunesTrackInfo &a, const TItunesTrackInfo &b) {
                int x = a.intDiscNumber != b.intDiscNumber ? a.intDiscNumber : a.intTrackNumber;
                int y = a.intDiscNumber != b.intDiscNumber ? b.intDiscNumber : b.intTrackNumber;
                return x < y ? -1 : (x > y ? 1 : 0);
            });
            if (!EscolherDisco(*objAlbum, intQtdFaixasEsperadas, blnExigirContagem, actLog)) return nullptr;
        }

        return objAlbum;
    }
    catch (const std::exception &ex)
    {
        if (actLog) actLog(std::string("iTunes: Erro durante requisição: ") + ex.what());
        return nullptr;
    }
}

} // namespace ARCA::Modelo

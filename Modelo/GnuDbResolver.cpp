// ARCA no xOS - Modelo/GnuDbResolver (de Modelo\GnuDbResolver.cs).
#include "GnuDbResolver.h"
#include "AccurateRip.h"
#include "ExternalMetadataClient.h"
#include "TextoIdentificacao.h"

#include "Base/Texto.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>

namespace ARCA::Modelo {

namespace TN = TextoNet;

namespace {

std::string Num(int64_t v) { return std::to_string(v); }

// int.Parse de uma sequencia de digitos (lanca como o .NET quando nao cabe num Int32)
int ParseInt(const std::string &s)
{
    int v;
    if (!Base::TentaInt(s, v)) throw std::runtime_error("O valor era muito grande ou muito pequeno para um Int32.");
    return v;
}

// Regex.Match(line, @"^#\s*(\d+)\s*$"): o grupo 1, ou "" se nao casa
std::string CasaOffset(const std::string &line)
{
    std::vector<uint32_t> c = Base::CodePoints(line);
    size_t n = c.size();
    if (n > 0 && c[n - 1] == '\n') n--;
    if (n == 0 || c[0] != '#') return "";
    size_t i = 1;
    while (i < n && TN::EhEspaco(c[i])) i++;
    size_t d = i;
    std::string r;
    while (i < n && TN::EhDigito(c[i])) Base::AcrescentaUtf8(r, c[i++]);
    if (i == d) return "";
    while (i < n && TN::EhEspaco(c[i])) i++;
    return i == n ? r : "";
}

// Regex.Match(line, @"^TTITLE(\d+)=(.*)"): grupos 1 e 2
bool CasaTTitle(const std::string &line, std::string &strIdx, std::string &strTexto)
{
    if (line.compare(0, 6, "TTITLE") != 0) return false;
    std::vector<uint32_t> c = Base::CodePoints(line);
    size_t i = 6;
    strIdx.clear();
    while (i < c.size() && TN::EhDigito(c[i])) Base::AcrescentaUtf8(strIdx, c[i++]);
    if (strIdx.empty() || i >= c.size() || c[i] != '=') return false;
    i++;
    strTexto.clear();
    while (i < c.size() && c[i] != '\n') Base::AcrescentaUtf8(strTexto, c[i++]);    // "." nao pega \n
    return true;
}

// Regex.Replace(strLimpo, @"^" + Regex.Escape(strArtista) + @"\s*[-=:/_]\s*", "", RegexOptions.IgnoreCase)
std::string TiraArtistaDoComeco(const std::string &strLimpo, const std::string &strArtista)
{
    std::vector<uint32_t> c = Base::CodePoints(strLimpo), a = Base::CodePoints(strArtista);
    if (a.size() > c.size()) return strLimpo;
    for (size_t k = 0; k < a.size(); k++)
        if (!TN::IgualSemCaixa(c[k], a[k])) return strLimpo;
    size_t i = a.size();
    while (i < c.size() && TN::EhEspaco(c[i])) i++;
    if (i >= c.size() || !(c[i] == '-' || c[i] == '=' || c[i] == ':' || c[i] == '/' || c[i] == '_')) return strLimpo;
    i++;
    while (i < c.size() && TN::EhEspaco(c[i])) i++;
    std::string r;
    for (; i < c.size(); i++) Base::AcrescentaUtf8(r, c[i]);
    return r;
}

} // namespace

int GnuDbResolver::CddbSum(int n)
{
    int ret = 0;
    while (n > 0)
    {
        ret += (n % 10);
        n /= 10;
    }
    return ret;
}

std::string GnuDbResolver::ResolveToc(const std::string &strCue, int64_t lngBytesBin, std::string &artist,
                                      std::string &album, std::string &genre, std::string &year,
                                      std::vector<std::string> &trackTitles)
{
    artist.clear();
    album.clear();
    genre.clear();
    year.clear();
    trackTitles.clear();

    std::shared_ptr<TTocAudio> objToc = TTocAudio::DeCue(strCue, lngBytesBin);
    if (!objToc || objToc->QtdFaixas() == 0) return "Sem faixas";
    if (objToc->intLeadOut <= objToc->lstInicios[objToc->QtdFaixas() - 1])
        return "O fim do disco é desconhecido (sem lead-out no CUE e sem .bin): consultar o GnuDB com um fim inventado acharia outro disco.";
    int numTracks = objToc->QtdFaixas();

    // freedb conta todas as faixas, inclusive a de dados, com os 150
    // setores do pre-gap; o fim e o lead-out fisico do disco.
    std::vector<int> lstTodas(objToc->lstInicios);
    if (objToc->intDadosAntes >= 0) lstTodas.insert(lstTodas.begin(), objToc->intDadosAntes);
    int intFim = objToc->intLeadOut;
    if (objToc->intInicioDados >= 0) { lstTodas.push_back(objToc->intInicioDados); intFim = objToc->intLeadOutFisico > 0 ? objToc->intLeadOutFisico : intFim; }
    uint32_t id1, id2, idFreedb;
    AccurateRip::CalcularIds(*objToc, id1, id2, idFreedb);
    char discIdHex[16];
    snprintf(discIdHex, sizeof discIdHex, "%08x", idFreedb);
    int totalSeconds = (intFim + 150) / 75;

    std::string queryParams;
    queryParams += std::string(discIdHex) + "+" + Num((int64_t) lstTodas.size());
    for (int intLba : lstTodas) queryParams += "+" + Num(intLba + 150);
    queryParams += "+" + Num(totalSeconds);

    std::string urlQuery = "https://gnudb.gnudb.org/~cddb/cddb.cgi?cmd=cddb+query+" + queryParams +
                           "&hello=anon+anon.com+ExactAudioCopy+1.3&proto=6";

    std::string responseQuery = FetchUrl(urlQuery);
    if (responseQuery.empty()) return "Falha ao conectar no GnuDB.";

    std::vector<std::string> lines = Base::DivideQualquer(responseQuery, "\r\n", true);
    if (lines.empty()) return "Resposta vazia do GnuDB.";

    std::string firstLine = lines[0];
    std::string category;
    std::string finalDiscId;
    bool blnTemCategoria = false;
    bool blnInexata = Base::ComecaCom(firstLine, "211");

    if (Base::ComecaCom(firstLine, "200"))
    {
        std::vector<std::string> parts = Base::Divide(firstLine, " ");
        if (parts.size() >= 3)
        {
            category = parts[1];
            finalDiscId = parts[2];
            blnTemCategoria = true;
        }
    }
    else if (Base::ComecaCom(firstLine, "210") || blnInexata)
    {
        if (lines.size() > 1 && !Base::ComecaCom(lines[1], "."))
        {
            std::vector<std::string> parts = Base::Divide(lines[1], " ");
            if (parts.size() >= 2)
            {
                category = parts[0];
                finalDiscId = parts[1];
                blnTemCategoria = true;
            }
        }
    }

    if (!blnTemCategoria)
        return "Nenhuma correspondência exata encontrada no GnuDB.";

    std::string urlRead = "https://gnudb.gnudb.org/~cddb/cddb.cgi?cmd=cddb+read+" + category + "+" + finalDiscId +
                          "&hello=anon+anon.com+ExactAudioCopy+1.3&proto=6";
    std::string responseRead = FetchUrl(urlRead);
    if (responseRead.empty()) return "Falha ao ler os metadados do GnuDB.";

    std::vector<std::string> readLines = Base::DivideQualquer(responseRead, "\r\n", true);

    // Correspondencia inexata (211) e "parecido", nao "este": confere
    // as posicoes das faixas que o proprio registro traz (comentario
    // # Track frame offsets) com as do disco - mesma quantidade e cada
    // uma a ate 2 s. Nao conferindo, e outro disco.
    if (blnInexata)
    {
        std::vector<int> lstRegistro;
        bool blnNaLista = false;
        for (const std::string &line : readLines)
        {
            if (TN::ComecaComSemCaixa(line, "# Track frame offsets")) { blnNaLista = true; continue; }
            if (!blnNaLista) continue;
            std::string mOff = CasaOffset(line);
            if (!mOff.empty()) lstRegistro.push_back(ParseInt(mOff));
            else if (!lstRegistro.empty()) break;
        }
        bool blnConfere = lstRegistro.size() == lstTodas.size();
        for (size_t i = 0; blnConfere && i < lstRegistro.size(); i++)
            if (std::abs(lstRegistro[i] - (lstTodas[i] + 150)) > 150) blnConfere = false;
        if (!blnConfere)
            return "O GnuDB só achou um disco parecido (" + Num((int64_t) lstRegistro.size()) + " faixas no registro, " +
                   Num((int64_t) lstTodas.size()) + " no CD, ou posições diferentes): não é este.";
    }

    std::map<int, std::string> tt;

    // No formato xmcd, titulo longo vem em varias linhas com a mesma
    // chave, e o texto continua de uma para a outra (as vezes no meio de
    // uma palavra). Antes, cada linha sobrescrevia a anterior.
    std::string sbDTitle;
    for (const std::string &line : readLines)
        if (Base::ComecaCom(line, "DTITLE=")) sbDTitle += line.substr(7);

    for (const std::string &line : readLines)
    {
        if (Base::ComecaCom(line, "DTITLE="))
        {
            if (sbDTitle.empty()) continue;   // ja tratado: o titulo inteiro, uma vez so
            std::string dTitle = sbDTitle;
            sbDTitle.clear();
            size_t splitIdx = dTitle.find(" / ");
            if (splitIdx != std::string::npos)
            {
                artist = Base::Trim(dTitle.substr(0, splitIdx));
                album = Base::Trim(dTitle.substr(splitIdx + 3));
            }
            else
            {
                artist = "Desconhecido";
                album = Base::Trim(dTitle);
            }
            // O titulo vai para as faixas como o registro o traz. Limpar
            // aqui (tirar o artista do comeco, apagar parenteses do fim)
            // era o ARCA decidindo o nome do album: "Queen Greatest Hits"
            // virava "Greatest Hits"; "(Vol. 2)" sumia. A limpeza serve
            // so para BUSCAR (DiscogsClient), nunca para gravar.
        }
        else if (Base::ComecaCom(line, "DYEAR="))
        {
            year = Base::Trim(line.substr(6));
        }
        else if (Base::ComecaCom(line, "DGENRE="))
        {
            genre = Base::Trim(line.substr(7));
        }
        else if (Base::ComecaCom(line, "TTITLE"))
        {
            std::string strIdx, strTexto;
            if (CasaTTitle(line, strIdx, strTexto))
            {
                int tIdx = ParseInt(strIdx);
                auto it = tt.find(tIdx);
                tt[tIdx] = it != tt.end() ? it->second + strTexto : strTexto;
            }
        }
    }

    for (int i = 0; i < numTracks; i++)
    {
        auto it = tt.find(i);
        if (it != tt.end() && !Base::Trim(it->second).empty())
            trackTitles.push_back(Base::Trim(it->second));   // aparado so depois de juntar as linhas
        else
            trackTitles.push_back("Faixa " + Num(i + 1));
    }

    for (size_t i = 0; i < trackTitles.size(); i++)
    {
        if (!artist.empty() && artist != "Desconhecido")
        {
            std::string prefixSlash = artist + " / ";
            if (TN::ComecaComSemCaixa(trackTitles[i], prefixSlash))
                trackTitles[i] = Base::Trim(Base::Sub(trackTitles[i], Base::Caracteres(prefixSlash)));

            std::string prefixDash = artist + " - ";
            if (TN::ComecaComSemCaixa(trackTitles[i], prefixDash))
                trackTitles[i] = Base::Trim(Base::Sub(trackTitles[i], Base::Caracteres(prefixDash)));
        }
    }

    return "OK";
}

std::string GnuDbResolver::FetchUrl(const std::string &url)
{
    try
    {
        return ExternalMetadataClient::FetchString(url);
    }
    catch (const std::exception &ex)
    {
        // Console.WriteLine: no xOS vai para a saida padrao do programa
        printf("Erro GnuDB: %s\n", ex.what());
        fflush(stdout);
        return "";
    }
}

std::string GnuDbResolver::HigienizarTituloAlbum(const std::string &strArtista, const std::string &strAlbum)
{
    if (strAlbum.empty()) return "";
    std::string strLimpo = Base::Trim(strAlbum);

    if (!strArtista.empty() && strArtista != "Desconhecido")
    {
        // So tira o artista do comeco quando ha um separador de verdade
        // ("Queen - Greatest Hits"). Com o separador opcional, bastava um
        // espaco: "Queen II" virava "II", "Led Zeppelin IV" virava "IV".
        std::string strTentativaPrefixo = TiraArtistaDoComeco(strLimpo, strArtista);
        if (!Base::VazioOuBranco(strTentativaPrefixo))
        {
            strLimpo = strTentativaPrefixo;
        }

        std::string strArtistaSemEspacos = TN::TrocaEspacos(strArtista, "");
        strLimpo = TiraArtistaDoComeco(strLimpo, strArtistaSemEspacos);

        std::string strArtistaSemAcentoNemEspaco = RemoverAcentos(strArtistaSemEspacos);
        std::string strLimpoSemAcento = RemoverAcentos(strLimpo);
        if (TN::ComecaComSemCaixa(strLimpoSemAcento, strArtistaSemAcentoNemEspaco))
        {
            // LINUX: o original indexa por char (UTF-16); aqui por code point (so difere fora do BMP)
            std::vector<uint32_t> c = Base::CodePoints(strLimpo);
            size_t intTamPrefixo = Base::Caracteres(strArtistaSemAcentoNemEspaco);
            if (c.size() > intTamPrefixo && (c[intTamPrefixo] == '=' || c[intTamPrefixo] == '-' || c[intTamPrefixo] == ':' ||
                                             c[intTamPrefixo] == '_' || c[intTamPrefixo] == '/'))
            {
                strLimpo = Base::Trim(Base::Sub(strLimpo, intTamPrefixo + 1));
            }
        }
    }

    strLimpo = TN::TiraParentesesDoFim(strLimpo);
    return Base::Trim(strLimpo);
}

std::string GnuDbResolver::RemoverAcentos(const std::string &strTexto)
{
    if (strTexto.empty()) return "";
    // LINUX: Normalize(FormD), fora os NonSpacingMark, Normalize(FormC) - TextoNet::SemMarcas
    return TN::SemMarcas(strTexto);
}

} // namespace ARCA::Modelo

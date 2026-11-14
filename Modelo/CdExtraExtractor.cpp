// ARCA no xOS - Modelo/CdExtraExtractor (de Modelo\CdExtraExtractor.cs).
#include "CdExtraExtractor.h"

#include "Base/Arquivo.h"
#include "Base/Hash.h"
#include "Base/Texto.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <unistd.h>

namespace ARCA::Modelo {

using namespace ARCA::Base;

namespace {

const char *const arrExtensoesVideo[] = { ".mpg", ".mpeg", ".avi", ".mov", ".wmv", ".mp4", ".mkv", ".m4v" };

// string.Format("{0:F2}") na cultura pt-BR: virgula decimal, sem separador de milhar
std::string F(double v, int casas)
{
    char b[64];
    snprintf(b, sizeof b, "%.*f", casas, v);
    for (char *p = b; *p; p++)
        if (*p == '.') *p = ',';
    return b;
}

// StringComparer.OrdinalIgnoreCase: compara as letras em maiusculas
std::string ChaveSemCaixa(const std::string &s) { return Maiusculas(s); }

// ---- WebUtility.HtmlDecode ----
// LINUX: o .NET traz a tabela das 253 entidades do HTML 4 (as 252 da norma e &apos;); aqui ela
// vem escrita, com os mesmos nomes e valores.
const std::map<std::string, uint32_t> &Entidades()
{
    static const std::map<std::string, uint32_t> m = {
    {"AElig", 198}, {"Aacute", 193}, {"Acirc", 194}, {"Agrave", 192}, {"Alpha", 913}, {"Aring", 197},
    {"Atilde", 195}, {"Auml", 196}, {"Beta", 914}, {"Ccedil", 199}, {"Chi", 935}, {"Dagger", 8225},
    {"Delta", 916}, {"ETH", 208}, {"Eacute", 201}, {"Ecirc", 202}, {"Egrave", 200}, {"Epsilon", 917},
    {"Eta", 919}, {"Euml", 203}, {"Gamma", 915}, {"Iacute", 205}, {"Icirc", 206}, {"Igrave", 204},
    {"Iota", 921}, {"Iuml", 207}, {"Kappa", 922}, {"Lambda", 923}, {"Mu", 924}, {"Ntilde", 209}, {"Nu", 925},
    {"OElig", 338}, {"Oacute", 211}, {"Ocirc", 212}, {"Ograve", 210}, {"Omega", 937}, {"Omicron", 927},
    {"Oslash", 216}, {"Otilde", 213}, {"Ouml", 214}, {"Phi", 934}, {"Pi", 928}, {"Prime", 8243},
    {"Psi", 936}, {"Rho", 929}, {"Scaron", 352}, {"Sigma", 931}, {"THORN", 222}, {"Tau", 932},
    {"Theta", 920}, {"Uacute", 218}, {"Ucirc", 219}, {"Ugrave", 217}, {"Upsilon", 933}, {"Uuml", 220},
    {"Xi", 926}, {"Yacute", 221}, {"Yuml", 376}, {"Zeta", 918}, {"aacute", 225}, {"acirc", 226},
    {"acute", 180}, {"aelig", 230}, {"agrave", 224}, {"alefsym", 8501}, {"alpha", 945}, {"amp", 38},
    {"and", 8743}, {"ang", 8736}, {"apos", 39}, {"aring", 229}, {"asymp", 8776}, {"atilde", 227},
    {"auml", 228}, {"bdquo", 8222}, {"beta", 946}, {"brvbar", 166}, {"bull", 8226}, {"cap", 8745},
    {"ccedil", 231}, {"cedil", 184}, {"cent", 162}, {"chi", 967}, {"circ", 710}, {"clubs", 9827},
    {"cong", 8773}, {"copy", 169}, {"crarr", 8629}, {"cup", 8746}, {"curren", 164}, {"dArr", 8659},
    {"dagger", 8224}, {"darr", 8595}, {"deg", 176}, {"delta", 948}, {"diams", 9830}, {"divide", 247},
    {"eacute", 233}, {"ecirc", 234}, {"egrave", 232}, {"empty", 8709}, {"emsp", 8195}, {"ensp", 8194},
    {"epsilon", 949}, {"equiv", 8801}, {"eta", 951}, {"eth", 240}, {"euml", 235}, {"euro", 8364},
    {"exist", 8707}, {"fnof", 402}, {"forall", 8704}, {"frac12", 189}, {"frac14", 188}, {"frac34", 190},
    {"frasl", 8260}, {"gamma", 947}, {"ge", 8805}, {"gt", 62}, {"hArr", 8660}, {"harr", 8596},
    {"hearts", 9829}, {"hellip", 8230}, {"iacute", 237}, {"icirc", 238}, {"iexcl", 161}, {"igrave", 236},
    {"image", 8465}, {"infin", 8734}, {"int", 8747}, {"iota", 953}, {"iquest", 191}, {"isin", 8712},
    {"iuml", 239}, {"kappa", 954}, {"lArr", 8656}, {"lambda", 955}, {"lang", 9001}, {"laquo", 171},
    {"larr", 8592}, {"lceil", 8968}, {"ldquo", 8220}, {"le", 8804}, {"lfloor", 8970}, {"lowast", 8727},
    {"loz", 9674}, {"lrm", 8206}, {"lsaquo", 8249}, {"lsquo", 8216}, {"lt", 60}, {"macr", 175},
    {"mdash", 8212}, {"micro", 181}, {"middot", 183}, {"minus", 8722}, {"mu", 956}, {"nabla", 8711},
    {"nbsp", 160}, {"ndash", 8211}, {"ne", 8800}, {"ni", 8715}, {"not", 172}, {"notin", 8713},
    {"nsub", 8836}, {"ntilde", 241}, {"nu", 957}, {"oacute", 243}, {"ocirc", 244}, {"oelig", 339},
    {"ograve", 242}, {"oline", 8254}, {"omega", 969}, {"omicron", 959}, {"oplus", 8853}, {"or", 8744},
    {"ordf", 170}, {"ordm", 186}, {"oslash", 248}, {"otilde", 245}, {"otimes", 8855}, {"ouml", 246},
    {"para", 182}, {"part", 8706}, {"permil", 8240}, {"perp", 8869}, {"phi", 966}, {"pi", 960}, {"piv", 982},
    {"plusmn", 177}, {"pound", 163}, {"prime", 8242}, {"prod", 8719}, {"prop", 8733}, {"psi", 968},
    {"quot", 34}, {"rArr", 8658}, {"radic", 8730}, {"rang", 9002}, {"raquo", 187}, {"rarr", 8594},
    {"rceil", 8969}, {"rdquo", 8221}, {"real", 8476}, {"reg", 174}, {"rfloor", 8971}, {"rho", 961},
    {"rlm", 8207}, {"rsaquo", 8250}, {"rsquo", 8217}, {"sbquo", 8218}, {"scaron", 353}, {"sdot", 8901},
    {"sect", 167}, {"shy", 173}, {"sigma", 963}, {"sigmaf", 962}, {"sim", 8764}, {"spades", 9824},
    {"sub", 8834}, {"sube", 8838}, {"sum", 8721}, {"sup", 8835}, {"sup1", 185}, {"sup2", 178}, {"sup3", 179},
    {"supe", 8839}, {"szlig", 223}, {"tau", 964}, {"there4", 8756}, {"theta", 952}, {"thetasym", 977},
    {"thinsp", 8201}, {"thorn", 254}, {"tilde", 732}, {"times", 215}, {"trade", 8482}, {"uArr", 8657},
    {"uacute", 250}, {"uarr", 8593}, {"ucirc", 251}, {"ugrave", 249}, {"uml", 168}, {"upsih", 978},
    {"upsilon", 965}, {"uuml", 252}, {"weierp", 8472}, {"xi", 958}, {"yacute", 253}, {"yen", 165},
    {"yuml", 255}, {"zeta", 950}, {"zwj", 8205}, {"zwnj", 8204},
    };
    return m;
}

std::string HtmlDecode(const std::string &s)
{
    if (s.find('&') == std::string::npos) return s;
    std::string r;
    for (size_t i = 0; i < s.size(); i++)
    {
        char c = s[i];
        if (c == '&')
        {
            // o fim da entidade e o primeiro ';' ou '&' depois daqui; tem de ser ';'
            size_t j = s.find_first_of(";&", i + 1);
            if (j != std::string::npos && s[j] == ';')
            {
                std::string strEntidade = s.substr(i + 1, j - i - 1);
                if (strEntidade.size() > 1 && strEntidade[0] == '#')
                {
                    bool blnHex = strEntidade[1] == 'x' || strEntidade[1] == 'X';
                    std::string strNum = blnHex ? strEntidade.substr(2) : strEntidade.substr(1);
                    bool blnOk = !strNum.empty();
                    uint64_t v = 0;
                    for (char d : strNum)
                    {
                        int x = d >= '0' && d <= '9' ? d - '0'
                              : blnHex && d >= 'a' && d <= 'f' ? d - 'a' + 10
                              : blnHex && d >= 'A' && d <= 'F' ? d - 'A' + 10 : -1;
                        if (x < 0) { blnOk = false; break; }
                        v = v * (blnHex ? 16 : 10) + (uint64_t) x;
                        if (v > 0xFFFFFFFFull) { blnOk = false; break; }
                    }
                    if (blnOk && v <= 0x10FFFF && !(v >= 0xD800 && v <= 0xDFFF))
                    {
                        AcrescentaUtf8(r, (uint32_t) v);
                        i = j;
                        continue;
                    }
                }
                else
                {
                    auto it = Entidades().find(strEntidade);
                    if (it != Entidades().end())
                    {
                        AcrescentaUtf8(r, it->second);
                        i = j;
                        continue;
                    }
                }
            }
        }
        r += c;
    }
    return r;
}

// ---- codificacoes de LerTextoDetectando ----
std::string DeUtf16BE(const uint8_t *d, size_t n)
{
    std::vector<uint8_t> le(n);
    for (size_t i = 0; i + 1 < n; i += 2) { le[i] = d[i + 1]; le[i + 1] = d[i]; }
    return DeUtf16LE(le.data(), n);
}

// UTF-8 do .NET: estrito = new UTF8Encoding(false, true), que lanca (aqui, false) em qualquer
// sequencia invalida; senao, Encoding.UTF8, que poe U+FFFD no lugar dela.
bool DecodificaUtf8(const uint8_t *d, size_t n, bool blnEstrito, std::string &strSaida)
{
    strSaida.clear();
    size_t i = 0;
    while (i < n)
    {
        uint8_t c = d[i];
        if (c < 0x80) { strSaida += (char) c; i++; continue; }
        int k = 0;
        uint32_t cp = 0;
        if ((c & 0xE0) == 0xC0) { k = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { k = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { k = 3; cp = c & 0x07; }
        bool blnOk = k > 0 && i + k < n;
        for (int j = 1; blnOk && j <= k; j++)
        {
            if ((d[i + j] & 0xC0) != 0x80) blnOk = false;
            else cp = (cp << 6) | (d[i + j] & 0x3F);
        }
        if (blnOk && ((k == 1 && cp < 0x80) || (k == 2 && cp < 0x800) || (k == 3 && cp < 0x10000))) blnOk = false;   // forma longa
        if (blnOk && (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))) blnOk = false;
        if (!blnOk)
        {
            if (blnEstrito) return false;
            strSaida += "\xEF\xBF\xBD";
            i++;
            continue;
        }
        strSaida.append((const char *) d + i, k + 1);
        i += k + 1;
    }
    return true;
}

// Encoding.GetEncoding(1252).GetString
std::string DeCp1252(const uint8_t *d, size_t n)
{
    static const uint16_t tab[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178 };
    std::string r;
    for (size_t i = 0; i < n; i++)
    {
        uint8_t c = d[i];
        AcrescentaUtf8(r, c >= 0x80 && c < 0xA0 ? tab[c - 0x80] : c);
    }
    return r;
}

// ---- Image.FromStream(fs, false, false): so o tamanho ----
// LINUX: o GDI+ lia o cabecalho da imagem; aqui ele e lido a mao para JPEG, PNG, BMP e GIF (o
// GDI+ reconhece pelo conteudo, nao pela extensao, e aqui tambem). Formato desconhecido ou
// cabecalho quebrado lanca, como a imagem ilegivel lancava.
void TamanhoDaImagem(const std::string &strArquivo, int &intLargura, int &intAltura)
{
    std::vector<uint8_t> a;
    {
        FILE *f = fopen(strArquivo.c_str(), "rb");
        if (!f) throw std::runtime_error("não abriu");
        a.resize(256 * 1024);
        size_t n = fread(a.data(), 1, a.size(), f);
        a.resize(n);
        // JPEG com miniatura grande pode ter o SOF depois dos primeiros 256 KB
        if (n >= 2 && a[0] == 0xFF && a[1] == 0xD8)
        {
            uint8_t buf[65536];
            size_t x;
            while ((x = fread(buf, 1, sizeof buf, f)) > 0) a.insert(a.end(), buf, buf + x);
        }
        fclose(f);
    }
    auto U16BE = [&](size_t p) { return (int) ((a[p] << 8) | a[p + 1]); };
    auto U16LE = [&](size_t p) { return (int) (a[p] | (a[p + 1] << 8)); };
    auto I32LE = [&](size_t p) { return (int32_t) ((uint32_t) a[p] | ((uint32_t) a[p + 1] << 8) | ((uint32_t) a[p + 2] << 16) | ((uint32_t) a[p + 3] << 24)); };
    auto U32BE = [&](size_t p) { return (int) (((uint32_t) a[p] << 24) | ((uint32_t) a[p + 1] << 16) | ((uint32_t) a[p + 2] << 8) | (uint32_t) a[p + 3]); };

    if (a.size() >= 24 && a[0] == 0x89 && a[1] == 'P' && a[2] == 'N' && a[3] == 'G')
    {
        intLargura = U32BE(16);
        intAltura = U32BE(20);
        return;
    }
    if (a.size() >= 10 && a[0] == 'G' && a[1] == 'I' && a[2] == 'F' && a[3] == '8')
    {
        intLargura = U16LE(6);
        intAltura = U16LE(8);
        return;
    }
    if (a.size() >= 26 && a[0] == 'B' && a[1] == 'M')
    {
        int32_t intDib = I32LE(14);
        if (intDib == 12) { intLargura = U16LE(18); intAltura = U16LE(20); }
        else { intLargura = I32LE(18); intAltura = std::abs(I32LE(22)); }
        if (intLargura <= 0 || intAltura <= 0) throw std::runtime_error("BMP inválido");
        return;
    }
    if (a.size() >= 4 && a[0] == 0xFF && a[1] == 0xD8)
    {
        size_t p = 2;
        while (p + 4 <= a.size())
        {
            if (a[p] != 0xFF) { p++; continue; }
            uint8_t m = a[p + 1];
            if (m == 0xFF) { p++; continue; }
            if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { p += 2; continue; }
            if (m == 0xD9 || m == 0xDA) break;
            int intTam = U16BE(p + 2);
            if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)
            {
                if (p + 9 > a.size()) break;
                intAltura = U16BE(p + 5);
                intLargura = U16BE(p + 7);
                return;
            }
            p += 2 + (size_t) intTam;
        }
        throw std::runtime_error("JPEG sem SOF");
    }
    throw std::runtime_error("formato de imagem desconhecido");
}

// Regex com IgnoreCase (e Singleline quando o padrao ja usa [\s\S] no lugar do '.')
std::regex Re(const char *strPadrao) { return std::regex(strPadrao, std::regex::ECMAScript | std::regex::icase); }

std::string Grupo1(const std::string &s, const std::regex &re, bool &blnAchou)
{
    std::smatch m;
    blnAchou = std::regex_search(s, m, re);
    return blnAchou ? m[1].str() : "";
}

// Directory.GetFiles(pasta, "*.*", SearchOption.AllDirectories)
std::vector<std::string> TodosOsArquivos(const std::string &strPasta)
{
    std::vector<std::string> r;
    std::error_code e;
    fs::recursive_directory_iterator it(fs::u8path(strPasta), e), fim;
    if (e) throw std::runtime_error(e.message());
    for (; it != fim; it.increment(e))
    {
        if (e) throw std::runtime_error(e.message());
        std::error_code e2;
        if (it->is_regular_file(e2)) r.push_back(it->path().u8string());
    }
    return r;
}

} // namespace

std::vector<TCdExtraFile> TCdExtraExtractor::CopiarVideosDaUnidade(const std::string &strRaiz, const std::string &strPastaDestino, TLog actLog)
{
    std::vector<TCdExtraFile> lstCopiados;
    if (!actLog) actLog = [](const std::string &) {};
    if (strRaiz.empty() || !PastaExiste(strRaiz))
    {
        actLog("CD Extra: a unidade não está acessível; os vídeos não foram copiados.");
        return lstCopiados;
    }

    std::vector<std::string> lstVideos;
    std::deque<std::string> filaPastas;
    filaPastas.push_back(strRaiz);
    int intPastas = 0;
    while (!filaPastas.empty() && intPastas++ < 2000)
    {
        std::string strPasta = filaPastas.front();
        filaPastas.pop_front();
        try
        {
            // LINUX: Base::Arquivos/Pastas nao lancam; pasta ilegivel e conferida aqui.
            std::error_code e;
            fs::directory_iterator teste(fs::u8path(strPasta), e);
            if (e) throw std::runtime_error(e.message());
            for (const std::string &f : Arquivos(strPasta))
            {
                std::string strExt = Minusculas(Extensao(f));
                if (std::find(std::begin(arrExtensoesVideo), std::end(arrExtensoesVideo), strExt) != std::end(arrExtensoesVideo)) lstVideos.push_back(f);
            }
            for (const std::string &d : Pastas(strPasta)) filaPastas.push_back(d);
        }
        catch (const std::exception &ex) { actLog("CD Extra: não consegui ler a pasta " + strPasta + ": " + ex.what()); }
    }
    if (lstVideos.empty())
    {
        actLog("CD Extra: nenhum vídeo na sessão de dados.");
        return lstCopiados;
    }
    std::sort(lstVideos.begin(), lstVideos.end(), [](const std::string &a, const std::string &b) { return ChaveSemCaixa(a) < ChaveSemCaixa(b); });
    actLog("CD Extra: " + std::to_string(lstVideos.size()) + " vídeo(s) na sessão de dados. Copiando e conferindo cada um com uma segunda leitura do disco...");

    std::set<std::string> setNomes;     // pela ChaveSemCaixa
    for (const std::string &strOrigem : lstVideos)
    {
        std::string strNome = NomeDoArquivo(strOrigem);
        if (setNomes.count(ChaveSemCaixa(strNome)))
        {
            std::string strPai = NomeDoArquivo(PastaDe(strOrigem));
            strNome = (strPai.empty() ? "disco" : strPai) + " - " + strNome;
        }
        for (int n = 2; setNomes.count(ChaveSemCaixa(strNome)); n++)
            strNome = NomeSemExtensao(strOrigem) + " (" + std::to_string(n) + ")" + Extensao(strOrigem);
        setNomes.insert(ChaveSemCaixa(strNome));
        std::string strDestino = Combina(strPastaDestino, strNome);

        try
        {
            std::string strHashCopia;
            {
                FILE *fsOrigem = fopen(strOrigem.c_str(), "rb");
                if (!fsOrigem) throw std::runtime_error(std::string("não abriu: ") + strerror(errno));
                FILE *fsDestino = fopen(strDestino.c_str(), "wb");
                if (!fsDestino) { int e = errno; fclose(fsOrigem); throw std::runtime_error(std::string("não criou: ") + strerror(e)); }
                Hasher sha(Algoritmo::Sha256);
                std::vector<uint8_t> buf(1 << 20);
                size_t r;
                bool blnOk = true;
                while ((r = fread(buf.data(), 1, buf.size(), fsOrigem)) > 0)
                {
                    if (fwrite(buf.data(), 1, r, fsDestino) != r) { blnOk = false; break; }
                    sha.Acrescenta(buf.data(), r);
                }
                bool blnErroLeitura = ferror(fsOrigem) != 0;
                fclose(fsOrigem);
                // fsDestino.Flush(true): ate o disco
                if (fflush(fsDestino) != 0 || fsync(fileno(fsDestino)) != 0) blnOk = false;
                fclose(fsDestino);
                if (blnErroLeitura) throw std::runtime_error("erro de leitura do disco");
                if (!blnOk) throw std::runtime_error("erro ao gravar a cópia");
                strHashCopia = Hex(sha.Final());
            }
            // A copia no disco de destino e uma segunda leitura do CD tem de dar o mesmo numero
            // que a primeira.
            std::string strHashGravado = HashDe(strDestino);
            std::string strHashRelido = HashDe(strOrigem);
            if (strHashGravado != strHashCopia || strHashRelido != strHashCopia)
            {
                actLog("⚠ CD Extra: '" + strNome + "' NÃO confere (" +
                       (strHashRelido != strHashCopia ? "duas leituras do disco deram resultados diferentes" : "a cópia gravada difere do que foi lido") +
                       "). A cópia foi descartada.");
                std::error_code e;
                fs::remove(fs::u8path(strDestino), e);
                continue;
            }
            int64_t lngTamanho = Tamanho(strDestino);
            actLog("CD Extra: '" + strNome + "' copiado e conferido (" + F(lngTamanho / (1024.0 * 1024.0), 2) + " MB, SHA-256 " + strHashCopia.substr(0, 16) + "…).");
            TCdExtraFile objArq;
            objArq.strNomeArquivo = strNome;
            objArq.strCaminhoCompleto = strDestino;
            objArq.lngTamanhoBytes = lngTamanho;
            objArq.strSha256 = strHashCopia;
            lstCopiados.push_back(objArq);
        }
        catch (const std::exception &ex)
        {
            actLog("⚠ CD Extra: não consegui copiar '" + strNome + "': " + ex.what());
            std::error_code e;
            if (ArquivoExiste(strDestino)) fs::remove(fs::u8path(strDestino), e);
        }
    }
    return lstCopiados;
}

std::string TCdExtraExtractor::HashDe(const std::string &strArquivo)
{
    return Sha256HexArquivo(strArquivo);     // minusculas, como o Hex() do original
}

bool TCdExtraExtractor::MinerarSessaoDados(const std::string &strPastaOrigem, TLog actLog, TMetadadosCdExtra &objMetadados)
{
    objMetadados = TMetadadosCdExtra();
    if (!actLog) actLog = [](const std::string &) {};

    if (strPastaOrigem.empty() || !PastaExiste(strPastaOrigem))
    {
        return false;
    }

    try
    {
        actLog("TCdExtraExtractor: Iniciando mineração de artes e metadados na pasta '" + strPastaOrigem + "'...");

        // 1. Mineracao da Capa de Alta Resolucao (~1:1 com pontuacao por heuristica visual)
        std::vector<std::string> arrTodosArquivos = TodosOsArquivos(strPastaOrigem);

        std::string strMelhorImagem;
        int64_t lngMaiorScore = std::numeric_limits<int64_t>::min();
        int intMelhorLargura = 0;
        int intMelhorAltura = 0;

        for (const std::string &strArquivo : arrTodosArquivos)
        {
            std::string strExt = Minusculas(Extensao(strArquivo));
            if (strExt != ".jpg" && strExt != ".jpeg" && strExt != ".png" && strExt != ".bmp")
            {
                continue;
            }

            try
            {
                int64_t lngTamanhoArq = Tamanho(strArquivo);
                if (lngTamanhoArq < 1024) continue; // Ignora icones ou placeholders minusculos

                int intLargura = 0;
                int intAltura = 0;

                TamanhoDaImagem(strArquivo, intLargura, intAltura);

                if (intLargura < 200 || intAltura < 200) continue;

                double dblRatio = (double) intLargura / intAltura;
                // Encartes de CD tem formato estritamente quadrado (tolerancia entre 0.85 e 1.18)
                if (dblRatio < 0.85 || dblRatio > 1.18) continue;

                // Calculo do Score Heuristico
                int64_t lngScore = ((int64_t) intLargura * intAltura) + (lngTamanhoArq / 2);
                std::string strNomeLower = Minusculas(NomeSemExtensao(strArquivo));

                // Bonus para nomes consagrados de capa oficial
                if (Contem(strNomeLower, "cover") || Contem(strNomeLower, "front") ||
                    Contem(strNomeLower, "background") || Contem(strNomeLower, "album") ||
                    Contem(strNomeLower, "folder") || Contem(strNomeLower, "capa") ||
                    Contem(strNomeLower, "artwork"))
                {
                    lngScore += 250000;
                }

                // Penalidade para skins, botoes ou elementos de interface do player proprietario
                if (Contem(strNomeLower, "gui") || Contem(strNomeLower, "skin") ||
                    Contem(strNomeLower, "btn") || Contem(strNomeLower, "button") ||
                    Contem(strNomeLower, "logo") || Contem(strNomeLower, "banner") ||
                    Contem(strNomeLower, "icon") || Contem(strNomeLower, "thumb") ||
                    Contem(strNomeLower, "slider"))
                {
                    lngScore -= 1000000;
                }

                if (lngScore > lngMaiorScore)
                {
                    lngMaiorScore = lngScore;
                    strMelhorImagem = strArquivo;
                    intMelhorLargura = intLargura;
                    intMelhorAltura = intAltura;
                }
            }
            catch (...)
            {
                // Arquivo de imagem ilegivel ou corrompido, ignora silenciosamente
            }
        }

        if (!strMelhorImagem.empty() && lngMaiorScore > 0)
        {
            try
            {
                objMetadados.arrCapaBytes = LeBytes(strMelhorImagem);
                objMetadados.strCaminhoCapaOriginal = strMelhorImagem;
                objMetadados.intLarguraCapa = intMelhorLargura;
                objMetadados.intAlturaCapa = intMelhorAltura;

                actLog("TCdExtraExtractor: Capa oficial localizada: '" + NomeDoArquivo(strMelhorImagem) + "' (" + std::to_string(intMelhorLargura) + "x" +
                       std::to_string(intMelhorAltura) + ", " + F(objMetadados.arrCapaBytes.size() / 1024.0, 1) + " KB).");
            }
            catch (const std::exception &exCapa)
            {
                actLog(std::string("TCdExtraExtractor: Erro ao carregar bytes da capa: ") + exCapa.what());
            }
        }

        // 2. Mineracao de Metadados Ricos (XML, WAX, ASX)
        std::vector<std::string> lstArquivosMeta;
        for (const std::string &strArquivo : arrTodosArquivos)
        {
            std::string strExt = Minusculas(Extensao(strArquivo));
            if (strExt == ".xml" || strExt == ".wax" || strExt == ".asx" || strExt == ".inf" || strExt == ".ini")
            {
                lstArquivosMeta.push_back(strArquivo);
            }
        }

        // Prioriza na ordem: *.wax / *.asx (geralmente contem compositores), depois *.xml
        // (o List.Sort do .NET nao e estavel; aqui os empates ficam na ordem em que vieram)
        auto Prioridade = [](const std::string &a) {
            std::string ext = Minusculas(Extensao(a));
            return (ext == ".wax" || ext == ".asx") ? 0 : (ext == ".xml" ? 1 : 2);
        };
        std::stable_sort(lstArquivosMeta.begin(), lstArquivosMeta.end(),
                         [&](const std::string &a, const std::string &b) { return Prioridade(a) < Prioridade(b); });

        for (const std::string &strArquivoMeta : lstArquivosMeta)
        {
            try
            {
                std::string strConteudo = LerTextoDetectando(strArquivoMeta);
                if (VazioOuBranco(strConteudo)) continue;

                // Se o arquivo contiver estrutura ASX/WAX
                if (Contem(strConteudo, "<ENTRY", true) || Contem(strConteudo, "<ASX", true))
                {
                    ParsearAsxWax(strConteudo, objMetadados, actLog);
                }
                // Se o arquivo contiver estrutura CDS300 / TrackInfo
                else if (Contem(strConteudo, "<cds300", true) || Contem(strConteudo, "<track id=", true))
                {
                    ParsearCds300(strConteudo, objMetadados, actLog);
                }
            }
            catch (...)
            {
                // Continua tentando outros arquivos de metadados
            }
        }

        bool blnTemAlgo = !objMetadados.arrCapaBytes.empty() ||
                          !objMetadados.lstTitulosFaixas.empty() ||
                          !objMetadados.strAlbum.empty();

        if (blnTemAlgo)
        {
            actLog("TCdExtraExtractor: Mineração concluída com sucesso! Álbum: '" + (objMetadados.strAlbum.empty() ? std::string("N/D") : objMetadados.strAlbum) +
                   "', Artista: '" + (objMetadados.strArtistaAlbum.empty() ? std::string("N/D") : objMetadados.strArtistaAlbum) + "', " +
                   std::to_string(objMetadados.lstTitulosFaixas.size()) + " faixas, " + std::to_string(objMetadados.lstCompositoresFaixas.size()) +
                   " compositores.");
        }

        return blnTemAlgo;
    }
    catch (const std::exception &ex)
    {
        actLog(std::string("TCdExtraExtractor: Falha durante mineração da sessão de dados: ") + ex.what());
        return false;
    }
}

// LINUX: o Regex do .NET vira std::regex (ECMAScript, sem caixa). O RegexOptions.Singleline
// (o '.' pega quebra de linha) vira [\s\S] no lugar do '.'.
void TCdExtraExtractor::ParsearAsxWax(const std::string &strConteudo, TMetadadosCdExtra &objMeta, const TLog &actLog)
{
    try
    {
        static const std::regex reTitulo = Re(R"re(<TITLE>\s*([^<]+?)\s*</TITLE>)re");
        static const std::regex reAutor = Re(R"re(<AUTHOR>\s*([^<]+?)\s*</AUTHOR>)re");
        static const std::regex reArtista = Re(R"re(<PARAM\s+NAME\s*=\s*"ARTIST"\s+VALUE\s*=\s*"([^"]+)")re");
        static const std::regex reAno = Re(R"re(<PARAM\s+NAME\s*=\s*"YEAR"\s+VALUE\s*=\s*"([^"]+)")re");
        static const std::regex reEntry = Re(R"re(<ENTRY\b[^>]*>([\s\S]*?)</ENTRY>)re");
        static const std::regex reCompositor = Re(R"re(<PARAM\s+NAME\s*=\s*"COMPOSER"\s+VALUE\s*=\s*"([^"]+)")re");
        bool ok;

        std::string mTituloAlbum = Grupo1(strConteudo, reTitulo, ok);
        if (ok && objMeta.strAlbum.empty())
        {
            objMeta.strAlbum = LimparTextoXml(mTituloAlbum);
        }

        std::string mAutor = Grupo1(strConteudo, reAutor, ok);
        if (ok && objMeta.strArtistaAlbum.empty())
        {
            objMeta.strArtistaAlbum = LimparTextoXml(mAutor);
        }
        else
        {
            std::string mArtistaParam = Grupo1(strConteudo, reArtista, ok);
            if (ok && objMeta.strArtistaAlbum.empty())
            {
                objMeta.strArtistaAlbum = LimparTextoXml(mArtistaParam);
            }
        }

        std::string mAno = Grupo1(strConteudo, reAno, ok);
        if (ok && objMeta.strAno.empty())
        {
            objMeta.strAno = LimparTextoXml(mAno);
        }

        auto itIni = std::sregex_iterator(strConteudo.begin(), strConteudo.end(), reEntry), itFim = std::sregex_iterator();
        if (itIni != itFim)
        {
            int intIndice = 0;
            for (auto it = itIni; it != itFim; ++it)
            {
                std::string strBloco = (*it)[1].str();

                std::string mTrkTitle = Grupo1(strBloco, reTitulo, ok);
                std::string strTrkTitle = ok ? LimparTextoXml(mTrkTitle) : "";

                std::string mTrkArtist = Grupo1(strBloco, reArtista, ok);
                std::string strTrkArtist = ok ? LimparTextoXml(mTrkArtist) : "";
                if (strTrkArtist.empty())
                {
                    std::string mAuthor = Grupo1(strBloco, reAutor, ok);
                    if (ok) strTrkArtist = LimparTextoXml(mAuthor);
                }

                std::string mComposer = Grupo1(strBloco, reCompositor, ok);
                std::string strComposer = ok ? LimparTextoXml(mComposer) : "";

                DefinirNaPosicao(objMeta.lstTitulosFaixas, intIndice, strTrkTitle);

                DefinirNaPosicao(objMeta.lstArtistasFaixas, intIndice, strTrkArtist);

                if (!strComposer.empty())
                {
                    while ((int) objMeta.lstCompositoresFaixas.size() < intIndice)
                    {
                        objMeta.lstCompositoresFaixas.push_back("");
                    }
                    if (intIndice < (int) objMeta.lstCompositoresFaixas.size())
                    {
                        objMeta.lstCompositoresFaixas[intIndice] = strComposer;
                    }
                    else
                    {
                        objMeta.lstCompositoresFaixas.push_back(strComposer);
                    }
                }

                intIndice++;
            }
        }
    }
    catch (const std::exception &ex)
    {
        actLog(std::string("TCdExtraExtractor: Aviso no parser ASX: ") + ex.what());
    }
}

void TCdExtraExtractor::ParsearCds300(const std::string &strConteudo, TMetadadosCdExtra &objMeta, const TLog &actLog)
{
    try
    {
        static const std::regex reAlbumTitulo = Re(R"re(<album>[\s\S]*?<title>\s*([^<]+?)\s*</title>)re");
        static const std::regex reAlbumArtista = Re(R"re(<album>[\s\S]*?<artist>\s*([^<]+?)\s*</artist>)re");
        static const std::regex reAlbumAno = Re(R"re(<album>[\s\S]*?<year>\s*([^<]+?)\s*</year>)re");
        static const std::regex reTrack = Re(R"re(<track\b[^>]*>([\s\S]*?)</track>)re");
        static const std::regex reTitulo = Re(R"re(<title>\s*([^<]+?)\s*</title>)re");
        static const std::regex reArtista = Re(R"re(<artist>\s*([^<]+?)\s*</artist>)re");
        bool ok;

        std::string mAlbumTitle = Grupo1(strConteudo, reAlbumTitulo, ok);
        if (ok && objMeta.strAlbum.empty())
        {
            objMeta.strAlbum = LimparTextoXml(mAlbumTitle);
        }

        std::string mAlbumArtist = Grupo1(strConteudo, reAlbumArtista, ok);
        if (ok && objMeta.strArtistaAlbum.empty())
        {
            objMeta.strArtistaAlbum = LimparTextoXml(mAlbumArtist);
        }

        std::string mAlbumYear = Grupo1(strConteudo, reAlbumAno, ok);
        if (ok && objMeta.strAno.empty())
        {
            objMeta.strAno = LimparTextoXml(mAlbumYear);
        }

        auto itIni = std::sregex_iterator(strConteudo.begin(), strConteudo.end(), reTrack), itFim = std::sregex_iterator();
        if (itIni != itFim)
        {
            int intIndice = 0;
            for (auto it = itIni; it != itFim; ++it)
            {
                std::string strBloco = (*it)[1].str();

                std::string mTrkTitle = Grupo1(strBloco, reTitulo, ok);
                std::string strTrkTitle = ok ? LimparTextoXml(mTrkTitle) : "";

                std::string mTrkArtist = Grupo1(strBloco, reArtista, ok);
                std::string strTrkArtist = ok ? LimparTextoXml(mTrkArtist) : "";

                DefinirNaPosicao(objMeta.lstTitulosFaixas, intIndice, strTrkTitle);

                DefinirNaPosicao(objMeta.lstArtistasFaixas, intIndice, strTrkArtist);

                intIndice++;
            }
        }
    }
    catch (const std::exception &ex)
    {
        actLog(std::string("TCdExtraExtractor: Aviso no parser CDS300: ") + ex.what());
    }
}

std::string TCdExtraExtractor::LimparTextoXml(const std::string &strTexto)
{
    if (strTexto.empty()) return "";
    return HtmlDecode(Trim(strTexto));
}

void TCdExtraExtractor::DefinirNaPosicao(std::vector<std::string> &lst, int intIndice, const std::string &strValor)
{
    if (strValor.empty()) return;
    while ((int) lst.size() <= intIndice) lst.push_back("");
    if (lst[intIndice].empty()) lst[intIndice] = strValor;
}

std::string TCdExtraExtractor::LerTextoDetectando(const std::string &strArquivo)
{
    std::vector<uint8_t> arr = LeBytes(strArquivo);
    if (arr.size() >= 2 && arr[0] == 0xFF && arr[1] == 0xFE) return DeUtf16LE(arr.data() + 2, arr.size() - 2);
    if (arr.size() >= 2 && arr[0] == 0xFE && arr[1] == 0xFF) return DeUtf16BE(arr.data() + 2, arr.size() - 2);
    std::string s;
    if (arr.size() >= 3 && arr[0] == 0xEF && arr[1] == 0xBB && arr[2] == 0xBF) { DecodificaUtf8(arr.data() + 3, arr.size() - 3, false, s); return s; }
    if (DecodificaUtf8(arr.data(), arr.size(), true, s)) return s;
    return DeCp1252(arr.data(), arr.size());
}

} // namespace ARCA::Modelo

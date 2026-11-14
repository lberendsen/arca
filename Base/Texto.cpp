// ARCA no xOS - Base/Texto (ver Texto.h).
#include "Texto.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <ctime>

namespace ARCA::Base {

namespace {

bool EspacoBranco(uint32_t c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f' || c == 0xA0 ||
           c == 0x2000 || c == 0x2001 || c == 0x2002 || c == 0x2003 || c == 0x2009 || c == 0x200A ||
           c == 0x202F || c == 0x3000 || c == 0xFEFF || c == 0x85;
}

// decodifica um code point a partir de i (avanca i); byte invalido vira ele mesmo como latim-1
uint32_t Proximo(const std::string &s, size_t &i)
{
    unsigned char c = (unsigned char) s[i];
    if (c < 0x80) { i++; return c; }
    int n = (c >= 0xF0 && c < 0xF8) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : -1;
    if (n < 0) { i++; return c; }
    uint32_t cp = (n == 1) ? (c & 0x1F) : (n == 2) ? (c & 0x0F) : (c & 0x07);
    for (int k = 1; k <= n; k++)
    {
        if (i + k >= s.size()) { i++; return c; }
        unsigned char d = (unsigned char) s[i + k];
        if ((d & 0xC0) != 0x80) { i++; return c; }
        cp = (cp << 6) | (d & 0x3F);
    }
    i += n + 1;
    return cp;
}

// minuscula/maiuscula das letras que o acervo tem: ASCII, latim-1 e latim estendido A
uint32_t MinusculaCp(uint32_t c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if ((c >= 0xC0 && c <= 0xDE) && c != 0xD7) return c + 32;
    if (c >= 0x100 && c <= 0x137 && (c % 2) == 0) return c + 1;
    if (c >= 0x139 && c <= 0x148 && (c % 2) == 1) return c + 1;
    if (c >= 0x14A && c <= 0x177 && (c % 2) == 0) return c + 1;
    if (c == 0x178) return 0xFF;
    if (c == 0x179 || c == 0x17B || c == 0x17D) return c + 1;
    if (c == 0x218 || c == 0x21A) return c + 1;   // S, T com virgula (romeno)
    return c;
}
uint32_t MaiusculaCp(uint32_t c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    if ((c >= 0xE0 && c <= 0xFE) && c != 0xF7) return c - 32;
    if (c == 0xFF) return 0x178;
    if (c >= 0x101 && c <= 0x137 && (c % 2) == 1) return c - 1;
    if (c >= 0x13A && c <= 0x148 && (c % 2) == 0) return c - 1;
    if (c >= 0x14B && c <= 0x177 && (c % 2) == 1) return c - 1;
    if (c == 0x17A || c == 0x17C || c == 0x17E) return c - 1;
    if (c == 0x219 || c == 0x21B) return c - 1;
    return c;
}

// letra base de U+00C0..U+017F (e S/T com virgula); 0: sem letra base (fica como esta)
const char *BaseLatina(uint32_t c)
{
    static const char *t00C0[] = {
        "A","A","A","A","A","A","AE","C","E","E","E","E","I","I","I","I",
        "D","N","O","O","O","O","O",nullptr,"O","U","U","U","U","Y","TH","ss",
        "a","a","a","a","a","a","ae","c","e","e","e","e","i","i","i","i",
        "d","n","o","o","o","o","o",nullptr,"o","u","u","u","u","y","th","y"};
    static const char *t0100[] = {
        "A","a","A","a","A","a","C","c","C","c","C","c","C","c","D","d",
        "D","d","E","e","E","e","E","e","E","e","E","e","G","g","G","g",
        "G","g","G","g","H","h","H","h","I","i","I","i","I","i","I","i",
        "I","i","IJ","ij","J","j","K","k","k","L","l","L","l","L","l","L",
        "l","L","l","N","n","N","n","N","n","n","N","n","O","o","O","o",
        "O","o","OE","oe","R","r","R","r","R","r","S","s","S","s","S","s",
        "S","s","T","t","T","t","T","t","U","u","U","u","U","u","U","u",
        "U","u","U","u","W","w","Y","y","Y","Z","z","Z","z","Z","z","s"};
    if (c >= 0xC0 && c <= 0xFF) return t00C0[c - 0xC0];
    if (c >= 0x100 && c <= 0x17F) return t0100[c - 0x100];
    if (c == 0x218) return "S";
    if (c == 0x219) return "s";
    if (c == 0x21A) return "T";
    if (c == 0x21B) return "t";
    return nullptr;
}

} // namespace

void AcrescentaUtf8(std::string &s, uint32_t cp)
{
    if (cp < 0x80) s += (char) cp;
    else if (cp < 0x800) { s += (char) (0xC0 | (cp >> 6)); s += (char) (0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000)
    {
        s += (char) (0xE0 | (cp >> 12));
        s += (char) (0x80 | ((cp >> 6) & 0x3F));
        s += (char) (0x80 | (cp & 0x3F));
    }
    else
    {
        s += (char) (0xF0 | (cp >> 18));
        s += (char) (0x80 | ((cp >> 12) & 0x3F));
        s += (char) (0x80 | ((cp >> 6) & 0x3F));
        s += (char) (0x80 | (cp & 0x3F));
    }
}

std::vector<uint32_t> CodePoints(const std::string &s)
{
    std::vector<uint32_t> r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size();)
        r.push_back(Proximo(s, i));
    return r;
}

std::string DeCodePoints(const std::vector<uint32_t> &cps)
{
    std::string r;
    r.reserve(cps.size());
    for (uint32_t c : cps)
        AcrescentaUtf8(r, c);
    return r;
}

std::string TrimInicio(const std::string &s)
{
    auto cps = CodePoints(s);
    size_t i = 0;
    while (i < cps.size() && EspacoBranco(cps[i])) i++;
    return DeCodePoints(std::vector<uint32_t>(cps.begin() + i, cps.end()));
}

std::string TrimFim(const std::string &s)
{
    auto cps = CodePoints(s);
    size_t n = cps.size();
    while (n > 0 && EspacoBranco(cps[n - 1])) n--;
    cps.resize(n);
    return DeCodePoints(cps);
}

std::string Trim(const std::string &s) { return TrimFim(TrimInicio(s)); }

std::string Trim(const std::string &s, const std::string &caracteres)
{
    auto cps = CodePoints(s);
    auto tirar = CodePoints(caracteres);
    auto e = [&](uint32_t c) { return std::find(tirar.begin(), tirar.end(), c) != tirar.end(); };
    size_t i = 0, n = cps.size();
    while (i < n && e(cps[i])) i++;
    while (n > i && e(cps[n - 1])) n--;
    return DeCodePoints(std::vector<uint32_t>(cps.begin() + i, cps.begin() + n));
}

std::string Minusculas(const std::string &s)
{
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size();)
        AcrescentaUtf8(r, MinusculaCp(Proximo(s, i)));
    return r;
}

std::string Maiusculas(const std::string &s)
{
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size();)
        AcrescentaUtf8(r, MaiusculaCp(Proximo(s, i)));
    return r;
}

std::string SemAcentos(const std::string &s)
{
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size();)
    {
        uint32_t c = Proximo(s, i);
        if (c >= 0x300 && c <= 0x36F) continue;          // marca combinante solta (texto ja em NFD)
        const char *b = BaseLatina(c);
        if (b) r += b;
        else AcrescentaUtf8(r, c);
    }
    return r;
}

bool IgualSemCaixa(const std::string &a, const std::string &b) { return Minusculas(a) == Minusculas(b); }

bool ComecaCom(const std::string &s, const std::string &p, bool semCaixa)
{
    if (!semCaixa) return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
    std::string a = Minusculas(s), b = Minusculas(p);
    return a.size() >= b.size() && a.compare(0, b.size(), b) == 0;
}

bool TerminaCom(const std::string &s, const std::string &p, bool semCaixa)
{
    std::string a = semCaixa ? Minusculas(s) : s, b = semCaixa ? Minusculas(p) : p;
    return a.size() >= b.size() && a.compare(a.size() - b.size(), b.size(), b) == 0;
}

int64_t Indice(const std::string &s, const std::string &t, bool semCaixa, size_t inicio)
{
    if (!semCaixa)
    {
        size_t p = s.find(t, inicio);
        return p == std::string::npos ? -1 : (int64_t) p;
    }
    // minusculas preservam o tamanho em bytes dentro do latim (mesma faixa de 2 bytes),
    // entao a posicao vale para a string original
    std::string a = Minusculas(s), b = Minusculas(t);
    size_t p = a.find(b, inicio);
    return p == std::string::npos ? -1 : (int64_t) p;
}

bool Contem(const std::string &s, const std::string &t, bool semCaixa) { return Indice(s, t, semCaixa) >= 0; }

std::vector<std::string> Divide(const std::string &s, const std::string &sep, bool tirarVazias)
{
    std::vector<std::string> r;
    if (sep.empty()) { r.push_back(s); return r; }
    size_t i = 0;
    for (;;)
    {
        size_t p = s.find(sep, i);
        std::string parte = s.substr(i, p == std::string::npos ? std::string::npos : p - i);
        if (!(tirarVazias && parte.empty())) r.push_back(parte);
        if (p == std::string::npos) break;
        i = p + sep.size();
    }
    return r;
}

std::vector<std::string> DivideQualquer(const std::string &s, const std::string &seps, bool tirarVazias)
{
    auto cps = CodePoints(s);
    auto sp = CodePoints(seps);
    std::vector<std::string> r;
    std::string atual;
    for (uint32_t c : cps)
    {
        if (std::find(sp.begin(), sp.end(), c) != sp.end())
        {
            if (!(tirarVazias && atual.empty())) r.push_back(atual);
            atual.clear();
        }
        else AcrescentaUtf8(atual, c);
    }
    if (!(tirarVazias && atual.empty())) r.push_back(atual);
    return r;
}

std::string Junta(const std::vector<std::string> &partes, const std::string &sep)
{
    std::string r;
    for (size_t i = 0; i < partes.size(); i++)
    {
        if (i) r += sep;
        r += partes[i];
    }
    return r;
}

std::string Substitui(const std::string &s, const std::string &de, const std::string &para)
{
    if (de.empty()) return s;
    std::string r;
    size_t i = 0;
    for (;;)
    {
        size_t p = s.find(de, i);
        if (p == std::string::npos) { r.append(s, i, std::string::npos); break; }
        r.append(s, i, p - i);
        r += para;
        i = p + de.size();
    }
    return r;
}

bool VazioOuBranco(const std::string &s) { return Trim(s).empty(); }

bool TentaInt(const std::string &s, int &v)
{
    int64_t x;
    if (!TentaInt64(s, x) || x < INT32_MIN || x > INT32_MAX) return false;
    v = (int) x;
    return true;
}

bool TentaInt64(const std::string &s, int64_t &v)
{
    std::string t = Trim(s);
    if (t.empty()) return false;
    errno = 0;
    char *fim = nullptr;
    long long x = strtoll(t.c_str(), &fim, 10);
    if (errno || !fim || *fim) return false;
    v = x;
    return true;
}

bool TentaDouble(const std::string &s, double &v)
{
    std::string t = Trim(s);
    if (t.empty()) return false;
    errno = 0;
    char *fim = nullptr;
    double x = strtod(t.c_str(), &fim);     // o xOS roda na locale "C": ponto decimal
    if (errno || !fim || *fim) return false;
    v = x;
    return true;
}

std::string Hex(const uint8_t *d, size_t n, bool mai)
{
    static const char *m = "0123456789abcdef", *M = "0123456789ABCDEF";
    const char *h = mai ? M : m;
    std::string r(n * 2, '0');
    for (size_t i = 0; i < n; i++)
    {
        r[2 * i] = h[d[i] >> 4];
        r[2 * i + 1] = h[d[i] & 15];
    }
    return r;
}
std::string Hex(const std::vector<uint8_t> &d, bool mai) { return Hex(d.data(), d.size(), mai); }

size_t Caracteres(const std::string &s) { return CodePoints(s).size(); }

std::string Sub(const std::string &s, size_t inicio, size_t n)
{
    auto cps = CodePoints(s);
    if (inicio >= cps.size()) return "";
    size_t fim = (n == std::string::npos || inicio + n > cps.size()) ? cps.size() : inicio + n;
    return DeCodePoints(std::vector<uint32_t>(cps.begin() + inicio, cps.begin() + fim));
}

std::string Agora(const char *fmt)
{
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    char b[128];
    strftime(b, sizeof b, fmt, &tm);
    return b;
}
std::string AgoraIso() { return Agora("%Y-%m-%dT%H:%M:%S"); }
std::string AgoraCarimbo() { return Agora("%Y%m%d%H%M%S"); }

std::string DeUtf16LE(const uint8_t *d, size_t n)
{
    std::string r;
    for (size_t i = 0; i + 1 < n; i += 2)
    {
        uint32_t c = d[i] | (d[i + 1] << 8);
        if (c >= 0xD800 && c <= 0xDBFF && i + 3 < n)
        {
            uint32_t c2 = d[i + 2] | (d[i + 3] << 8);
            if (c2 >= 0xDC00 && c2 <= 0xDFFF)
            {
                c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
                i += 2;
            }
        }
        AcrescentaUtf8(r, c);
    }
    return r;
}

std::string DeLatin1(const uint8_t *d, size_t n)
{
    std::string r;
    for (size_t i = 0; i < n; i++)
        AcrescentaUtf8(r, d[i]);
    return r;
}

std::string Utf8Valido(const std::string &s)
{
    // valido se toda sequencia multibyte esta completa
    size_t i = 0;
    bool ok = true;
    while (i < s.size() && ok)
    {
        unsigned char c = (unsigned char) s[i];
        int n = c < 0x80 ? 0 : (c >= 0xC2 && c < 0xE0) ? 1 : (c >= 0xE0 && c < 0xF0) ? 2 : (c >= 0xF0 && c < 0xF5) ? 3 : -1;
        if (n < 0) { ok = false; break; }
        for (int k = 1; k <= n; k++)
            if (i + k >= s.size() || ((unsigned char) s[i + k] & 0xC0) != 0x80) ok = false;
        i += n + 1;
    }
    if (ok) return s;
    return DeLatin1((const uint8_t *) s.data(), s.size());
}

} // namespace ARCA::Base

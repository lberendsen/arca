// ARCA no xOS - Base/Arquivo (ver Arquivo.h).
#include "Arquivo.h"
#include "Texto.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

namespace ARCA::Base {

namespace {
std::string DoAmbiente(const char *var, const char *padrao)
{
    const char *v = getenv(var);
    return (v && *v) ? v : padrao;
}

void Fsync(const std::string &caminho, bool pasta)
{
    int fd = open(caminho.c_str(), pasta ? O_RDONLY | O_DIRECTORY : O_RDONLY);
    if (fd < 0) return;
    fsync(fd);
    close(fd);
}
} // namespace

std::string PastaAcervo() { return DoAmbiente("ARCA_ACERVO", "/mnt/DADOS/Músicas"); }
std::string PastaTrabalho() { return DoAmbiente("ARCA_TRABALHO", "/var/lib/arca/trabalho"); }
std::string PastaCache() { return DoAmbiente("ARCA_CACHE", "/var/cache/arca"); }
std::string ArquivoLedger() { return Combina(PastaAcervo(), "ARCA_Ledger.jsonl"); }
std::string DispositivoCd() { return DoAmbiente("ARCA_CD", "/dev/sr0"); }

std::string TokenDiscogs()
{
    const char *v = getenv("ARCA_DISCOGS_TOKEN");
    if (v && *v) return v;
    try { return Trim(LeTexto("/var/lib/xos/discogs-token")); }
    catch (...) { return ""; }
}

bool Existe(const std::string &c) { std::error_code e; return fs::exists(fs::u8path(c), e); }
bool ArquivoExiste(const std::string &c) { std::error_code e; return fs::is_regular_file(fs::u8path(c), e); }
bool PastaExiste(const std::string &c) { std::error_code e; return fs::is_directory(fs::u8path(c), e); }

void CriaPasta(const std::string &c)
{
    std::error_code e;
    fs::create_directories(fs::u8path(c), e);
    if (e && !PastaExiste(c)) throw std::runtime_error("não foi possível criar a pasta " + c + ": " + e.message());
}

int64_t Tamanho(const std::string &c)
{
    std::error_code e;
    auto t = fs::file_size(fs::u8path(c), e);
    return e ? -1 : (int64_t) t;
}

std::vector<uint8_t> LeBytes(const std::string &c)
{
    FILE *f = fopen(c.c_str(), "rb");
    if (!f) throw std::runtime_error("não foi possível abrir " + c);
    std::vector<uint8_t> r;
    uint8_t b[65536];
    size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0)
        r.insert(r.end(), b, b + n);
    bool erro = ferror(f);
    fclose(f);
    if (erro) throw std::runtime_error("erro de leitura em " + c);
    return r;
}

std::string LeTexto(const std::string &c)
{
    auto b = LeBytes(c);
    size_t i = (b.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) ? 3 : 0;
    return std::string(b.begin() + i, b.end());
}

std::vector<std::string> LeLinhas(const std::string &c)
{
    std::vector<std::string> r;
    for (auto &l : Divide(LeTexto(c), "\n"))
        r.push_back(!l.empty() && l.back() == '\r' ? l.substr(0, l.size() - 1) : l);
    if (!r.empty() && r.back().empty()) r.pop_back();
    return r;
}

void GravaBytes(const std::string &c, const void *d, size_t n)
{
    FILE *f = fopen(c.c_str(), "wb");
    if (!f) throw std::runtime_error("não foi possível criar " + c);
    size_t g = n ? fwrite(d, 1, n, f) : 0;
    bool ok = g == n && fflush(f) == 0;
    fclose(f);
    if (!ok) throw std::runtime_error("erro ao gravar " + c);
}

void GravaTexto(const std::string &c, const std::string &t) { GravaBytes(c, t.data(), t.size()); }

void AcrescentaTexto(const std::string &c, const std::string &t)
{
    FILE *f = fopen(c.c_str(), "ab");
    if (!f) throw std::runtime_error("não foi possível abrir " + c);
    bool ok = fwrite(t.data(), 1, t.size(), f) == t.size() && fflush(f) == 0;
    fsync(fileno(f));
    fclose(f);
    if (!ok) throw std::runtime_error("erro ao gravar " + c);
}

void GravaBytesSincronizado(const std::string &c, const void *d, size_t n)
{
    GravaBytes(c, d, n);
    SincronizaArquivo(c);
}

void SincronizaArquivo(const std::string &c)
{
    Fsync(c, false);
    Fsync(PastaDe(c), true);
}

std::string Combina(const std::string &a, const std::string &b)
{
    if (a.empty() || (!b.empty() && b[0] == '/')) return b;
    return a.back() == '/' ? a + b : a + "/" + b;
}

std::string NomeDoArquivo(const std::string &c)
{
    auto p = c.find_last_of('/');
    return p == std::string::npos ? c : c.substr(p + 1);
}

std::string Extensao(const std::string &c)
{
    std::string n = NomeDoArquivo(c);
    auto p = n.find_last_of('.');
    return (p == std::string::npos || p == 0) ? "" : n.substr(p);
}

std::string NomeSemExtensao(const std::string &c)
{
    std::string n = NomeDoArquivo(c);
    auto p = n.find_last_of('.');
    return (p == std::string::npos || p == 0) ? n : n.substr(0, p);
}

std::string PastaDe(const std::string &c)
{
    auto p = c.find_last_of('/');
    if (p == std::string::npos) return "";
    return p == 0 ? "/" : c.substr(0, p);
}

std::string TrocaExtensao(const std::string &c, const std::string &ext)
{
    std::string pasta = PastaDe(c), base = NomeSemExtensao(c);
    std::string e = ext.empty() || ext[0] == '.' ? ext : "." + ext;
    return pasta.empty() ? base + e : Combina(pasta, base + e);
}

const std::string &CaracteresInvalidosEmNome()
{
    static const std::string s = [] {
        std::string r = "\\/:*?\"<>|";
        for (char c = 1; c < 32; c++) r += c;
        return r;
    }();
    return s;
}

std::vector<std::string> Arquivos(const std::string &pasta, const std::string &ext)
{
    std::vector<std::string> r;
    std::error_code e;
    for (auto &d : fs::directory_iterator(fs::u8path(pasta), e))
    {
        std::error_code e2;
        if (!d.is_regular_file(e2)) continue;
        std::string c = d.path().u8string();
        if (ext.empty() || IgualSemCaixa(Extensao(c), ext)) r.push_back(c);
    }
    std::sort(r.begin(), r.end());
    return r;
}

std::vector<std::string> Pastas(const std::string &pasta)
{
    std::vector<std::string> r;
    std::error_code e;
    for (auto &d : fs::directory_iterator(fs::u8path(pasta), e))
    {
        std::error_code e2;
        if (d.is_directory(e2)) r.push_back(d.path().u8string());
    }
    std::sort(r.begin(), r.end());
    return r;
}

} // namespace ARCA::Base

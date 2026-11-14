// ARCA no xOS - Base/Hash (ver Hash.h).
#include "Hash.h"
#include "Texto.h"

#include <openssl/evp.h>

#include <cstdio>
#include <stdexcept>

namespace ARCA::Base {

struct Hasher::Impl
{
    EVP_MD_CTX *ctx = nullptr;
};

Hasher::Hasher(Algoritmo a) : m(new Impl)
{
    m->ctx = EVP_MD_CTX_new();
    const EVP_MD *md = a == Algoritmo::Sha256 ? EVP_sha256() : a == Algoritmo::Sha1 ? EVP_sha1() : EVP_md5();
    EVP_DigestInit_ex(m->ctx, md, nullptr);
}

Hasher::~Hasher() { EVP_MD_CTX_free(m->ctx); }

void Hasher::Acrescenta(const void *d, size_t n) { EVP_DigestUpdate(m->ctx, d, n); }

std::vector<uint8_t> Hasher::Final()
{
    unsigned char b[EVP_MAX_MD_SIZE];
    unsigned int n = 0;
    EVP_DigestFinal_ex(m->ctx, b, &n);
    return std::vector<uint8_t>(b, b + n);
}

std::vector<uint8_t> Hash(Algoritmo a, const void *d, size_t n)
{
    Hasher h(a);
    h.Acrescenta(d, n);
    return h.Final();
}

std::string Sha256Hex(const void *d, size_t n) { return Hex(Hash(Algoritmo::Sha256, d, n)); }
std::string Sha256Hex(const std::vector<uint8_t> &d) { return Sha256Hex(d.data(), d.size()); }

std::string Sha256HexArquivo(const std::string &caminho)
{
    FILE *f = fopen(caminho.c_str(), "rb");
    if (!f) throw std::runtime_error("não foi possível abrir " + caminho);
    Hasher h;
    std::vector<uint8_t> b(1 << 20);
    size_t n;
    while ((n = fread(b.data(), 1, b.size(), f)) > 0)
        h.Acrescenta(b.data(), n);
    bool erro = ferror(f);
    fclose(f);
    if (erro) throw std::runtime_error("erro de leitura em " + caminho);
    return Hex(h.Final());
}

std::string Base64(const uint8_t *d, size_t n)
{
    static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string r;
    size_t i = 0;
    for (; i + 2 < n; i += 3)
    {
        uint32_t v = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
        r += t[v >> 18]; r += t[(v >> 12) & 63]; r += t[(v >> 6) & 63]; r += t[v & 63];
    }
    if (i + 1 == n)
    {
        uint32_t v = d[i] << 16;
        r += t[v >> 18]; r += t[(v >> 12) & 63]; r += "==";
    }
    else if (i + 2 == n)
    {
        uint32_t v = (d[i] << 16) | (d[i + 1] << 8);
        r += t[v >> 18]; r += t[(v >> 12) & 63]; r += t[(v >> 6) & 63]; r += '=';
    }
    return r;
}
std::string Base64(const std::vector<uint8_t> &d) { return Base64(d.data(), d.size()); }

std::vector<uint8_t> DeBase64(const std::string &s)
{
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    std::vector<uint8_t> r;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : s)
    {
        int v = val(c);
        if (v < 0) continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            r.push_back((uint8_t) (acc >> bits));
        }
    }
    return r;
}

} // namespace ARCA::Base

// ARCA no xOS - Base/Hash: SHA256/SHA1/MD5 do .NET, pela libcrypto (OpenSSL), e Base64.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ARCA::Base {

enum class Algoritmo { Sha256, Sha1, Md5 };

// incremental (o TransformBlock/TransformFinalBlock do .NET)
class Hasher
{
public:
    explicit Hasher(Algoritmo a = Algoritmo::Sha256);
    ~Hasher();
    Hasher(const Hasher &) = delete;
    Hasher &operator=(const Hasher &) = delete;
    void Acrescenta(const void *dados, size_t n);
    std::vector<uint8_t> Final();           // so uma vez
private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

std::vector<uint8_t> Hash(Algoritmo a, const void *dados, size_t n);
std::string Sha256Hex(const void *dados, size_t n);                // minusculas, como o ARCA grava
std::string Sha256Hex(const std::vector<uint8_t> &dados);
// arquivo inteiro, lido em blocos; lanca std::runtime_error se nao abrir
std::string Sha256HexArquivo(const std::string &caminho);

std::string Base64(const uint8_t *dados, size_t n);
std::string Base64(const std::vector<uint8_t> &dados);
std::vector<uint8_t> DeBase64(const std::string &s);

} // namespace ARCA::Base

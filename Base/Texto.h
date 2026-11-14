// ARCA no xOS - Base/Texto: o que o .NET dava pronto para strings e que o C++ nao tem.
// Tudo em UTF-8. "Ordinal" e comparacao byte a byte; "SemCaixa" ignora maiusculas (ASCII e as
// letras acentuadas do portugues e do latim-1, que e o que o acervo tem).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ARCA::Base {

std::string Trim(const std::string &s);                       // string.Trim()
std::string TrimInicio(const std::string &s);
std::string TrimFim(const std::string &s);
std::string Trim(const std::string &s, const std::string &caracteres);   // Trim(char[])

std::string Minusculas(const std::string &s);                 // ToLowerInvariant (UTF-8, latim)
std::string Maiusculas(const std::string &s);                 // ToUpperInvariant (UTF-8, latim)
std::string SemAcentos(const std::string &s);                 // NFD e tira as marcas: "Ação" -> "Acao"

bool IgualSemCaixa(const std::string &a, const std::string &b);          // OrdinalIgnoreCase
bool ComecaCom(const std::string &s, const std::string &prefixo, bool semCaixa = false);
bool TerminaCom(const std::string &s, const std::string &sufixo, bool semCaixa = false);
bool Contem(const std::string &s, const std::string &trecho, bool semCaixa = false);
// IndexOf: posicao em BYTES, ou -1
int64_t Indice(const std::string &s, const std::string &trecho, bool semCaixa = false, size_t inicio = 0);

std::vector<std::string> Divide(const std::string &s, const std::string &separador,
                                bool tirarVazias = false);     // Split
std::vector<std::string> DivideQualquer(const std::string &s, const std::string &separadores,
                                        bool tirarVazias = false);   // Split(char[])
std::string Junta(const std::vector<std::string> &partes, const std::string &separador);
std::string Substitui(const std::string &s, const std::string &de, const std::string &para);

bool VazioOuBranco(const std::string &s);                     // string.IsNullOrWhiteSpace

// numeros (cultura invariante, como int.TryParse / double.TryParse com InvariantCulture)
bool TentaInt(const std::string &s, int &v);
bool TentaInt64(const std::string &s, int64_t &v);
bool TentaDouble(const std::string &s, double &v);
std::string Hex(const uint8_t *dados, size_t n, bool maiusculas = false);
std::string Hex(const std::vector<uint8_t> &dados, bool maiusculas = false);

// tamanho em caracteres (code points), nao em bytes; e o corte por caracteres
size_t Caracteres(const std::string &s);
std::string Sub(const std::string &s, size_t inicio, size_t n = std::string::npos);   // Substring em caracteres

// datas (hora local da maquina)
std::string AgoraIso();                 // 2026-11-07T21:05:33 (o "quando" do ledger)
std::string AgoraCarimbo();             // 20261107210533 (o sufixo .backup-aaaaMMddHHmmss)
std::string Agora(const char *formatoStrftime);

// UTF-16LE <-> UTF-8 (CD-TEXT em Unicode, MCDI)
std::string DeUtf16LE(const uint8_t *dados, size_t n);
// latim-1 / ISO-8859-1 -> UTF-8 (CD-TEXT em 8859-1, ISO 9660 primario)
std::string DeLatin1(const uint8_t *dados, size_t n);
// texto que nao e UTF-8 valido vira latim-1 convertido (arquivos antigos, respostas de servidores)
std::string Utf8Valido(const std::string &s);

// code points <-> UTF-8 (para quem precisa andar caractere a caractere, como o C# anda em char)
std::vector<uint32_t> CodePoints(const std::string &s);
std::string DeCodePoints(const std::vector<uint32_t> &cps);
void AcrescentaUtf8(std::string &s, uint32_t cp);

} // namespace ARCA::Base

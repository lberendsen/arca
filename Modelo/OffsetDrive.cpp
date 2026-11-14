// ARCA no xOS - Modelo/OffsetDrive (de Modelo\OffsetDrive.cs).
#include "OffsetDrive.h"
#include "ExternalMetadataClient.h"

#include "Base/Arquivo.h"
#include "Base/Texto.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <sys/stat.h>

namespace ARCA::Modelo {

using namespace ARCA::Base;

namespace {

// int.TryParse(s, NumberStyles.AllowLeadingSign, InvariantCulture): sinal na frente, sem espacos
bool TentaComSinal(const std::string &s, int &n)
{
    if (s.empty() || s != Trim(s)) return false;
    return TentaInt(s, n);
}

// ToString("+0;-0;0", InvariantCulture)
std::string ComSinal(int n)
{
    return n > 0 ? "+" + std::to_string(n) : std::to_string(n);
}

// o Dictionary do original: troca o valor se a chave ja existe, senao acrescenta no fim
template <class K, class V>
void Poe(std::vector<std::pair<K, V>> &dic, const K &k, const V &v)
{
    for (auto &e : dic)
        if (e.first == k) { e.second = v; return; }
    dic.emplace_back(k, v);
}

} // namespace

std::string OffsetDrive::Pasta()
{
    // LINUX: no original, %LOCALAPPDATA%\ARCA. Aqui, a pasta de caches do ARCA (PORTE.md, secao 4).
    std::string p = PastaCache();
    CriaPasta(p);
    return p;
}

std::string OffsetDrive::Chave(const std::string &strFabricante, const std::string &strModelo)
{
    return Maiusculas(Sem(strFabricante) + "|" + Sem(strModelo));
}

std::string OffsetDrive::Sem(const std::string &s)
{
    return Substitui(Substitui(s, " ", ""), std::string(1, '\0'), "");
}

std::optional<int> OffsetDrive::Obter(const std::string &strFabricante, const std::string &strModelo, std::string &strOrigem)
{
    strOrigem.clear();
    if (strModelo.empty()) return std::nullopt;
    std::string strChave = Chave(strFabricante, strModelo);

    TLocal dicLocal = LerLocal();
    for (auto &kv : dicLocal)
        if (kv.first == strChave) { strOrigem = kv.second.second; return kv.second.first; }

    std::vector<uint8_t> arrTabela = Tabela();
    if (arrTabela.empty()) return std::nullopt;
    for (size_t p = 0; p + TamanhoRegistro <= arrTabela.size(); p += TamanhoRegistro)
    {
        // Encoding.ASCII.GetString(arr, p + 2, 32).Split('\0')[0]
        std::string strNome;
        for (size_t i = 0; i < 32 && arrTabela[p + 2 + i] != 0; i++)
            strNome += arrTabela[p + 2 + i] < 0x80 ? (char) arrTabela[p + 2 + i] : '?';
        size_t i = strNome.find(" - ");
        if (i == std::string::npos) continue;
        if (Chave(strNome.substr(0, i), strNome.substr(i + 3)) != strChave) continue;
        strOrigem = "tabela do AccurateRip";
        return (int) (int16_t) (arrTabela[p] | (arrTabela[p + 1] << 8));     // BitConverter.ToInt16
    }
    return std::nullopt;
}

void OffsetDrive::Registrar(const std::string &strFabricante, const std::string &strModelo, int intOffset, const std::string &strOrigem)
{
    TLocal dic = LerLocal();
    Poe(dic, Chave(strFabricante, strModelo), std::make_pair(intOffset, strOrigem));
    std::string sb;
    sb += "# Offset de leitura por modelo de drive. Uma linha por drive: CHAVE<TAB>offset<TAB>origem\r\n";
    for (auto &e : dic)
        sb += e.first + '\t' + ComSinal(e.second.first) + '\t' + Substitui(e.second.second, "\t", " ") + "\r\n";
    GravaTexto(Combina(Pasta(), "offsets.txt"), sb);      // UTF-8 sem BOM
}

std::vector<int> OffsetDrive::OffsetsDoAcervo()
{
    std::vector<std::pair<int, int>> lst;
    try
    {
        std::string strArq = Combina(Pasta(), "offsets_acervo.txt");
        if (ArquivoExiste(strArq))
            for (const std::string &strLinha : LeLinhas(strArq))
            {
                std::vector<std::string> arr = Divide(strLinha, "\t");
                int k, n;
                if (arr.size() == 2 && TentaComSinal(arr[0], k) && TentaInt(arr[1], n)) lst.emplace_back(k, n);
            }
    }
    catch (...) {}
    // O List.Sort do .NET nao e estavel; aqui os empates ficam na ordem do arquivo.
    std::stable_sort(lst.begin(), lst.end(), [](const std::pair<int, int> &x, const std::pair<int, int> &y) { return y.second < x.second; });
    std::vector<int> lstK;
    for (size_t i = 0; i < lst.size() && i < 4; i++) lstK.push_back(lst[i].first);
    return lstK;
}

void OffsetDrive::RegistrarOffsetDoAcervo(int intOffset)
{
    try
    {
        std::string strArq = Combina(Pasta(), "offsets_acervo.txt");
        std::vector<std::pair<int, int>> dic;
        if (ArquivoExiste(strArq))
            for (const std::string &strLinha : LeLinhas(strArq))
            {
                std::vector<std::string> arr = Divide(strLinha, "\t");
                int k, n;
                if (arr.size() == 2 && TentaComSinal(arr[0], k) && TentaInt(arr[1], n)) Poe(dic, k, n);
            }
        int intAtual = 0;
        for (auto &e : dic)
            if (e.first == intOffset) intAtual = e.second;
        Poe(dic, intOffset, intAtual + 1);
        std::string sb;
        for (auto &kv : dic) sb += std::to_string(kv.first) + '\t' + std::to_string(kv.second) + "\r\n";
        GravaTexto(strArq, sb);
    }
    catch (...) {}
}

OffsetDrive::TLocal OffsetDrive::LerLocal()
{
    TLocal dic;
    try
    {
        std::string strArq = Combina(Pasta(), "offsets.txt");
        if (!ArquivoExiste(strArq)) return dic;
        for (const std::string &strLinha : LeLinhas(strArq))
        {
            if (ComecaCom(strLinha, "#")) continue;
            std::vector<std::string> arr = Divide(strLinha, "\t");
            int n;
            if (arr.size() >= 2 && TentaComSinal(arr[1], n))
                Poe(dic, Trim(arr[0]), std::make_pair(n, arr.size() > 2 ? arr[2] : std::string("offsets.txt")));
        }
    }
    catch (...) {}
    return dic;
}

std::vector<uint8_t> OffsetDrive::Tabela()
{
    std::string strArq = Combina(Pasta(), "DriveOffsets.bin");
    try
    {
        struct stat st;
        if (ArquivoExiste(strArq) && stat(strArq.c_str(), &st) == 0 && difftime(time(nullptr), st.st_mtime) / 86400.0 < 180)
            return LeBytes(strArq);
    }
    catch (...) {}
    try
    {
        // LINUX: HttpWebRequest pelo NetworkConfiguration (libcurl), com o mesmo User-Agent e prazo.
        RespostaHttp resp = NetworkConfiguration::Responder(UrlTabela, {}, 30000, "ARCA");
        const std::string &m = resp.corpo;
        std::vector<uint8_t> arr(m.begin(), m.end());
        if ((int) arr.size() >= TamanhoRegistro && arr.size() % TamanhoRegistro == 0)
        {
            GravaBytes(strArq, arr.data(), arr.size());
            return arr;
        }
    }
    catch (...) {}
    // Sem rede: a copia antiga ainda serve.
    try { if (ArquivoExiste(strArq)) return LeBytes(strArq); } catch (...) {}
    return {};
}

} // namespace ARCA::Modelo

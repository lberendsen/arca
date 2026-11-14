// ARCA no xOS - Modelo/FontesDisco (de Modelo\FontesDisco.cs).
#include "FontesDisco.h"

#include "Base/Arquivo.h"
#include "Base/Excecoes.h"
#include "Interop/LeitorAudio.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace ARCA::Modelo {

using namespace ARCA::Base;

namespace {

// string.Format("{0:+#;-#;0}")
std::string ComSinalOuZero(int64_t n)
{
    if (n == 0) return "0";
    return (n > 0 ? "+" : "-") + std::to_string(n > 0 ? n : -n);
}

} // namespace

// ===============================================================
//  TFonteBin
// ===============================================================

TFonteBin::TFonteBin(const std::string &strArquivo) : m_strArquivo(strArquivo)
{
    // LINUX: new FileInfo(x).Length lançava FileNotFoundException se o arquivo não existisse.
    int64_t lngTamanho = Tamanho(strArquivo);
    if (lngTamanho < 0) throw IOException("Não foi possível localizar o arquivo '" + strArquivo + "'.");
    m_lngTotalAmostras = lngTamanho / 4;
}

TFonteBin::~TFonteBin() { Dispose(); }

void TFonteBin::Reiniciar()
{
    Dispose();
    m_objArq = fopen(m_strArquivo.c_str(), "rb");
    if (!m_objArq) throw IOException("Não foi possível abrir " + m_strArquivo + ": " + strerror(errno));
    setvbuf(m_objArq, nullptr, _IOFBF, 1 << 20);
}

int TFonteBin::Ler(uint32_t *arrDestino, int intMax)
{
    // LINUX: o original lia para um byte[] e copiava (Buffer.BlockCopy) para o uint[]; os bytes
    // do CD são little-endian como o Core 2, então se lê direto no destino.
    size_t intLidos = 0, intQuer = (size_t) intMax * 4;
    uint8_t *p = reinterpret_cast<uint8_t *>(arrDestino);
    while (intLidos < intQuer)
    {
        size_t r = fread(p + intLidos, 1, intQuer - intLidos, m_objArq);
        if (r == 0) break;
        intLidos += r;
    }
    return (int) (intLidos / 4);
}

void TFonteBin::Dispose()
{
    if (m_objArq != nullptr) { fclose(m_objArq); m_objArq = nullptr; }
}

// ===============================================================
//  TFonteWma
// ===============================================================

TFonteWma::TFonteWma(const std::vector<std::string> &lstArquivos, int intLbaInicial)
    : m_lstArquivos(lstArquivos), m_intLbaInicial(intLbaInicial)
{
}

TFonteWma::~TFonteWma() { Dispose(); }

std::string TFonteWma::Validar(const TTocAudio &objToc, const std::function<bool()> &cancelou)
{
    if ((int) m_lstArquivos.size() != objToc.QtdFaixas())
        return "o álbum tem " + std::to_string(m_lstArquivos.size()) + " arquivo(s) e o índice do CD tem " +
               std::to_string(objToc.QtdFaixas()) + " faixa(s) de áudio";
    m_lngTotalAmostras = 0;
    for (size_t i = 0; i < m_lstArquivos.size(); i++)
    {
        Interop::LeitorAudio l;
        if (!l.Abrir(m_lstArquivos[i])) return NomeDoArquivo(m_lstArquivos[i]) + ": " + l.Falha();
        if (l.Canais() != 2 || l.Bits() != 16 || l.Taxa() != 44100)
            return NomeDoArquivo(m_lstArquivos[i]) + " não é áudio de CD (" + std::to_string(l.Canais()) + " canais, " +
                   std::to_string(l.Bits()) + " bits, " + std::to_string(l.Taxa()) + " Hz)";
        int64_t n = 0;
        std::vector<uint8_t> b;
        while (!(b = l.ProximoBlocoBytes()).empty())
        {
            n += (int64_t) b.size() / 4;
            if (cancelou && cancelou()) throw OperationCanceledException();
        }
        if (!l.Terminou())
            return NomeDoArquivo(m_lstArquivos[i]) + ": " + (!l.Falha().empty() ? l.Falha() : "a decodificação parou antes do fim");
        int64_t esperado = objToc.AmostrasDaFaixa((int) i);
        // A última faixa pode vir curta: o WMP não lê o último
        // setor do disco. Até 5 setores não importa — o
        // AccurateRip ignora exatamente as 5 últimas setores.
        bool blnUltima = i == m_lstArquivos.size() - 1;
        if (blnUltima && n < esperado && esperado - n <= AccurateRip::AmostrasPontas)
            m_strObservacao = "a última faixa tem " + std::to_string((esperado - n) / TTocAudio::AmostrasPorSetor) +
                              " setor(es) a menos que o CD (o fim do disco não foi lido); a conferência ignora essa ponta";
        else if (n != esperado)
            return NomeDoArquivo(m_lstArquivos[i]) + " tem " + std::to_string(n) + " amostras, e o índice do CD dá " +
                   std::to_string(esperado) + " à faixa (" + ComSinalOuZero(n - esperado) +
                   "): o arquivo não foi cortado nas fronteiras do CD";
        m_lngTotalAmostras += n;
    }
    return "";
}

void TFonteWma::Reiniciar()
{
    Fechar();
    m_intAtual = -1;
    m_arrResto.clear();
    m_intRestoPos = 0;
}

int TFonteWma::Ler(uint32_t *arrDestino, int intMax)
{
    int intFeitos = 0;
    while (intFeitos < intMax)
    {
        if (m_intRestoPos >= m_arrResto.size())
        {
            m_arrResto = ProximoBloco();
            m_intRestoPos = 0;
            if (m_arrResto.empty()) break;
        }
        int intQtd = std::min(intMax - intFeitos, (int) ((m_arrResto.size() - m_intRestoPos) / 4));
        memcpy(reinterpret_cast<uint8_t *>(arrDestino) + (size_t) intFeitos * 4, m_arrResto.data() + m_intRestoPos, (size_t) intQtd * 4);
        m_intRestoPos += (size_t) intQtd * 4;
        intFeitos += intQtd;
    }
    return intFeitos;
}

std::vector<uint8_t> TFonteWma::ProximoBloco()
{
    while (true)
    {
        if (m_objLeitor != nullptr)
        {
            std::vector<uint8_t> b = m_objLeitor->ProximoBlocoBytes();
            if (!b.empty()) return b;
            if (!m_objLeitor->Terminou())
                throw IOException(!m_objLeitor->Falha().empty() ? m_objLeitor->Falha() : "erro de decodificação");
            Fechar();
        }
        m_intAtual++;
        if (m_intAtual >= (int) m_lstArquivos.size()) return {};
        m_objLeitor = std::make_unique<Interop::LeitorAudio>();
        if (!m_objLeitor->Abrir(m_lstArquivos[m_intAtual])) throw IOException(m_objLeitor->Falha());
    }
}

void TFonteWma::Fechar()
{
    m_objLeitor.reset();
}

void TFonteWma::Dispose() { Fechar(); }

} // namespace ARCA::Modelo

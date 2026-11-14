// ARCA no xOS - Modelo/AcquisitionLogger (de Modelo\AcquisitionLogger.cs).
#include "AcquisitionLogger.h"

#include "Base/Arquivo.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <thread>

namespace ARCA::Modelo {

using namespace ARCA::Base;

namespace {

// DateTime.Now no formato do .NET: "yyyy-MM-dd HH:mm:ss.fff" ou "HH:mm:ss.fff"
std::string AgoraComMs(bool blnComData)
{
    auto agora = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(agora);
    int ms = (int) (std::chrono::duration_cast<std::chrono::milliseconds>(agora.time_since_epoch()).count() % 1000);
    std::tm tm;
    localtime_r(&t, &tm);
    char b[64];
    strftime(b, sizeof b, blnComData ? "%Y-%m-%d %H:%M:%S" : "%H:%M:%S", &tm);
    char r[80];
    snprintf(r, sizeof r, "%s.%03d", b, ms);
    return r;
}

// LINUX: File.AppendAllText(caminho, texto, Encoding.UTF8) poe o BOM quando o arquivo e novo.
void AcrescentaUtf8ComBom(const std::string &strCaminho, const std::string &strTexto)
{
    if (Tamanho(strCaminho) <= 0) AcrescentaTexto(strCaminho, "\xEF\xBB\xBF" + strTexto);
    else AcrescentaTexto(strCaminho, strTexto);
}

// string.Format("{0:F1}") na cultura pt-BR: virgula decimal, sem separador de milhar
std::string F(double v, int casas)
{
    char b[64];
    snprintf(b, sizeof b, "%.*f", casas, v);
    for (char *p = b; *p; p++)
        if (*p == '.') *p = ',';
    return b;
}

} // namespace

AcquisitionLogger::AcquisitionLogger(const std::string &strPastaDestino, std::function<void(const std::string &)> actCallbackUi)
    : m_actCallbackUi(std::move(actCallbackUi))
{
    try
    {
        if (!PastaExiste(strPastaDestino))
        {
            CriaPasta(strPastaDestino);
        }
        m_strCaminhoArquivoLog = Combina(strPastaDestino, "aquisicao_detalhada.log");

        std::lock_guard<std::mutex> trava(m_objLock);
        std::string sbCabecalho;
        sbCabecalho += "\r\n";
        sbCabecalho += "================================================================================\r\n";
        sbCabecalho += "SESSÃO DE AQUISIÇÃO FÍSICA INICIADA EM: " + AgoraComMs(true) + "\r\n";
        sbCabecalho += "================================================================================\r\n";
        AcrescentaUtf8ComBom(m_strCaminhoArquivoLog, sbCabecalho);
    }
    catch (...)
    {
        // O lugar do log e a area de trabalho, sempre. Antes, uma falha aqui (pasta
        // momentaneamente inacessivel) mandava o log da sessao inteira para a pasta do programa.
        // Agora o cabecalho fica pendente e sai junto com a proxima linha que conseguir ser gravada.
        m_strCaminhoArquivoLog = Combina(strPastaDestino, "aquisicao_detalhada.log");
        m_sbPendente += "\r\n";
        m_sbPendente += "SESSÃO DE AQUISIÇÃO FÍSICA INICIADA EM: " + AgoraComMs(true) + "\r\n";
    }
}

void AcquisitionLogger::RegistrarInfo(const std::string &strMensagem)
{
    GravarLogArquivo("[INFO] " + strMensagem);
    if (m_actCallbackUi)
    {
        m_actCallbackUi(strMensagem);
    }
}

void AcquisitionLogger::RegistrarDiagnostico(const std::string &strMensagem)
{
    GravarLogArquivo("[DIAG] " + strMensagem);
}

void AcquisitionLogger::RegistrarAviso(const std::string &strMensagem)
{
    GravarLogArquivo("[AVISO] " + strMensagem);
    if (m_actCallbackUi)
    {
        m_actCallbackUi("⚠ " + strMensagem);
    }
}

void AcquisitionLogger::RegistrarErro(const std::string &strMensagem, const std::exception *ex)
{
    std::string strTexto = "[ERRO] " + strMensagem;
    if (ex != nullptr)
    {
        // LINUX: o C++ nao guarda a pilha na excecao; a linha "Stack Trace:" sai vazia.
        strTexto += std::string("\r\nDetalhes da Exceção: ") + ex->what() + "\r\nStack Trace:\r\n";
    }

    GravarLogArquivo(strTexto);
    if (m_actCallbackUi)
    {
        m_actCallbackUi("❌ " + strMensagem);
    }
}

void AcquisitionLogger::RegistrarProgressoLba(int64_t lngLbaAtual, int64_t lngTotalLba, int intTentativas, int intSetoresBuffer)
{
    if (lngTotalLba <= 0) return;
    double dblPct = (double) lngLbaAtual / lngTotalLba * 100.0;
    std::string strLog = "LBA " + std::to_string(lngLbaAtual) + "/" + std::to_string(lngTotalLba) + " (" + F(dblPct, 1) +
                         "%) | Bloco: " + std::to_string(intSetoresBuffer) + " setores | Retries: " + std::to_string(intTentativas);

    GravarLogArquivo("[PROGRESSO] " + strLog);
}

void AcquisitionLogger::RegistrarFalhaSetor(int64_t lngLba, const std::string &strMotivo, bool blnRecuperado)
{
    std::string strStatus = blnRecuperado ? "RECUPERADO VIA BINARY HALVING" : "ZERO-MASKED (SUBSTITUÍDO POR SILÊNCIO)";
    std::string strLog = "SETOR " + std::to_string(lngLba) + " | MOTIVO: " + strMotivo + " | STATUS: " + strStatus;
    GravarLogArquivo("[SETOR_CRÍTICO] " + strLog);

    if (m_actCallbackUi)
    {
        m_actCallbackUi("Setor " + std::to_string(lngLba) + ": " + strStatus);
    }
}

void AcquisitionLogger::GravarLogArquivo(const std::string &strLinha)
{
    std::lock_guard<std::mutex> trava(m_objLock);
    m_sbPendente += "[" + AgoraComMs(false) + "] " + strLinha + "\r\n";
    // Tres tentativas curtas; nao dando, a linha espera a proxima vez.
    for (int intTentativa = 0; intTentativa < 3; intTentativa++)
    {
        try
        {
            std::string strDir = PastaDe(m_strCaminhoArquivoLog);
            if (!strDir.empty() && !PastaExiste(strDir)) CriaPasta(strDir);
            AcrescentaUtf8ComBom(m_strCaminhoArquivoLog, m_sbPendente);
            m_sbPendente.clear();
            return;
        }
        catch (...) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    }
}

} // namespace ARCA::Modelo

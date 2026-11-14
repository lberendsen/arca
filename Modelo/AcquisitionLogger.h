// ARCA no xOS - Modelo/AcquisitionLogger (de Modelo\AcquisitionLogger.cs).
#pragma once

#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <string>

namespace ARCA::Modelo {

/// Gerenciador de logs diagnosticos ricos para o processo de aquisicao fisica.
/// Registra detalhes em tempo real no arquivo 'aquisicao_detalhada.log' para auditoria
/// e repassa resumos para o delegate de interface.
/// O callback e chamado no fio de quem registra (a extracao): quem o passa decide se despacha
/// para a tela (no original, Dispatcher.BeginInvoke; aqui, Base::Despacha).
class AcquisitionLogger
{
public:
    AcquisitionLogger(const std::string &strPastaDestino, std::function<void(const std::string &)> actCallbackUi);

    const std::string &CaminhoArquivoLog() const { return m_strCaminhoArquivoLog; }

    void RegistrarInfo(const std::string &strMensagem);
    void RegistrarDiagnostico(const std::string &strMensagem);
    void RegistrarAviso(const std::string &strMensagem);
    /// LINUX: no original, Exception ex = null (com ex.StackTrace). Aqui, a excecao do C++ (sem pilha).
    void RegistrarErro(const std::string &strMensagem, const std::exception *ex = nullptr);
    void RegistrarProgressoLba(int64_t lngLbaAtual, int64_t lngTotalLba, int intTentativas, int intSetoresBuffer);
    void RegistrarFalhaSetor(int64_t lngLba, const std::string &strMotivo, bool blnRecuperado);

private:
    void GravarLogArquivo(const std::string &strLinha);

    std::string m_strCaminhoArquivoLog;
    std::function<void(const std::string &)> m_actCallbackUi;
    std::mutex m_objLock;
    // Linhas que nao puderam ser gravadas ainda (arquivo ocupado, pasta fora do ar por um
    // instante). Saem antes da proxima gravacao bem-sucedida.
    std::string m_sbPendente;
};

} // namespace ARCA::Modelo

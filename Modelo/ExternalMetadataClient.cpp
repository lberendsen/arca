// ARCA no xOS - Modelo/ExternalMetadataClient (de Modelo\ExternalMetadataClient.cs).
#include "ExternalMetadataClient.h"

#include "Base/Texto.h"

#include <cstdio>

namespace ARCA::Modelo {

void NetworkConfiguration::Initialize() {}

Base::RespostaHttp NetworkConfiguration::Responder(const std::string &url, const std::vector<std::string> &cabecalhos,
                                                   int timeoutMs, const std::string &userAgent)
{
    std::vector<std::string> cab = cabecalhos;
    if (!userAgent.empty()) cab.push_back("User-Agent: " + userAgent);
    int seg = (timeoutMs + 999) / 1000;
    Base::RespostaHttp r = Base::HttpGet(url, cab, seg < 1 ? 1 : seg);
    if (r.status == 0)
        throw WebException(r.erro.empty() ? "Não foi possível conectar ao servidor remoto" : r.erro, 0);
    if (r.status >= 400)
        throw WebException("O servidor remoto retornou um erro: (" + std::to_string(r.status) + ").", (int) r.status);
    return r;
}

std::vector<uint8_t> NetworkConfiguration::LerCorpo(const Base::RespostaHttp &resp, int intMaximo)
{
    if ((int64_t) resp.corpo.size() > intMaximo)
    {
        std::string lim = std::to_string(intMaximo);
        throw Base::InvalidDataException("Resposta maior que o limite (" + lim + " bytes).");
    }
    return std::vector<uint8_t>(resp.corpo.begin(), resp.corpo.end());
}

namespace {
// CreateRequest: User-Agent, 15 s, ate 3 redirecionamentos
Base::RespostaHttp Pede(const std::string &url)
{
    NetworkConfiguration::CreateProxy();
    return NetworkConfiguration::Responder(url, {}, 15000, ExternalMetadataClient::UserAgent);
}

void ValidateResponse(const Base::RespostaHttp &response)
{
    if (response.status != 200)
        throw WebException("A resposta HTTP não retornou OK. Status: " + std::to_string(response.status),
                           (int) response.status);
}
} // namespace

std::string ExternalMetadataClient::FetchString(const std::string &url)
{
    NetworkConfiguration::Initialize();
    auto response = Pede(url);
    ValidateResponse(response);
    return Base::Utf8Valido(response.corpo);
}

std::vector<uint8_t> ExternalMetadataClient::FetchImage(const std::string &url)
{
    NetworkConfiguration::Initialize();
    auto response = Pede(url);
    ValidateResponse(response);
    // Alguns servidores retornam application/octet-stream; validacao pelo magic number e o principal

    if (response.corpo.size() > 10 * 1024 * 1024)   // 10 MB limit
        throw Base::InvalidDataException("Imagem excede o limite de tamanho seguro.");

    const std::string &data = response.corpo;
    auto b = [&](size_t i) { return (uint8_t) data[i]; };
    bool bIsJpeg = data.size() >= 3 && b(0) == 0xFF && b(1) == 0xD8 && b(2) == 0xFF;
    bool bIsPng = data.size() >= 8 && b(0) == 0x89 && b(1) == 0x50 && b(2) == 0x4E && b(3) == 0x47;

    if (!bIsJpeg && !bIsPng)
        throw Base::InvalidDataException("O arquivo baixado não é uma imagem JPEG ou PNG válida.");
    return std::vector<uint8_t>(data.begin(), data.end());
}

} // namespace ARCA::Modelo

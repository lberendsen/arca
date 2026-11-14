// ARCA no xOS - Modelo/ExternalMetadataClient (de Modelo\ExternalMetadataClient.cs).
// NetworkConfiguration e ExternalMetadataClient: o acesso a internet de todos os resolvedores.
#pragma once

#include "Base/Excecoes.h"
#include "Base/Http.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ARCA::Modelo {

using Base::WebException;

class NetworkConfiguration
{
public:
    // LINUX: no original, liga TLS 1.2 e a validacao de certificados (aceitando o mitmproxy do
    // RetroProxy). Aqui a libcurl ja fala TLS moderno e valida pelo /etc/ssl; nao ha proxy.
    static void Initialize();

    // A requisicao. No original, GetResponse lanca WebException em erro HTTP (>= 400) e em falha
    // de rede, guardando o codigo HTTP na excecao; aqui e igual: lanca Base::WebException com
    // StatusHttp preenchido. Redirecionamentos sao seguidos (ate maxRedirecionamentos).
    static Base::RespostaHttp Responder(const std::string &url, const std::vector<std::string> &cabecalhos,
                                        int timeoutMs, const std::string &userAgent = "");

    static constexpr const char *ChaveStatusHttp = "ARCA.StatusHttp";

    // O codigo HTTP de uma falha vinda de Responder, ou 0.
    static int StatusHttp(const WebException &ex) { return ex.StatusHttp; }

    // Le o corpo inteiro com teto: servidor que nao informa o tamanho (resposta em partes) nao
    // pode encher a memoria. LINUX: a libcurl ja trouxe o corpo; o teto e conferido sobre ele.
    static std::vector<uint8_t> LerCorpo(const Base::RespostaHttp &resp, int intMaximo);

    // LINUX: sem proxy (o RetroProxy so existia porque a Windows 7 nao falava TLS 1.2).
    static void CreateProxy() { Initialize(); }
};

class ExternalMetadataClient
{
public:
    static std::string FetchString(const std::string &url);
    static std::vector<uint8_t> FetchImage(const std::string &url);
    // LINUX: o original mandava "ARCA/1.0 ( <e-mail do Lorenzo> )"; o e-mail dele nao vai a
    // servicos externos (regra dele), entao vai so o nome do programa.
    static constexpr const char *UserAgent = "ARCA/1.0 (xOS)";
};

} // namespace ARCA::Modelo

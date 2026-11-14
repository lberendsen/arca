// ARCA no xOS - Base/Http: o HttpWebRequest/WebClient do ARCA, pela libcurl.
// Direto a internet (o RetroProxy 192.168.50.1:3128 so existia porque a Windows 7 nao falava TLS 1.2).
// Nao lanca excecao: o resultado diz o que houve (status 0 = nao chegou ao servidor).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace ARCA::Base {

struct RespostaHttp
{
    long status = 0;                    // codigo HTTP; 0 se nao houve resposta
    std::string corpo;                  // bytes da resposta (texto em UTF-8 na pratica)
    std::string erro;                   // mensagem da libcurl quando status == 0
    std::map<std::string, std::string> cabecalhos;   // nomes em minusculas
    bool Ok() const { return status >= 200 && status < 300; }
};

// o User-Agent do ARCA (servidores como Discogs e MusicBrainz exigem um)
extern const char *USER_AGENT;

RespostaHttp HttpGet(const std::string &url,
                     const std::vector<std::string> &cabecalhos = {},   // "Nome: valor"
                     int timeoutSegundos = 30);
RespostaHttp HttpPost(const std::string &url, const std::string &corpo,
                      const std::vector<std::string> &cabecalhos = {}, int timeoutSegundos = 30);

std::string UrlEncode(const std::string &s);          // Uri.EscapeDataString
std::string UrlDecode(const std::string &s);

} // namespace ARCA::Base

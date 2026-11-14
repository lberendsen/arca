// ARCA no xOS - Base/Http (ver Http.h).
#include "Http.h"
#include "Texto.h"

#include <curl/curl.h>
#include <mutex>

namespace ARCA::Base {

const char *USER_AGENT = "ARCA/1.0 (xOS)";

namespace {

std::once_flag iniciado;

size_t Recebe(char *p, size_t t, size_t n, void *u)
{
    static_cast<std::string *>(u)->append(p, t * n);
    return t * n;
}

size_t Cabecalho(char *p, size_t t, size_t n, void *u)
{
    std::string linha(p, t * n);
    auto dp = linha.find(':');
    if (dp != std::string::npos)
        (*static_cast<std::map<std::string, std::string> *>(u))[Minusculas(Trim(linha.substr(0, dp)))] =
            Trim(linha.substr(dp + 1));
    return t * n;
}

RespostaHttp Pede(const std::string &url, const std::string *corpo, const std::vector<std::string> &cab, int timeout)
{
    std::call_once(iniciado, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    RespostaHttp r;
    CURL *c = curl_easy_init();
    if (!c)
    {
        r.erro = "curl_easy_init falhou";
        return r;
    }
    curl_slist *lista = nullptr;
    for (auto &h : cab)
        lista = curl_slist_append(lista, h.c_str());
    char erro[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, (long) timeout);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");          // gzip/deflate, descomprimido aqui
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, lista);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, Recebe);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.corpo);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, Cabecalho);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &r.cabecalhos);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, erro);
    if (corpo)
    {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, corpo->data());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long) corpo->size());
    }
    CURLcode rc = curl_easy_perform(c);
    if (rc == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
    else
        r.erro = erro[0] ? erro : curl_easy_strerror(rc);
    curl_slist_free_all(lista);
    curl_easy_cleanup(c);
    return r;
}

} // namespace

RespostaHttp HttpGet(const std::string &url, const std::vector<std::string> &cab, int t) { return Pede(url, nullptr, cab, t); }
RespostaHttp HttpPost(const std::string &url, const std::string &corpo, const std::vector<std::string> &cab, int t)
{
    return Pede(url, &corpo, cab, t);
}

std::string UrlEncode(const std::string &s)
{
    static const char *h = "0123456789ABCDEF";
    std::string r;
    for (unsigned char c : s)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~')
            r += (char) c;
        else
        {
            r += '%';
            r += h[c >> 4];
            r += h[c & 15];
        }
    }
    return r;
}

std::string UrlDecode(const std::string &s)
{
    std::string r;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            r += (char) strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        }
        else if (s[i] == '+') r += ' ';
        else r += s[i];
    }
    return r;
}

} // namespace ARCA::Base

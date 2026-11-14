// ARCA no xOS - Base/Json: nlohmann::json (pacote nlohmann-json do Alpine, so cabecalho).
// Ao gravar no ledger: Json::dump(-1, ' ', false) mantem os acentos em UTF-8, como o ARCA gravava.
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace ARCA::Base {

using Json = nlohmann::ordered_json;     // ordered: as chaves saem na ordem em que foram postas (o ledger)

// leitura que nao lanca: devolve json "discarded" se o texto nao for JSON
inline Json JsonDe(const std::string &texto)
{
    return Json::parse(texto, nullptr, false);
}
inline bool JsonValido(const Json &j) { return !j.is_discarded(); }

// valor de texto de um campo, ou "" se faltar ou nao for texto (o padrao do ARCA ao ler respostas)
inline std::string JsonTexto(const Json &j, const char *chave)
{
    if (!j.is_object()) return "";
    auto it = j.find(chave);
    if (it == j.end() || it->is_null()) return "";
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<long long>());
    if (it->is_number()) return it->dump();
    if (it->is_boolean()) return it->get<bool>() ? "true" : "false";
    return "";
}

} // namespace ARCA::Base

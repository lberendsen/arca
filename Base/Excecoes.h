// ARCA no xOS - Base/Excecoes: as excecoes do .NET que o ARCA lanca e captura pelo tipo.
// Mesmo nome do .NET, para o catch traduzido ficar igual ao original. Todas derivam de
// std::runtime_error (o "catch (Exception ex)" vira "catch (const std::exception &ex)").
#pragma once

#include <stdexcept>
#include <string>

namespace ARCA::Base {

struct OperationCanceledException : std::runtime_error
{
    OperationCanceledException() : std::runtime_error("A operação foi cancelada.") {}
    explicit OperationCanceledException(const std::string &m) : std::runtime_error(m) {}
};
struct InvalidDataException : std::runtime_error { using std::runtime_error::runtime_error; };
struct InvalidOperationException : std::runtime_error { using std::runtime_error::runtime_error; };
struct ArgumentException : std::runtime_error { using std::runtime_error::runtime_error; };
struct IOException : std::runtime_error { using std::runtime_error::runtime_error; };
struct TimeoutException : std::runtime_error { using std::runtime_error::runtime_error; };

// WebException: falha de rede ou resposta HTTP de erro. StatusHttp guarda o codigo
// (o ex.Data[ChaveStatusHttp] do original); 0 = nao houve resposta HTTP.
struct WebException : std::runtime_error
{
    int StatusHttp = 0;
    WebException(const std::string &m, int status = 0) : std::runtime_error(m), StatusHttp(status) {}
};

} // namespace ARCA::Base

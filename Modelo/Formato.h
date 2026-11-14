// ARCA no xOS - Modelo/Formato (de Modelo\Formato.cs). Numeros e tempos no jeito brasileiro.
#pragma once

#include <string>

namespace ARCA::Modelo {

struct Formato
{
    static std::string Inteiro(double v);              // ToString("N0", pt-BR): 12.345
    static std::string Num(double v, int casas);       // ToString("N2", pt-BR): 12.345,68
    static std::string Tempo(double segundos);         // 4:29 ou 1:02:03
    static std::string Itens(int n);                   // "1 item", "12 itens"
};

} // namespace ARCA::Modelo

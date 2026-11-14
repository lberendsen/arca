// ARCA no xOS - Modelo/Formato (de Modelo\Formato.cs).
#include "Formato.h"

#include <cmath>
#include <cstdio>

namespace ARCA::Modelo {

// LINUX: o .NET formata com a CultureInfo pt-BR (ponto nos milhares, virgula nos decimais, e
// arredondamento "away from zero"); aqui isso e feito a mao, sem depender de locale instalada.
std::string Formato::Num(double v, int casas)
{
    if (std::isnan(v) || std::isinf(v)) v = 0;
    double escala = std::pow(10.0, casas);
    double arred = std::round(std::fabs(v) * escala);        // round() afasta do zero, como o .NET
    bool negativo = v < 0 && arred > 0;
    unsigned long long inteiro = (unsigned long long) (arred / escala);
    unsigned long long frac = (unsigned long long) (arred - (double) inteiro * escala + 0.5);
    std::string digitos = std::to_string(inteiro), r;
    int n = (int) digitos.size();
    for (int i = 0; i < n; i++)
    {
        if (i > 0 && (n - i) % 3 == 0) r += '.';
        r += digitos[i];
    }
    if (casas > 0)
    {
        char b[32];
        snprintf(b, sizeof b, "%0*llu", casas, frac);
        r += ',';
        r += b;
    }
    return negativo ? "-" + r : r;
}

std::string Formato::Inteiro(double v) { return Num(v, 0); }

std::string Formato::Tempo(double segundos)
{
    if (segundos < 0 || std::isnan(segundos)) segundos = 0;
    // TimeSpan.FromSeconds arredonda ao milissegundo; Minutes/Seconds sao as partes inteiras
    long long ms = (long long) std::llround(segundos * 1000.0);
    long long total = ms / 1000;
    long long horas = total / 3600, minutos = (total / 60) % 60, segs = total % 60;
    char b[48];
    if (horas >= 1)
        snprintf(b, sizeof b, "%lld:%02lld:%02lld", horas, minutos, segs);
    else
        snprintf(b, sizeof b, "%lld:%02lld", minutos, segs);
    return b;
}

std::string Formato::Itens(int n)
{
    return Inteiro(n) + (n == 1 ? " item" : " itens");
}

} // namespace ARCA::Modelo

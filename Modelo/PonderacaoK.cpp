// ARCA no xOS - Modelo/PonderacaoK (de Modelo\PonderacaoK.cs).
#include "PonderacaoK.h"

#include <cmath>
#include <limits>

namespace ARCA::Modelo {

std::unique_ptr<PonderacaoK::Filtro> PonderacaoK::Shelf(double fs)
{
    const double fc = 1681.9744509555319;
    const double q = 0.7071752369554196;
    const double g = 3.999843853973347;
    double a = std::pow(10.0, g / 40.0);
    double w0 = 2.0 * M_PI * fc / fs;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2.0 * q);
    double sa = std::sqrt(a);
    double a0 = (a + 1) - (a - 1) * cw + 2 * sa * alpha;
    return std::make_unique<Filtro>(
        a * ((a + 1) + (a - 1) * cw + 2 * sa * alpha) / a0,
        -2 * a * ((a - 1) + (a + 1) * cw) / a0,
        a * ((a + 1) + (a - 1) * cw - 2 * sa * alpha) / a0,
        2 * ((a - 1) - (a + 1) * cw) / a0,
        ((a + 1) - (a - 1) * cw - 2 * sa * alpha) / a0);
}

std::unique_ptr<PonderacaoK::Filtro> PonderacaoK::PassaAlta(double fs)
{
    const double fc = 38.13547087602444;
    const double q = 0.5003270373238773;
    double w0 = 2.0 * M_PI * fc / fs;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2.0 * q);
    double a0 = 1 + alpha;
    return std::make_unique<Filtro>(
        (1 + cw) / 2 / a0,
        -(1 + cw) / a0,
        (1 + cw) / 2 / a0,
        -2 * cw / a0,
        (1 - alpha) / a0);
}

double PonderacaoK::Lufs(double z)
{
    return z > 1e-12 ? -0.691 + 10.0 * std::log10(z) : -std::numeric_limits<double>::infinity();
}

} // namespace ARCA::Modelo

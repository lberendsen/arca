// ARCA no xOS - Modelo/Fft (de Modelo\Fft.cs).
#include "Fft.h"

#include <algorithm>
#include <cmath>

namespace ARCA::Modelo {

namespace {

/// <summary>
/// Janela de Hann. Sem ela, o corte abrupto nas bordas do trecho
/// analisado espalha energia por todo o espectro e o gráfico vira
/// serra.
/// </summary>
std::vector<double> MontarJanela()
{
    std::vector<double> j(Fft::Tamanho);
    for (int i = 0; i < Fft::Tamanho; i++)
        j[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (Fft::Tamanho - 1)));
    return j;
}

double Somar(const std::vector<double> &arr)
{
    double s = 0;
    for (double v : arr) s += v;
    return s;
}

const std::vector<double> _janela = MontarJanela();

// Ganho coerente da janela: a Hann reduz a amplitude pela metade em
// média. Dividir por n (e não pela soma da janela) deixava todo o
// espectro 6 dB abaixo do real — um seno a 0 dBFS lia −6 dB.
const double _somaJanela = Somar(_janela);

// Reaproveitados entre chamadas. Alocar dois vetores de 2048 doubles a
// cada quadro dava 32 KB de lixo por quadro — coleta de geração zero
// sem necessidade, e numa máquina modesta isso aparece. Tudo aqui roda
// na thread da interface, então não há concorrência a proteger.
std::vector<double> _re(Fft::Tamanho);
std::vector<double> _im(Fft::Tamanho);

thread_local std::vector<double> _tmpDir;

} // namespace

void Fft::EspectroEstereo(const std::vector<float> &esq, const std::vector<float> *dir, std::vector<double> &destino)
{
    Espectro(esq, destino);
    if (dir == nullptr || dir == &esq) return;
    if (_tmpDir.size() != destino.size()) _tmpDir.assign(destino.size(), 0.0);
    Espectro(*dir, _tmpDir);
    for (size_t i = 0; i < destino.size(); i++)
    {
        double p = (std::pow(10, destino[i] / 10.0) + std::pow(10, _tmpDir[i] / 10.0)) / 2.0;
        destino[i] = p <= 1e-14 ? -140.0 : 10.0 * std::log10(p);
    }
}

void Fft::Espectro(const std::vector<float> &amostras, std::vector<double> &destino)
{
    int n = Tamanho;
    std::vector<double> &re = _re;
    std::vector<double> &im = _im;
    std::fill(im.begin(), im.end(), 0.0);

    int tam = (int) amostras.size();
    int inicio = std::max(0, tam - n);
    for (int i = 0; i < n; i++)
    {
        int k = inicio + i;
        re[i] = (k < tam ? amostras[k] : 0.0f) * _janela[i];
    }

    Transformar(re, im);

    int raias = std::min((int) destino.size(), n / 2);
    for (int i = 0; i < raias; i++)
    {
        double mag = std::sqrt(re[i] * re[i] + im[i] * im[i]) * 2.0 / _somaJanela;
        destino[i] = mag <= 1e-7 ? -140.0 : 20.0 * std::log10(mag);
    }
}

void Fft::Transformar(std::vector<double> &re, std::vector<double> &im)
{
    int n = (int) re.size();

    // Reordenação por inversão de bits.
    for (int i = 1, j = 0; i < n; i++)
    {
        int bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j)
        {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }

    for (int tam = 2; tam <= n; tam <<= 1)
    {
        double ang = -2.0 * M_PI / tam;
        double wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += tam)
        {
            double cr = 1.0, ci = 0.0;
            for (int k = 0; k < tam / 2; k++)
            {
                int a = i + k, b = i + k + tam / 2;
                double xr = re[b] * cr - im[b] * ci;
                double xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr; im[a] += xi;

                double nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

} // namespace ARCA::Modelo

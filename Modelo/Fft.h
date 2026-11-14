// ARCA no xOS - Modelo/Fft (de Modelo\Fft.cs).
#pragma once

#include <vector>

namespace ARCA::Modelo {

/// <summary>
/// Transformada rápida de Fourier, radix-2, no próprio vetor.
///
/// Escrita à mão porque não há biblioteca disponível nesta máquina — sem
/// NuGet, só o que já está instalado. São poucas linhas e o algoritmo é de
/// 1965; não vale a dependência.
/// </summary>
// Os float[] / double[] do original são std::vector (o tamanho do vetor faz o papel do .Length).
struct Fft
{
    /// <summary>Tamanho da janela. 2048 a 44,1 kHz dá ~21 Hz por raia.</summary>
    static constexpr int Tamanho = 2048;

    /// <summary>
    /// Espectro dos dois canais: a média das potências, raia a raia. Os
    /// instrumentos olhavam só o canal esquerdo — o que estava só à direita
    /// não aparecia.
    /// </summary>
    static void EspectroEstereo(const std::vector<float> &esq, const std::vector<float> *dir, std::vector<double> &destino);

    /// <summary>
    /// Magnitudes das raias úteis (metade do tamanho), já em dB.
    /// Recebe o fim de um vetor de amostras.
    /// </summary>
    static void Espectro(const std::vector<float> &amostras, std::vector<double> &destino);

private:
    static void Transformar(std::vector<double> &re, std::vector<double> &im);
};

} // namespace ARCA::Modelo

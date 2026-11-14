// ARCA no xOS - Vistas/Instrumentos (de Vistas\Instrumentos.cs).
#include "Instrumentos.h"

#include <algorithm>
#include <cmath>

namespace ARCA::Vistas {

using Modelo::AnalisadorAudio;
using Modelo::Fft;

const int Instrumento::Fundo = Tela::Cor(0x17, 0x12, 0x0B);
const int Instrumento::Grade = Tela::Cor(0x2A, 0x21, 0x14);
const int Instrumento::GradeForte = Tela::Cor(0x3E, 0x31, 0x20);

Instrumento::Instrumento()
{
    Stretch = Vistas::Stretch::Fill;
    // Bilinear: ampliar uma superfície pequena com vizinho mais próximo
    // deixaria tudo serrilhado, e a suavização sai de graça na placa.
    // LINUX: a textura do NanoVG já é bilinear (RenderOptions.SetBitmapScalingMode LowQuality).
}

void Instrumento::Garantir()
{
    if (T) return;
    T = std::make_unique<Tela>(LarguraInterna(), AlturaInterna());
    Source = T->Bitmap;
    T->Limpar(Fundo);
}

void Instrumento::Atualizar(AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir)
{
    Garantir();
    Desenhar(a, esq, dir);
    T->Aplicar();
}

// ===============================================================
void Goniometro::Desenhar(AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir)
{
    T->Esmaecer(200, Fundo);

    int cx = T->Largura / 2, cy = T->Altura / 2;
    int raio = std::min(cx, cy) - 2;

    T->Linha(cx, cy - raio, cx, cy + raio, Grade);
    T->Linha(cx - raio, cy, cx + raio, cy, Grade);

    const double UmSobreRaiz2 = 0.70710678;
    // Um ponto a cada duas amostras: a figura fica igual e custa metade.
    for (size_t i = 0; i < esq.size(); i += 2)
    {
        double e = esq[i], d = dir[i];
        double x = (e - d) * UmSobreRaiz2;
        double y = (e + d) * UmSobreRaiz2;
        T->PontoSomado(cx + (int) (x * raio), cy - (int) (y * raio), 90, 56, 16);
    }
}

// ===============================================================
const int Osciloscopio::CorEsq = Tela::Cor(0xFF, 0x9E, 0x2C);
const int Osciloscopio::CorDir = Tela::Cor(0xF0, 0xE2, 0xAE);

void Osciloscopio::Desenhar(AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir)
{
    T->Limpar(Fundo);
    int meio = T->Altura / 2;
    int alturaCanal = T->Altura / 2;

    T->Linha(0, meio, T->Largura - 1, meio, GradeForte);
    T->Linha(0, alturaCanal / 2, T->Largura - 1, alturaCanal / 2, Grade);
    T->Linha(0, meio + alturaCanal / 2, T->Largura - 1, meio + alturaCanal / 2, Grade);

    Traco(esq, 0, alturaCanal, CorEsq);
    Traco(dir, meio, alturaCanal, CorDir);
}

/// <summary>
/// Um pixel de largura contém várias amostras. Desenhar só uma delas
/// esconderia os picos; cada coluna mostra o intervalo entre o mínimo e
/// o máximo do trecho que lhe cabe.
/// </summary>
void Osciloscopio::Traco(const std::vector<float> &amostras, int topo, int altura, int cor)
{
    int meio = topo + altura / 2;
    int metade = altura / 2 - 1;
    int n = (int) amostras.size();
    if (n == 0) return;

    for (int x = 0; x < T->Largura; x++)
    {
        int i0 = (int) ((int64_t) x * n / T->Largura);
        int i1 = (int) ((int64_t) (x + 1) * n / T->Largura);
        if (i1 <= i0) i1 = i0 + 1;
        if (i1 > n) i1 = n;

        float min = 1.0f, max = -1.0f;
        for (int i = i0; i < i1; i++)
        {
            float v = amostras[i];
            if (v < min) min = v;
            if (v > max) max = v;
        }
        T->ColunaVertical(x, meio - (int) (max * metade), meio - (int) (min * metade), cor);
    }
}

// ===============================================================
const int Espectro::CorBarra = Tela::Cor(0xFF, 0x9E, 0x2C);
const int Espectro::CorPico = Tela::Cor(0xFF, 0xF0, 0xCC);

/// <summary>
/// O mapa de coluna para raia usa potências e logaritmos, e não muda
/// enquanto a taxa de amostragem for a mesma. Calcular uma vez por
/// faixa em vez de mil vezes por segundo.
/// </summary>
void Espectro::MontarMapa(int taxa)
{
    if (_taxaDoMapa == taxa && !_mapaIni.empty()) return;
    _taxaDoMapa = taxa;

    int largura = T->Largura;
    _mapaIni.assign(largura, 0);
    _mapaFim.assign(largura, 0);
    _picos.assign(largura, DbMin);

    double nyquist = taxa / 2.0;
    double logMin = std::log10(20.0);
    double logMax = std::log10(std::min(20000.0, nyquist));

    for (int x = 0; x < largura; x++)
    {
        double f0 = std::pow(10, logMin + (logMax - logMin) * x / largura);
        double f1 = std::pow(10, logMin + (logMax - logMin) * (x + 1.0) / largura);
        int i0 = (int) (f0 * Fft::Tamanho / taxa);
        int i1 = (int) (f1 * Fft::Tamanho / taxa);
        if (i1 <= i0) i1 = i0 + 1;
        if (i0 < 0) i0 = 0;
        if (i1 > (int) _raias.size()) i1 = (int) _raias.size();
        _mapaIni[x] = i0;
        _mapaFim[x] = i1;
    }

    const double refs[] = {100, 1000, 10000};
    _decadas.assign(3, 0);
    for (int i = 0; i < 3; i++)
        _decadas[i] = (int) ((std::log10(refs[i]) - logMin) / (logMax - logMin) * (largura - 1));
}

void Espectro::Desenhar(AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir)
{
    if (a.Taxa() <= 0) return;
    MontarMapa(a.Taxa());
    T->Limpar(Fundo);

    for (int x : _decadas) T->ColunaVertical(x, 0, T->Altura - 1, Grade);

    Fft::EspectroEstereo(esq, &dir, _raias);   // os dois canais, não só o esquerdo

    int alturaMax = T->Altura - 1;
    for (int x = 0; x < T->Largura; x++)
    {
        int i0 = _mapaIni[x], i1 = _mapaFim[x];
        if (i0 >= i1) continue;

        double db = DbMin;
        for (int i = i0; i < i1; i++) if (_raias[i] > db) db = _raias[i];

        int y = (int) ((1.0 - (db - DbMin) / -DbMin) * alturaMax);
        T->ColunaVertical(x, y, alturaMax, CorBarra);

        // Pico com queda lenta: o olho não acompanha o valor instantâneo.
        if (db > _picos[x]) _picos[x] = db;
        else _picos[x] -= 0.35;
        if (_picos[x] < DbMin) _picos[x] = DbMin;

        T->Ponto(x, (int) ((1.0 - (_picos[x] - DbMin) / -DbMin) * alturaMax), CorPico);
    }
}

// ===============================================================
const int Histograma::CorBarra = Tela::Cor(0xE8, 0xC0, 0x78);

void Histograma::Desenhar(AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir)
{
    T->Limpar(Fundo);

    for (int i = 0; i < Faixas; i++) _conta[i] *= 0.90;

    // Uma amostra a cada quatro: a distribuição não muda e poupa três
    // quartos dos logaritmos, que são a parte cara daqui.
    for (size_t i = 0; i < esq.size(); i += 4)
    {
        double v = std::max(std::fabs(esq[i]), std::fabs(dir[i]));
        if (v <= 0.001) continue;               // abaixo de -60 dB
        int faixa = Faixas - 1 + (int) (20.0 * std::log10(v));
        if (faixa < 0) faixa = 0;
        else if (faixa >= Faixas) faixa = Faixas - 1;
        _conta[faixa] += 1.0;
    }

    double maior = 1.0;
    for (int i = 0; i < Faixas; i++) if (_conta[i] > maior) maior = _conta[i];

    int alturaMax = T->Altura - 2;
    for (int x = 0; x < T->Largura; x++)
    {
        int f = x * Faixas / T->Largura;
        int h = (int) (_conta[f] / maior * alturaMax);
        T->ColunaVertical(x, T->Altura - 1 - h, T->Altura - 1, CorBarra);
    }

    T->ColunaVertical((Faixas - 20) * T->Largura / Faixas, 0, T->Altura - 1, GradeForte);
}

} // namespace ARCA::Vistas

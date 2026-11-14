// ARCA no xOS - Vistas/Tela (de Vistas\Tela.cs).
#include "Tela.h"

#include <algorithm>
#include <cstdlib>

namespace ARCA::Vistas {

Tela::Tela(int largura, int altura)
{
    Largura = std::max(1, largura);
    Altura = std::max(1, altura);
    _pixels.assign((size_t) Largura * Altura, 0);
    _area = Int32Rect(0, 0, Largura, Altura);
    // new WriteableBitmap(Largura, Altura, 96, 96, PixelFormats.Bgra32, null)
    Bitmap = BitmapSource::CreateRgba(Largura, Altura, std::vector<uint8_t>((size_t) Largura * Altura * 4, 0));
}

int Tela::Cor(uint8_t r, uint8_t g, uint8_t b)
{
    return (int32_t) (0xFF000000u | ((uint32_t) r << 16) | ((uint32_t) g << 8) | b);
}

void Tela::Limpar(int cor)
{
    for (size_t i = 0; i < _pixels.size(); i++) _pixels[i] = cor;
}

void Tela::Esmaecer(int permanencia, int piso)
{
    int pr = (piso >> 16) & 0xFF, pg = (piso >> 8) & 0xFF, pb = piso & 0xFF;
    for (size_t i = 0; i < _pixels.size(); i++)
    {
        int p = _pixels[i];
        int r = pr + (((((p >> 16) & 0xFF) - pr) * permanencia) >> 8);
        int g = pg + (((((p >> 8) & 0xFF) - pg) * permanencia) >> 8);
        int b = pb + ((((p & 0xFF) - pb) * permanencia) >> 8);
        _pixels[i] = (int32_t) (0xFF000000u | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b);
    }
}

void Tela::Ponto(int x, int y, int cor)
{
    if (x < 0 || y < 0 || x >= Largura || y >= Altura) return;
    _pixels[(size_t) y * Largura + x] = cor;
}

void Tela::PontoSomado(int x, int y, int r, int g, int b)
{
    if (x < 0 || y < 0 || x >= Largura || y >= Altura) return;
    size_t i = (size_t) y * Largura + x;
    int p = _pixels[i];
    int nr = std::min(255, ((p >> 16) & 0xFF) + r);
    int ng = std::min(255, ((p >> 8) & 0xFF) + g);
    int nb = std::min(255, (p & 0xFF) + b);
    _pixels[i] = (int32_t) (0xFF000000u | ((uint32_t) nr << 16) | ((uint32_t) ng << 8) | (uint32_t) nb);
}

void Tela::Linha(int x0, int y0, int x1, int y1, int cor)
{
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true)
    {
        Ponto(x0, y0, cor);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err << 1;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Tela::ColunaVertical(int x, int yTopo, int yBase, int cor)
{
    if (x < 0 || x >= Largura) return;
    if (yTopo > yBase) { int t = yTopo; yTopo = yBase; yBase = t; }
    if (yTopo < 0) yTopo = 0;
    if (yBase >= Altura) yBase = Altura - 1;
    for (int y = yTopo; y <= yBase; y++) _pixels[(size_t) y * Largura + x] = cor;
}

void Tela::Aplicar()
{
    // Bitmap.WritePixels(_area, _pixels, Largura * 4, 0)
    // LINUX: o inteiro e 0xAARRGGBB (o Bgra32 do WPF na memoria); a textura do NanoVG quer R, G, B, A.
    // A conversao e aqui; a textura GL e reenviada no proximo desenho (no fio da tela).
    std::vector<uint8_t> &d = Bitmap->Pixels;
    d.resize((size_t) _area.Width * _area.Height * 4);
    for (size_t i = 0; i < _pixels.size(); i++)
    {
        uint32_t p = (uint32_t) _pixels[i];
        d[i * 4] = (uint8_t) (p >> 16);
        d[i * 4 + 1] = (uint8_t) (p >> 8);
        d[i * 4 + 2] = (uint8_t) p;
        d[i * 4 + 3] = (uint8_t) (p >> 24);
    }
    Bitmap->MarcaAlterada();
}

} // namespace ARCA::Vistas

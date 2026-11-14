// ARCA no xOS - Vistas/Tela (de Vistas\Tela.cs).
#pragma once

#include "Controles.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace ARCA::Vistas {

/// <summary>
/// Superfície de desenho por pixel, sobre um WriteableBitmap.
///
/// Os instrumentos não são desenhados com formas do WPF: um goniômetro põe
/// milhares de pontos por quadro, e criar milhares de objetos de desenho a
/// 30 quadros por segundo entupiria a árvore visual. Aqui é um vetor de
/// inteiros e uma cópia por quadro — o que a placa faz sem sentir.
///
/// O esmaecimento existe para o rastro: multiplicar tudo em direção ao
/// preto a cada quadro é o que imita o fósforo de um osciloscópio, e é o
/// que deixa a figura do goniômetro legível em vez de piscante.
/// </summary>
// LINUX: o WriteableBitmap vira um BitmapSource cuja textura GL (NanoVG) e reenviada a cada
// Aplicar(); quem mostra e um Image com Source = Bitmap, como no original.
class Tela
{
public:
    std::shared_ptr<BitmapSource> Bitmap;
    int Largura = 0;
    int Altura = 0;

    Tela(int largura, int altura);

    static int Cor(uint8_t r, uint8_t g, uint8_t b);

    void Limpar(int cor);

    /// <summary>Puxa a imagem inteira em direção ao piso. 0..255, onde 255 não apaga nada.</summary>
    void Esmaecer(int permanencia, int piso);

    void Ponto(int x, int y, int cor);

    /// <summary>Soma luz em vez de substituir: onde o traço passa muitas vezes, brilha mais.</summary>
    void PontoSomado(int x, int y, int r, int g, int b);

    void Linha(int x0, int y0, int x1, int y1, int cor);

    void ColunaVertical(int x, int yTopo, int yBase, int cor);

    /// <summary>Envia o quadro para a placa.</summary>
    void Aplicar();

private:
    std::vector<int32_t> _pixels;
    Int32Rect _area;
};

} // namespace ARCA::Vistas

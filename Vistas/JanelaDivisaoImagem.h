// ARCA no xOS - Vistas/JanelaDivisaoImagem (de Vistas\JanelaDivisaoImagem.cs).
#pragma once

#include "Janela.h"

#include <memory>
#include <string>

namespace ARCA::Vistas {

/// <summary>
/// Divide uma imagem em duas por uma linha vertical — para quando duas
/// páginas do encarte foram digitalizadas juntas, abertas na mesa.
///
/// A linha começa no meio. Clicar ou arrastar sobre a imagem a leva até
/// o ponteiro; as setas a movem 1 px (com Shift, 10 px), para acertar a
/// dobra com precisão. O corte é feito na imagem em resolução plena; a
/// tela só mostra uma versão ajustada ao espaço.
/// </summary>
class JanelaDivisaoImagem : public Window
{
public:
    /// <summary>A metade esquerda, em resolução plena, ou null se cancelado.</summary>
    std::shared_ptr<BitmapSource> Esquerda;

    /// <summary>A metade direita, em resolução plena, ou null se cancelado.</summary>
    std::shared_ptr<BitmapSource> Direita;

    JanelaDivisaoImagem(std::shared_ptr<BitmapSource> objImagem, const std::string &strTitulo);

private:
    static constexpr int intLadoMinimo = 32;       // px de imagem de cada lado

    std::shared_ptr<BitmapSource> m_objImagem;
    int m_intLinhaX;                               // em pixels da imagem
    double m_dblEscala = 1;                        // tela / imagem
    bool m_blnArrastando = false;

    std::shared_ptr<Canvas> m_objPalco;
    std::shared_ptr<Image> m_objVista;
    std::shared_ptr<Line> m_objLinha;
    std::shared_ptr<TextBlock> m_txtTamanho;

    UIElementPtr MontarBarra();
    UIElementPtr MontarRodape();
    void AoDividir();
    void MoverLinha(int intX);
    void Diagramar();
    void Desenhar();
    int ColunaDaImagem(Point objTela) const;
    void AoApertar(MouseButtonEventArgs &e);
    void AoMover(MouseEventArgs &e);
    void Soltar();
    void AoTeclar(KeyEventArgs &e);
};

} // namespace ARCA::Vistas

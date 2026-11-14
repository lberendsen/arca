// ARCA no xOS - Vistas/JanelaPagina (de Vistas\JanelaPagina.cs).
#pragma once

#include "Janela.h"

#include <memory>
#include <string>
#include <vector>

namespace ARCA::Vistas {

/// <summary>
/// Uma página do encarte em tela cheia.
///
/// Sem barra de título e maximizada: encarte é para ser olhado, e cada
/// pixel gasto com moldura é pixel a menos de papel.
///
/// Memória: decodifica no tamanho da tela e redecodifica maior só quando há
/// zoom, com teto. Uma imagem de cada vez — trocar de página solta a
/// anterior.
/// </summary>
// Quem abre e o PainelEncarte do EM EXECUCAO (outra frente do porte).
class JanelaPagina : public Window
{
public:
    JanelaPagina(const std::vector<std::string> &arquivos, int inicial);

private:
    static constexpr int Teto = 3000;      // não decodificar além disto

    std::vector<std::string> _arquivos;
    std::shared_ptr<Image> _img;
    std::shared_ptr<ScrollViewer> _area;
    std::shared_ptr<TextBlock> _legenda;

    int _indice = 0;
    double _zoom = 1.0;         // 1 = ajustado à tela
    int _larguraDecodificada = 0;
    Point _ondeDesceu;

    void AoTeclar(KeyEventArgs &e);
    void AoGirarRoda(MouseWheelEventArgs &e);
    void Ir(int passo);
    bool Zoom(double fator);
    void Mostrar();
    /// PainelEncarte.Abrir do original (a pagina decodificada na largura pedida, ou null).
    static std::shared_ptr<BitmapSource> Abrir(const std::string &arquivo, int larguraAlvo);
};

} // namespace ARCA::Vistas

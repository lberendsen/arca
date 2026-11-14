// ARCA no xOS - Vistas/JanelaCorteImagem (de Vistas\JanelaCorteImagem.cs).
#pragma once

#include "Janela.h"

#include <array>
#include <memory>
#include <optional>
#include <string>

namespace ARCA::Vistas {

class VeuDoCorte;

/// <summary>
/// Corte de imagem — para a capa que vem do scanner ou de uma foto.
///
/// O scanner devolve a mesa inteira: a capa num canto, branco em volta.
/// Aqui se escolhe o retângulo que interessa:
/// arrastar por dentro move, pelos cantos redimensiona, fora desenha um
/// novo. "Quadrado" vem ligado porque capa de CD é quadrada (12 x 12 cm).
///
/// O corte é feito na imagem em resolução plena (CroppedBitmap); a tela só
/// mostra uma versão ajustada ao espaço. A janela não comprime nada —
/// devolve pixels, e quem chama decide o formato.
/// </summary>
class JanelaCorteImagem : public Window
{
public:
    /// <summary>A imagem cortada em resolução plena, ou null se cancelado.</summary>
    std::shared_ptr<BitmapSource> Resultado;

    JanelaCorteImagem(std::shared_ptr<BitmapSource> objImagem, const std::string &strTitulo);

    /// <param name="blnQuadrado">
    /// Começa com "Quadrado" ligado (capa) ou desligado (página do encarte:
    /// a contracapa e o inlay traseiro são retangulares, e o corte começa
    /// exatamente em volta do que foi detectado).
    /// </param>
    JanelaCorteImagem(std::shared_ptr<BitmapSource> objImagem, const std::string &strTitulo, bool blnQuadrado);

private:
    static constexpr double dblRaioAlca = 7;       // px de tela em volta de cada canto que contam como "pegar a alça"
    static constexpr int intLadoMinimo = 32;       // px de imagem

    enum class Modo { Nenhum, Mover, Redimensionar, Desenhar };

    /// <summary>
    /// Moldura de tamanho físico fixo. Encarte de borda branca sobre a
    /// mesa branca do scanner não tem contorno a detectar nem a enxergar;
    /// o tamanho, porém, é sabido: a página do livreto é do tamanho da
    /// capa, 12 x 12 cm, e o encarte aberto tem duas páginas, 24 x 12.
    /// </summary>
    enum class Tamanho { Livre, Pagina, Aberto };
    static const char *const arrNomesTamanho[3];
    static const double arrLarguraMm[3];
    static const double arrAlturaMm[3];

    // Lembrados entre uma página e outra: quem digitaliza um encarte
    // passa várias páginas seguidas do mesmo jeito.
    static Tamanho s_objUltimoTamanhoPagina;
    static bool s_blnUltimaEmPe;
    static double s_dblUltimoDpi;

    bool _blnPagina;                            // corte de página (lembra o tamanho) ou de capa
    Tamanho _objTamanho = Tamanho::Livre;
    bool _blnMolduraEmPe = false;               // tamanho fixo com largura e altura trocadas
    std::shared_ptr<ComboBox> _cmbTamanho;
    std::shared_ptr<TextBox> _txtDpi;
    std::shared_ptr<TextBlock> _txtDpiOrigem;

    std::shared_ptr<BitmapSource> _objImagem;
    Rect _objCorte;                             // em pixels da imagem
    double _dblEscala = 1;                      // tela / imagem

    std::shared_ptr<Canvas> _objPalco;
    std::shared_ptr<Image> _objVista;
    std::shared_ptr<VeuDoCorte> _objVeu;
    std::shared_ptr<Rectangle> _objMoldura;
    std::array<std::shared_ptr<Rectangle>, 4> _arrAlcas;
    std::shared_ptr<CheckBox> _chkQuadrado;
    std::shared_ptr<TextBlock> _txtTamanho;

    Modo _objModo = Modo::Nenhum;
    Point _objAncora;                           // canto fixo (redimensionar/desenhar) ou ponto de pega (mover), em px de imagem
    Rect _objCorteNoInicio;

    UIElementPtr MontarBarra();
    UIElementPtr MontarRodape();
    void DefinirImagem(std::shared_ptr<BitmapSource> objImagem);
    void TrocarTamanho(Tamanho objNovo);
    void AplicarTamanho();
    void AjustarMolduraFixa();
    void GirarMoldura();
    Rect Limitado(Rect objR) const;
    double DpiDigitado() const;
    void AoTeclar(KeyEventArgs &e);
    static std::optional<Rect> DetectarConteudo(const std::shared_ptr<BitmapSource> &objImagem);
    static int Primeiro(const std::vector<int> &arrContagem, int intMinimo);
    static int Ultimo(const std::vector<int> &arrContagem, int intMinimo);
    void AjustarAoQuadradoMaior();
    void Diagramar();
    Point ParaTela(Point objP) const;
    Point ParaImagem(Point objP) const;
    void Desenhar();
    void AoApertar(MouseButtonEventArgs &e);
    void AoMover(MouseEventArgs &e);
    void Soltar();
    Rect RetanguloEntre(Point objFixo, Point objMovel) const;
    void AjustarAoQuadrado();
    Vistas::Cursor CursorPara(Point objTela) const;
};

} // namespace ARCA::Vistas

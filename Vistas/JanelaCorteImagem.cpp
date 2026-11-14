// ARCA no xOS - Vistas/JanelaCorteImagem (de Vistas\JanelaCorteImagem.cs). Ver JanelaCorteImagem.h.
#include "JanelaCorteImagem.h"

#include "ImagensDoEncarte.h"

#include "../Base/Texto.h"
#include "../Modelo/Formato.h"

#include <algorithm>
#include <cmath>

namespace ARCA::Vistas {

// ---- o que o System.Windows.Rect dava pronto ----
namespace {

Point TopLeft(const Rect &r) { return Point(r.X, r.Y); }
Point TopRight(const Rect &r) { return Point(r.X + r.Width, r.Y); }
Point BottomRight(const Rect &r) { return Point(r.X + r.Width, r.Y + r.Height); }
Point BottomLeft(const Rect &r) { return Point(r.X, r.Y + r.Height); }

// new Rect(Point, Point): o retangulo entre dois cantos quaisquer
Rect Entre(Point a, Point b)
{
    return Rect(std::min(a.X, b.X), std::min(a.Y, b.Y), std::fabs(a.X - b.X), std::fabs(a.Y - b.Y));
}

// Rect.Contains do WPF: as bordas da direita e de baixo contam como dentro
bool Contem(const Rect &r, Point p)
{
    return p.X >= r.X && p.X <= r.X + r.Width && p.Y >= r.Y && p.Y <= r.Y + r.Height;
}

// Math.Round (para o par mais proximo, como o .NET)
int Arred(double v) { return (int) std::nearbyint(v); }

} // namespace

// LINUX: o veu do original e um Path com um GeometryGroup de FillRule EvenOdd (a imagem inteira menos
// o corte). A camada nao preenche par-impar; aqui o mesmo veu sao os quatro retangulos em volta do
// corte, que dao o mesmo desenho (o corte fica sempre dentro da imagem).
class VeuDoCorte : public UIElement
{
public:
    Brush Fill;
    Rect Fora, Dentro;    // em coordenadas do palco
    VeuDoCorte() { IsHitTestVisible = false; }
    const char *Tipo() const override { return "Path"; }
    Size MeasureOverride(Size d) override
    {
        (void) d;
        return Size(std::max(0.0, Fora.X + Fora.Width), std::max(0.0, Fora.Y + Fora.Height));
    }
    void OnRender(DrawingContext &dc) override
    {
        if (Fill.Nulo() || Fora.IsEmpty()) return;
        double ox = Limites.X, oy = Limites.Y;
        Rect d = Dentro.Intersecao(Fora);
        auto pinta = [&](double x, double y, double w, double h) {
            if (w > 0 && h > 0) dc.Retangulo(Rect(ox + x, oy + y, w, h), Fill);
        };
        pinta(Fora.X, Fora.Y, Fora.Width, d.Y - Fora.Y);                                       // em cima
        pinta(Fora.X, d.Y + d.Height, Fora.Width, Fora.Y + Fora.Height - (d.Y + d.Height));    // embaixo
        pinta(Fora.X, d.Y, d.X - Fora.X, d.Height);                                            // a esquerda
        pinta(d.X + d.Width, d.Y, Fora.X + Fora.Width - (d.X + d.Width), d.Height);            // a direita
    }
};

const char *const JanelaCorteImagem::arrNomesTamanho[3] = {"Livre", "Página — 12 × 12 cm", "Encarte aberto — 24 × 12 cm"};
const double JanelaCorteImagem::arrLarguraMm[3] = {0, 120, 240};
const double JanelaCorteImagem::arrAlturaMm[3] = {0, 120, 120};

JanelaCorteImagem::Tamanho JanelaCorteImagem::s_objUltimoTamanhoPagina = JanelaCorteImagem::Tamanho::Livre;
bool JanelaCorteImagem::s_blnUltimaEmPe = false;
double JanelaCorteImagem::s_dblUltimoDpi = 300;

JanelaCorteImagem::JanelaCorteImagem(std::shared_ptr<BitmapSource> objImagem, const std::string &strTitulo)
    : JanelaCorteImagem(std::move(objImagem), strTitulo, true)
{
}

JanelaCorteImagem::JanelaCorteImagem(std::shared_ptr<BitmapSource> objImagem, const std::string &strTitulo, bool blnQuadrado)
{
    _objImagem = objImagem;

    Title = strTitulo.empty() ? "Cortar imagem" : strTitulo;
    Width = 900;
    Height = 760;
    MinWidth = 600;
    MinHeight = 500;
    WindowStartupLocation = Vistas::WindowStartupLocation::CenterOwner;
    ShowInTaskbar = false;
    Background = Tema::Recurso("Canvas");

    _objVista = std::make_shared<Image>();
    _objVista->Stretch = Stretch::Fill;
    // RenderOptions.SetBitmapScalingMode(_objVista, BitmapScalingMode.HighQuality): ver DefinirImagem
    _objVeu = std::make_shared<VeuDoCorte>();
    _objVeu->Fill = SolidColorBrush(Color::FromArgb(0x99, 0x17, 0x12, 0x0B));
    _objVeu->Canvas_Left = 0;
    _objVeu->Canvas_Top = 0;
    _objMoldura = std::make_shared<Rectangle>();
    _objMoldura->Stroke = Tema::Recurso("AcentoVivo");
    _objMoldura->StrokeThickness = 2;
    _objMoldura->IsHitTestVisible = false;
    _objPalco = std::make_shared<Canvas>();
    _objPalco->Background = Brushes::Transparent;
    _objPalco->ClipToBounds = true;
    _objPalco->Cursor = Cursors::Cross;
    _objPalco->Children.Add(_objVista);
    _objPalco->Children.Add(_objVeu);
    _objPalco->Children.Add(_objMoldura);
    for (int i = 0; i < 4; i++)
    {
        _arrAlcas[i] = std::make_shared<Rectangle>();
        _arrAlcas[i]->Width = 10;
        _arrAlcas[i]->Height = 10;
        _arrAlcas[i]->Fill = Tema::Recurso("AcentoVivo");
        _arrAlcas[i]->Stroke = Brushes::White;
        _arrAlcas[i]->StrokeThickness = 1;
        _arrAlcas[i]->IsHitTestVisible = false;
        _objPalco->Children.Add(_arrAlcas[i]);
    }

    _chkQuadrado = std::make_shared<CheckBox>();
    _chkQuadrado->Content = "Quadrado";
    _chkQuadrado->IsChecked = blnQuadrado;
    _chkQuadrado->VerticalAlignment = VerticalAlignment::Center;
    _chkQuadrado->Margin = Thickness(0, 0, 14, 0);
    _chkQuadrado->ToolTip = "Capa de CD é quadrada. Desligue para cortar outros formatos.";
    _chkQuadrado->Checked += [this](RoutedEventArgs &) { AjustarAoQuadrado(); };
    _txtTamanho = std::make_shared<TextBlock>();
    _txtTamanho->Style = Tema::Estilo("Fraco");
    _txtTamanho->VerticalAlignment = VerticalAlignment::Center;

    _blnPagina = !blnQuadrado;
    _cmbTamanho = std::make_shared<ComboBox>();
    _cmbTamanho->Width = 190;
    _cmbTamanho->VerticalAlignment = VerticalAlignment::Center;
    _cmbTamanho->Margin = Thickness(0, 0, 10, 0);
    for (const char *strNome : arrNomesTamanho) _cmbTamanho->Items.Add(strNome);
    _cmbTamanho->ToolTip = "Moldura do tamanho real do encarte. Ela só se move: arraste, clique onde quer, ou use as setas.";
    _txtDpi = std::make_shared<TextBox>();
    _txtDpi->Width = 44;
    _txtDpi->VerticalAlignment = VerticalAlignment::Center;
    _txtDpi->MaxLength = 4;
    _txtDpi->ToolTip = "Resolução do scan, que transforma centímetros em pixels.";
    _txtDpiOrigem = std::make_shared<TextBlock>();
    _txtDpiOrigem->Style = Tema::Estilo("Fraco");
    _txtDpiOrigem->VerticalAlignment = VerticalAlignment::Center;
    _txtDpiOrigem->Margin = Thickness(4, 0, 14, 0);

    // A resolução do próprio arquivo manda; sem ela, a última usada.
    double dblDpiImagem = ImagensDoEncarte::DpiDe(objImagem);
    _txtDpi->Text = std::to_string(Arred(dblDpiImagem > 0 ? dblDpiImagem : s_dblUltimoDpi));
    _txtDpiOrigem->Text = dblDpiImagem > 0 ? "dpi" : "dpi (o arquivo não informa — confira)";
    _txtDpi->TextChanged += [this](TextChangedEventArgs &) { if (_objTamanho != Tamanho::Livre) AplicarTamanho(); };

    _cmbTamanho->SelectedIndex = (int) (_blnPagina ? s_objUltimoTamanhoPagina : Tamanho::Livre);
    _blnMolduraEmPe = _blnPagina && s_blnUltimaEmPe;
    _objTamanho = (Tamanho) _cmbTamanho->SelectedIndex();
    _cmbTamanho->SelectionChanged += [this](SelectionChangedEventArgs &) { TrocarTamanho((Tamanho) _cmbTamanho->SelectedIndex()); };

    auto objAreaPalco = std::make_shared<Border>();
    objAreaPalco->Background = Tema::Recurso("ChapaFunda");
    objAreaPalco->BorderBrush = Tema::Recurso("Borda");
    objAreaPalco->BorderThickness = Thickness(1);
    objAreaPalco->Margin = Thickness(14);
    objAreaPalco->SetChild(_objPalco);
    objAreaPalco->SizeChanged += [this](SizeChangedEventArgs &) { Diagramar(); };

    auto objRaiz = std::make_shared<DockPanel>();
    objRaiz->Children.Add(MontarBarra());
    objRaiz->Children.Add(MontarRodape());
    objRaiz->Children.Add(objAreaPalco);
    Content = objRaiz;

    _objPalco->MouseLeftButtonDown += [this](MouseButtonEventArgs &e) { AoApertar(e); };
    _objPalco->MouseMove += [this](MouseEventArgs &e) { AoMover(e); };
    _objPalco->MouseLeftButtonUp += [this](MouseButtonEventArgs &) { Soltar(); };
    _objPalco->LostMouseCapture += [this](RoutedEventArgs &) { _objModo = Modo::Nenhum; };
    PreviewKeyDown += [this](KeyEventArgs &e) { AoTeclar(e); };

    if (_objTamanho != Tamanho::Livre) _chkQuadrado->IsChecked = false;
    _chkQuadrado->IsEnabled = _objTamanho == Tamanho::Livre;
    DefinirImagem(objImagem);
}

// ===============================================================
UIElementPtr JanelaCorteImagem::MontarBarra()
{
    // WrapPanel: com o tamanho e a resolução, a barra não cabe numa
    // linha em janela estreita.
    auto objBarra = std::make_shared<WrapPanel>();
    auto objRotulo = std::make_shared<TextBlock>("Tamanho");
    objRotulo->VerticalAlignment = VerticalAlignment::Center;
    objRotulo->Margin = Thickness(0, 0, 6, 0);
    objBarra->Children.Add(objRotulo);
    objBarra->Children.Add(_cmbTamanho);
    objBarra->Children.Add(_txtDpi);
    objBarra->Children.Add(_txtDpiOrigem);
    objBarra->Children.Add(_chkQuadrado);
    // Gira a moldura, não a imagem: o encarte aberto posto em pé na
    // mesa vira uma moldura de 12 x 24. A página, se precisar, se
    // endireita depois, pelos botões de giro da miniatura.
    auto objGirar = std::make_shared<Button>();
    objGirar->Content = "Girar moldura 90°";
    objGirar->Padding = Thickness(10, 3, 10, 3);
    objGirar->Margin = Thickness(0, 0, 8, 0);
    objGirar->ToolTip = "Troca largura e altura da moldura, em volta do centro dela. A imagem não gira.";
    objGirar->Click += [this](RoutedEventArgs &) { GirarMoldura(); };
    auto objTudo = std::make_shared<Button>();
    objTudo->Content = "Selecionar tudo";
    objTudo->Padding = Thickness(10, 3, 10, 3);
    objTudo->Margin = Thickness(0, 0, 14, 0);
    objTudo->Click += [this](RoutedEventArgs &) {
        _cmbTamanho->SelectedIndex = (int) Tamanho::Livre;   // "tudo" não tem tamanho fixo
        bool blnQuadrado = _chkQuadrado->IsChecked();
        _chkQuadrado->IsChecked = false;
        _objCorte = Rect(0, 0, _objImagem->PixelWidth, _objImagem->PixelHeight);
        if (blnQuadrado && _objImagem->PixelWidth == _objImagem->PixelHeight) _chkQuadrado->IsChecked = true;
        Desenhar();
    };
    objBarra->Children.Add(objGirar);
    objBarra->Children.Add(objTudo);
    objBarra->Children.Add(_txtTamanho);

    auto objBorda = std::make_shared<Border>();
    objBorda->Background = Tema::Recurso("BarraFerramentas");
    objBorda->BorderBrush = Tema::Recurso("Borda");
    objBorda->BorderThickness = Thickness(0, 0, 0, 1);
    objBorda->Padding = Thickness(14, 8, 14, 8);
    objBorda->SetChild(objBarra);
    DockPanel::SetDock(objBorda, Dock::Top);
    return objBorda;
}

UIElementPtr JanelaCorteImagem::MontarRodape()
{
    auto objGrade = std::make_shared<Grid>();
    objGrade->ColumnDefinitions.push_back(ColumnDefinition());
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));
    auto objAjuda = std::make_shared<TextBlock>(
        "Arraste por dentro para mover, pelos cantos para redimensionar, ou fora para desenhar um novo corte. "
        "Com tamanho fixo, a moldura só se move: arraste ou clique onde ela deve ficar. Setas: 1 px; com Shift, 10 px.");
    objAjuda->Style = Tema::Estilo("Fraco");
    objAjuda->VerticalAlignment = VerticalAlignment::Center;
    objAjuda->TextWrapping = TextWrapping::Wrap;
    objGrade->Children.Add(objAjuda);
    auto objCancelar = std::make_shared<Button>();
    objCancelar->Content = "Cancelar";
    objCancelar->IsCancel = true;
    objCancelar->Padding = Thickness(18, 6, 18, 6);
    objCancelar->Margin = Thickness(12, 0, 0, 0);
    objCancelar->Click += [this](RoutedEventArgs &) { Resultado = nullptr; Close(); };
    auto objUsar = std::make_shared<Button>();
    objUsar->Content = "Cortar e usar";
    objUsar->IsDefault = true;
    objUsar->Padding = Thickness(18, 6, 18, 6);
    objUsar->Margin = Thickness(8, 0, 0, 0);
    objUsar->Click += [this](RoutedEventArgs &) {
        Int32Rect objR(Arred(_objCorte.X), Arred(_objCorte.Y), Arred(_objCorte.Width), Arred(_objCorte.Height));
        if (_blnPagina) { s_objUltimoTamanhoPagina = _objTamanho; s_blnUltimaEmPe = _blnMolduraEmPe; }
        double dblDpi = DpiDigitado();
        if (dblDpi > 0) s_dblUltimoDpi = dblDpi;
        objR.Width = std::min(objR.Width, _objImagem->PixelWidth - objR.X);
        objR.Height = std::min(objR.Height, _objImagem->PixelHeight - objR.Y);
        // CroppedBitmap
        Resultado = _objImagem->Cortada(objR);
        Close();
    };
    Grid::SetColumn(objCancelar, 1);
    Grid::SetColumn(objUsar, 2);
    objGrade->Children.Add(objCancelar);
    objGrade->Children.Add(objUsar);

    auto objBorda = std::make_shared<Border>();
    objBorda->Background = Tema::Recurso("BarraFerramentas");
    objBorda->BorderBrush = Tema::Recurso("Borda");
    objBorda->BorderThickness = Thickness(0, 1, 0, 0);
    objBorda->Padding = Thickness(14, 8, 14, 8);
    objBorda->SetChild(objGrade);
    DockPanel::SetDock(objBorda, Dock::Bottom);
    return objBorda;
}

// ===============================================================
/// <summary>
/// Troca a imagem (abertura ou giro) e recomeça o corte. Primeiro
/// tenta achar a capa sobre o fundo do scanner; não achando, quadrado
/// central de 90% do lado menor.
/// </summary>
void JanelaCorteImagem::DefinirImagem(std::shared_ptr<BitmapSource> objImagem)
{
    _objImagem = objImagem;
    // LINUX: o WPF mostra a imagem plena reduzida pela placa com BitmapScalingMode.HighQuality. Aqui a
    // imagem vira uma textura do NanoVG, que tem limite de lado (8192 na GT210) e reduz sem mipmaps
    // (serrilha); a tela mostra entao uma copia de no maximo 2048 px, reduzida pela media de area. O
    // corte continua na imagem plena: so a vista muda.
    int intMaior = std::max(objImagem->PixelWidth, objImagem->PixelHeight);
    _objVista->Source = intMaior > 2048 ? objImagem->Escalada(2048.0 / intMaior) : objImagem;

    std::optional<Rect> objAchado = DetectarConteudo(objImagem);
    if (objAchado)
    {
        _objCorte = *objAchado;
        if (_chkQuadrado->IsChecked()) AjustarAoQuadradoMaior();
    }
    else
    {
        double dblLado = std::min(objImagem->PixelWidth, objImagem->PixelHeight) * 0.9;
        _objCorte = Rect((objImagem->PixelWidth - dblLado) / 2, (objImagem->PixelHeight - dblLado) / 2, dblLado, dblLado);
        if (!_chkQuadrado->IsChecked())
            _objCorte = Rect(objImagem->PixelWidth * 0.05, objImagem->PixelHeight * 0.05,
                             objImagem->PixelWidth * 0.9, objImagem->PixelHeight * 0.9);
    }
    // Com tamanho fixo, a detecção só serve de ponto de partida: a
    // moldura nasce centrada no que foi achado (o miolo impresso da
    // página, quando a borda é branca).
    if (_objTamanho != Tamanho::Livre) AjustarMolduraFixa();
    Diagramar();
}

// ===============================================================
void JanelaCorteImagem::TrocarTamanho(Tamanho objNovo)
{
    _objTamanho = objNovo;
    if (objNovo == Tamanho::Livre)
    {
        // Livre mantém a moldura onde está, agora com as alças: é
        // assim que se afina um encarte um pouco maior ou menor.
        _chkQuadrado->IsEnabled = true;
        Desenhar();
        return;
    }
    _chkQuadrado->IsChecked = false;
    _chkQuadrado->IsEnabled = false;
    AplicarTamanho();
}

void JanelaCorteImagem::AplicarTamanho()
{
    AjustarMolduraFixa();
    Desenhar();
}

/// <summary>
/// Dá à moldura o tamanho físico escolhido, convertido pela resolução,
/// mantendo o centro atual. Se não couber (resolução errada, scan
/// menor que o encarte), reduz mantendo a proporção.
/// </summary>
void JanelaCorteImagem::AjustarMolduraFixa()
{
    double dblDpi = DpiDigitado();
    if (dblDpi <= 0) return;   // ainda digitando
    int i = (int) _objTamanho;
    double dblL = arrLarguraMm[i] / 25.4 * dblDpi, dblA = arrAlturaMm[i] / 25.4 * dblDpi;
    if (_blnMolduraEmPe) { double dblT = dblL; dblL = dblA; dblA = dblT; }
    double dblFator = std::min(1.0, std::min(_objImagem->PixelWidth / dblL, _objImagem->PixelHeight / dblA));
    dblL *= dblFator;
    dblA *= dblFator;
    Point objCentro(_objCorte.X + _objCorte.Width / 2, _objCorte.Y + _objCorte.Height / 2);
    _objCorte = Limitado(Rect(objCentro.X - dblL / 2, objCentro.Y - dblA / 2, dblL, dblA));
}

/// <summary>
/// Troca largura e altura da moldura em volta do centro. Com tamanho
/// fixo, a orientação fica guardada (vale também ao mudar o dpi); no
/// Livre, se a moldura girada não couber, encolhe na mesma proporção.
/// </summary>
void JanelaCorteImagem::GirarMoldura()
{
    if (_objTamanho != Tamanho::Livre)
    {
        _blnMolduraEmPe = !_blnMolduraEmPe;
        AplicarTamanho();
        return;
    }
    double dblL = _objCorte.Height, dblA = _objCorte.Width;
    double dblFator = std::min(1.0, std::min(_objImagem->PixelWidth / dblL, _objImagem->PixelHeight / dblA));
    dblL *= dblFator;
    dblA *= dblFator;
    Point objCentro(_objCorte.X + _objCorte.Width / 2, _objCorte.Y + _objCorte.Height / 2);
    _objCorte = Limitado(Rect(objCentro.X - dblL / 2, objCentro.Y - dblA / 2, dblL, dblA));
    Desenhar();
}

/// <summary>O retângulo empurrado para dentro da imagem, sem mudar o tamanho.</summary>
Rect JanelaCorteImagem::Limitado(Rect objR) const
{
    double dblX = std::max(0.0, std::min(_objImagem->PixelWidth - objR.Width, objR.X));
    double dblY = std::max(0.0, std::min(_objImagem->PixelHeight - objR.Height, objR.Y));
    return Rect(dblX, dblY, objR.Width, objR.Height);
}

/// <summary>A resolução do campo, ou 0 se o que está lá não serve.</summary>
double JanelaCorteImagem::DpiDigitado() const
{
    int intDpi;
    return Base::TentaInt(Base::Trim(_txtDpi->Text()), intDpi) && intDpi >= 50 && intDpi <= 4800 ? intDpi : 0;
}

/// <summary>As setas movem a moldura 1 px da imagem; com Shift, 10. Fora dos campos de texto e da lista.</summary>
void JanelaCorteImagem::AoTeclar(KeyEventArgs &e)
{
    // LINUX: o ComboBoxItem do WPF nao existe na camada (a lista suspensa trata as proprias teclas)
    if (dynamic_cast<TextBox *>(e.OriginalSource) || dynamic_cast<ComboBox *>(e.OriginalSource)) return;
    double dblDx = 0, dblDy = 0;
    double dblPasso = Tem(Keyboard::Modifiers(), ModifierKeys::Shift) ? 10 : 1;
    switch (e.Key)
    {
    case Key::Left: dblDx = -dblPasso; break;
    case Key::Right: dblDx = dblPasso; break;
    case Key::Up: dblDy = -dblPasso; break;
    case Key::Down: dblDy = dblPasso; break;
    default: return;
    }
    _objCorte = Limitado(Rect(_objCorte.X + dblDx, _objCorte.Y + dblDy, _objCorte.Width, _objCorte.Height));
    Desenhar();
    e.Handled = true;
}

/// <summary>
/// Onde a capa está sobre a mesa do scanner. Trabalha numa cópia
/// reduzida (~300 px): a cor do fundo sai da mediana dos quatro cantos,
/// e conta como conteúdo a linha ou coluna em que mais de 2% dos pixels
/// se afastam dela. Ruído e poeira não chegam a 2%; a capa passa
/// muito. null quando não há fundo claro a separar (foto já recortada,
/// imagem toda preenchida) — aí vale o corte padrão.
/// </summary>
std::optional<Rect> JanelaCorteImagem::DetectarConteudo(const std::shared_ptr<BitmapSource> &objImagem)
{
    try
    {
        double dblReducao = std::min(1.0, 300.0 / std::max(objImagem->PixelWidth, objImagem->PixelHeight));
        // TransformedBitmap(ScaleTransform) + FormatConvertedBitmap(Bgr32): a camada guarda RGBA; os
        // canais vao na ordem R, G, B em vez de B, G, R, o que nao muda a soma das diferencas
        std::shared_ptr<BitmapSource> objPequena = objImagem->Escalada(dblReducao);
        int intL = objPequena->PixelWidth, intA = objPequena->PixelHeight;
        if (intL < 16 || intA < 16) return std::nullopt;
        const std::vector<uint8_t> &arrPx = objPequena->Pixels;
        auto canal = [&](int intIndice, int c) { return (int) arrPx[(size_t) intIndice * 4 + c]; };

        // Fundo: mediana dos quatro cantos (um canto pode cair na capa).
        int arrCantos[4] = {0, intL - 1, (intA - 1) * intL, intA * intL - 1};
        int arrCanal[3];
        for (int c = 0; c < 3; c++)
        {
            int arrV[4];
            for (int k = 0; k < 4; k++) arrV[k] = canal(arrCantos[k], c);
            std::sort(arrV, arrV + 4);
            arrCanal[c] = (arrV[1] + arrV[2]) / 2;
        }

        std::vector<int> arrPorLinha(intA), arrPorColuna(intL);
        for (int y = 0; y < intA; y++)
            for (int x = 0; x < intL; x++)
            {
                int intP = y * intL + x;
                int intDif = std::abs(canal(intP, 0) - arrCanal[0]) + std::abs(canal(intP, 1) - arrCanal[1])
                           + std::abs(canal(intP, 2) - arrCanal[2]);
                if (intDif > 60) { arrPorLinha[y]++; arrPorColuna[x]++; }
            }

        int intY0 = Primeiro(arrPorLinha, intL / 50), intY1 = Ultimo(arrPorLinha, intL / 50);
        int intX0 = Primeiro(arrPorColuna, intA / 50), intX1 = Ultimo(arrPorColuna, intA / 50);
        if (intY0 < 0 || intX0 < 0) return std::nullopt;

        Rect objR(intX0 / dblReducao, intY0 / dblReducao, (intX1 - intX0 + 1) / dblReducao, (intY1 - intY0 + 1) / dblReducao);
        objR = objR.Intersecao(Rect(0, 0, objImagem->PixelWidth, objImagem->PixelHeight));
        double dblFracao = objR.Width * objR.Height / ((double) objImagem->PixelWidth * objImagem->PixelHeight);
        // Quase a imagem toda: não havia fundo. Pequeno demais: era sujeira.
        if (dblFracao > 0.92 || objR.Width < intLadoMinimo * 2 || objR.Height < intLadoMinimo * 2) return std::nullopt;
        return objR;
    }
    catch (...) { return std::nullopt; }
}

int JanelaCorteImagem::Primeiro(const std::vector<int> &arrContagem, int intMinimo)
{
    for (int i = 0; i < (int) arrContagem.size(); i++)
        if (arrContagem[i] > std::max(1, intMinimo)) return i;
    return -1;
}

int JanelaCorteImagem::Ultimo(const std::vector<int> &arrContagem, int intMinimo)
{
    for (int i = (int) arrContagem.size() - 1; i >= 0; i--)
        if (arrContagem[i] > std::max(1, intMinimo)) return i;
    return -1;
}

/// <summary>
/// Quadrado pelo lado MAIOR em volta do conteúdo detectado: a capa
/// inteira fica dentro, mesmo que a detecção tenha cortado uma borda
/// clara. Limitado à imagem.
/// </summary>
void JanelaCorteImagem::AjustarAoQuadradoMaior()
{
    double dblLado = std::min(std::max(_objCorte.Width, _objCorte.Height),
                              (double) std::min(_objImagem->PixelWidth, _objImagem->PixelHeight));
    Point objCentro(_objCorte.X + _objCorte.Width / 2, _objCorte.Y + _objCorte.Height / 2);
    double dblX = std::max(0.0, std::min(_objImagem->PixelWidth - dblLado, objCentro.X - dblLado / 2));
    double dblY = std::max(0.0, std::min(_objImagem->PixelHeight - dblLado, objCentro.Y - dblLado / 2));
    _objCorte = Rect(dblX, dblY, dblLado, dblLado);
}

/// <summary>Ajusta a imagem ao espaço disponível, sem distorcer, e redesenha o corte.</summary>
void JanelaCorteImagem::Diagramar()
{
    double dblLargura = _objPalco->ActualWidth, dblAltura = _objPalco->ActualHeight;
    if (dblLargura <= 0 || dblAltura <= 0 || !_objImagem) return;
    _dblEscala = std::min(dblLargura / _objImagem->PixelWidth, dblAltura / _objImagem->PixelHeight);
    _objVista->Width = _objImagem->PixelWidth * _dblEscala;
    _objVista->Height = _objImagem->PixelHeight * _dblEscala;
    Canvas::SetLeft(_objVista, (dblLargura - _objVista->Width) / 2);
    Canvas::SetTop(_objVista, (dblAltura - _objVista->Height) / 2);
    Desenhar();
}

Point JanelaCorteImagem::ParaTela(Point objP) const
{
    return Point(Canvas::GetLeft(_objVista) + objP.X * _dblEscala, Canvas::GetTop(_objVista) + objP.Y * _dblEscala);
}

Point JanelaCorteImagem::ParaImagem(Point objP) const
{
    double dblX = (objP.X - Canvas::GetLeft(_objVista)) / _dblEscala;
    double dblY = (objP.Y - Canvas::GetTop(_objVista)) / _dblEscala;
    return Point(std::max(0.0, std::min((double) _objImagem->PixelWidth, dblX)),
                 std::max(0.0, std::min((double) _objImagem->PixelHeight, dblY)));
}

void JanelaCorteImagem::Desenhar()
{
    if (std::isnan(Canvas::GetLeft(_objVista))) return;
    Point objA = ParaTela(TopLeft(_objCorte)), objB = ParaTela(BottomRight(_objCorte));
    Rect objNaTela = Entre(objA, objB);
    Rect objImagemNaTela(Canvas::GetLeft(_objVista), Canvas::GetTop(_objVista), _objVista->Width, _objVista->Height);

    // GeometryGroup { FillRule = EvenOdd }: a imagem menos o corte (ver VeuDoCorte)
    _objVeu->Fora = objImagemNaTela;
    _objVeu->Dentro = objNaTela;

    Canvas::SetLeft(_objMoldura, objNaTela.X);
    Canvas::SetTop(_objMoldura, objNaTela.Y);
    _objMoldura->Width = objNaTela.Width;
    _objMoldura->Height = objNaTela.Height;

    Point arrCantos[4] = {TopLeft(objNaTela), TopRight(objNaTela), BottomRight(objNaTela), BottomLeft(objNaTela)};
    for (int i = 0; i < 4; i++)
    {
        Canvas::SetLeft(_arrAlcas[i], arrCantos[i].X - 5);
        Canvas::SetTop(_arrAlcas[i], arrCantos[i].Y - 5);
    }

    _txtTamanho->Text = "Corte: " + std::to_string(Arred(_objCorte.Width)) + " x " + std::to_string(Arred(_objCorte.Height)) +
                        " px   (imagem " + std::to_string(_objImagem->PixelWidth) + " x " + std::to_string(_objImagem->PixelHeight) + ")";
    double dblDpi = DpiDigitado();
    if (dblDpi > 0)
        _txtTamanho->Text += "   ≈ " + Modelo::Formato::Num(_objCorte.Width / dblDpi * 2.54, 1) + " x " +
                             Modelo::Formato::Num(_objCorte.Height / dblDpi * 2.54, 1) + " cm";
    InvalidateVisual();
}

// ===============================================================
void JanelaCorteImagem::AoApertar(MouseButtonEventArgs &e)
{
    Point objTela = e.GetPosition(_objPalco.get());
    Point objImg = ParaImagem(objTela);

    // Tamanho fixo: a moldura só se move. Clicar fora dela a traz
    // centrada no clique, e o arraste continua dali.
    if (_objTamanho != Tamanho::Livre)
    {
        if (!Contem(_objCorte, objImg))
        {
            _objCorte = Limitado(Rect(objImg.X - _objCorte.Width / 2, objImg.Y - _objCorte.Height / 2,
                                      _objCorte.Width, _objCorte.Height));
            Desenhar();
        }
        _objCorteNoInicio = _objCorte;
        _objModo = Modo::Mover;
        _objAncora = objImg;
        _objPalco->CaptureMouse();
        return;
    }
    _objCorteNoInicio = _objCorte;

    // Canto mais próximo, dentro do raio da alça: redimensiona com o
    // canto oposto fixo.
    Point arrCantos[4] = {TopLeft(_objCorte), TopRight(_objCorte), BottomRight(_objCorte), BottomLeft(_objCorte)};
    for (int i = 0; i < 4; i++)
    {
        Point objCantoTela = ParaTela(arrCantos[i]);
        if (std::fabs(objCantoTela.X - objTela.X) <= dblRaioAlca && std::fabs(objCantoTela.Y - objTela.Y) <= dblRaioAlca)
        {
            _objModo = Modo::Redimensionar;
            _objAncora = arrCantos[(i + 2) % 4];
            _objPalco->CaptureMouse();
            return;
        }
    }

    if (Contem(_objCorte, objImg))
    {
        _objModo = Modo::Mover;
        _objAncora = objImg;
    }
    else
    {
        _objModo = Modo::Desenhar;
        _objAncora = objImg;
    }
    _objPalco->CaptureMouse();
}

void JanelaCorteImagem::AoMover(MouseEventArgs &e)
{
    Point objTela = e.GetPosition(_objPalco.get());
    if (_objModo == Modo::Nenhum)
    {
        _objPalco->Cursor = CursorPara(objTela);
        return;
    }
    if (e.LeftButton != MouseButtonState::Pressed) { Soltar(); return; }

    Point objImg = ParaImagem(objTela);
    if (_objModo == Modo::Mover)
    {
        double dblX = _objCorteNoInicio.X + (objImg.X - _objAncora.X);
        double dblY = _objCorteNoInicio.Y + (objImg.Y - _objAncora.Y);
        dblX = std::max(0.0, std::min(_objImagem->PixelWidth - _objCorteNoInicio.Width, dblX));
        dblY = std::max(0.0, std::min(_objImagem->PixelHeight - _objCorteNoInicio.Height, dblY));
        _objCorte = Rect(dblX, dblY, _objCorteNoInicio.Width, _objCorteNoInicio.Height);
    }
    else
    {
        Rect objNovo = RetanguloEntre(_objAncora, objImg);
        if (objNovo.Width >= intLadoMinimo && objNovo.Height >= intLadoMinimo) _objCorte = objNovo;
    }
    Desenhar();
}

void JanelaCorteImagem::Soltar()
{
    _objModo = Modo::Nenhum;
    if (_objPalco->IsMouseCaptured()) _objPalco->ReleaseMouseCapture();
}

/// <summary>
/// Retângulo do canto fixo até o ponteiro. No modo quadrado, o lado é
/// o maior dos dois deslocamentos, limitado ao que cabe na imagem a
/// partir do canto fixo, na direção em que o ponteiro foi.
/// </summary>
Rect JanelaCorteImagem::RetanguloEntre(Point objFixo, Point objMovel) const
{
    if (!_chkQuadrado->IsChecked()) return Entre(objFixo, objMovel);

    double dblDx = objMovel.X - objFixo.X, dblDy = objMovel.Y - objFixo.Y;
    double dblSinalX = dblDx < 0 ? -1 : 1, dblSinalY = dblDy < 0 ? -1 : 1;
    double dblLado = std::max(std::fabs(dblDx), std::fabs(dblDy));
    double dblCabeX = dblSinalX > 0 ? _objImagem->PixelWidth - objFixo.X : objFixo.X;
    double dblCabeY = dblSinalY > 0 ? _objImagem->PixelHeight - objFixo.Y : objFixo.Y;
    dblLado = std::min(dblLado, std::min(dblCabeX, dblCabeY));
    return Entre(objFixo, Point(objFixo.X + dblSinalX * dblLado, objFixo.Y + dblSinalY * dblLado));
}

/// <summary>Ao ligar "Quadrado", o corte atual vira quadrado pelo lado menor, mantendo o centro.</summary>
void JanelaCorteImagem::AjustarAoQuadrado()
{
    double dblLado = std::min(_objCorte.Width, _objCorte.Height);
    Point objCentro(_objCorte.X + _objCorte.Width / 2, _objCorte.Y + _objCorte.Height / 2);
    double dblX = std::max(0.0, std::min(_objImagem->PixelWidth - dblLado, objCentro.X - dblLado / 2));
    double dblY = std::max(0.0, std::min(_objImagem->PixelHeight - dblLado, objCentro.Y - dblLado / 2));
    _objCorte = Rect(dblX, dblY, dblLado, dblLado);
    Desenhar();
}

Vistas::Cursor JanelaCorteImagem::CursorPara(Point objTela) const
{
    if (_objTamanho != Tamanho::Livre) return Cursors::SizeAll;
    Point arrCantos[4] = {TopLeft(_objCorte), TopRight(_objCorte), BottomRight(_objCorte), BottomLeft(_objCorte)};
    for (int i = 0; i < 4; i++)
    {
        Point objC = ParaTela(arrCantos[i]);
        if (std::fabs(objC.X - objTela.X) <= dblRaioAlca && std::fabs(objC.Y - objTela.Y) <= dblRaioAlca)
            return i % 2 == 0 ? Cursors::SizeNWSE : Cursors::SizeNESW;
    }
    return Contem(_objCorte, ParaImagem(objTela)) ? Cursors::SizeAll : Cursors::Cross;
}

} // namespace ARCA::Vistas

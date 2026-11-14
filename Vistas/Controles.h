// ARCA no xOS - Vistas/Controles: os controles do WPF que o ARCA usa, desenhados com NanoVG no
// visual do Tema.
//
// O MODELO: arvore retida, como a do WPF. As vistas do ARCA montam a tela em codigo (new Grid,
// ColumnDefinitions, Border { Child = ... }), guardam os elementos com nome (TxtTitulo, BtnTocar) e
// depois so trocam propriedades (TxtTitulo.Text = ..., PainelAviso.Visibility = Collapsed). Um
// modelo imediato obrigaria a reescrever cada tela; este deixa traduzir linha por linha.
//
//   - Elemento = std::shared_ptr<T> (o "new" do C#): auto t = std::make_shared<TextBlock>();
//   - Propriedades sao campos publicos com os nomes do WPF. Nao ha notificacao por campo: o laco da
//     janela refaz o layout (Measure/Arrange, as mesmas regras do WPF) e redesenha depois de
//     qualquer evento, despacho (Base::Despacha) ou tique de relogio. Parado, nao gasta nada.
//   - Algumas propriedades disparam evento ao mudar, como no WPF: TextBox::Text (TextChanged),
//     ToggleButton::IsChecked (Checked/Unchecked), Slider::Value (ValueChanged),
//     Selector::SelectedIndex (SelectionChanged). Sao Prop<T>: atribuir com "=" e ler com "()".
//   - Eventos: Evento<Args>, com "+=" como no C#: Botao->Click += [this](RoutedEventArgs &) {...};
//     Os de mouse e teclado sao roteados como no WPF: Preview* desce da janela ate o alvo, o resto
//     sobe do alvo ate a janela, e Handled = true para a subida.
//   - Estilo: Elemento->Style = Tema::Estilo("Fraco"). O valor posto no elemento vence o do estilo.
//     Foreground, FontSize, FontWeight, FontStyle e FontFamily herdam do pai quando ninguem os define.
//   - Tudo isso so no fio da tela. Fio de trabalho pede com Base::Despacha([=] { ... }).
#pragma once

#include "Tema.h"

#include <any>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

struct NVGcontext;

namespace ARCA::Vistas {

class UIElement;
class Window;
class DrawingContext;

// =====================================================================================
// Eventos e propriedades que avisam
// =====================================================================================

// event EventHandler<T>: varios ouvintes, "+=" para ligar. Chamar com evento(args).
template <class... A>
class Evento
{
public:
    using Funcao = std::function<void(A...)>;
    Evento &operator+=(Funcao f)
    {
        _ouvintes.push_back(std::move(f));
        return *this;
    }
    void operator()(A... a) const
    {
        auto copia = _ouvintes;   // um ouvinte pode ligar outro durante a chamada
        for (auto &f : copia) f(a...);
    }
    bool Vazio() const { return _ouvintes.empty(); }
    void Limpa() { _ouvintes.clear(); }

private:
    std::vector<Funcao> _ouvintes;
};

// Uma propriedade de dependencia que avisa o dono quando muda (so quando o valor e outro).
template <class T>
class Prop
{
public:
    using AoMudar = std::function<void(const T &antigo, const T &novo)>;
    Prop(T inicial, AoMudar f) : _v(std::move(inicial)), _f(std::move(f)) {}
    Prop &operator=(const T &v)
    {
        if (v == _v) return *this;
        T antigo = _v;
        _v = v;
        if (_f) _f(antigo, _v);
        return *this;
    }
    template <class U, class = std::enable_if_t<std::is_convertible_v<U, T> && !std::is_same_v<std::decay_t<U>, T>>>
    Prop &operator=(const U &v) { return *this = T(v); }
    const T &operator()() const { return _v; }
    operator const T &() const { return _v; }
    const T *operator->() const { return &_v; }
    // muda sem avisar (uso interno dos controles)
    void Define(const T &v) { _v = v; }

private:
    T _v;
    AoMudar _f;
};
template <class T, class U> bool operator==(const Prop<T> &a, const U &b) { return a() == b; }
template <class T, class U> bool operator!=(const Prop<T> &a, const U &b) { return !(a() == b); }

// ---- argumentos ----
struct RoutedEventArgs
{
    bool Handled = false;
    UIElement *OriginalSource = nullptr;   // o elemento mais fundo atingido
    UIElement *Source = nullptr;           // quem esta tratando agora
};

enum class MouseButton { Left, Middle, Right, XButton1, XButton2 };
enum class MouseButtonState { Released, Pressed };

struct MouseEventArgs : RoutedEventArgs
{
    Point Posicao;                                  // na janela
    Point GetPosition(const UIElement *relativoA) const;
    // e.LeftButton do WPF: o estado do botao esquerdo no momento do evento (o arraste de
    // JanelaCorteImagem e JanelaDivisaoImagem confere se ainda esta apertado)
    MouseButtonState LeftButton = MouseButtonState::Released;
};
struct MouseButtonEventArgs : MouseEventArgs
{
    MouseButton ChangedButton = MouseButton::Left;
    int ClickCount = 1;
};
struct MouseWheelEventArgs : MouseEventArgs
{
    int Delta = 0;     // 120 por degrau da roda, positivo para cima (como no Windows)
};

enum class ModifierKeys { None = 0, Alt = 1, Control = 2, Shift = 4 };
inline ModifierKeys operator|(ModifierKeys a, ModifierKeys b) { return ModifierKeys(int(a) | int(b)); }
inline ModifierKeys operator&(ModifierKeys a, ModifierKeys b) { return ModifierKeys(int(a) & int(b)); }
inline bool Tem(ModifierKeys m, ModifierKeys bit) { return (int(m) & int(bit)) != 0; }

// System.Windows.Input.Key (so as que o ARCA e os controles usam; o resto chega como Key::None,
// com a KeySym do X em KeyEventArgs::KeySym)
enum class Key
{
    None, Enter, Escape, Tab, Back, Delete, Insert, Space,
    Left, Right, Up, Down, Home, End, PageUp, PageDown,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    D0, D1, D2, D3, D4, D5, D6, D7, D8, D9,
    NumPad0, NumPad1, NumPad2, NumPad3, NumPad4, NumPad5, NumPad6, NumPad7, NumPad8, NumPad9,
    Add, Subtract, Multiply, Divide, OemPlus, OemMinus, OemComma, OemPeriod,
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt, System,
    MediaPlayPause, MediaStop, MediaNextTrack, MediaPreviousTrack, VolumeUp, VolumeDown, VolumeMute,
};

struct KeyEventArgs : RoutedEventArgs
{
    ARCA::Vistas::Key Key = Key::None;
    unsigned long KeySym = 0;
    ModifierKeys Modificadores = ModifierKeys::None;   // Keyboard.Modifiers no momento da tecla
    bool IsRepeat = false;
};
struct TextCompositionEventArgs : RoutedEventArgs
{
    std::string Text;     // UTF-8, ja composto pelo XIM (acentos)
};
struct SizeChangedEventArgs
{
    Size PreviousSize, NewSize;
    bool WidthChanged = false, HeightChanged = false;
};
template <class T>
struct RoutedPropertyChangedEventArgs : RoutedEventArgs
{
    T OldValue{}, NewValue{};
};
struct SelectionChangedEventArgs : RoutedEventArgs
{
    std::vector<int> AddedItems, RemovedItems;   // indices na vista
};
struct TextChangedEventArgs : RoutedEventArgs {};
struct CancelEventArgs
{
    bool Cancel = false;
};
struct ScrollChangedEventArgs : RoutedEventArgs
{
    double VerticalOffset = 0, VerticalChange = 0, HorizontalOffset = 0, HorizontalChange = 0;
    double ExtentHeight = 0, ExtentWidth = 0, ViewportHeight = 0, ViewportWidth = 0;
    double ExtentHeightChange = 0, ViewportHeightChange = 0;
};

// =====================================================================================
// Imagem (BitmapSource / BitmapImage / WriteableBitmap)
// =====================================================================================

// Pixels RGBA (nao pre-multiplicados), 8 bits por canal. Decodifica JPEG (libjpeg, com a reducao
// do proprio decodificador para miniaturas) e PNG/BMP (stb_image). A textura na placa e criada
// so na hora de desenhar, no fio da tela; decodificar pode ser em qualquer fio.
class BitmapSource
{
public:
    int PixelWidth = 0, PixelHeight = 0;
    double DpiX = 96, DpiY = 96;
    std::vector<uint8_t> Pixels;            // RGBA, PixelWidth*4 por linha

    virtual ~BitmapSource();
    double Width() const { return PixelWidth * 96.0 / (DpiX > 0 ? DpiX : 96); }   // em DIP
    double Height() const { return PixelHeight * 96.0 / (DpiY > 0 ? DpiY : 96); }

    // BitmapImage { UriSource / StreamSource, DecodePixelWidth, CacheOption = OnLoad }. 0 = tamanho
    // cheio; so a largura ou so a altura mantem a proporcao. Falha: lanca std::runtime_error.
    static std::shared_ptr<BitmapSource> DeArquivo(const std::string &caminho, int decodePixelWidth = 0,
                                                   int decodePixelHeight = 0);
    static std::shared_ptr<BitmapSource> DeBytes(const std::vector<uint8_t> &dados, int decodePixelWidth = 0,
                                                 int decodePixelHeight = 0);
    static std::shared_ptr<BitmapSource> DeBytes(const uint8_t *dados, size_t n, int decodePixelWidth = 0,
                                                 int decodePixelHeight = 0);
    // BitmapSource.Create com Bgra32 (o formato do WPF): converte para RGBA
    static std::shared_ptr<BitmapSource> CreateBgra32(int largura, int altura, double dpiX, double dpiY,
                                                      const uint8_t *bgra, int passo);
    static std::shared_ptr<BitmapSource> CreateRgba(int largura, int altura, std::vector<uint8_t> rgba);

    // TransformedBitmap / CroppedBitmap / JpegBitmapEncoder
    std::shared_ptr<BitmapSource> Girada(int graus) const;          // 90, 180, 270 (horario, como o RotateTransform)
    std::shared_ptr<BitmapSource> Escalada(double escala) const;    // media de area (reduz bem)
    std::shared_ptr<BitmapSource> Redimensionada(int largura, int altura) const;
    std::shared_ptr<BitmapSource> Cortada(Int32Rect r) const;
    std::vector<uint8_t> CodificaJpeg(int qualidade) const;        // QualityLevel 1..100; mantem os DPI
    // PngBitmapEncoder, BmpBitmapEncoder e TiffBitmapEncoder { Compression = Lzw }: sem perda, com os
    // DPI (pHYs, biXPelsPerMeter, XResolution). RGB; o PNG e o TIFF levam o alfa so se ele existir.
    // Falha: lanca std::runtime_error.
    std::vector<uint8_t> CodificaPng() const;
    std::vector<uint8_t> CodificaBmp() const;
    std::vector<uint8_t> CodificaTiffLzw() const;

    // WriteableBitmap: quem escreveu em Pixels chama isto para a textura ser reenviada
    void MarcaAlterada() { _versao++; }
    // a imagem do NanoVG (cria ou atualiza); 0 se nao deu
    int Textura(NVGcontext *vg);

private:
    int _textura = 0;
    uint64_t _versao = 1, _versaoTextura = 0;
};
using ImageSource = std::shared_ptr<BitmapSource>;

// =====================================================================================
// Texto
// =====================================================================================
struct FonteTexto
{
    std::string Familia = Tema::Fonte;     // "Fonte", "FonteMono", "Consolas", "Segoe UI"...
    double Tamanho = 12;
    ARCA::Vistas::FontWeight Peso = FontWeight::Normal;
    ARCA::Vistas::FontStyle Estilo = FontStyle::Normal;
};

struct LinhaDeTexto
{
    size_t Inicio = 0, Fim = 0;   // em bytes no texto
    double Largura = 0;
};

namespace Texto {
double Largura(const FonteTexto &f, const std::string &s);
double AlturaLinha(const FonteTexto &f);        // a do Segoe UI (1,33 em) ou do Consolas (1,17 em)
double Ascendente(const FonteTexto &f);         // do topo da linha a linha de base
// quebra em linhas (\n, \r\n e, com largura finita e quebrar=true, nas palavras)
std::vector<LinhaDeTexto> Quebra(const FonteTexto &f, const std::string &s, double largura, bool quebrar);
// corta e poe "…" para caber (TextTrimming.CharacterEllipsis)
std::string ComReticencias(const FonteTexto &f, const std::string &s, double largura);
// posicao em pixels de cada fronteira de caractere (tamanho = caracteres + 1) e o byte de cada uma
void Posicoes(const FonteTexto &f, const std::string &s, std::vector<double> &x, std::vector<size_t> &bytes);
} // namespace Texto

// =====================================================================================
// Desenho
// =====================================================================================
class DrawingContext
{
public:
    explicit DrawingContext(NVGcontext *vg) : vg(vg) {}
    NVGcontext *vg;     // para quem desenha direto (instrumentos)

    // Border: fundo, borda com espessuras por lado e cantos arredondados
    void Retangulo(const Rect &r, const Brush &fundo, const Brush &borda = Brush(), Thickness espessura = Thickness(),
                   ARCA::Vistas::CornerRadius cantos = ARCA::Vistas::CornerRadius());
    void Elipse(const Rect &r, const Brush &fundo, const Brush &borda = Brush(), double espessura = 0);
    void Linha(Point a, Point b, const Brush &cor, double espessura = 1);
    // texto de uma linha com o topo em y (a linha de base fica em y + Ascendente)
    void Texto(const std::string &s, const FonteTexto &f, double x, double y, const Brush &cor);
    void Imagem(BitmapSource &img, const Rect &destino, double opacidade = 1, const Rect *origem = nullptr);
    // recorte (ClipToBounds): empilha; Restaura desempilha
    void Recorta(const Rect &r);
    void Restaura();
    Rect RecorteAtual() const;
    // opacidade acumulada (UIElement.Opacity)
    void EmpilhaOpacidade(double o);
    void DesempilhaOpacidade();

    // pinta o caminho atual do NanoVG com um pincel (degrade de varias paradas em faixas)
    void PreencheCaminho(const Brush &b, const Rect &limites, const std::function<void()> &caminho);

private:
    std::vector<Rect> _recortes;
    std::vector<double> _opacidades;
};

// =====================================================================================
// UIElement / FrameworkElement
// =====================================================================================
class UIElement : public std::enable_shared_from_this<UIElement>
{
public:
    UIElement();
    virtual ~UIElement();
    UIElement(const UIElement &) = delete;
    UIElement &operator=(const UIElement &) = delete;

    // ---- FrameworkElement ----
    std::string Name;
    std::any Tag;
    std::string ToolTip;                    // "" = sem dica
    double Width = NaN, Height = NaN;       // NaN = Auto
    double MinWidth = 0, MinHeight = 0, MaxWidth = Infinito, MaxHeight = Infinito;
    std::optional<Thickness> Margin;
    Vistas::HorizontalAlignment HorizontalAlignment = Vistas::HorizontalAlignment::Stretch;
    Vistas::VerticalAlignment VerticalAlignment = Vistas::VerticalAlignment::Stretch;
    Vistas::Visibility Visibility = Vistas::Visibility::Visible;
    bool IsEnabled = true;
    bool IsHitTestVisible = true;
    bool Focusable = false;
    bool ClipToBounds = false;
    double Opacity = 1;
    Vistas::Cursor Cursor = Vistas::Cursor::Herda;
    const Vistas::Style *Style = nullptr;

    // TextElement: herdados pela arvore quando vazios
    Brush Foreground;                       // vazio = herda
    double FontSize = NaN;                  // NaN = herda
    std::optional<Vistas::FontWeight> FontWeight;
    std::optional<Vistas::FontStyle> FontStyle;
    std::string FontFamily;                 // "" = herda; "Fonte", "FonteMono", "Consolas"...

    // propriedades anexadas (DockPanel.Dock, Grid.Row..., Canvas.Left/Top)
    Dock DockPanel_Dock = Dock::Left;
    int Grid_Row = 0, Grid_Column = 0, Grid_RowSpan = 1, Grid_ColumnSpan = 1;
    double Canvas_Left = NaN, Canvas_Top = NaN, Canvas_Right = NaN, Canvas_Bottom = NaN;

    // ---- estado do layout (so leitura) ----
    Size DesiredSize;
    double ActualWidth = 0, ActualHeight = 0;
    Rect Limites;                           // na janela, depois do Arrange
    UIElement *Parent = nullptr;

    // ---- consultas ----
    bool IsMouseOver() const;               // o mouse esta nele ou num filho
    bool IsMouseDirectlyOver() const;
    bool IsKeyboardFocused() const;
    bool IsKeyboardFocusWithin() const;
    bool IsMouseCaptured() const;
    bool IsMouseCaptureWithin() const;
    bool IsVisible() const;                 // visivel ele e toda a linhagem, e numa janela aberta
    bool IsEnabledEfetivo() const;          // IsEnabled dele e da linhagem
    Window *Janela() const;                 // Window.GetWindow(this)
    Point TranslatePoint(Point p, const UIElement *relativoA) const;
    bool EhAncestralDe(const UIElement *e) const;

    // ---- acoes ----
    bool Focus();
    bool CaptureMouse();
    void ReleaseMouseCapture();
    void InvalidateVisual();                // pede redesenho (o laco ja redesenha depois de eventos)
    void InvalidateMeasure() { InvalidateVisual(); }
    void UpdateLayout();
    void BringIntoView();                   // rola os ScrollViewer de cima ate ele aparecer

    // ---- valores efetivos (local > gabarito > estilo > herdado > padrao) ----
    Brush ForegroundEfetivo() const;
    double FontSizeEfetivo() const;
    Vistas::FontWeight FontWeightEfetivo() const;
    Vistas::FontStyle FontStyleEfetivo() const;
    std::string FontFamilyEfetiva() const;
    FonteTexto Fonte() const;
    Thickness MargemEfetiva() const;
    const Vistas::Style *EstiloEfetivo() const;    // Style ou o implicito do tipo
    virtual const char *Tipo() const { return "UIElement"; }

    // ---- eventos ----
    Evento<MouseButtonEventArgs &> PreviewMouseLeftButtonDown, MouseLeftButtonDown;
    Evento<MouseButtonEventArgs &> PreviewMouseLeftButtonUp, MouseLeftButtonUp;
    Evento<MouseButtonEventArgs &> PreviewMouseRightButtonDown, MouseRightButtonDown, MouseRightButtonUp;
    Evento<MouseButtonEventArgs &> PreviewMouseDown, MouseDown, MouseUp;     // qualquer botao
    Evento<MouseButtonEventArgs &> MouseDoubleClick;                         // Control.MouseDoubleClick
    Evento<MouseEventArgs &> PreviewMouseMove, MouseMove, MouseEnter, MouseLeave;
    Evento<MouseWheelEventArgs &> PreviewMouseWheel, MouseWheel;
    Evento<KeyEventArgs &> PreviewKeyDown, KeyDown, PreviewKeyUp, KeyUp;
    Evento<TextCompositionEventArgs &> PreviewTextInput, TextInput;
    Evento<SizeChangedEventArgs &> SizeChanged;
    Evento<RoutedEventArgs &> Loaded, GotFocus, LostFocus, GotKeyboardFocus, LostKeyboardFocus;
    Evento<RoutedEventArgs &> LostMouseCapture;

    // ---- layout (as regras do FrameworkElement) ----
    void Measure(Size disponivel);
    void Arrange(Rect final);
    virtual Size MeasureOverride(Size disponivel);
    virtual Size ArrangeOverride(Size final);
    // filhos visuais (layout, desenho, clique e roteamento), na ordem de desenho
    virtual void Filhos(std::vector<UIElement *> &saida) const { (void) saida; }

    // ---- desenho ----
    void Render(DrawingContext &dc);              // ele, os filhos, recorte e opacidade
    virtual void OnRender(DrawingContext &dc);    // so ele (antes dos filhos)
    virtual void OnRenderDepois(DrawingContext &dc) { (void) dc; }   // por cima dos filhos

    // ---- clique ----
    // o elemento mais fundo sob o ponto (nullptr se nenhum)
    UIElement *Acerta(Point p);
    // o proprio elemento recebe clique neste ponto? (padrao: tem fundo, ou e um controle)
    virtual bool AcertaProprio(Point p) const;

    // ---- tratamento de classe (OnXxx do WPF): roda antes dos ouvintes do proprio elemento ----
    virtual void OnPreviewMouseDown(MouseButtonEventArgs &e) { (void) e; }
    virtual void OnMouseDown(MouseButtonEventArgs &e) { (void) e; }
    virtual void OnMouseUp(MouseButtonEventArgs &e) { (void) e; }
    virtual void OnMouseMove(MouseEventArgs &e) { (void) e; }
    virtual void OnMouseEnter(MouseEventArgs &e) { (void) e; }
    virtual void OnMouseLeave(MouseEventArgs &e) { (void) e; }
    virtual void OnMouseWheel(MouseWheelEventArgs &e) { (void) e; }
    virtual void OnKeyDown(KeyEventArgs &e) { (void) e; }
    virtual void OnKeyUp(KeyEventArgs &e) { (void) e; }
    virtual void OnTextInput(TextCompositionEventArgs &e) { (void) e; }
    virtual void OnGotFocus() {}
    virtual void OnLostFocus() {}
    virtual void OnLostMouseCapture() {}
    // o cursor quando o mouse esta sobre este elemento (o Cursor dele, se posto)
    virtual Vistas::Cursor CursorEm(Point p) const;
    // a dica a mostrar neste ponto (padrao: ToolTip)
    virtual std::string DicaEm(Point p) const { (void) p; return ToolTip; }
    // o gabarito pode mudar o texto (gatilhos); nullopt = nao muda
    virtual std::optional<Brush> ForegroundDoGabarito() const { return std::nullopt; }
    virtual std::optional<Vistas::FontWeight> FontWeightDoGabarito() const { return std::nullopt; }
    virtual double OpacidadeDoGabarito() const { return 1; }

    // liga o pai (os paineis chamam ao acrescentar filhos)
    static void Adota(UIElement *pai, UIElement *filho);
    bool _carregado = false;                // uso da janela: o Loaded ja foi disparado
    static void Solta(UIElement *filho);

private:
    bool _medindoTamanho = false;
    Size _ultimoTamanho;
    bool _tamanhoValido = false;
};
using FrameworkElement = UIElement;
using UIElementPtr = std::shared_ptr<UIElement>;

// Children de um painel: List<UIElement> que liga o Parent de quem entra
class UIElementCollection
{
public:
    explicit UIElementCollection(UIElement *dono) : _dono(dono) {}
    void Add(UIElementPtr e);
    void Insert(size_t i, UIElementPtr e);
    bool Remove(const UIElementPtr &e);
    bool Remove(const UIElement *e);
    void RemoveAt(size_t i);
    void Clear();
    size_t Count() const { return _itens.size(); }
    bool Contains(const UIElement *e) const;
    int IndexOf(const UIElement *e) const;
    const UIElementPtr &operator[](size_t i) const { return _itens[i]; }
    std::vector<UIElementPtr>::const_iterator begin() const { return _itens.begin(); }
    std::vector<UIElementPtr>::const_iterator end() const { return _itens.end(); }

private:
    UIElement *_dono;
    std::vector<UIElementPtr> _itens;
};

// =====================================================================================
// Paineis
// =====================================================================================
class Panel : public UIElement
{
public:
    Panel() : Children(this) {}
    ~Panel() override;
    UIElementCollection Children;
    Brush Background;
    void Filhos(std::vector<UIElement *> &s) const override;
    void OnRender(DrawingContext &dc) override;
    bool AcertaProprio(Point p) const override;
};

class StackPanel : public Panel
{
public:
    Vistas::Orientation Orientation = Vistas::Orientation::Vertical;
    const char *Tipo() const override { return "StackPanel"; }
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;
};

class DockPanel : public Panel
{
public:
    bool LastChildFill = true;
    static void SetDock(const UIElementPtr &e, Dock d) { e->DockPanel_Dock = d; }
    static Dock GetDock(const UIElementPtr &e) { return e->DockPanel_Dock; }
    const char *Tipo() const override { return "DockPanel"; }
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;
};

class WrapPanel : public Panel
{
public:
    Vistas::Orientation Orientation = Vistas::Orientation::Horizontal;
    double ItemWidth = NaN, ItemHeight = NaN;
    const char *Tipo() const override { return "WrapPanel"; }
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;
};

class Canvas : public Panel
{
public:
    static void SetLeft(const UIElementPtr &e, double v) { e->Canvas_Left = v; }
    static void SetTop(const UIElementPtr &e, double v) { e->Canvas_Top = v; }
    static void SetRight(const UIElementPtr &e, double v) { e->Canvas_Right = v; }
    static void SetBottom(const UIElementPtr &e, double v) { e->Canvas_Bottom = v; }
    static double GetLeft(const UIElementPtr &e) { return e->Canvas_Left; }
    static double GetTop(const UIElementPtr &e) { return e->Canvas_Top; }
    const char *Tipo() const override { return "Canvas"; }
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;
};

struct ColumnDefinition
{
    GridLength Width = GridLength::Star(1);
    double MinWidth = 0, MaxWidth = Infinito;
    double ActualWidth = 0;
    double Offset = 0;
    ColumnDefinition() = default;
    ColumnDefinition(GridLength w) : Width(w) {}
};
struct RowDefinition
{
    GridLength Height = GridLength::Star(1);
    double MinHeight = 0, MaxHeight = Infinito;
    double ActualHeight = 0;
    double Offset = 0;
    RowDefinition() = default;
    RowDefinition(GridLength h) : Height(h) {}
};

class Grid : public Panel
{
public:
    std::vector<ColumnDefinition> ColumnDefinitions;
    std::vector<RowDefinition> RowDefinitions;
    static void SetRow(const UIElementPtr &e, int v) { e->Grid_Row = v; }
    static void SetColumn(const UIElementPtr &e, int v) { e->Grid_Column = v; }
    static void SetRowSpan(const UIElementPtr &e, int v) { e->Grid_RowSpan = v; }
    static void SetColumnSpan(const UIElementPtr &e, int v) { e->Grid_ColumnSpan = v; }
    static int GetRow(const UIElementPtr &e) { return e->Grid_Row; }
    static int GetColumn(const UIElementPtr &e) { return e->Grid_Column; }
    const char *Tipo() const override { return "Grid"; }
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;

private:
    std::vector<double> _col, _lin;
    double _desejoEstrela = 0;
    void Resolve(std::vector<double> &tam, bool colunas, double disponivel, bool medindo);
};

// =====================================================================================
// Border, TextBlock, Image, formas
// =====================================================================================
class Border : public UIElement
{
public:
    ~Border() override;
    UIElementPtr Child() const { return _filho; }
    void SetChild(UIElementPtr c);
    Brush Background, BorderBrush;
    Thickness BorderThickness;
    Vistas::CornerRadius CornerRadius;
    Thickness Padding;
    const char *Tipo() const override { return "Border"; }
    void Filhos(std::vector<UIElement *> &s) const override;
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;
    void OnRender(DrawingContext &dc) override;
    bool AcertaProprio(Point p) const override;

private:
    UIElementPtr _filho;
};

class TextBlock : public UIElement
{
public:
    std::string Text;
    std::optional<Vistas::TextWrapping> TextWrapping;    // vazio = o do estilo, ou NoWrap
    Vistas::TextTrimming TextTrimming = Vistas::TextTrimming::None;
    Vistas::TextAlignment TextAlignment = Vistas::TextAlignment::Left;
    double LineHeight = NaN;
    Thickness Padding;
    Brush Background;
    TextBlock() = default;
    explicit TextBlock(std::string t) : Text(std::move(t)) {}
    const char *Tipo() const override { return "TextBlock"; }
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;
    void OnRender(DrawingContext &dc) override;
    bool AcertaProprio(Point p) const override;
    Vistas::TextWrapping QuebraEfetiva() const;

private:
    std::vector<LinhaDeTexto> _linhas;
    std::string _textoMedido;
    double _larguraMedida = -1;
    FonteTexto _fonteMedida;
    bool _quebraMedida = false;
    void Quebrar(double largura);
};

class Image : public UIElement
{
public:
    ImageSource Source;
    Vistas::Stretch Stretch = Vistas::Stretch::Uniform;
    const char *Tipo() const override { return "Image"; }
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;
    void OnRender(DrawingContext &dc) override;
    bool AcertaProprio(Point p) const override;
    // onde a imagem cai dentro do elemento, conforme o Stretch (em coordenadas da janela)
    Rect AreaDaImagem() const;
};

class Shape : public UIElement
{
public:
    Brush Fill, Stroke;
    double StrokeThickness = 1;
    Vistas::Stretch Stretch = Vistas::Stretch::Fill;
    bool AcertaProprio(Point p) const override;
};
class Rectangle : public Shape
{
public:
    double RadiusX = 0, RadiusY = 0;
    const char *Tipo() const override { return "Rectangle"; }
    void OnRender(DrawingContext &dc) override;
};
class Ellipse : public Shape
{
public:
    const char *Tipo() const override { return "Ellipse"; }
    void OnRender(DrawingContext &dc) override;
};
// System.Windows.Shapes.Line: de (X1, Y1) a (X2, Y2), relativo ao proprio elemento (num Canvas sem
// Left/Top, relativo ao Canvas)
class Line : public Shape
{
public:
    double X1 = 0, Y1 = 0, X2 = 0, Y2 = 0;
    const char *Tipo() const override { return "Line"; }
    Size MeasureOverride(Size d) override;
    void OnRender(DrawingContext &dc) override;
};
// Path com Data na mini-linguagem do XAML (M, L, H, V, C, Q, Z, maiusculas e minusculas)
class Path : public Shape
{
public:
    Path() { Stretch = Vistas::Stretch::None; }
    std::string Data;
    const char *Tipo() const override { return "Path"; }
    Size MeasureOverride(Size d) override;
    void OnRender(DrawingContext &dc) override;
};

// =====================================================================================
// Controles
// =====================================================================================
class Control : public UIElement
{
public:
    Control() { Focusable = true; }
    Brush Background, BorderBrush;                  // vazios = os do estilo
    std::optional<Thickness> BorderThickness, Padding;
    Vistas::HorizontalAlignment HorizontalContentAlignment = Vistas::HorizontalAlignment::Left;
    Vistas::VerticalAlignment VerticalContentAlignment = Vistas::VerticalAlignment::Top;
    bool IsTabStop = true;
    std::optional<Vistas::Gabarito> Template;       // <Control.Template> no lugar; vazio = o do estilo

    Brush BackgroundEfetivo() const;
    Brush BorderBrushEfetivo() const;
    Thickness BorderThicknessEfetiva() const;
    Thickness PaddingEfetivo() const;
    Vistas::Gabarito GabaritoEfetivo() const;
    bool AcertaProprio(Point p) const override;
    const char *Tipo() const override { return "Control"; }
};

// Content = "texto" ou Content = elemento. Texto vira um TextBlock (o ContentPresenter do WPF).
class Conteudo
{
public:
    explicit Conteudo(UIElement *dono) : _dono(dono) {}
    Conteudo &operator=(const std::string &texto);
    Conteudo &operator=(const char *texto) { return *this = std::string(texto ? texto : ""); }
    Conteudo &operator=(UIElementPtr e);
    template <class T, class = std::enable_if_t<std::is_base_of_v<UIElement, T>>>
    Conteudo &operator=(const std::shared_ptr<T> &e) { return *this = std::static_pointer_cast<UIElement>(e); }
    Conteudo &operator=(std::nullptr_t) { return *this = UIElementPtr(); }
    UIElementPtr Elemento() const { return _el; }
    std::string ToString() const;                 // o texto, se o conteudo e texto
    bool EhTexto() const { return _ehTexto; }
    explicit operator bool() const { return (bool) _el; }

private:
    UIElement *_dono;
    UIElementPtr _el;
    bool _ehTexto = false;
};

class ContentControl : public Control
{
public:
    ContentControl() : Content(this) {}
    ~ContentControl() override;
    Conteudo Content;
    const char *Tipo() const override { return "ContentControl"; }
    void Filhos(std::vector<UIElement *> &s) const override;
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;
    void OnRender(DrawingContext &dc) override;
    // o retangulo onde o conteudo cai (sem borda e recuo)
    virtual Thickness MolduraDoConteudo() const;
};

class UserControl : public ContentControl
{
public:
    UserControl()
    {
        Focusable = false;
        IsTabStop = false;
        // o UserControl do WPF estica o conteudo (o ContentControl comum o encosta em cima a esquerda)
        HorizontalContentAlignment = Vistas::HorizontalAlignment::Stretch;
        VerticalContentAlignment = Vistas::VerticalAlignment::Stretch;
    }
    const char *Tipo() const override { return "UserControl"; }
    bool AcertaProprio(Point p) const override;
};

// ---- botoes ----
enum class ClickMode { Release, Press, Hover };

class ButtonBase : public ContentControl
{
public:
    Evento<RoutedEventArgs &> Click;
    Vistas::ClickMode ClickMode = Vistas::ClickMode::Release;
    bool IsPressed() const { return _apertado && IsMouseOver(); }
    void PerformClick();                          // o Click do teclado e do codigo
    const char *Tipo() const override { return "ButtonBase"; }
    void OnMouseDown(MouseButtonEventArgs &e) override;
    void OnMouseUp(MouseButtonEventArgs &e) override;
    void OnKeyDown(KeyEventArgs &e) override;
    void OnKeyUp(KeyEventArgs &e) override;
    void OnLostMouseCapture() override;
    Vistas::Cursor CursorEm(Point p) const override;

protected:
    virtual void OnClick();
    bool _apertado = false;
    bool _apertadoTeclado = false;
};

class Button : public ButtonBase
{
public:
    Button();
    bool IsDefault = false, IsCancel = false;
    const char *Tipo() const override { return "Button"; }
    void OnRender(DrawingContext &dc) override;
    void OnRenderDepois(DrawingContext &dc) override;
    std::optional<Brush> ForegroundDoGabarito() const override;
    double OpacidadeDoGabarito() const override;
    Thickness MolduraDoConteudo() const override;
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;

protected:
    void OnClick() override;
};

class ToggleButton : public ButtonBase
{
public:
    ToggleButton();
    Prop<bool> IsChecked;
    Evento<RoutedEventArgs &> Checked, Unchecked;
    const char *Tipo() const override { return "ToggleButton"; }
    void OnRender(DrawingContext &dc) override;
    std::optional<Brush> ForegroundDoGabarito() const override;
    std::optional<Vistas::FontWeight> FontWeightDoGabarito() const override;
    double OpacidadeDoGabarito() const override;
    Thickness MolduraDoConteudo() const override;
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;

protected:
    void OnClick() override;
    virtual void AoMarcar(bool marcado);
};

class RadioButton : public ToggleButton
{
public:
    RadioButton() = default;
    std::string GroupName;
    const char *Tipo() const override { return "RadioButton"; }

protected:
    void OnClick() override;
    void AoMarcar(bool marcado) override;
};

class CheckBox : public ToggleButton
{
public:
    CheckBox() = default;
    const char *Tipo() const override { return "CheckBox"; }
};

// ---- caixa de texto ----
class TextBox : public Control
{
public:
    TextBox();
    Prop<std::string> Text;
    Evento<TextChangedEventArgs &> TextChanged;
    bool IsReadOnly = false;
    int MaxLength = 0;                          // 0 = sem limite (em caracteres)
    Vistas::TextAlignment TextAlignment = Vistas::TextAlignment::Left;
    Brush CaretBrush, SelectionBrush;           // vazios = os do estilo
    double SelectionOpacity = 0.4;

    // em caracteres (como o C#, que conta em char)
    int CaretIndex() const;
    void SetCaretIndex(int i);
    int SelectionStart() const;
    int SelectionLength() const;
    std::string SelectedText() const;
    void Select(int inicio, int n);
    void SelectAll();
    void Clear() { Text = std::string(); }
    void Copy() const;
    void Cut();
    void Paste();
    bool Undo();

    const char *Tipo() const override { return "TextBox"; }
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;
    void OnRender(DrawingContext &dc) override;
    void OnMouseDown(MouseButtonEventArgs &e) override;
    void OnMouseMove(MouseEventArgs &e) override;
    void OnMouseUp(MouseButtonEventArgs &e) override;
    void OnKeyDown(KeyEventArgs &e) override;
    void OnTextInput(TextCompositionEventArgs &e) override;
    void OnGotFocus() override;
    void OnLostFocus() override;
    Vistas::Cursor CursorEm(Point p) const override;
    // o ponto (na janela) do cursor de texto, para o XIM pôr a janelinha de composição
    Point PontoDoCursor() const;

private:
    int _cursor = 0, _ancora = 0;               // em caracteres
    double _rolagem = 0;
    bool _arrastando = false;
    std::vector<std::string> _desfazer;
    void Insere(const std::string &s);
    void ApagaSelecao();
    int IndiceEm(double x) const;
    void Mostra();
    std::vector<double> _xs;
    std::vector<size_t> _bytes;
    void Posicoes();
    double _ultimoClique = 0;
};

// ---- rolagem ----
// A barra de rolagem do Tema (fina, sem setas). Nao e um elemento: quem rola (ScrollViewer, grade,
// listas) desenha e encaminha o mouse para ela.
struct BarraDeRolagem
{
    Vistas::Orientation Orientacao = Vistas::Orientation::Vertical;
    Rect Area;                       // na janela
    double Maximo = 0;               // ScrollableHeight
    double Visivel = 0;              // ViewportSize
    double Valor = 0;
    bool Sobre = false, Arrastando = false;
    double _inicioArrasto = 0, _valorArrasto = 0;
    int _repetindo = 0;              // -1 pagina para tras, +1 para frente

    Rect Polegar() const;
    void Desenha(DrawingContext &dc) const;
    // devolve true se o clique e dela; o valor novo fica em Valor
    bool Aperta(Point p);
    void Move(Point p);
    void Solta();
};

class ScrollViewer : public UIElement
{
public:
    ScrollViewer();
    ~ScrollViewer() override;
    Conteudo Content;
    Vistas::ScrollBarVisibility VerticalScrollBarVisibility = Vistas::ScrollBarVisibility::Visible;
    Vistas::ScrollBarVisibility HorizontalScrollBarVisibility = Vistas::ScrollBarVisibility::Disabled;
    Brush Background;
    Evento<ScrollChangedEventArgs &> ScrollChanged;

    double VerticalOffset() const { return _v; }
    double HorizontalOffset() const { return _h; }
    double ExtentHeight() const { return _extensao.Height; }
    double ExtentWidth() const { return _extensao.Width; }
    double ViewportHeight() const { return _janela.Height; }
    double ViewportWidth() const { return _janela.Width; }
    double ScrollableHeight() const { return std::max(0.0, _extensao.Height - _janela.Height); }
    double ScrollableWidth() const { return std::max(0.0, _extensao.Width - _janela.Width); }
    void ScrollToVerticalOffset(double v);
    void ScrollToHorizontalOffset(double h);
    void ScrollToTop() { ScrollToVerticalOffset(0); }
    void ScrollToBottom() { ScrollToVerticalOffset(Infinito); }
    void ScrollToEnd() { ScrollToVerticalOffset(Infinito); ScrollToHorizontalOffset(0); }
    void LineUp() { ScrollToVerticalOffset(_v - 16); }
    void LineDown() { ScrollToVerticalOffset(_v + 16); }
    void PageUp() { ScrollToVerticalOffset(_v - _janela.Height); }
    void PageDown() { ScrollToVerticalOffset(_v + _janela.Height); }
    // deixa o retangulo (na janela) a vista
    void MostraRetangulo(const Rect &r);

    const char *Tipo() const override { return "ScrollViewer"; }
    void Filhos(std::vector<UIElement *> &s) const override;
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;
    void OnRender(DrawingContext &dc) override;
    void OnRenderDepois(DrawingContext &dc) override;
    bool AcertaProprio(Point p) const override;
    void OnMouseDown(MouseButtonEventArgs &e) override;
    void OnMouseMove(MouseEventArgs &e) override;
    void OnMouseUp(MouseButtonEventArgs &e) override;
    void OnMouseWheel(MouseWheelEventArgs &e) override;
    void OnKeyDown(KeyEventArgs &e) override;
    void OnMouseLeave(MouseEventArgs &e) override;

private:
    double _v = 0, _h = 0, _vAvisado = 0, _hAvisado = 0, _extAvisada = -1, _janAvisada = -1;
    // pedido de rolagem alem do fim atual, aplicado no proximo arranjo (ver ScrollToVerticalOffset)
    std::optional<double> _vAdiado;
    Size _extensao, _janela;
    bool _mostraV = false, _mostraH = false;
    BarraDeRolagem _barraV, _barraH{Vistas::Orientation::Horizontal};
    void Limita();
    void Avisa();
};

// ---- deslizante e progresso ----
class RangeBase : public Control
{
public:
    double Minimum = 0, Maximum = 1;
    double SmallChange = 0.1, LargeChange = 1;
};

class Slider : public RangeBase
{
public:
    Slider();
    Prop<double> Value;
    Evento<RoutedPropertyChangedEventArgs<double> &> ValueChanged;
    bool IsMoveToPointEnabled = false;
    Vistas::Orientation Orientation = Vistas::Orientation::Horizontal;
    bool IsDragging() const { return _arrastando; }
    const char *Tipo() const override { return "Slider"; }
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;
    void OnRender(DrawingContext &dc) override;
    void OnMouseDown(MouseButtonEventArgs &e) override;
    void OnMouseMove(MouseEventArgs &e) override;
    void OnMouseUp(MouseButtonEventArgs &e) override;
    void OnKeyDown(KeyEventArgs &e) override;
    void OnLostMouseCapture() override { _arrastando = false; }

private:
    bool _arrastando = false;
    double _desvio = 0;
    double ValorEm(double x) const;
    Rect Polegar() const;
};

class ProgressBar : public RangeBase
{
public:
    ProgressBar() { Focusable = false; Maximum = 100; }
    double Value = 0;
    bool IsIndeterminate = false;
    const char *Tipo() const override { return "ProgressBar"; }
    Size MeasureOverride(Size d) override;
    void OnRender(DrawingContext &dc) override;
};

// =====================================================================================
// Listas: fonte de itens, ListBox, ComboBox, DataGrid
// =====================================================================================

// ItemsSource: um vetor de qualquer tipo, sem copiar. FonteDeItens::De(vetor) aceita
// std::shared_ptr<std::vector<T>> (a lista fica viva enquanto o controle a usar) ou um ponteiro
// para um vetor que vive mais que o controle (um campo da vista).
class FonteDeItens
{
public:
    FonteDeItens() = default;
    template <class T>
    static FonteDeItens De(std::shared_ptr<std::vector<T>> v)
    {
        FonteDeItens f;
        f._dono = v;
        std::vector<T> *p = v.get();
        f._n = [p] { return p->size(); };
        f._item = [p](size_t i) -> void * { return &(*p)[i]; };
        f._tipo = std::type_index(typeid(T));
        return f;
    }
    template <class T>
    static FonteDeItens De(std::vector<T> *p)
    {
        FonteDeItens f;
        f._n = [p] { return p->size(); };
        f._item = [p](size_t i) -> void * { return &(*p)[i]; };
        f._tipo = std::type_index(typeid(T));
        return f;
    }
    bool Vazia() const { return !_n; }
    bool operator==(const FonteDeItens &) const { return false; }   // atribuir sempre rele
    size_t Count() const { return _n ? _n() : 0; }
    void *Item(size_t i) const { return _item(i); }
    template <class T> T *Como(size_t i) const
    {
        return _tipo == std::type_index(typeid(T)) ? static_cast<T *>(_item(i)) : nullptr;
    }
    std::type_index TipoDosItens() const { return _tipo; }

private:
    std::shared_ptr<void> _dono;
    std::function<size_t()> _n;
    std::function<void *(size_t)> _item;
    std::type_index _tipo = std::type_index(typeid(void));
};

// Selector: a selecao por indice na vista (a ordem e o filtro atuais)
class Selector : public Control
{
public:
    Selector();
    Prop<int> SelectedIndex;                     // -1 = nada
    Evento<SelectionChangedEventArgs &> SelectionChanged;
    const char *Tipo() const override { return "Selector"; }

protected:
    virtual void AoMudarSelecao(int antigo, int novo);
};

class ListBoxItem : public ContentControl
{
public:
    ListBoxItem() { Focusable = false; HorizontalContentAlignment = Vistas::HorizontalAlignment::Stretch; }
    bool IsSelected = false;
    int Indice = -1;                             // na vista da lista
    const char *Tipo() const override { return "ListBoxItem"; }
    void OnRender(DrawingContext &dc) override;
    std::optional<Brush> ForegroundDoGabarito() const override;
    Thickness MolduraDoConteudo() const override;
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;
    Vistas::Gabarito GabaritoDoItem() const;
};

class ListBox : public Selector
{
public:
    ListBox();
    ~ListBox() override;
    // ou ItemsSource (com ItemTemplate/TextoDoItem) ou Items (elementos prontos ou textos).
    // Atribuir ItemsSource rele a lista (como no WPF); mudar o vetor por baixo pede Refresh().
    Prop<FonteDeItens> ItemsSource{FonteDeItens(), [this](const FonteDeItens &, const FonteDeItens &) { Refresh(); }};
    std::vector<UIElementPtr> Items;
    void AddItem(UIElementPtr e) { Items.push_back(std::move(e)); Refresh(); }
    void AddItem(const std::string &texto);
    // DataTemplate: o conteudo de cada item; vazio = um TextBlock com TextoDoItem
    std::function<UIElementPtr(void *item, int indiceFonte)> ItemTemplate;
    std::function<std::string(void *item)> TextoDoItem;
    // gatilhos por dado (o DataTrigger do ItemAlbum: <todos> em italico)
    std::function<void(ListBoxItem &container, void *item)> PreparaContainer;
    // ListCollectionView.Filter: true = fica
    std::function<bool(void *item)> Filter;
    const Vistas::Style *ItemContainerStyle = nullptr;
    Vistas::Orientation ItemsPanelOrientation = Vistas::Orientation::Vertical;   // ItemsPanelTemplate
    Vistas::SelectionMode SelectionMode = Vistas::SelectionMode::Single;
    Vistas::ScrollBarVisibility VerticalScrollBarVisibility = Vistas::ScrollBarVisibility::Auto;
    Vistas::ScrollBarVisibility HorizontalScrollBarVisibility = Vistas::ScrollBarVisibility::Auto;
    bool IsVirtualizing = true;

    template <class T> void DefineItemTemplate(std::function<UIElementPtr(T &item, int i)> f)
    {
        ItemTemplate = [f](void *p, int i) { return f(*static_cast<T *>(p), i); };
    }
    template <class T> void DefineTexto(std::function<std::string(T &item)> f)
    {
        TextoDoItem = [f](void *p) { return f(*static_cast<T *>(p)); };
    }
    template <class T> void DefineFiltro(std::function<bool(T &item)> f)
    {
        Filter = f ? std::function<bool(void *)>([f](void *p) { return f(*static_cast<T *>(p)); }) : nullptr;
    }

    // Items.Refresh(): rele a fonte, refaz o filtro e os containers. A selecao fica no mesmo item,
    // se ele continuar a vista; senao cai para -1 (com SelectionChanged).
    void Refresh();
    int Count() const { return (int) _vista.size(); }              // Items.Count
    void *ItemDaVista(int i) const;                                  // Items[i]
    template <class T> T *Item(int i) const { return static_cast<T *>(ItemDaVista(i)); }
    template <class T> T *SelectedItem() const { return SelectedIndex() >= 0 ? Item<T>(SelectedIndex()) : nullptr; }
    int IndiceNaFonte(int iVista) const { return iVista >= 0 && iVista < (int) _vista.size() ? _vista[iVista] : -1; }
    int IndiceNaVista(int iFonte) const;
    std::vector<int> SelectedIndices() const;
    void ScrollIntoView(int i);
    ListBoxItem *ContainerFromIndex(int i) const;
    // ItemsControl.ContainerFromElement(lista, elemento): o item da lista que contem o elemento
    static ListBoxItem *ContainerFromElement(const ListBox *lista, const UIElement *e);

    const char *Tipo() const override { return "ListBox"; }
    void Filhos(std::vector<UIElement *> &s) const override;
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;
    void OnRender(DrawingContext &dc) override;
    void OnRenderDepois(DrawingContext &dc) override;
    void OnMouseDown(MouseButtonEventArgs &e) override;
    void OnMouseMove(MouseEventArgs &e) override;
    void OnMouseUp(MouseButtonEventArgs &e) override;
    void OnMouseWheel(MouseWheelEventArgs &e) override;
    void OnKeyDown(KeyEventArgs &e) override;
    void OnMouseLeave(MouseEventArgs &e) override;

protected:
    void AoMudarSelecao(int antigo, int novo) override;

private:
    std::vector<int> _vista;                    // indices na fonte (ou em Items)
    std::map<int, std::shared_ptr<ListBoxItem>> _containers;
    std::vector<ListBoxItem *> _realizados;
    std::vector<int> _multi;                    // selecao multipla (indices na vista)
    double _rolagem = 0, _alturaItem = 0, _alturaVisivel = 0, _extensao = 0;
    bool _temBarra = false;
    int _mostrarDepois = -1;
    BarraDeRolagem _barra;
    bool _vistaFeita = false;
    void FazVista();
    void *ItemFonte(int iFonte) const;
    std::shared_ptr<ListBoxItem> Container(int iVista);
    bool Virtual() const;
    int IndiceEm(Point p) const;
    void Escolhe(int i, ModifierKeys m);
};

class ComboBox : public Selector
{
public:
    ComboBox();
    ~ComboBox() override;
    // Items.Add("texto"): a lista suspensa do ARCA so tem texto
    class Lista
    {
    public:
        explicit Lista(ComboBox *dono) : _dono(dono) {}
        void Add(const std::string &s);
        void Insert(size_t i, const std::string &s);
        void RemoveAt(size_t i);
        void Clear();
        size_t Count() const { return _v.size(); }
        const std::string &operator[](size_t i) const { return _v[i]; }
        std::vector<std::string>::const_iterator begin() const { return _v.begin(); }
        std::vector<std::string>::const_iterator end() const { return _v.end(); }
        int IndexOf(const std::string &s) const;

    private:
        ComboBox *_dono;
        std::vector<std::string> _v;
    };
    Lista Items;
    std::vector<std::any> Tags;                  // dado de cada item, se quiser (Tag do ComboBoxItem)
    double MaxDropDownHeight = Tema::ComboAlturaMaxima;
    Evento<RoutedEventArgs &> DropDownOpened, DropDownClosed;
    bool IsDropDownOpen() const { return _aberta; }
    void SetIsDropDownOpen(bool abrir);
    std::string SelectedItem() const;            // "" se nada (o null do C#)
    bool TemSelecao() const { return SelectedIndex() >= 0 && SelectedIndex() < (int) Items.Count(); }
    std::string Text() const { return SelectedItem(); }

    const char *Tipo() const override { return "ComboBox"; }
    Size MeasureOverride(Size d) override;
    void OnRender(DrawingContext &dc) override;
    void OnMouseDown(MouseButtonEventArgs &e) override;
    void OnKeyDown(KeyEventArgs &e) override;
    void OnMouseWheel(MouseWheelEventArgs &e) override;
    void OnLostFocus() override;

private:
    bool _aberta = false;
    std::shared_ptr<UIElement> _popup;
    friend class ListaSuspensa;
};

// ---- DataGrid ----
enum class DataGridGridLinesVisibility { All, Horizontal, None, Vertical };
enum class DataGridHeadersVisibility { All, Column, None, Row };
enum class DataGridSelectionUnit { Cell, FullRow, CellOrRowHeader };
enum class ListSortDirection { Ascending, Descending };

class DataGridTextColumn
{
public:
    std::string Header;
    DataGridLength Width = DataGridLength::Star(1);
    double MinWidth = 20, MaxWidth = Infinito;
    double ActualWidth = 0;
    bool IsReadOnly = false;
    bool CanUserSort = true, CanUserResize = true;
    Vistas::Visibility Visibility = Vistas::Visibility::Visible;
    std::optional<ListSortDirection> SortDirection;
    const Vistas::Style *ElementStyle = nullptr;       // Foreground, FontWeight... da celula
    const Vistas::Style *EditingElementStyle = nullptr;
    Vistas::TextAlignment AlinhamentoTexto = Vistas::TextAlignment::Left;
    std::string HeaderToolTip;
    // Binding: ler e (se editavel) gravar o campo do item
    std::function<std::string(void *item)> Binding;
    std::function<void(void *item, const std::string &valor)> BindingGrava;
    // SortMemberPath: a comparacao da ordenacao (<0, 0, >0); vazio = compara o texto (numeros
    // como numeros, o resto como a CompareInfo pt-BR: sem caixa e sem acento primeiro)
    std::function<int(void *a, void *b)> Compara;
    // cor da celula por dado (o que o ARCA fazia com ElementStyle + DataTrigger)
    std::function<Brush(void *item)> CorDoTexto;

    template <class T>
    static std::shared_ptr<DataGridTextColumn> Nova(std::string cabecalho, DataGridLength largura,
                                                    std::function<std::string(T &)> ler,
                                                    std::function<void(T &, const std::string &)> gravar = nullptr)
    {
        auto c = std::make_shared<DataGridTextColumn>();
        c->Header = std::move(cabecalho);
        c->Width = largura;
        c->Binding = [ler](void *p) { return ler(*static_cast<T *>(p)); };
        if (gravar) c->BindingGrava = [gravar](void *p, const std::string &v) { gravar(*static_cast<T *>(p), v); };
        else c->IsReadOnly = true;
        return c;
    }
    template <class T, class K>
    void OrdenaPor(std::function<K(T &)> chave)
    {
        Compara = [chave](void *a, void *b) {
            K x = chave(*static_cast<T *>(a)), y = chave(*static_cast<T *>(b));
            return x < y ? -1 : (y < x ? 1 : 0);
        };
    }
};
using DataGridColumn = DataGridTextColumn;

class DataGrid : public Selector
{
public:
    DataGrid();
    ~DataGrid() override;
    std::vector<std::shared_ptr<DataGridTextColumn>> Columns;
    // atribuir rele a grade (ItemsSource = null: FonteDeItens()); mudar o vetor por baixo pede Refresh()
    Prop<FonteDeItens> ItemsSource{FonteDeItens(), [this](const FonteDeItens &, const FonteDeItens &) { Refresh(); }};
    std::function<bool(void *item)> Filter;
    double RowHeight = 24;
    double ColumnHeaderHeight = NaN;
    Brush RowBackground, AlternatingRowBackground;
    Brush HorizontalGridLinesBrush, VerticalGridLinesBrush;
    DataGridGridLinesVisibility GridLinesVisibility = DataGridGridLinesVisibility::All;
    DataGridHeadersVisibility HeadersVisibility = DataGridHeadersVisibility::Column;
    Vistas::SelectionMode SelectionMode = Vistas::SelectionMode::Extended;
    DataGridSelectionUnit SelectionUnit = DataGridSelectionUnit::FullRow;
    bool IsReadOnly = false;
    bool CanUserSortColumns = true, CanUserResizeColumns = true;
    bool AutoGenerateColumns = false, CanUserAddRows = false, CanUserDeleteRows = false, CanUserReorderColumns = false;
    Evento<RoutedEventArgs &> Sorted;                 // depois de ordenar por um cabecalho

    template <class T> void DefineFiltro(std::function<bool(T &item)> f)
    {
        Filter = f ? std::function<bool(void *)>([f](void *p) { return f(*static_cast<T *>(p)); }) : nullptr;
    }

    // Items.Refresh(): rele a fonte e reaplica filtro e ordenacao, mantendo a selecao nos mesmos itens
    void Refresh();
    int Count() const { return (int) _vista.size(); }           // Items.Count
    void *ItemDaVista(int i) const;
    template <class T> T *Item(int i) const { return static_cast<T *>(ItemDaVista(i)); }
    template <class T> T *SelectedItem() const { return SelectedIndex() >= 0 ? Item<T>(SelectedIndex()) : nullptr; }
    std::vector<int> SelectedIndices() const;                    // na vista, em ordem
    template <class T> std::vector<T *> SelectedItems() const
    {
        std::vector<T *> r;
        for (int i : SelectedIndices()) r.push_back(Item<T>(i));
        return r;
    }
    int IndiceNaFonte(int iVista) const { return iVista >= 0 && iVista < (int) _vista.size() ? _vista[iVista] : -1; }
    int IndiceNaVista(int iFonte) const;
    void SelectAll();
    void UnselectAll();
    void SelecionaIndices(const std::vector<int> &indicesVista);
    void ScrollIntoView(int i);
    // a celula atual e a edicao (SelectionUnit = Cell)
    int CurrentRow() const { return _linhaAtual; }
    int CurrentColumn() const { return _colAtual; }
    void SetCurrentCell(int linha, int coluna);
    bool BeginEdit();
    bool CommitEdit();
    void CancelEdit();
    bool IsEditing() const { return (bool) _editor; }
    // ordenar por codigo (como clicar no cabecalho)
    void OrdenaPor(int coluna, ListSortDirection direcao);
    // a linha sob o ponto (na janela); -1 se nenhuma (cabecalho, barra, vazio)
    int LinhaEm(Point p) const;

    const char *Tipo() const override { return "DataGrid"; }
    void Filhos(std::vector<UIElement *> &s) const override;
    Size MeasureOverride(Size d) override;
    Size ArrangeOverride(Size f) override;
    void OnRender(DrawingContext &dc) override;
    void OnMouseDown(MouseButtonEventArgs &e) override;
    void OnMouseMove(MouseEventArgs &e) override;
    void OnMouseUp(MouseButtonEventArgs &e) override;
    void OnMouseWheel(MouseWheelEventArgs &e) override;
    void OnMouseLeave(MouseEventArgs &e) override;
    void OnKeyDown(KeyEventArgs &e) override;
    void OnTextInput(TextCompositionEventArgs &e) override;
    Vistas::Cursor CursorEm(Point p) const override;
    std::string DicaEm(Point p) const override;

protected:
    void AoMudarSelecao(int antigo, int novo) override;

private:
    std::vector<int> _vista;
    std::vector<char> _sel;                        // por indice na vista
    int _ancora = -1, _linhaAtual = -1, _colAtual = 0;
    double _rolagemV = 0, _rolagemH = 0;
    double _alturaCab = 0;
    Rect _corpo;                                   // area das linhas (na janela)
    double _larguraTotal = 0;
    bool _temV = false, _temH = false;
    BarraDeRolagem _barraV, _barraH{Vistas::Orientation::Horizontal};
    int _colSobre = -1;                            // cabecalho sob o mouse
    int _redimensionando = -1;
    double _xRedim = 0, _larguraRedim = 0;
    bool _arrastandoSelecao = false;
    std::shared_ptr<TextBox> _editor;
    int _linhaEditada = -1, _colEditada = -1;
    bool _vistaFeita = false;
    int _colOrdenada = -1;
    int _mostrarDepois = -1;
    void FazVista();
    void Ordena();
    void CalculaColunas(double largura);
    int ColunaEm(double x) const;
    int GarraEm(Point p) const;
    double XDaColuna(int c) const;
    void Clica(int linha, ModifierKeys m, bool arrasto);
    void AvisaSelecao(const std::vector<char> &antes);
    void PosicionaEditor();
    void MoveCelula(int dl, int dc, bool editar);
};

// =====================================================================================
// Funcoes de apoio
// =====================================================================================

// o desenho de um gabarito "Border com gatilhos" (fundo e borda conforme o estado)
void DesenhaGabarito(DrawingContext &dc, const Rect &r, const GabaritoBorda &g, bool sobre, bool apertado,
                     bool marcado, bool ligado, const Brush &fundoLocal = Brush(), const Brush &bordaLocal = Brush());

// UTF-8 <-> caracteres (o C# conta em char)
size_t ByteDoCaractere(const std::string &s, int caractere);
int CaracteresEm(const std::string &s);

// a hora monotonica em segundos (relogios, clique duplo)
double Agora();

} // namespace ARCA::Vistas

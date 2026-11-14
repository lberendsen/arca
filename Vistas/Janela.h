// ARCA no xOS - Vistas/Janela: Window, Application, DispatcherTimer, Clipboard, Mouse e Keyboard.
//
// Cada Window e uma janela do X propria, com uma superficie EGL; todas desenham com o mesmo
// contexto GLES2 e o mesmo NanoVG (as imagens e as fontes carregadas servem a todas). As janelas
// auxiliares do ARCA (as Janela*.cs) sao "transient" da dona (Owner), e ShowDialog roda um laco
// aninhado ate a janela fechar, como no WPF: o codigo depois do ShowDialog so continua quando a
// pessoa responde.
//
// O laco (Application::Run e o laco do ShowDialog) espera com poll() no X e no
// Base::DescritorDespacho(): eventos do X, pedidos de outros fios (Base::Despacha) e relogios
// (DispatcherTimer) acordam o laco; parado, ele dorme. Depois de qualquer um deles a janela refaz o
// layout e redesenha.
#pragma once

#include "Controles.h"

#include <optional>
#include <string>

namespace ARCA::Vistas {

enum class WindowStartupLocation { Manual, CenterScreen, CenterOwner };
enum class ResizeMode { NoResize, CanMinimize, CanResize, CanResizeWithGrip };
enum class WindowStyle { None, SingleBorderWindow, ThreeDBorderWindow, ToolWindow };
enum class WindowState { Normal, Minimized, Maximized };
enum class SizeToContent { Manual, Width, Height, WidthAndHeight };

// System.TimeSpan, so o que o DispatcherTimer precisa
struct TimeSpan
{
    double TotalMilliseconds = 0;
    static TimeSpan FromMilliseconds(double ms) { return TimeSpan{ms}; }
    static TimeSpan FromSeconds(double s) { return TimeSpan{s * 1000}; }
    double TotalSeconds() const { return TotalMilliseconds / 1000; }
};

// System.Windows.Threading.DispatcherTimer: o Tick roda no fio da tela
class DispatcherTimer
{
public:
    DispatcherTimer();
    ~DispatcherTimer();
    DispatcherTimer(const DispatcherTimer &) = delete;
    DispatcherTimer &operator=(const DispatcherTimer &) = delete;
    TimeSpan Interval = TimeSpan::FromMilliseconds(100);
    Evento<> Tick;
    void Start();                 // reinicia a contagem, como o WPF
    void Stop();
    bool IsEnabled() const { return _ligado; }
    double Proximo() const { return _proximo; }
    void Dispara();               // uso do laco

private:
    bool _ligado = false;
    double _proximo = 0;
};

class Window : public ContentControl
{
public:
    Window();
    ~Window() override;

    std::string Title;
    double Left = NaN, Top = NaN;
    Vistas::WindowStartupLocation WindowStartupLocation = Vistas::WindowStartupLocation::Manual;
    Vistas::ResizeMode ResizeMode = Vistas::ResizeMode::CanResize;
    Vistas::WindowStyle WindowStyle = Vistas::WindowStyle::SingleBorderWindow;
    Vistas::WindowState WindowState = Vistas::WindowState::Normal;
    Vistas::SizeToContent SizeToContent = Vistas::SizeToContent::Manual;
    bool ShowInTaskbar = true;
    bool Topmost = false;
    Window *Owner = nullptr;
    std::string ClasseX = "arca";          // WM_CLASS (nome e classe)

    Evento<CancelEventArgs &> Closing;
    Evento<RoutedEventArgs &> Closed, Activated, Deactivated, ContentRendered;

    void Show();
    std::optional<bool> ShowDialog();      // bool? do WPF: true, false ou nulo (fechou sem resposta)
    void Close();
    void Hide();
    bool Activate();                       // traz para a frente
    std::optional<bool> DialogResult() const { return _resultado; }
    // DialogResult = x: numa janela modal, fecha (como no WPF)
    void SetDialogResult(std::optional<bool> r);
    bool IsLoaded() const { return _mostrada; }
    bool IsActive() const { return _ativa; }
    bool EstaAberta() const { return _aberta; }
    bool EhModal() const { return _modal; }
    Size TamanhoCliente() const { return Size(_largura, _altura); }

    const char *Tipo() const override { return "Window"; }
    void OnRender(DrawingContext &dc) override;
    bool AcertaProprio(Point p) const override { (void) p; return true; }

    // ---- uso dos controles (nao das vistas) ----
    UIElement *Foco() const;
    void DefineFoco(UIElement *e);
    UIElement *Captura() const;
    void DefineCaptura(UIElement *e);
    bool EstaSobre(const UIElement *e) const;          // na cadeia do mouse
    bool EstaDiretamenteSobre(const UIElement *e) const;
    Point PosicaoDoMouse() const { return _mouse; }
    // Popup: um elemento por cima da janela (lista suspensa). dono: o elemento que abriu (o
    // Parent logico do conteudo, para herdar fonte e cor). Clique fora fecha e chama aoFechar.
    void AbrePopup(UIElementPtr conteudo, Rect posicao, UIElement *dono, std::function<void()> aoFechar);
    void FechaPopup(const UIElement *conteudo);
    void FechaPopups();
    bool TemPopup() const { return !_popups.empty(); }
    void Redesenha();
    // o elemento foi destruido ou saiu da arvore: esquece referencias a ele
    void Esquece(const UIElement *e);
    // rodar depois do proximo layout (DispatcherPriority.Loaded)
    void DepoisDoLayout(std::function<void()> f);
    // o laco chama
    void TrataEvento(void *xevento);
    void Desenha();
    bool Suja() const { return _suja; }
    unsigned long Xid() const { return _xid; }
    // pisca do cursor de texto (TextBox): ligado agora?
    bool CursorAceso() const;
    void ReiniciaPisca();
    void AtualizaCursor();
    void AtualizaDica();
    UIElement *DiretamenteSobre() const { return _sobre.empty() ? nullptr : _sobre.front(); }

private:
    friend class Application;
    unsigned long _xid = 0;
    void *_superficie = nullptr;           // EGLSurface
    void *_xic = nullptr;                  // XIC
    int _largura = 0, _altura = 0;
    bool _aberta = false, _mostrada = false, _modal = false, _ativa = false, _suja = true;
    bool _fechando = false;
    bool _mapeada = false;                 // so desenha depois do MapNotify
    std::optional<bool> _resultado;
    UIElement *_foco = nullptr, *_captura = nullptr;
    std::vector<UIElement *> _sobre;       // do mais fundo a raiz
    Point _mouse{-1, -1};
    double _ultimoCliqueT = 0;
    Point _ultimoCliqueP;
    int _cliques = 0;
    unsigned _ultimoBotao = 0;
    UIElement *_dicaAlvo = nullptr;
    std::string _dicaTexto;
    double _dicaDesde = 0;
    bool _dicaVisivel = false;
    Point _dicaPonto;
    Vistas::Cursor _cursorAtual = Vistas::Cursor::Herda;
    struct Popup
    {
        UIElementPtr Conteudo;
        Rect Posicao;
        UIElement *Dono;
        std::function<void()> AoFechar;
    };
    std::vector<Popup> _popups;
    std::vector<std::function<void()>> _depoisDoLayout;
    double _piscaDesde = 0;

    void Cria();
    void Destroi();
    void Layout();
    void AtualizaSobre(Point p);
    UIElement *AlvoEm(Point p, bool *emPopup = nullptr);
    void Tecla(void *xevento, bool apertou);
    void Botao(void *xevento, bool apertou);
    void Movimento(Point p, unsigned estado);
    void NavegaFoco(bool paraTras);
    bool TeclaPadrao(KeyEventArgs &k);
    void DisparaCarregados(UIElement *e, bool &algum);
    bool NaArvore(const UIElement *e) const;
};
using WindowPtr = std::shared_ptr<Window>;

// Application: o X, o EGL, o NanoVG, as fontes e o laco
class Application
{
public:
    // abre o X e a placa; falha: false com o motivo em erro
    static bool Inicia(std::string &erro);
    // uma instancia por vez: se ja ha um ARCA aberto neste X, traz ele para a frente e devolve true
    static bool JaAberto(const std::string &nome = "ARCA");
    // marca esta como a instancia aberta (depois de mostrar a janela principal)
    static void MarcaInstancia(Window &principal, const std::string &nome = "ARCA");
    // roda ate a janela principal fechar (ou Shutdown); devolve o codigo de saida
    static int Run(Window &principal);
    static void Shutdown(int codigo = 0);
    static Window *MainWindow();
    static NVGcontext *Vg();
    static void *DisplayX();                      // Display* do X
    // uma volta do laco: trata o que houver e, com espera=true, dorme ate haver algo
    static void Bombeia(bool espera);
    // redesenha tudo na proxima volta
    static void RedesenhaTudo();
    // a lista das janelas abertas (para o modal e o Esquece)
    static const std::vector<Window *> &Janelas();
    static Window *JanelaModalAtual();
    // texturas a apagar no fio da tela (BitmapSource destruida em qualquer fio)
    static void ApagaTexturaDepois(int textura);
    // o que o X tem: largura e altura da tela
    static Size TamanhoDaTela();
};

// System.Windows.Clipboard (a CLIPBOARD do X, texto UTF-8)
namespace Clipboard {
bool ContainsText();
std::string GetText();
void SetText(const std::string &texto);
} // namespace Clipboard

// Mouse.OverrideCursor (Cursors.Wait durante trabalho no fio da tela) e a posicao na tela
namespace Mouse {
void SetOverrideCursor(Cursor c);          // Mouse.OverrideCursor = Cursors.Wait; Cursor::Herda = null
Cursor OverrideCursor();
Point PosicaoNaTela();                     // System.Windows.Forms.Control.MousePosition
UIElement *DirectlyOver();
UIElement *Captured();
} // namespace Mouse

namespace Keyboard {
ModifierKeys Modifiers();
UIElement *FocusedElement();
bool Focus(UIElement *e);
} // namespace Keyboard

} // namespace ARCA::Vistas

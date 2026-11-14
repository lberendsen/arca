// ARCA no xOS - Vistas/Janela: as janelas do X, o EGL/GLES2, o NanoVG, as fontes e o laco de
// eventos (o Dispatcher do WPF). Nao vem de um .cs do ARCA: e o que o Windows e o WPF davam.
// LINUX: o WPF desenhava pelo Direct3D numa HWND; aqui cada Window e uma janela do X com uma
// superficie EGL, e todas desenham com o mesmo contexto GLES2 (NanoVG) na GT210.
#include "Janela.h"
#include "IconeArca.h"

#include "../Base/Despacho.h"

#ifndef NANOVG_GLES2
#define NANOVG_GLES2
#endif
#include <GLES2/gl2.h>
#include "nanovg/nanovg.h"
#include "nanovg/nanovg_gl.h"

#include <algorithm>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XF86keysym.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
// o X define None, Bool, Status... como macros; os enums do WPF tambem tem None
#undef None
#undef Bool
#undef Status
#undef True
#undef False
#undef Always
static constexpr long XNenhum = 0L;

namespace ARCA::Vistas {

namespace {

struct Estado
{
    ::Display *d = nullptr;
    int tela = 0;
    EGLDisplay ed = EGL_NO_DISPLAY;
    EGLConfig cfg = nullptr;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLSurface pb = EGL_NO_SURFACE, atual = EGL_NO_SURFACE;
    Visual *visual = nullptr;
    int prof = 0;
    Colormap cmap = 0;
    NVGcontext *vg = nullptr;
    XIM xim = nullptr;
    ::Window oculta = 0;
    Atom WM_DELETE = 0, WM_PROTOCOLS = 0, UTF8 = 0, NET_WM_NAME = 0, NET_WM_STATE = 0, NET_ACTIVE = 0;
    Atom CLIPBOARD = 0, TARGETS = 0, PROPRIEDADE = 0, TEXT = 0;
    std::vector<Window *> janelas, modais;
    std::vector<DispatcherTimer *> relogios;
    Window *principal = nullptr;
    bool sair = false;
    int codigo = 0;
    std::string area;
    bool donoArea = false;
    ModifierKeys mods = ModifierKeys::None;
    Cursor sobreposto = Cursor::Herda;
    std::map<int, ::Cursor> cursores;
    std::mutex travaTexturas;
    std::vector<int> texturasApagar;
    std::set<unsigned> teclasApertadas;
};

Estado &E()
{
    static Estado *e = new Estado();   // nunca destruido: os elementos podem morrer depois do main
    return *e;
}

Key MapeiaTecla(KeySym k)
{
    if (k >= XK_a && k <= XK_z) return Key(int(Key::A) + int(k - XK_a));
    if (k >= XK_A && k <= XK_Z) return Key(int(Key::A) + int(k - XK_A));
    if (k >= XK_0 && k <= XK_9) return Key(int(Key::D0) + int(k - XK_0));
    if (k >= XK_KP_0 && k <= XK_KP_9) return Key(int(Key::NumPad0) + int(k - XK_KP_0));
    if (k >= XK_F1 && k <= XK_F12) return Key(int(Key::F1) + int(k - XK_F1));
    switch (k)
    {
    case XK_Return: case XK_KP_Enter: return Key::Enter;
    case XK_Escape: return Key::Escape;
    case XK_Tab: case XK_ISO_Left_Tab: return Key::Tab;
    case XK_BackSpace: return Key::Back;
    case XK_Delete: case XK_KP_Delete: return Key::Delete;
    case XK_Insert: case XK_KP_Insert: return Key::Insert;
    case XK_space: return Key::Space;
    case XK_Left: case XK_KP_Left: return Key::Left;
    case XK_Right: case XK_KP_Right: return Key::Right;
    case XK_Up: case XK_KP_Up: return Key::Up;
    case XK_Down: case XK_KP_Down: return Key::Down;
    case XK_Home: case XK_KP_Home: return Key::Home;
    case XK_End: case XK_KP_End: return Key::End;
    case XK_Prior: case XK_KP_Prior: return Key::PageUp;
    case XK_Next: case XK_KP_Next: return Key::PageDown;
    case XK_KP_Add: return Key::Add;
    case XK_KP_Subtract: return Key::Subtract;
    case XK_KP_Multiply: return Key::Multiply;
    case XK_KP_Divide: return Key::Divide;
    case XK_plus: case XK_equal: return Key::OemPlus;
    case XK_minus: return Key::OemMinus;
    case XK_comma: return Key::OemComma;
    case XK_period: return Key::OemPeriod;
    case XK_Shift_L: return Key::LeftShift;
    case XK_Shift_R: return Key::RightShift;
    case XK_Control_L: return Key::LeftCtrl;
    case XK_Control_R: return Key::RightCtrl;
    case XK_Alt_L: return Key::LeftAlt;
    case XK_Alt_R: case XK_ISO_Level3_Shift: return Key::RightAlt;
    case XF86XK_AudioPlay: case XF86XK_AudioPause: return Key::MediaPlayPause;
    case XF86XK_AudioStop: return Key::MediaStop;
    case XF86XK_AudioNext: return Key::MediaNextTrack;
    case XF86XK_AudioPrev: return Key::MediaPreviousTrack;
    case XF86XK_AudioRaiseVolume: return Key::VolumeUp;
    case XF86XK_AudioLowerVolume: return Key::VolumeDown;
    case XF86XK_AudioMute: return Key::VolumeMute;
    default: return Key::None;
    }
}

ModifierKeys ModsDe(unsigned estado)
{
    ModifierKeys m = ModifierKeys::None;
    if (estado & ShiftMask) m = m | ModifierKeys::Shift;
    if (estado & ControlMask) m = m | ModifierKeys::Control;
    if (estado & Mod1Mask) m = m | ModifierKeys::Alt;
    return m;
}

// as teclas mortas do ABNT2 sem o XIM (se o X nao tiver metodo de entrada para a locale)
std::string ComporMorta(KeySym morta, uint32_t base)
{
    static const struct { KeySym m; char b; const char *r; } t[] = {
        {XK_dead_acute, 'a', "á"}, {XK_dead_acute, 'e', "é"}, {XK_dead_acute, 'i', "í"}, {XK_dead_acute, 'o', "ó"}, {XK_dead_acute, 'u', "ú"},
        {XK_dead_acute, 'A', "Á"}, {XK_dead_acute, 'E', "É"}, {XK_dead_acute, 'I', "Í"}, {XK_dead_acute, 'O', "Ó"}, {XK_dead_acute, 'U', "Ú"},
        {XK_dead_acute, 'c', "ç"}, {XK_dead_acute, 'C', "Ç"}, {XK_dead_acute, ' ', "´"},
        {XK_dead_grave, 'a', "à"}, {XK_dead_grave, 'e', "è"}, {XK_dead_grave, 'i', "ì"}, {XK_dead_grave, 'o', "ò"}, {XK_dead_grave, 'u', "ù"},
        {XK_dead_grave, 'A', "À"}, {XK_dead_grave, 'E', "È"}, {XK_dead_grave, 'O', "Ò"}, {XK_dead_grave, ' ', "`"},
        {XK_dead_circumflex, 'a', "â"}, {XK_dead_circumflex, 'e', "ê"}, {XK_dead_circumflex, 'i', "î"}, {XK_dead_circumflex, 'o', "ô"},
        {XK_dead_circumflex, 'u', "û"}, {XK_dead_circumflex, 'A', "Â"}, {XK_dead_circumflex, 'E', "Ê"}, {XK_dead_circumflex, 'O', "Ô"},
        {XK_dead_circumflex, ' ', "^"},
        {XK_dead_tilde, 'a', "ã"}, {XK_dead_tilde, 'o', "õ"}, {XK_dead_tilde, 'n', "ñ"}, {XK_dead_tilde, 'A', "Ã"}, {XK_dead_tilde, 'O', "Õ"},
        {XK_dead_tilde, 'N', "Ñ"}, {XK_dead_tilde, ' ', "~"},
        {XK_dead_diaeresis, 'a', "ä"}, {XK_dead_diaeresis, 'e', "ë"}, {XK_dead_diaeresis, 'i', "ï"}, {XK_dead_diaeresis, 'o', "ö"},
        {XK_dead_diaeresis, 'u', "ü"}, {XK_dead_diaeresis, 'U', "Ü"}, {XK_dead_diaeresis, ' ', "¨"},
        {XK_dead_cedilla, 'c', "ç"}, {XK_dead_cedilla, 'C', "Ç"},
    };
    for (auto &x : t)
        if (x.m == morta && (uint32_t) (unsigned char) x.b == base) return x.r;
    return "";
}

::Cursor CursorX(Cursor c)
{
    Estado &e = E();
    auto it = e.cursores.find((int) c);
    if (it != e.cursores.end()) return it->second;
    unsigned forma = XC_left_ptr;
    switch (c)
    {
    case Cursor::Hand: forma = XC_hand2; break;
    case Cursor::IBeam: forma = XC_xterm; break;
    case Cursor::Wait: forma = XC_watch; break;
    case Cursor::SizeWE: forma = XC_sb_h_double_arrow; break;
    case Cursor::SizeNS: forma = XC_sb_v_double_arrow; break;
    case Cursor::SizeAll: forma = XC_fleur; break;
    case Cursor::Cross: forma = XC_crosshair; break;
    case Cursor::SizeNWSE: forma = XC_bottom_right_corner; break;
    case Cursor::SizeNESW: forma = XC_bottom_left_corner; break;
    default: break;
    }
    ::Cursor x;
    if (c == Cursor::None)
    {
        // cursor invisivel: um pixmap de 1x1 sem nada
        char zero = 0;
        Pixmap p = XCreateBitmapFromData(e.d, DefaultRootWindow(e.d), &zero, 1, 1);
        XColor preto{};
        x = XCreatePixmapCursor(e.d, p, p, &preto, &preto, 0, 0);
        XFreePixmap(e.d, p);
    }
    else
        x = XCreateFontCursor(e.d, forma);
    e.cursores[(int) c] = x;
    return x;
}

void TornaAtual(EGLSurface s)
{
    Estado &e = E();
    if (e.atual == s) return;
    eglMakeCurrent(e.ed, s, s, e.ctx);
    e.atual = s;
}

void ApagaTexturasPendentes()
{
    Estado &e = E();
    std::vector<int> v;
    {
        std::lock_guard<std::mutex> g(e.travaTexturas);
        v.swap(e.texturasApagar);
    }
    for (int t : v) nvgDeleteImage(e.vg, t);
}

const char *PrimeiroQueExiste(std::initializer_list<const char *> l)
{
    struct stat st;
    for (const char *c : l)
        if (stat(c, &st) == 0) return c;
    return nullptr;
}

// a rota do evento: do alvo ate a raiz (o Parent logico; o conteudo de Popup sobe pelo dono)
std::vector<UIElement *> Rota(UIElement *alvo)
{
    std::vector<UIElement *> r;
    for (UIElement *e = alvo; e; e = e->Parent) r.push_back(e);
    return r;
}

} // namespace

// =====================================================================================
// DispatcherTimer
// =====================================================================================
DispatcherTimer::DispatcherTimer() { E().relogios.push_back(this); }

DispatcherTimer::~DispatcherTimer()
{
    auto &r = E().relogios;
    r.erase(std::remove(r.begin(), r.end(), this), r.end());
}

void DispatcherTimer::Start()
{
    _ligado = true;
    _proximo = Agora() + std::max(0.001, Interval.TotalSeconds());
}

void DispatcherTimer::Stop() { _ligado = false; }

void DispatcherTimer::Dispara()
{
    double intervalo = std::max(0.001, Interval.TotalSeconds());
    _proximo += intervalo;
    double agora = Agora();
    if (_proximo < agora) _proximo = agora + intervalo;
    Tick();
}

// =====================================================================================
// Application
// =====================================================================================
bool Application::Inicia(std::string &erro)
{
    Estado &e = E();
    if (e.d) return true;
    // a locale para o XIM (acentos pelo metodo de entrada do X): a do ambiente ou C.UTF-8
    if (!setlocale(LC_CTYPE, "") || !XSupportsLocale()) setlocale(LC_CTYPE, "C.UTF-8");
    XSetLocaleModifiers("");
    e.d = XOpenDisplay(nullptr);
    if (!e.d)
    {
        erro = "sem o X (DISPLAY)";
        return false;
    }
    e.tela = DefaultScreen(e.d);
    int detectavel = 0;
    XkbSetDetectableAutoRepeat(e.d, 1, &detectavel);

    auto getPlat = (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress("eglGetPlatformDisplayEXT");
    e.ed = getPlat ? getPlat(EGL_PLATFORM_X11_KHR, e.d, nullptr) : eglGetDisplay((EGLNativeDisplayType) e.d);
    if (e.ed == EGL_NO_DISPLAY || !eglInitialize(e.ed, nullptr, nullptr))
    {
        erro = "o EGL nao iniciou";
        return false;
    }
    const EGLint atrib[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_STENCIL_SIZE, 8, EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(e.ed, atrib, &e.cfg, 1, &n) || n < 1)
    {
        erro = "nenhuma configuracao EGL serve (GLES2 com stencil)";
        return false;
    }
    EGLint visualId = 0;
    eglGetConfigAttrib(e.ed, e.cfg, EGL_NATIVE_VISUAL_ID, &visualId);
    XVisualInfo modelo{};
    modelo.visualid = (VisualID) visualId;
    int nv = 0;
    XVisualInfo *vi = XGetVisualInfo(e.d, VisualIDMask, &modelo, &nv);
    e.visual = vi ? vi->visual : DefaultVisual(e.d, e.tela);
    e.prof = vi ? vi->depth : DefaultDepth(e.d, e.tela);
    if (vi) XFree(vi);
    e.cmap = XCreateColormap(e.d, RootWindow(e.d, e.tela), e.visual, AllocNone);

    const EGLint ctxAtrib[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    e.ctx = eglCreateContext(e.ed, e.cfg, EGL_NO_CONTEXT, ctxAtrib);
    const EGLint pbAtrib[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    e.pb = eglCreatePbufferSurface(e.ed, e.cfg, pbAtrib);
    if (e.ctx == EGL_NO_CONTEXT || !eglMakeCurrent(e.ed, e.pb, e.pb, e.ctx))
    {
        erro = "o contexto GLES2 nao abriu";
        return false;
    }
    e.atual = e.pb;
    fprintf(stderr, "xos-arca: %s / %s\n", (const char *) glGetString(GL_RENDERER), (const char *) glGetString(GL_VERSION));
    e.vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!e.vg)
    {
        erro = "o NanoVG nao iniciou";
        return false;
    }

    // ---- as fontes: o Noto Sans no papel do Segoe UI, o Noto Sans Mono no do Consolas ----
    // (o Noto Sans e humanista como o Segoe UI e tem medidas parecidas; o DejaVu Sans e bem mais
    // largo e estouraria as larguras fixas do layout). Sem o Noto, o DejaVu.
    struct Face { const char *nome; std::initializer_list<const char *> arquivos; };
    const Face faces[] = {
        {"sans", {"/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf"}},
        {"sans-bold", {"/usr/share/fonts/noto/NotoSans-Bold.ttf", "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf"}},
        {"sans-semibold", {"/usr/share/fonts/noto/NotoSans-SemiBold.ttf", "/usr/share/fonts/noto/NotoSans-Bold.ttf",
                           "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf"}},
        {"sans-italic", {"/usr/share/fonts/noto/NotoSans-Italic.ttf", "/usr/share/fonts/dejavu/DejaVuSans-Oblique.ttf"}},
        {"sans-bolditalic", {"/usr/share/fonts/noto/NotoSans-BoldItalic.ttf", "/usr/share/fonts/dejavu/DejaVuSans-BoldOblique.ttf"}},
        {"sans-semibolditalic", {"/usr/share/fonts/noto/NotoSans-SemiBoldItalic.ttf", "/usr/share/fonts/noto/NotoSans-BoldItalic.ttf",
                                 "/usr/share/fonts/dejavu/DejaVuSans-BoldOblique.ttf"}},
        {"mono", {"/usr/share/fonts/noto/NotoSansMono-Regular.ttf", "/usr/share/fonts/dejavu/DejaVuSansMono.ttf"}},
        {"mono-bold", {"/usr/share/fonts/noto/NotoSansMono-Bold.ttf", "/usr/share/fonts/dejavu/DejaVuSansMono-Bold.ttf"}},
    };
    for (auto &f : faces)
    {
        const char *a = PrimeiroQueExiste(f.arquivos);
        if (!a || nvgCreateFont(e.vg, f.nome, a) < 0)
        {
            if (std::strcmp(f.nome, "sans") == 0)
            {
                erro = "nenhuma fonte (Noto Sans ou DejaVu Sans) em /usr/share/fonts";
                return false;
            }
        }
    }
    // os simbolos que o Noto Sans nao tem (os glifos do transporte: ◀ ■ ▶ ❙) vem destas
    const char *simb2 = PrimeiroQueExiste({"/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf"});
    const char *simb = PrimeiroQueExiste({"/usr/share/fonts/noto/NotoSansSymbols-Regular.ttf"});
    const char *dejavu = PrimeiroQueExiste({"/usr/share/fonts/dejavu/DejaVuSans.ttf"});
    if (simb2) nvgCreateFont(e.vg, "simbolos2", simb2);
    if (simb) nvgCreateFont(e.vg, "simbolos", simb);
    if (dejavu) nvgCreateFont(e.vg, "dejavu", dejavu);
    for (auto &f : faces)
    {
        if (nvgFindFont(e.vg, f.nome) < 0) continue;
        if (simb2) nvgAddFallbackFont(e.vg, f.nome, "simbolos2");
        if (simb) nvgAddFallbackFont(e.vg, f.nome, "simbolos");
        if (dejavu) nvgAddFallbackFont(e.vg, f.nome, "dejavu");
    }

    e.xim = XOpenIM(e.d, nullptr, nullptr, nullptr);
    if (!e.xim) fprintf(stderr, "xos-arca: sem metodo de entrada do X (XIM); acentos pelas teclas mortas internas\n");

    auto atomo = [&](const char *n) { return XInternAtom(e.d, n, 0); };
    e.WM_DELETE = atomo("WM_DELETE_WINDOW");
    e.WM_PROTOCOLS = atomo("WM_PROTOCOLS");
    e.UTF8 = atomo("UTF8_STRING");
    e.NET_WM_NAME = atomo("_NET_WM_NAME");
    e.NET_WM_STATE = atomo("_NET_WM_STATE");
    e.NET_ACTIVE = atomo("_NET_ACTIVE_WINDOW");
    e.CLIPBOARD = atomo("CLIPBOARD");
    e.TARGETS = atomo("TARGETS");
    e.TEXT = atomo("TEXT");
    e.PROPRIEDADE = atomo("_XOS_ARCA_SELECAO");
    e.oculta = XCreateSimpleWindow(e.d, RootWindow(e.d, e.tela), -10, -10, 1, 1, 0, 0, 0);
    XSelectInput(e.d, e.oculta, PropertyChangeMask);
    Base::MarcaFioDaTela();
    return true;
}

bool Application::JaAberto(const std::string &nome)
{
    Estado &e = E();
    if (!e.d) return false;
    Atom a = XInternAtom(e.d, ("_XOS_" + nome + "_INSTANCIA").c_str(), 0);
    ::Window dono = XGetSelectionOwner(e.d, a);
    if (dono == (::Window) XNenhum) return false;
    // traz a janela da outra instancia para a frente (o IceWM aceita o pedido de "pager")
    XEvent ev{};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = dono;
    ev.xclient.message_type = e.NET_ACTIVE;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 2;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(e.d, RootWindow(e.d, e.tela), 0, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XMapRaised(e.d, dono);
    XFlush(e.d);
    return true;
}

void Application::MarcaInstancia(Window &principal, const std::string &nome)
{
    Estado &e = E();
    if (!e.d || !principal.Xid()) return;
    Atom a = XInternAtom(e.d, ("_XOS_" + nome + "_INSTANCIA").c_str(), 0);
    XSetSelectionOwner(e.d, a, (::Window) principal.Xid(), CurrentTime);
}

int Application::Run(Window &principal)
{
    Estado &e = E();
    e.principal = &principal;
    if (!principal.EstaAberta()) principal.Show();
    while (!e.sair && principal.EstaAberta()) Bombeia(true);
    return e.codigo;
}

void Application::Shutdown(int codigo)
{
    E().sair = true;
    E().codigo = codigo;
}

Window *Application::MainWindow() { return E().principal; }
NVGcontext *Application::Vg() { return E().vg; }
void *Application::DisplayX() { return E().d; }
const std::vector<Window *> &Application::Janelas() { return E().janelas; }
Window *Application::JanelaModalAtual() { return E().modais.empty() ? nullptr : E().modais.back(); }

void Application::RedesenhaTudo()
{
    for (Window *j : E().janelas) j->Redesenha();
}

void Application::ApagaTexturaDepois(int t)
{
    std::lock_guard<std::mutex> g(E().travaTexturas);
    E().texturasApagar.push_back(t);
}

Size Application::TamanhoDaTela()
{
    Estado &e = E();
    if (!e.d) return Size(1920, 1080);
    return Size(DisplayWidth(e.d, e.tela), DisplayHeight(e.d, e.tela));
}

namespace {

void TrataSelecao(XEvent &ev)
{
    Estado &e = E();
    if (ev.type == SelectionClear && ev.xselectionclear.selection == e.CLIPBOARD)
        e.donoArea = false;
    else if (ev.type == SelectionRequest)
    {
        XSelectionRequestEvent &r = ev.xselectionrequest;
        XSelectionEvent s{};
        s.type = SelectionNotify;
        s.requestor = r.requestor;
        s.selection = r.selection;
        s.target = r.target;
        s.time = r.time;
        s.property = (Atom) XNenhum;
        Atom prop = r.property != (Atom) XNenhum ? r.property : r.target;
        if (r.selection == e.CLIPBOARD && e.donoArea)
        {
            if (r.target == e.TARGETS)
            {
                Atom alvos[] = {e.TARGETS, e.UTF8, XA_STRING, e.TEXT};
                XChangeProperty(e.d, r.requestor, prop, XA_ATOM, 32, PropModeReplace, (unsigned char *) alvos, 4);
                s.property = prop;
            }
            else if (r.target == e.UTF8 || r.target == XA_STRING || r.target == e.TEXT)
            {
                XChangeProperty(e.d, r.requestor, prop, r.target == XA_STRING ? XA_STRING : e.UTF8, 8, PropModeReplace,
                                (const unsigned char *) e.area.data(), (int) e.area.size());
                s.property = prop;
            }
        }
        XSendEvent(e.d, r.requestor, 0, 0, (XEvent *) &s);
    }
}

void TrataUmEvento(XEvent &ev)
{
    Estado &e = E();
    if (XFilterEvent(&ev, (::Window) XNenhum)) return;
    if (ev.type == SelectionRequest || ev.type == SelectionClear)
    {
        TrataSelecao(ev);
        return;
    }
    for (Window *j : e.janelas)
        if ((::Window) j->Xid() == ev.xany.window)
        {
            j->TrataEvento(&ev);
            return;
        }
}

double ProximoPrazo()
{
    double p = 1e18;
    for (DispatcherTimer *t : E().relogios)
        if (t->IsEnabled()) p = std::min(p, t->Proximo());
    return p;
}

} // namespace

void Application::Bombeia(bool espera)
{
    Estado &e = E();
    if (!e.d) return;
    while (XPending(e.d))
    {
        XEvent ev;
        XNextEvent(e.d, &ev);
        TrataUmEvento(ev);
    }
    // os pedidos de outros fios
    {
        pollfd p{Base::DescritorDespacho(), POLLIN, 0};
        if (poll(&p, 1, 0) > 0)
        {
            Base::RodaDespachos();
            RedesenhaTudo();
        }
    }
    // os relogios
    double agora = Agora();
    auto relogios = e.relogios;
    for (DispatcherTimer *t : relogios)
    {
        // pode ter sido destruido por um Tick anterior
        if (std::find(e.relogios.begin(), e.relogios.end(), t) == e.relogios.end()) continue;
        if (t->IsEnabled() && agora >= t->Proximo())
        {
            t->Dispara();
            RedesenhaTudo();
        }
    }
    // dica, pisca do cursor de texto
    double prazo = ProximoPrazo();
    auto janelas = e.janelas;
    for (Window *j : janelas)
    {
        if (std::find(e.janelas.begin(), e.janelas.end(), j) == e.janelas.end()) continue;
        j->AtualizaDica();
        if (j->Foco() && dynamic_cast<TextBox *>(j->Foco()) && j->IsActive())
        {
            double d = Agora() - j->_piscaDesde;
            if (d < 10)
            {
                double fase = std::fmod(d, 1.06);
                prazo = std::min(prazo, Agora() + (fase < 0.53 ? 0.53 - fase : 1.06 - fase) + 0.005);
                bool aceso = fase < 0.53;
                static std::map<Window *, bool> antes;
                if (antes[j] != aceso) antes[j] = aceso, j->Redesenha();
            }
        }
        if (j->_dicaAlvo && !j->_dicaVisivel) prazo = std::min(prazo, j->_dicaDesde + 0.4);
        if (j->_dicaVisivel) prazo = std::min(prazo, j->_dicaDesde + 0.4 + 5.0);
    }
    // desenha o que mudou
    bool aindaSuja = false;
    janelas = e.janelas;
    for (Window *j : janelas)
    {
        if (std::find(e.janelas.begin(), e.janelas.end(), j) == e.janelas.end()) continue;
        if (j->Suja() && j->EstaAberta()) j->Desenha();
        if (j->Suja()) aindaSuja = true;
    }
    if (!espera || e.sair) return;
    if (XPending(e.d)) return;
    XFlush(e.d);
    int espera_ms = -1;
    double agora2 = Agora();
    if (prazo < 1e17) espera_ms = (int) std::clamp((prazo - agora2) * 1000.0 + 0.5, 0.0, 60000.0);
    // algo pediu redesenho durante o desenho (animacao): no maximo 60 quadros por segundo
    if (aindaSuja) espera_ms = espera_ms < 0 ? 16 : std::min(espera_ms, 16);
    pollfd fds[2] = {{ConnectionNumber(e.d), POLLIN, 0}, {Base::DescritorDespacho(), POLLIN, 0}};
    poll(fds, 2, espera_ms);
}

// =====================================================================================
// Window
// =====================================================================================
Window::Window()
{
    Focusable = false;
    IsTabStop = false;
    HorizontalContentAlignment = Vistas::HorizontalAlignment::Stretch;
    VerticalContentAlignment = Vistas::VerticalAlignment::Stretch;
}

Window::~Window()
{
    if (_aberta) Destroi();
    auto &j = E().janelas;
    j.erase(std::remove(j.begin(), j.end(), this), j.end());
    auto &m = E().modais;
    m.erase(std::remove(m.begin(), m.end(), this), m.end());
    if (E().principal == this) E().principal = nullptr;
}

void Window::OnRender(DrawingContext &dc)
{
    Brush b = BackgroundEfetivo();
    if (b.Nulo()) b = Brush(Tema::SistemaJanela);
    dc.Retangulo(Limites, b);
}

void Window::Cria()
{
    Estado &e = E();
    if (!e.d)
    {
        std::string erro;
        if (!Application::Inicia(erro))
        {
            fprintf(stderr, "xos-arca: %s\n", erro.c_str());
            return;
        }
    }
    Size tela = Application::TamanhoDaTela();
    double w = std::isnan(Width) ? 800 : Width, h = std::isnan(Height) ? 600 : Height;
    if (SizeToContent != Vistas::SizeToContent::Manual)
    {
        DesiredSize = MeasureOverride(Size(SizeToContent == Vistas::SizeToContent::Height ? w : std::min(tela.Width, MaxWidth),
                                           SizeToContent == Vistas::SizeToContent::Width ? h : std::min(tela.Height, MaxHeight)));
        if (SizeToContent != Vistas::SizeToContent::Height) w = std::ceil(DesiredSize.Width);
        if (SizeToContent != Vistas::SizeToContent::Width) h = std::ceil(DesiredSize.Height);
    }
    w = std::clamp(w, std::max(1.0, MinWidth), std::max(1.0, std::min(MaxWidth, tela.Width)));
    h = std::clamp(h, std::max(1.0, MinHeight), std::max(1.0, std::min(MaxHeight, tela.Height)));
    _largura = (int) w;
    _altura = (int) h;
    int x = std::isnan(Left) ? 0 : (int) Left, y = std::isnan(Top) ? 0 : (int) Top;
    bool posicionada = !std::isnan(Left) || !std::isnan(Top);
    if (WindowStartupLocation == Vistas::WindowStartupLocation::CenterScreen || (WindowStartupLocation == Vistas::WindowStartupLocation::CenterOwner && !Owner))
    {
        x = (int) ((tela.Width - w) / 2);
        y = (int) ((tela.Height - h) / 2);
        posicionada = true;
    }
    else if (WindowStartupLocation == Vistas::WindowStartupLocation::CenterOwner && Owner && Owner->Xid())
    {
        ::Window filho;
        int ox = 0, oy = 0;
        XTranslateCoordinates(e.d, (::Window) Owner->Xid(), RootWindow(e.d, e.tela), 0, 0, &ox, &oy, &filho);
        x = ox + (Owner->_largura - (int) w) / 2;
        y = oy + (Owner->_altura - (int) h) / 2;
        x = std::clamp(x, 0, std::max(0, (int) tela.Width - (int) w));
        y = std::clamp(y, 0, std::max(0, (int) tela.Height - (int) h));
        posicionada = true;
    }

    XSetWindowAttributes a{};
    a.colormap = e.cmap;
    a.background_pixel = 0;
    a.border_pixel = 0;
    a.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                   PointerMotionMask | EnterWindowMask | LeaveWindowMask | FocusChangeMask;
    ::Window xid = XCreateWindow(e.d, RootWindow(e.d, e.tela), x, y, (unsigned) w, (unsigned) h, 0, e.prof, InputOutput, e.visual,
                                 CWColormap | CWBackPixel | CWBorderPixel | CWEventMask, &a);
    _xid = xid;
    XSetWMProtocols(e.d, xid, &e.WM_DELETE, 1);
    // o titulo em UTF-8 (e em latim-1 para quem so le WM_NAME)
    XChangeProperty(e.d, xid, e.NET_WM_NAME, e.UTF8, 8, PropModeReplace, (const unsigned char *) Title.data(), (int) Title.size());
    XStoreName(e.d, xid, Title.c_str());
    std::string classe = ClasseX;
    XClassHint ch{(char *) classe.c_str(), (char *) classe.c_str()};
    XSetClassHint(e.d, xid, &ch);
    long pid = (long) getpid();
    XChangeProperty(e.d, xid, XInternAtom(e.d, "_NET_WM_PID", 0), XA_CARDINAL, 32, PropModeReplace, (unsigned char *) &pid, 1);
    // Icon="arca.ico" (MainWindow.xaml; as outras janelas herdam o ApplicationIcon do .exe)
    XChangeProperty(e.d, xid, XInternAtom(e.d, "_NET_WM_ICON", 0), XA_CARDINAL, 32, PropModeReplace,
                    (const unsigned char *) IconeArca, (int) (sizeof(IconeArca) / sizeof(IconeArca[0])));
    XSizeHints dica{};
    dica.flags = PMinSize | PSize;
    dica.width = (int) w;
    dica.height = (int) h;
    dica.min_width = (int) std::max(1.0, MinWidth);
    dica.min_height = (int) std::max(1.0, MinHeight);
    if (std::isfinite(MaxWidth) || std::isfinite(MaxHeight) || ResizeMode == Vistas::ResizeMode::NoResize || ResizeMode == Vistas::ResizeMode::CanMinimize)
    {
        dica.flags |= PMaxSize;
        bool fixa = ResizeMode == Vistas::ResizeMode::NoResize || ResizeMode == Vistas::ResizeMode::CanMinimize;
        dica.max_width = fixa ? (int) w : (int) std::min(MaxWidth, 30000.0);
        dica.max_height = fixa ? (int) h : (int) std::min(MaxHeight, 30000.0);
        if (fixa) dica.min_width = (int) w, dica.min_height = (int) h;
    }
    if (posicionada)
    {
        dica.flags |= PPosition | USPosition;
        dica.x = x;
        dica.y = y;
    }
    XSetWMNormalHints(e.d, xid, &dica);
    if (Owner && Owner->Xid()) XSetTransientForHint(e.d, xid, (::Window) Owner->Xid());
    // tipo e estados (_NET_WM_*)
    Atom tipo = XInternAtom(e.d, Owner ? "_NET_WM_WINDOW_TYPE_DIALOG" : "_NET_WM_WINDOW_TYPE_NORMAL", 0);
    XChangeProperty(e.d, xid, XInternAtom(e.d, "_NET_WM_WINDOW_TYPE", 0), XA_ATOM, 32, PropModeReplace, (unsigned char *) &tipo, 1);
    std::vector<Atom> estados;
    if (_modal) estados.push_back(XInternAtom(e.d, "_NET_WM_STATE_MODAL", 0));
    if (!ShowInTaskbar) estados.push_back(XInternAtom(e.d, "_NET_WM_STATE_SKIP_TASKBAR", 0));
    if (Topmost) estados.push_back(XInternAtom(e.d, "_NET_WM_STATE_ABOVE", 0));
    if (WindowState == Vistas::WindowState::Maximized)
    {
        if (WindowStyle == Vistas::WindowStyle::None) estados.push_back(XInternAtom(e.d, "_NET_WM_STATE_FULLSCREEN", 0));
        else
        {
            estados.push_back(XInternAtom(e.d, "_NET_WM_STATE_MAXIMIZED_VERT", 0));
            estados.push_back(XInternAtom(e.d, "_NET_WM_STATE_MAXIMIZED_HORZ", 0));
        }
    }
    if (!estados.empty())
        XChangeProperty(e.d, xid, e.NET_WM_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char *) estados.data(), (int) estados.size());
    if (WindowStyle == Vistas::WindowStyle::None)
    {
        long motif[5] = {2, 0, 0, 0, 0};   // MWM_HINTS_DECORATIONS, sem decoracao
        Atom m = XInternAtom(e.d, "_MOTIF_WM_HINTS", 0);
        XChangeProperty(e.d, xid, m, m, 32, PropModeReplace, (unsigned char *) motif, 5);
    }
    if (e.xim)
    {
        XIC ic = XCreateIC(e.xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, xid, XNFocusWindow, xid, nullptr);
        _xic = ic;
        if (ic)
        {
            long filtro = 0;
            XGetICValues(ic, XNFilterEvents, &filtro, nullptr);
            XSelectInput(e.d, xid, a.event_mask | filtro);
        }
    }
    _superficie = eglCreateWindowSurface(e.ed, e.cfg, (EGLNativeWindowType) xid, nullptr);
    if (_superficie == EGL_NO_SURFACE) fprintf(stderr, "xos-arca: a superficie EGL da janela \"%s\" nao abriu\n", Title.c_str());
    TornaAtual((EGLSurface) _superficie);
    eglSwapInterval(e.ed, 0);
    XMapWindow(e.d, xid);
    if (posicionada) XMoveWindow(e.d, xid, x, y);
    XFlush(e.d);
    _aberta = true;
    _mapeada = false;
    _suja = true;
    e.janelas.push_back(this);
}

void Window::Destroi()
{
    Estado &e = E();
    FechaPopups();
    if (_superficie)
    {
        if (e.atual == (EGLSurface) _superficie) TornaAtual(e.pb);
        eglDestroySurface(e.ed, (EGLSurface) _superficie);
        _superficie = nullptr;
    }
    if (_xic) XDestroyIC((XIC) _xic), _xic = nullptr;
    if (_xid) XDestroyWindow(e.d, (::Window) _xid), _xid = 0;
    XFlush(e.d);
    _aberta = false;
    _foco = _captura = nullptr;
    _sobre.clear();
    _dicaAlvo = nullptr;
    auto &j = e.janelas;
    j.erase(std::remove(j.begin(), j.end(), this), j.end());
    // a dona volta a ser a ativa
    if (Owner && Owner->_aberta) Owner->Activate();
}

void Window::Show()
{
    if (_aberta)
    {
        Activate();
        return;
    }
    _fechando = false;
    Cria();
}

std::optional<bool> Window::ShowDialog()
{
    _resultado.reset();
    _modal = true;
    _fechando = false;
    E().modais.push_back(this);
    Cria();
    while (_aberta && !E().sair) Application::Bombeia(true);
    auto &m = E().modais;
    m.erase(std::remove(m.begin(), m.end(), this), m.end());
    _modal = false;
    if (_aberta) Destroi();
    return _resultado;
}

void Window::Close()
{
    if (!_aberta || _fechando) return;
    CancelEventArgs c;
    Closing(c);
    if (c.Cancel) return;
    _fechando = true;
    Destroi();
    RoutedEventArgs a;
    a.Source = a.OriginalSource = this;
    Closed(a);
    if (E().principal == this) E().sair = true;
}

void Window::Hide()
{
    if (_xid) XUnmapWindow(E().d, (::Window) _xid), XFlush(E().d);
}

bool Window::Activate()
{
    Estado &e = E();
    if (!_xid) return false;
    XEvent ev{};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = (::Window) _xid;
    ev.xclient.message_type = e.NET_ACTIVE;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 1;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(e.d, RootWindow(e.d, e.tela), 0, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XRaiseWindow(e.d, (::Window) _xid);
    XFlush(e.d);
    return true;
}

void Window::SetDialogResult(std::optional<bool> r)
{
    _resultado = r;
    if (_modal && _aberta) Close();
}

bool Window::NaArvore(const UIElement *e) const
{
    for (; e; e = e->Parent)
        if (e == this) return true;
    return false;
}

UIElement *Window::Foco() const { return NaArvore(_foco) ? _foco : nullptr; }
UIElement *Window::Captura() const { return NaArvore(_captura) ? _captura : nullptr; }

void Window::DefineFoco(UIElement *e)
{
    UIElement *antes = Foco();
    if (antes == e) return;
    _foco = e;
    RoutedEventArgs a;
    if (antes)
    {
        a.Source = a.OriginalSource = antes;
        antes->OnLostFocus();
        antes->LostKeyboardFocus(a);
        antes->LostFocus(a);
    }
    if (e)
    {
        RoutedEventArgs b;
        b.Source = b.OriginalSource = e;
        e->OnGotFocus();
        e->GotKeyboardFocus(b);
        e->GotFocus(b);
    }
    ReiniciaPisca();
    Redesenha();
}

void Window::DefineCaptura(UIElement *e)
{
    UIElement *antes = Captura();
    _captura = e;
    if (antes && antes != e)
    {
        antes->OnLostMouseCapture();
        RoutedEventArgs a;
        a.Source = a.OriginalSource = antes;
        antes->LostMouseCapture(a);
    }
    Redesenha();
}

bool Window::EstaSobre(const UIElement *e) const
{
    return std::find(_sobre.begin(), _sobre.end(), e) != _sobre.end();
}

bool Window::EstaDiretamenteSobre(const UIElement *e) const { return !_sobre.empty() && _sobre.front() == e; }

void Window::Redesenha() { _suja = true; }

void Window::Esquece(const UIElement *e)
{
    if (_foco == e) _foco = nullptr;
    if (_captura == e) _captura = nullptr;
    if (_dicaAlvo == e) _dicaAlvo = nullptr, _dicaVisivel = false;
    _sobre.erase(std::remove(_sobre.begin(), _sobre.end(), e), _sobre.end());
}

void Window::DepoisDoLayout(std::function<void()> f)
{
    _depoisDoLayout.push_back(std::move(f));
    _suja = true;
}

void Window::AbrePopup(UIElementPtr conteudo, Rect posicao, UIElement *dono, std::function<void()> aoFechar)
{
    _popups.push_back({std::move(conteudo), posicao, dono, std::move(aoFechar)});
    Redesenha();
}

void Window::FechaPopup(const UIElement *conteudo)
{
    for (size_t i = 0; i < _popups.size(); i++)
        if (_popups[i].Conteudo.get() == conteudo)
        {
            Popup p = _popups[i];
            _popups.erase(_popups.begin() + (std::ptrdiff_t) i);
            if (_captura && p.Conteudo->EhAncestralDe(_captura)) _captura = nullptr;
            _sobre.clear();
            if (p.AoFechar) p.AoFechar();
            Redesenha();
            return;
        }
}

void Window::FechaPopups()
{
    while (!_popups.empty()) FechaPopup(_popups.back().Conteudo.get());
}

bool Window::CursorAceso() const
{
    double d = Agora() - _piscaDesde;
    if (d >= 10 || !_ativa) return true;   // depois de 10 s parado o cursor fica aceso (nao gasta)
    return std::fmod(d, 1.06) < 0.53;
}

void Window::ReiniciaPisca()
{
    _piscaDesde = Agora();
    _suja = true;
}

void Window::Layout()
{
    // a janela nao se mede pelo proprio Width/Height (no WPF eles sao o tamanho da janela do sistema):
    // o conteudo ocupa a area cliente que o gerenciador de janelas deu
    Size antes(ActualWidth, ActualHeight);
    DesiredSize = MeasureOverride(Size(_largura, _altura));
    Limites = Rect(0, 0, _largura, _altura);
    ActualWidth = _largura;
    ActualHeight = _altura;
    ArrangeOverride(Size(_largura, _altura));
    if ((antes.Width != _largura || antes.Height != _altura) && !SizeChanged.Vazio())
    {
        SizeChangedEventArgs a;
        a.PreviousSize = antes;
        a.NewSize = Size(_largura, _altura);
        a.WidthChanged = antes.Width != _largura;
        a.HeightChanged = antes.Height != _altura;
        SizeChanged(a);
    }
    for (auto &p : _popups)
    {
        p.Conteudo->Measure(Size(p.Posicao.Width, p.Posicao.Height));
        p.Conteudo->Arrange(p.Posicao);
    }
}

void Window::DisparaCarregados(UIElement *e, bool &algum)
{
    if (!e->_carregado)
    {
        e->_carregado = true;
        if (!e->Loaded.Vazio())
        {
            RoutedEventArgs a;
            a.Source = a.OriginalSource = e;
            e->Loaded(a);
            algum = true;
        }
    }
    std::vector<UIElement *> f;
    e->Filhos(f);
    for (UIElement *c : f) DisparaCarregados(c, algum);
}

void Window::Desenha()
{
    Estado &e = E();
    if (!_aberta || !_superficie || !_mapeada) return;
    TornaAtual((EGLSurface) _superficie);
    ApagaTexturasPendentes();
    // layout ate assentar: Loaded, SizeChanged e o DispatcherPriority.Loaded podem mexer na arvore
    for (int volta = 0; volta < 4; volta++)
    {
        _suja = false;
        Layout();
        bool algum = false;
        DisparaCarregados(this, algum);
        if (!_depoisDoLayout.empty())
        {
            auto v = std::move(_depoisDoLayout);
            _depoisDoLayout.clear();
            for (auto &f : v) f();
            algum = true;
        }
        if (!algum && !_suja) break;
    }
    if (!_aberta) return;
    _suja = false;
    glViewport(0, 0, _largura, _altura);
    Brush fundo = BackgroundEfetivo();
    Color c = fundo.Nulo() ? Tema::SistemaJanela : fundo.Cor();
    glClearColor(c.R / 255.f, c.G / 255.f, c.B / 255.f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    nvgBeginFrame(e.vg, (float) _largura, (float) _altura, 1);
    DrawingContext dc(e.vg);
    Render(dc);
    for (auto &p : _popups) p.Conteudo->Render(dc);
    // a dica (ToolTip do Tema): PainelLateral, borda Borda, recuo 8,5, no maximo 480 de largura
    if (_dicaVisivel && !_dicaTexto.empty())
    {
        const Vistas::Style *s = Tema::EstiloPadrao("ToolTip");
        FonteTexto f;
        f.Tamanho = s && s->FontSize ? *s->FontSize : 12;
        Thickness r = Tema::DicaRecuo;
        auto linhas = Texto::Quebra(f, _dicaTexto, Tema::DicaLarguraMaxima - r.Horizontal() - 2, true);
        double w = 0, lh = Texto::AlturaLinha(f);
        for (auto &l : linhas) w = std::max(w, l.Largura);
        Rect caixa(_dicaPonto.X, _dicaPonto.Y, std::ceil(w) + r.Horizontal() + 2, lh * linhas.size() + r.Vertical() + 2);
        if (caixa.Right() > _largura) caixa.X = std::max(0.0, _largura - caixa.Width);
        if (caixa.Bottom() > _altura) caixa.Y = std::max(0.0, _dicaPonto.Y - 20 - caixa.Height - 4);
        caixa.X = std::floor(caixa.X);
        caixa.Y = std::floor(caixa.Y);
        dc.Retangulo(caixa, Tema::Recurso("PainelLateral"), Tema::Recurso("Borda"), Thickness(1));
        Brush tinta = Tema::Recurso("Texto");
        for (size_t i = 0; i < linhas.size(); i++)
            dc.Texto(_dicaTexto.substr(linhas[i].Inicio, linhas[i].Fim - linhas[i].Inicio), f, caixa.X + 1 + r.Left,
                     caixa.Y + 1 + r.Top + i * lh, tinta);
    }
    nvgEndFrame(e.vg);
    eglSwapBuffers(e.ed, (EGLSurface) _superficie);
    if (!_mostrada)
    {
        _mostrada = true;
        RoutedEventArgs a;
        a.Source = a.OriginalSource = this;
        ContentRendered(a);
    }
}

UIElement *Window::AlvoEm(Point p, bool *emPopup)
{
    if (emPopup) *emPopup = false;
    for (auto it = _popups.rbegin(); it != _popups.rend(); ++it)
        if (it->Posicao.Contains(p))
        {
            if (emPopup) *emPopup = true;
            UIElement *r = it->Conteudo->Acerta(p);
            return r ? r : it->Conteudo.get();
        }
    UIElement *r = Acerta(p);
    return r ? r : this;
}

void Window::AtualizaSobre(Point p)
{
    std::vector<UIElement *> novo;
    if (p.X >= 0 && p.Y >= 0) novo = Rota(AlvoEm(p));
    if (novo == _sobre) return;
    std::vector<UIElement *> velho = _sobre;
    _sobre = novo;
    MouseEventArgs a;
    a.Posicao = p;
    for (UIElement *e : velho)
        if (std::find(novo.begin(), novo.end(), e) == novo.end() && NaArvore(e))
        {
            a.Source = e;
            e->OnMouseLeave(a);
            e->MouseLeave(a);
        }
    for (auto it = novo.rbegin(); it != novo.rend(); ++it)
        if (std::find(velho.begin(), velho.end(), *it) == velho.end())
        {
            a.Source = *it;
            (*it)->OnMouseEnter(a);
            (*it)->MouseEnter(a);
        }
    _suja = true;
}

void Window::AtualizaCursor()
{
    Estado &e = E();
    Vistas::Cursor c = e.sobreposto;
    if (c == Vistas::Cursor::Herda)
    {
        std::vector<UIElement *> cadeia = Captura() ? Rota(Captura()) : _sobre;
        for (UIElement *el : cadeia)
        {
            Vistas::Cursor k = el->CursorEm(_mouse);
            if (k != Vistas::Cursor::Herda)
            {
                c = k;
                break;
            }
        }
    }
    if (c == Vistas::Cursor::Herda) c = Vistas::Cursor::Arrow;
    if (c != _cursorAtual && _xid)
    {
        _cursorAtual = c;
        XDefineCursor(e.d, (::Window) _xid, CursorX(c));
    }
}

void Window::AtualizaDica()
{
    UIElement *alvo = nullptr;
    std::string texto;
    if (!Captura() && _ativa)
        for (UIElement *el : _sobre)
        {
            if (!NaArvore(el) || !el->IsEnabledEfetivo()) break;
            std::string d = el->DicaEm(_mouse);
            if (!d.empty())
            {
                alvo = el;
                texto = d;
                break;
            }
        }
    double agora = Agora();
    if (alvo != _dicaAlvo || texto != _dicaTexto)
    {
        if (_dicaVisivel) _suja = true;
        // passando de uma dica para outra, a nova aparece logo (BetweenShowDelay)
        bool seguida = _dicaVisivel && alvo;
        _dicaAlvo = alvo;
        _dicaTexto = texto;
        _dicaDesde = seguida ? agora - 0.4 : agora;
        _dicaVisivel = false;
        _dicaPonto = Point(_mouse.X, _mouse.Y + 20);
    }
    if (_dicaAlvo && !_dicaVisivel && agora - _dicaDesde >= 0.4 && agora - _dicaDesde < 5.4)
    {
        _dicaVisivel = true;
        _dicaPonto = Point(_mouse.X, _mouse.Y + 20);
        _suja = true;
    }
    if (_dicaVisivel && agora - _dicaDesde >= 5.4)
    {
        _dicaVisivel = false;
        _suja = true;
    }
}

namespace {
template <class A, class FC, class FE>
void Desce(const std::vector<UIElement *> &rota, A &a, FC classe, FE evento)
{
    for (auto it = rota.rbegin(); it != rota.rend() && !a.Handled; ++it)
    {
        a.Source = *it;
        classe(*it, a);
        if (!a.Handled) evento(*it, a);
    }
}
template <class A, class FC, class FE>
void Sobe(const std::vector<UIElement *> &rota, A &a, FC classe, FE evento)
{
    for (UIElement *e : rota)
    {
        if (a.Handled) break;
        a.Source = e;
        classe(e, a);
        if (!a.Handled) evento(e, a);
    }
}
} // namespace

void Window::Movimento(Point p, unsigned estado)
{
    E().mods = ModsDe(estado);
    _mouse = p;
    AtualizaSobre(p);
    UIElement *alvo = Captura() ? Captura() : (_sobre.empty() ? this : _sobre.front());
    auto rota = Rota(alvo);
    MouseEventArgs a;
    a.Posicao = p;
    a.OriginalSource = alvo;
    a.LeftButton = (estado & Button1Mask) ? MouseButtonState::Pressed : MouseButtonState::Released;
    bool temOuvinte = false;
    for (UIElement *e : rota)
        if (!e->MouseMove.Vazio() || !e->PreviewMouseMove.Vazio()) temOuvinte = true;
    Desce(rota, a, [](UIElement *, MouseEventArgs &) {}, [](UIElement *e, MouseEventArgs &x) { e->PreviewMouseMove(x); });
    a.Handled = false;
    Sobe(rota, a, [](UIElement *e, MouseEventArgs &x) { e->OnMouseMove(x); }, [](UIElement *e, MouseEventArgs &x) { e->MouseMove(x); });
    if (temOuvinte || Captura()) _suja = true;
    AtualizaCursor();
    AtualizaDica();
}

void Window::Botao(void *xe, bool apertou)
{
    XButtonEvent &b = ((XEvent *) xe)->xbutton;
    E().mods = ModsDe(b.state);
    Point p(b.x, b.y);
    _mouse = p;
    AtualizaSobre(p);
    // a dica some com o clique
    if (_dicaVisivel) _dicaVisivel = false, _suja = true;
    _dicaDesde = Agora() + 1e6;
    if (b.button >= 4 && b.button <= 7)
    {
        if (!apertou) return;
        bool emPopup = false;
        UIElement *alvo = AlvoEm(p, &emPopup);
        auto rota = Rota(alvo);
        MouseWheelEventArgs a;
        a.Posicao = p;
        a.OriginalSource = alvo;
        a.Delta = (b.button == 4 || b.button == 6) ? 120 : -120;
        // LINUX: a roda horizontal (botoes 6 e 7) chega como roda comum com Shift (o WPF 4 nao tinha MouseHWheel)
        Desce(rota, a, [](UIElement *, MouseWheelEventArgs &) {}, [](UIElement *e, MouseWheelEventArgs &x) { e->PreviewMouseWheel(x); });
        Sobe(rota, a, [](UIElement *e, MouseWheelEventArgs &x) { e->OnMouseWheel(x); }, [](UIElement *e, MouseWheelEventArgs &x) { e->MouseWheel(x); });
        _suja = true;
        AtualizaSobre(p);
        return;
    }
    MouseButton qual = b.button == 1 ? MouseButton::Left : b.button == 2 ? MouseButton::Middle : b.button == 3 ? MouseButton::Right
                       : b.button == 8 ? MouseButton::XButton1 : MouseButton::XButton2;
    bool emPopup = false;
    UIElement *alvo = Captura() ? Captura() : AlvoEm(p, &emPopup);
    if (apertou && !_popups.empty() && !emPopup && !(Captura() && Captura() != this))
    {
        // clique fora da lista suspensa: fecha e o clique morre aqui (o Popup do WPF captura o mouse)
        FechaPopups();
        AtualizaSobre(p);
        return;
    }
    MouseButtonEventArgs a;
    a.Posicao = p;
    a.OriginalSource = alvo;
    a.ChangedButton = qual;
    // o estado depois do evento, como o WPF informa (o b.state do X e o de antes)
    a.LeftButton = qual == MouseButton::Left ? (apertou ? MouseButtonState::Pressed : MouseButtonState::Released)
                   : (b.state & Button1Mask) ? MouseButtonState::Pressed : MouseButtonState::Released;
    if (apertou)
    {
        double t = Agora();
        // o clique duplo do Windows: 500 ms e 4 px
        if (b.button == _ultimoBotao && t - _ultimoCliqueT <= 0.5 && std::fabs(p.X - _ultimoCliqueP.X) <= 4 && std::fabs(p.Y - _ultimoCliqueP.Y) <= 4)
            _cliques++;
        else
            _cliques = 1;
        _ultimoCliqueT = t;
        _ultimoCliqueP = p;
        _ultimoBotao = b.button;
        a.ClickCount = _cliques;
    }
    else
        a.ClickCount = _cliques;
    auto rota = Rota(alvo);
    // o WPF segura vivos os elementos da rota enquanto o evento roda (o coletor nao leva um botao que
    // o proprio Click tirou da arvore); aqui, uma referencia forte a cada um ate o fim do despacho
    std::vector<std::shared_ptr<UIElement>> vivos;
    for (UIElement *x : rota)
        if (auto v = x->weak_from_this().lock()) vivos.push_back(v);
    UIElement *focoAntes = Foco();
    if (apertou)
    {
        Desce(rota, a, [](UIElement *e, MouseButtonEventArgs &x) { e->OnPreviewMouseDown(x); },
              [qual](UIElement *e, MouseButtonEventArgs &x) {
                  if (qual == MouseButton::Left) e->PreviewMouseLeftButtonDown(x);
                  else if (qual == MouseButton::Right) e->PreviewMouseRightButtonDown(x);
                  if (!x.Handled) e->PreviewMouseDown(x);
              });
        if (!a.Handled)
            Sobe(rota, a, [](UIElement *e, MouseButtonEventArgs &x) { e->OnMouseDown(x); },
                 [qual](UIElement *e, MouseButtonEventArgs &x) {
                     if (qual == MouseButton::Left) e->MouseLeftButtonDown(x);
                     else if (qual == MouseButton::Right) e->MouseRightButtonDown(x);
                     if (!x.Handled) e->MouseDown(x);
                 });
        // o foco vai para o primeiro elemento focavel da rota, se ninguem o moveu
        if (Foco() == focoAntes && qual == MouseButton::Left)
            for (UIElement *e : rota)
                if (e->Focusable && e->IsEnabledEfetivo())
                {
                    e->Focus();
                    break;
                }
        // Control.MouseDoubleClick: cada controle da rota recebe o seu
        if (a.ClickCount == 2)
            for (UIElement *e : rota)
                if (dynamic_cast<Control *>(e) && !e->MouseDoubleClick.Vazio() && NaArvore(e))
                {
                    MouseButtonEventArgs d = a;
                    d.Handled = false;
                    d.Source = e;
                    e->MouseDoubleClick(d);
                }
    }
    else
    {
        Desce(rota, a, [](UIElement *, MouseButtonEventArgs &) {},
              [qual](UIElement *e, MouseButtonEventArgs &x) {
                  if (qual == MouseButton::Left) e->PreviewMouseLeftButtonUp(x);
              });
        a.Handled = false;
        Sobe(rota, a, [](UIElement *e, MouseButtonEventArgs &x) { e->OnMouseUp(x); },
             [qual](UIElement *e, MouseButtonEventArgs &x) {
                 if (qual == MouseButton::Left) e->MouseLeftButtonUp(x);
                 else if (qual == MouseButton::Right) e->MouseRightButtonUp(x);
                 if (!x.Handled) e->MouseUp(x);
             });
    }
    _suja = true;
    AtualizaSobre(p);
    AtualizaCursor();
}

void Window::NavegaFoco(bool paraTras)
{
    std::vector<UIElement *> todos, pilha{this};
    // em ordem de arvore
    std::function<void(UIElement *)> anda = [&](UIElement *e) {
        if (e->Visibility != Vistas::Visibility::Visible || !e->IsEnabled) return;
        auto *c = dynamic_cast<Control *>(e);
        if (e->Focusable && (!c || c->IsTabStop)) todos.push_back(e);
        std::vector<UIElement *> f;
        e->Filhos(f);
        for (UIElement *x : f) anda(x);
    };
    anda(this);
    if (todos.empty()) return;
    UIElement *f = Foco();
    int i = -1;
    for (size_t k = 0; k < todos.size(); k++)
        if (todos[k] == f) i = (int) k;
    int n = (int) todos.size();
    int prox = i < 0 ? (paraTras ? n - 1 : 0) : ((i + (paraTras ? -1 : 1)) % n + n) % n;
    todos[(size_t) prox]->Focus();
    todos[(size_t) prox]->BringIntoView();
    if (auto *t = dynamic_cast<TextBox *>(todos[(size_t) prox])) t->SelectAll();
}

bool Window::TeclaPadrao(KeyEventArgs &k)
{
    if (k.Key == Key::Tab && !Tem(k.Modificadores, ModifierKeys::Control) && !Tem(k.Modificadores, ModifierKeys::Alt))
    {
        NavegaFoco(Tem(k.Modificadores, ModifierKeys::Shift));
        return true;
    }
    if (k.Key == Key::Enter || k.Key == Key::Escape)
    {
        // IsDefault / IsCancel
        Button *achado = nullptr;
        std::function<void(UIElement *)> anda = [&](UIElement *e) {
            if (achado || e->Visibility != Vistas::Visibility::Visible) return;
            if (auto *b = dynamic_cast<Button *>(e))
                if ((k.Key == Key::Enter ? b->IsDefault : b->IsCancel) && b->IsEnabledEfetivo()) achado = b;
            std::vector<UIElement *> f;
            e->Filhos(f);
            for (UIElement *x : f) anda(x);
        };
        anda(this);
        if (achado)
        {
            achado->PerformClick();
            return true;
        }
    }
    return false;
}

void Window::Tecla(void *xe, bool apertou)
{
    Estado &e = E();
    XKeyEvent &k = ((XEvent *) xe)->xkey;
    e.mods = ModsDe(k.state);
    char buf[64] = {0};
    KeySym ks = 0;
    int n = 0;
    static KeySym morta = 0;
    if (apertou && _xic)
    {
        int st = 0;
        n = Xutf8LookupString((XIC) _xic, &k, buf, sizeof buf - 1, &ks, &st);
        if (st == XLookupChars) ks = 0;
        if (st != XLookupChars && st != XLookupBoth) n = 0;
        if (ks == 0 && n == 0) ks = XLookupKeysym(&k, 0);
    }
    else
    {
        n = XLookupString(&k, buf, sizeof buf - 1, &ks, nullptr);
        if (apertou && !_xic)
        {
            // as teclas mortas sem o XIM
            if (ks >= XK_dead_grave && ks <= XK_dead_cedilla) { morta = ks; return; }
            if (morta)
            {
                std::string r = ComporMorta(morta, ks == XK_space ? ' ' : (uint32_t) (unsigned char) buf[0]);
                morta = 0;
                if (!r.empty()) { std::memcpy(buf, r.data(), r.size()); n = (int) r.size(); }
            }
            // XLookupString da latim-1; passa para UTF-8
            if (n == 1 && (unsigned char) buf[0] >= 0x80)
            {
                unsigned char c = (unsigned char) buf[0];
                buf[0] = (char) (0xC0 | (c >> 6));
                buf[1] = (char) (0x80 | (c & 0x3F));
                n = 2;
            }
        }
    }
    buf[std::max(0, n)] = 0;
    KeyEventArgs a;
    a.Key = MapeiaTecla(ks);
    a.KeySym = ks;
    a.Modificadores = ModsDe(k.state);
    if (apertou)
    {
        a.IsRepeat = e.teclasApertadas.count(k.keycode) > 0;
        e.teclasApertadas.insert(k.keycode);
    }
    else
        e.teclasApertadas.erase(k.keycode);
    UIElement *alvo = Foco() ? Foco() : this;
    auto rota = Rota(alvo);
    std::vector<std::shared_ptr<UIElement>> vivos;   // ver Window::Botao
    for (UIElement *x : rota)
        if (auto v = x->weak_from_this().lock()) vivos.push_back(v);
    a.OriginalSource = alvo;
    if (!apertou)
    {
        Desce(rota, a, [](UIElement *, KeyEventArgs &) {}, [](UIElement *x, KeyEventArgs &y) { x->PreviewKeyUp(y); });
        Sobe(rota, a, [](UIElement *x, KeyEventArgs &y) { x->OnKeyUp(y); }, [](UIElement *x, KeyEventArgs &y) { x->KeyUp(y); });
        _suja = true;
        return;
    }
    Desce(rota, a, [](UIElement *, KeyEventArgs &) {}, [](UIElement *x, KeyEventArgs &y) { x->PreviewKeyDown(y); });
    Sobe(rota, a, [](UIElement *x, KeyEventArgs &y) { x->OnKeyDown(y); }, [](UIElement *x, KeyEventArgs &y) { x->KeyDown(y); });
    if (!a.Handled) a.Handled = TeclaPadrao(a);
    // o texto (TextInput): so o que imprime, sem Ctrl e sem Alt
    bool controle = n <= 0 || (unsigned char) buf[0] < 0x20 || buf[0] == 0x7F;
    if (!a.Handled && !controle && !Tem(a.Modificadores, ModifierKeys::Control) && !Tem(a.Modificadores, ModifierKeys::Alt))
    {
        UIElement *alvoT = Foco() ? Foco() : this;
        auto rotaT = Rota(alvoT);
        TextCompositionEventArgs t;
        t.Text = std::string(buf, (size_t) n);
        t.OriginalSource = alvoT;
        Desce(rotaT, t, [](UIElement *, TextCompositionEventArgs &) {}, [](UIElement *x, TextCompositionEventArgs &y) { x->PreviewTextInput(y); });
        Sobe(rotaT, t, [](UIElement *x, TextCompositionEventArgs &y) { x->OnTextInput(y); }, [](UIElement *x, TextCompositionEventArgs &y) { x->TextInput(y); });
    }
    _suja = true;
    if (Foco() && dynamic_cast<TextBox *>(Foco())) ReiniciaPisca();
}

void Window::TrataEvento(void *xe)
{
    Estado &e = E();
    XEvent &ev = *(XEvent *) xe;
    Window *modal = Application::JanelaModalAtual();
    // com uma janela modal aberta, as outras janelas do programa nao recebem entrada
    bool bloqueada = modal && modal != this;
    switch (ev.type)
    {
    case Expose:
        if (ev.xexpose.count == 0) _suja = true;
        break;
    case ConfigureNotify:
        if (ev.xconfigure.width != _largura || ev.xconfigure.height != _altura)
        {
            _largura = ev.xconfigure.width;
            _altura = ev.xconfigure.height;
            FechaPopups();
            _suja = true;
        }
        break;
    case MapNotify:
        _mapeada = true;
        _suja = true;
        break;
    case UnmapNotify:
        _mapeada = false;
        break;
    case ClientMessage:
        if ((Atom) ev.xclient.data.l[0] == e.WM_DELETE)
        {
            // com uma janela modal aberta, a dona nao fecha (como no Windows)
            if (bloqueada)
            {
                if (modal) modal->Activate();
                break;
            }
            Close();
        }
        break;
    case FocusIn:
        if (ev.xfocus.mode == NotifyGrab || ev.xfocus.mode == NotifyUngrab) break;
        _ativa = true;
        if (_xic) XSetICFocus((XIC) _xic);
        {
            RoutedEventArgs a;
            a.Source = a.OriginalSource = this;
            Activated(a);
        }
        // a dona de uma modal manda o foco para a modal
        if (bloqueada && modal) modal->Activate();
        ReiniciaPisca();
        break;
    case FocusOut:
        if (ev.xfocus.mode == NotifyGrab || ev.xfocus.mode == NotifyUngrab) break;
        _ativa = false;
        if (_xic) XUnsetICFocus((XIC) _xic);
        FechaPopups();
        {
            RoutedEventArgs a;
            a.Source = a.OriginalSource = this;
            Deactivated(a);
        }
        _suja = true;
        break;
    case KeyPress:
    case KeyRelease:
        if (bloqueada) break;
        Tecla(&ev, ev.type == KeyPress);
        break;
    case ButtonPress:
    case ButtonRelease:
        if (bloqueada)
        {
            if (ev.type == ButtonPress && modal) modal->Activate();
            break;
        }
        Botao(&ev, ev.type == ButtonPress);
        break;
    case MotionNotify:
    {
        // so o ultimo movimento da fila
        XEvent ultimo = ev;
        while (XCheckTypedWindowEvent(e.d, (::Window) _xid, MotionNotify, &ultimo)) {}
        if (bloqueada) break;
        Movimento(Point(ultimo.xmotion.x, ultimo.xmotion.y), ultimo.xmotion.state);
        break;
    }
    case LeaveNotify:
        if (bloqueada || Captura()) break;
        if (ev.xcrossing.mode != NotifyNormal) break;
        _mouse = Point(-1, -1);
        AtualizaSobre(_mouse);
        AtualizaDica();
        break;
    default:
        break;
    }
}

// =====================================================================================
// Clipboard, Mouse, Keyboard
// =====================================================================================
namespace Clipboard {

void SetText(const std::string &texto)
{
    Estado &e = E();
    if (!e.d) return;
    e.area = texto;
    XSetSelectionOwner(e.d, e.CLIPBOARD, e.oculta, CurrentTime);
    e.donoArea = XGetSelectionOwner(e.d, e.CLIPBOARD) == e.oculta;
    XFlush(e.d);
}

std::string GetText()
{
    Estado &e = E();
    if (!e.d) return "";
    if (e.donoArea) return e.area;
    if (XGetSelectionOwner(e.d, e.CLIPBOARD) == (::Window) XNenhum) return "";
    XConvertSelection(e.d, e.CLIPBOARD, e.UTF8, e.PROPRIEDADE, e.oculta, CurrentTime);
    XFlush(e.d);
    // espera a resposta (ate 1 s) sem tirar os outros eventos da fila
    double ate = Agora() + 1.0;
    XEvent ev;
    while (Agora() < ate)
    {
        if (XCheckTypedWindowEvent(e.d, e.oculta, SelectionNotify, &ev))
        {
            if (ev.xselection.property == (Atom) XNenhum) return "";
            Atom tipo;
            int formato;
            unsigned long n, resto;
            unsigned char *dados = nullptr;
            std::string r;
            if (XGetWindowProperty(e.d, e.oculta, e.PROPRIEDADE, 0, 1 << 24, 1, AnyPropertyType, &tipo, &formato, &n, &resto, &dados) == Success && dados)
            {
                r.assign((const char *) dados, n);
                XFree(dados);
            }
            return r;
        }
        pollfd p{ConnectionNumber(e.d), POLLIN, 0};
        poll(&p, 1, 20);
        XEventsQueued(e.d, QueuedAfterReading);
    }
    return "";
}

bool ContainsText() { return !GetText().empty(); }

} // namespace Clipboard

namespace Mouse {

void SetOverrideCursor(Cursor c)
{
    E().sobreposto = c;
    for (Window *j : E().janelas) j->AtualizaCursor();
    if (E().d) XFlush(E().d);
}

Cursor OverrideCursor() { return E().sobreposto; }

Point PosicaoNaTela()
{
    Estado &e = E();
    if (!e.d) return Point();
    ::Window raiz, filho;
    int rx = 0, ry = 0, wx, wy;
    unsigned m;
    XQueryPointer(e.d, RootWindow(e.d, e.tela), &raiz, &filho, &rx, &ry, &wx, &wy, &m);
    return Point(rx, ry);
}

UIElement *DirectlyOver()
{
    for (Window *j : E().janelas)
        if (j->IsActive() && j->DiretamenteSobre()) return j->DiretamenteSobre();
    return nullptr;
}

UIElement *Captured()
{
    for (Window *j : E().janelas)
        if (j->Captura()) return j->Captura();
    return nullptr;
}

} // namespace Mouse

namespace Keyboard {

ModifierKeys Modifiers() { return E().mods; }

UIElement *FocusedElement()
{
    for (Window *j : E().janelas)
        if (j->IsActive()) return j->Foco();
    return nullptr;
}

bool Focus(UIElement *e) { return e && e->Focus(); }

} // namespace Keyboard

} // namespace ARCA::Vistas

// ARCA no xOS - Vistas/ControlesListas: Selector, ListBox, ComboBox e DataGrid, no visual do
// Tema.xaml (ItemAlbum, AbaInternaLista, ComboBoxItem, DataGridColumnHeader, DataGridCell...).
// Nao vem de um .cs do ARCA: e o WPF que o ARCA usava.
#include "Controles.h"
#include "Janela.h"

#include "../Base/Texto.h"

#ifndef NANOVG_GLES2
#define NANOVG_GLES2
#endif
#include "nanovg/nanovg.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace ARCA::Vistas {

namespace {
double Arred(double v) { return std::floor(v + 0.5); }
Thickness Soma(Thickness a, Thickness b) { return Thickness(a.Left + b.Left, a.Top + b.Top, a.Right + b.Right, a.Bottom + b.Bottom); }

// a comparacao da ordenacao: numeros como numeros; texto como a CompareInfo pt-BR (sem caixa e sem
// acento primeiro, depois a diferenca de acento e de caixa)
int ComparaTexto(const std::string &a, const std::string &b)
{
    double x, y;
    if (Base::TentaDouble(a, x) && Base::TentaDouble(b, y)) return x < y ? -1 : (x > y ? 1 : 0);
    int na = 0, nb = 0;
    if (Base::TentaInt(a, na) && Base::TentaInt(b, nb)) return na < nb ? -1 : (na > nb ? 1 : 0);
    std::string pa = Base::Minusculas(Base::SemAcentos(a)), pb = Base::Minusculas(Base::SemAcentos(b));
    int c = pa.compare(pb);
    if (c) return c < 0 ? -1 : 1;
    c = Base::Minusculas(a).compare(Base::Minusculas(b));
    if (c) return c < 0 ? -1 : 1;
    c = a.compare(b);
    return c < 0 ? 1 : (c > 0 ? -1 : 0);   // minuscula antes de maiuscula, como o Windows
}
} // namespace

// =====================================================================================
// Selector
// =====================================================================================
Selector::Selector() : SelectedIndex(-1, [this](const int &a, const int &n) { AoMudarSelecao(a, n); }) {}

void Selector::AoMudarSelecao(int antigo, int novo)
{
    SelectionChangedEventArgs e;
    e.Source = e.OriginalSource = this;
    if (antigo >= 0) e.RemovedItems.push_back(antigo);
    if (novo >= 0) e.AddedItems.push_back(novo);
    SelectionChanged(e);
    InvalidateVisual();
}

// =====================================================================================
// ListBoxItem
// =====================================================================================
Vistas::Gabarito ListBoxItem::GabaritoDoItem() const
{
    const Vistas::Style *s = EstiloEfetivo();
    if (Template) return *Template;
    if (s && s->Template) return *s->Template;
    return Vistas::Gabarito::ListBoxItem;
}

Thickness ListBoxItem::MolduraDoConteudo() const
{
    const GabaritoBorda &g = Tema::Modelo(GabaritoDoItem());
    return Soma(g.Espessura, g.RecuoDoControle ? PaddingEfetivo() : g.Recuo);
}

Size ListBoxItem::MeasureOverride(Size d) { return ContentControl::MeasureOverride(d); }

Size ListBoxItem::ArrangeOverride(Size f)
{
    const GabaritoBorda &g = Tema::Modelo(GabaritoDoItem());
    Thickness m = MolduraDoConteudo();
    Rect in(Limites.X + m.Left, Limites.Y + m.Top, std::max(0.0, f.Width - m.Horizontal()), std::max(0.0, f.Height - m.Vertical()));
    auto e = Content.Elemento();
    if (e)
    {
        double w = in.Width, h = in.Height, x = in.X, y = in.Y;
        if (g.Conteudo != Vistas::HorizontalAlignment::Stretch)
        {
            w = std::min(e->DesiredSize.Width, in.Width);
            if (g.Conteudo == Vistas::HorizontalAlignment::Center) x += (in.Width - w) / 2;
            else if (g.Conteudo == Vistas::HorizontalAlignment::Right) x += in.Width - w;
        }
        if (g.ConteudoV == Vistas::VerticalAlignment::Center)
        {
            h = std::min(e->DesiredSize.Height, in.Height);
            y += (in.Height - h) / 2;
        }
        e->Arrange(Rect(x, y, w, h));
    }
    return f;
}

std::optional<Brush> ListBoxItem::ForegroundDoGabarito() const
{
    const GabaritoBorda &g = Tema::Modelo(GabaritoDoItem());
    if (IsSelected && !g.TextoMarcado.Nulo()) return g.TextoMarcado;
    return std::nullopt;
}

void ListBoxItem::OnRender(DrawingContext &dc)
{
    DesenhaGabarito(dc, Limites, Tema::Modelo(GabaritoDoItem()), IsMouseOver(), false, IsSelected, IsEnabledEfetivo(),
                    Background, BorderBrush);
}

// =====================================================================================
// ListBox
// =====================================================================================
ListBox::ListBox()
{
    // o ListBox do Aero: fundo da cor de janela, borda cinza (#828790) de 1 px
    Background = Brush(Tema::SistemaJanela);
    BorderBrush = Brush(Color::De("#FF828790"));
    BorderThickness = Thickness(1);
}

ListBox::~ListBox()
{
    for (auto &c : _containers)
        if (c.second->Parent == this) c.second->Parent = nullptr;
    for (auto &e : Items)
        if (e->Parent == this) e->Parent = nullptr;
}

void ListBox::AddItem(const std::string &texto)
{
    auto t = std::make_shared<TextBlock>(texto);
    AddItem(std::static_pointer_cast<UIElement>(t));
}

void *ListBox::ItemFonte(int i) const
{
    if (!ItemsSource().Vazia()) return i >= 0 && (size_t) i < ItemsSource().Count() ? ItemsSource().Item((size_t) i) : nullptr;
    return i >= 0 && (size_t) i < Items.size() ? Items[(size_t) i].get() : nullptr;
}

void ListBox::FazVista()
{
    int selFonte = IndiceNaFonte(SelectedIndex());
    _vista.clear();
    size_t n = !ItemsSource().Vazia() ? ItemsSource().Count() : Items.size();
    for (size_t i = 0; i < n; i++)
        if (!Filter || Filter(ItemFonte((int) i))) _vista.push_back((int) i);
    _vistaFeita = true;
    for (auto &c : _containers) UIElement::Solta(c.second.get());
    _containers.clear();
    _realizados.clear();
    _multi.clear();
    int nova = -1;
    if (selFonte >= 0)
        for (size_t i = 0; i < _vista.size(); i++)
            if (_vista[i] == selFonte) nova = (int) i;
    if (nova != SelectedIndex()) SelectedIndex = nova;
    InvalidateVisual();
}

void ListBox::Refresh()
{
    _vistaFeita = false;
    // a selecao fica no mesmo item: a vista velha ainda vale aqui
    FazVista();
}

void *ListBox::ItemDaVista(int i) const { return i >= 0 && i < (int) _vista.size() ? ItemFonte(_vista[i]) : nullptr; }

int ListBox::IndiceNaVista(int iFonte) const
{
    for (size_t i = 0; i < _vista.size(); i++)
        if (_vista[i] == iFonte) return (int) i;
    return -1;
}

std::vector<int> ListBox::SelectedIndices() const
{
    if (SelectionMode == Vistas::SelectionMode::Single) return SelectedIndex() >= 0 ? std::vector<int>{SelectedIndex()} : std::vector<int>{};
    std::vector<int> r = _multi;
    std::sort(r.begin(), r.end());
    return r;
}

bool ListBox::Virtual() const { return IsVirtualizing && ItemsPanelOrientation == Vistas::Orientation::Vertical; }

std::shared_ptr<ListBoxItem> ListBox::Container(int iv)
{
    auto it = _containers.find(iv);
    if (it != _containers.end()) return it->second;
    int f = _vista[(size_t) iv];
    std::shared_ptr<ListBoxItem> c;
    if (ItemsSource().Vazia())
    {
        UIElementPtr e = Items[(size_t) f];
        if (auto li = std::dynamic_pointer_cast<ListBoxItem>(e)) c = li;
        else
        {
            c = std::make_shared<ListBoxItem>();
            if (e->Parent == this) e->Parent = nullptr;
            c->Content = e;
        }
    }
    else
    {
        c = std::make_shared<ListBoxItem>();
        void *item = ItemsSource().Item((size_t) f);
        if (ItemTemplate) c->Content = ItemTemplate(item, f);
        else c->Content = TextoDoItem ? TextoDoItem(item) : std::string();
        if (PreparaContainer) PreparaContainer(*c, item);
    }
    if (!c->Style) c->Style = ItemContainerStyle;
    c->Indice = iv;
    UIElement::Adota(this, c.get());
    _containers[iv] = c;
    return c;
}

ListBoxItem *ListBox::ContainerFromIndex(int i) const
{
    auto it = _containers.find(i);
    return it != _containers.end() ? it->second.get() : nullptr;
}

ListBoxItem *ListBox::ContainerFromElement(const ListBox *lista, const UIElement *e)
{
    for (; e; e = e->Parent)
        if (auto *li = dynamic_cast<const ListBoxItem *>(e))
            if (li->Parent == lista) return const_cast<ListBoxItem *>(li);
    return nullptr;
}

void ListBox::Filhos(std::vector<UIElement *> &s) const
{
    for (auto *c : _realizados) s.push_back(c);
}

Size ListBox::MeasureOverride(Size d)
{
    if (!_vistaFeita) FazVista();
    Thickness m = Soma(BorderThicknessEfetiva(), PaddingEfetivo());
    double w = std::max(0.0, d.Width - m.Horizontal()), h = std::max(0.0, d.Height - m.Vertical());
    int n = (int) _vista.size();
    if (ItemsPanelOrientation == Vistas::Orientation::Horizontal)
    {
        double tw = 0, th = 0;
        for (int i = 0; i < n; i++)
        {
            auto c = Container(i);
            c->Measure(Size(Infinito, h));
            tw += c->DesiredSize.Width;
            th = std::max(th, c->DesiredSize.Height);
        }
        return Size(tw + m.Horizontal(), th + m.Vertical());
    }
    double larguraItem = w - (VerticalScrollBarVisibility == Vistas::ScrollBarVisibility::Visible ? Tema::BarraLargura : 0);
    if (Virtual())
    {
        _alturaItem = 0;
        double mw = 0;
        if (n > 0)
        {
            auto c = Container(0);
            c->Measure(Size(std::isfinite(larguraItem) ? larguraItem : Infinito, Infinito));
            _alturaItem = std::max(1.0, c->DesiredSize.Height);
            mw = c->DesiredSize.Width;
        }
        _extensao = _alturaItem * n;
        return Size(std::isfinite(w) ? std::min(w, mw) + m.Horizontal() : mw + m.Horizontal(),
                    (std::isfinite(h) ? std::min(h, _extensao) : _extensao) + m.Vertical());
    }
    double tw = 0, th = 0;
    for (int i = 0; i < n; i++)
    {
        auto c = Container(i);
        c->Measure(Size(std::isfinite(larguraItem) ? larguraItem : Infinito, Infinito));
        tw = std::max(tw, c->DesiredSize.Width);
        th += c->DesiredSize.Height;
    }
    _extensao = th;
    return Size(tw + m.Horizontal(), (std::isfinite(h) ? std::min(h, th) : th) + m.Vertical());
}

Size ListBox::ArrangeOverride(Size f)
{
    if (!_vistaFeita) FazVista();
    Thickness m = Soma(BorderThicknessEfetiva(), PaddingEfetivo());
    Rect in(Limites.X + m.Left, Limites.Y + m.Top, std::max(0.0, f.Width - m.Horizontal()), std::max(0.0, f.Height - m.Vertical()));
    _realizados.clear();
    int n = (int) _vista.size();
    auto selecionado = [&](int i) {
        if (SelectionMode == Vistas::SelectionMode::Single) return i == SelectedIndex();
        return std::find(_multi.begin(), _multi.end(), i) != _multi.end();
    };
    if (ItemsPanelOrientation == Vistas::Orientation::Horizontal)
    {
        double x = in.X;
        for (int i = 0; i < n; i++)
        {
            auto c = Container(i);
            c->IsSelected = selecionado(i);
            c->Arrange(Rect(x, in.Y, c->DesiredSize.Width, in.Height));
            x += c->DesiredSize.Width;
            _realizados.push_back(c.get());
        }
        _temBarra = false;
        return f;
    }
    _alturaVisivel = in.Height;
    _temBarra = VerticalScrollBarVisibility == Vistas::ScrollBarVisibility::Visible ||
                (VerticalScrollBarVisibility == Vistas::ScrollBarVisibility::Auto && _extensao > in.Height + 0.5);
    double larg = in.Width - (_temBarra ? Tema::BarraLargura : 0);
    double maxRol = std::max(0.0, _extensao - in.Height);
    if (Virtual() && _alturaItem > 0)
    {
        // rolagem por item (CanContentScroll): o ultimo item fica inteiro embaixo
        double itensVisiveis = std::floor(in.Height / _alturaItem);
        maxRol = std::max(0.0, (n - itensVisiveis) * _alturaItem);
    }
    _rolagem = std::clamp(_rolagem, 0.0, maxRol);
    if (Virtual())
    {
        int primeiro = _alturaItem > 0 ? (int) (_rolagem / _alturaItem) : 0;
        int ultimo = _alturaItem > 0 ? std::min(n - 1, (int) ((_rolagem + in.Height) / _alturaItem)) : -1;
        // so os visiveis ficam vivos (VirtualizingStackPanel, Recycling)
        for (auto it = _containers.begin(); it != _containers.end();)
        {
            if (it->first < primeiro - 2 || it->first > ultimo + 2)
            {
                UIElement::Solta(it->second.get());
                it = _containers.erase(it);
            }
            else ++it;
        }
        for (int i = primeiro; i <= ultimo; i++)
        {
            auto c = Container(i);
            c->Measure(Size(larg, Infinito));
            c->IsSelected = selecionado(i);
            c->Arrange(Rect(in.X, in.Y + i * _alturaItem - _rolagem, larg, _alturaItem));
            _realizados.push_back(c.get());
        }
    }
    else
    {
        double y = in.Y - _rolagem;
        for (int i = 0; i < n; i++)
        {
            auto c = Container(i);
            c->IsSelected = selecionado(i);
            double h = c->DesiredSize.Height;
            c->Arrange(Rect(in.X, y, larg, h));
            if (y + h >= in.Y && y <= in.Bottom()) _realizados.push_back(c.get());
            y += h;
        }
    }
    if (_mostrarDepois >= 0 && _alturaVisivel > 0)
    {
        int i = _mostrarDepois;
        _mostrarDepois = -1;
        ScrollIntoView(i);
    }
    _barra.Area = Rect(in.X + larg, in.Y, Tema::BarraLargura, in.Height);
    _barra.Maximo = maxRol;
    _barra.Visivel = in.Height;
    _barra.Valor = _rolagem;
    return f;
}

void ListBox::OnRender(DrawingContext &dc)
{
    dc.Retangulo(Limites, BackgroundEfetivo(), BorderBrushEfetivo(), BorderThicknessEfetiva());
    Thickness m = Soma(BorderThicknessEfetiva(), PaddingEfetivo());
    dc.Recorta(Rect(Limites.X + m.Left, Limites.Y + m.Top, std::max(0.0, Limites.Width - m.Horizontal()),
                    std::max(0.0, Limites.Height - m.Vertical())));
}

void ListBox::OnRenderDepois(DrawingContext &dc)
{
    dc.Restaura();
    if (_temBarra) _barra.Desenha(dc);
}

int ListBox::IndiceEm(Point p) const
{
    for (auto *c : _realizados)
        if (c->Limites.Contains(p)) return c->Indice;
    return -1;
}

void ListBox::Escolhe(int i, ModifierKeys m)
{
    if (i < 0 || i >= (int) _vista.size()) return;
    if (SelectionMode == Vistas::SelectionMode::Single)
    {
        SelectedIndex = i;
        return;
    }
    std::vector<int> antes = _multi;
    if (SelectionMode == Vistas::SelectionMode::Multiple || Tem(m, ModifierKeys::Control))
    {
        auto it = std::find(_multi.begin(), _multi.end(), i);
        if (it != _multi.end()) _multi.erase(it);
        else _multi.push_back(i);
    }
    else if (Tem(m, ModifierKeys::Shift) && SelectedIndex() >= 0)
    {
        _multi.clear();
        int a = std::min(SelectedIndex(), i), b = std::max(SelectedIndex(), i);
        for (int k = a; k <= b; k++) _multi.push_back(k);
    }
    else
        _multi = {i};
    int primeiro = _multi.empty() ? -1 : *std::min_element(_multi.begin(), _multi.end());
    if (Tem(m, ModifierKeys::Shift)) primeiro = SelectedIndex();
    if (_multi != antes || primeiro != SelectedIndex())
    {
        SelectedIndex.Define(primeiro);
        SelectionChangedEventArgs e;
        e.Source = e.OriginalSource = this;
        for (int k : _multi)
            if (std::find(antes.begin(), antes.end(), k) == antes.end()) e.AddedItems.push_back(k);
        for (int k : antes)
            if (std::find(_multi.begin(), _multi.end(), k) == _multi.end()) e.RemovedItems.push_back(k);
        SelectionChanged(e);
        InvalidateVisual();
    }
}

void ListBox::AoMudarSelecao(int antigo, int novo)
{
    if (SelectionMode != Vistas::SelectionMode::Single)
    {
        _multi.clear();
        if (novo >= 0) _multi.push_back(novo);
    }
    Selector::AoMudarSelecao(antigo, novo);
}

void ListBox::OnMouseDown(MouseButtonEventArgs &e)
{
    if (e.ChangedButton != MouseButton::Left) return;
    if (_temBarra && _barra.Aperta(e.Posicao))
    {
        _rolagem = _alturaItem > 0 && Virtual() ? std::round(_barra.Valor / _alturaItem) * _alturaItem : _barra.Valor;
        CaptureMouse();
        e.Handled = true;
        InvalidateVisual();
        return;
    }
    Focus();
    ListBoxItem *c = ContainerFromElement(this, e.OriginalSource);
    int i = c ? c->Indice : IndiceEm(e.Posicao);
    if (i >= 0)
    {
        Escolhe(i, Keyboard::Modifiers());
        e.Handled = true;
    }
}

void ListBox::OnMouseMove(MouseEventArgs &e)
{
    bool antes = _barra.Sobre;
    _barra.Move(e.Posicao);
    if (_barra.Arrastando)
    {
        _rolagem = _alturaItem > 0 && Virtual() ? std::round(_barra.Valor / _alturaItem) * _alturaItem : _barra.Valor;
        InvalidateVisual();
    }
    if (antes != _barra.Sobre) InvalidateVisual();
}

void ListBox::OnMouseUp(MouseButtonEventArgs &e)
{
    (void) e;
    if (_barra.Arrastando || _barra._repetindo)
    {
        _barra.Solta();
        ReleaseMouseCapture();
        InvalidateVisual();
    }
}

void ListBox::OnMouseLeave(MouseEventArgs &e)
{
    (void) e;
    if (_barra.Sobre) _barra.Sobre = false, InvalidateVisual();
}

void ListBox::OnMouseWheel(MouseWheelEventArgs &e)
{
    if (ItemsPanelOrientation == Vistas::Orientation::Horizontal || !_temBarra) return;
    // CanContentScroll: tres itens por degrau
    double passo = Virtual() && _alturaItem > 0 ? 3 * _alturaItem : 48;
    double antes = _rolagem;
    _rolagem = std::clamp(_rolagem - e.Delta / 120.0 * passo, 0.0, _barra.Maximo);
    if (_rolagem != antes) e.Handled = true;
    InvalidateVisual();
}

void ListBox::ScrollIntoView(int i)
{
    if (i < 0 || i >= (int) _vista.size() || ItemsPanelOrientation == Vistas::Orientation::Horizontal) return;
    if (_alturaVisivel <= 0)
    {
        _mostrarDepois = i;
        return;
    }
    if (Virtual() && _alturaItem > 0)
    {
        double y = i * _alturaItem;
        if (y < _rolagem) _rolagem = y;
        else if (y + _alturaItem > _rolagem + _alturaVisivel) _rolagem = y + _alturaItem - std::floor(_alturaVisivel / _alturaItem) * _alturaItem;
    }
    else if (ListBoxItem *c = ContainerFromIndex(i))
    {
        Thickness m = Soma(BorderThicknessEfetiva(), PaddingEfetivo());
        double topo = Limites.Y + m.Top;
        if (c->Limites.Y < topo) _rolagem -= topo - c->Limites.Y;
        else if (c->Limites.Bottom() > topo + _alturaVisivel) _rolagem += c->Limites.Bottom() - (topo + _alturaVisivel);
    }
    _rolagem = std::max(0.0, _rolagem);
    InvalidateVisual();
}

void ListBox::OnKeyDown(KeyEventArgs &e)
{
    int n = (int) _vista.size();
    if (n == 0) return;
    bool h = ItemsPanelOrientation == Vistas::Orientation::Horizontal;
    int i = SelectedIndex();
    int pagina = _alturaItem > 0 ? std::max(1, (int) (_alturaVisivel / _alturaItem) - 1) : 10;
    int novo = i;
    switch (e.Key)
    {
    case Key::Up: if (!h) novo = std::max(0, i - 1); break;
    case Key::Down: if (!h) novo = std::min(n - 1, i + 1); break;
    case Key::Left: if (h) novo = std::max(0, i - 1); break;
    case Key::Right: if (h) novo = std::min(n - 1, i + 1); break;
    case Key::Home: novo = 0; break;
    case Key::End: novo = n - 1; break;
    case Key::PageUp: novo = std::max(0, i - pagina); break;
    case Key::PageDown: novo = std::min(n - 1, i + pagina); break;
    default: return;
    }
    if (novo != i || i < 0)
    {
        Escolhe(std::max(0, novo), e.Modificadores);
        ScrollIntoView(std::max(0, novo));
    }
    e.Handled = true;
}

// =====================================================================================
// ComboBox
// =====================================================================================
void ComboBox::Lista::Add(const std::string &s)
{
    _v.push_back(s);
    if (_dono->Tags.size() < _v.size()) _dono->Tags.resize(_v.size());
    _dono->InvalidateVisual();
}

void ComboBox::Lista::Insert(size_t i, const std::string &s)
{
    i = std::min(i, _v.size());
    _v.insert(_v.begin() + (std::ptrdiff_t) i, s);
    if (_dono->Tags.size() < _v.size() - 1) _dono->Tags.resize(_v.size() - 1);
    _dono->Tags.insert(_dono->Tags.begin() + (std::ptrdiff_t) i, std::any());
    if (_dono->SelectedIndex() >= (int) i) _dono->SelectedIndex.Define(_dono->SelectedIndex() + 1);
    _dono->InvalidateVisual();
}

void ComboBox::Lista::RemoveAt(size_t i)
{
    if (i >= _v.size()) return;
    _v.erase(_v.begin() + (std::ptrdiff_t) i);
    if (i < _dono->Tags.size()) _dono->Tags.erase(_dono->Tags.begin() + (std::ptrdiff_t) i);
    int s = _dono->SelectedIndex();
    if (s == (int) i) _dono->SelectedIndex = -1;
    else if (s > (int) i) _dono->SelectedIndex.Define(s - 1);
    _dono->InvalidateVisual();
}

void ComboBox::Lista::Clear()
{
    _v.clear();
    _dono->Tags.clear();
    _dono->SelectedIndex = -1;
    _dono->InvalidateVisual();
}

int ComboBox::Lista::IndexOf(const std::string &s) const
{
    for (size_t i = 0; i < _v.size(); i++)
        if (_v[i] == s) return (int) i;
    return -1;
}

// a lista suspensa: o Popup do gabarito, com a borda RodapeBorda e os ComboBoxItem do Tema
class ListaSuspensa : public UIElement
{
public:
    explicit ListaSuspensa(ComboBox *c) : _c(c) {}
    ComboBox *_c;
    int Realce = -1;
    double Rolagem = 0, AlturaItem = 0;
    BarraDeRolagem Barra;
    bool TemBarra = false;

    const char *Tipo() const override { return "ComboBoxPopup"; }
    double Altura() const { return AlturaItem * _c->Items.Count(); }
    Size MeasureOverride(Size d) override
    {
        (void) d;
        FonteTexto f = _c->Fonte();
        Thickness r = Tema::ComboItemRecuo;
        AlturaItem = Texto::AlturaLinha(f) + r.Vertical();
        double w = 0;
        for (auto &s : _c->Items) w = std::max(w, Texto::Largura(f, s));
        return Size(w + r.Horizontal() + 2, Altura() + 2);
    }
    bool AcertaProprio(Point p) const override { (void) p; return true; }
    Rect Corpo() const { return Rect(Limites.X + 1, Limites.Y + 1, std::max(0.0, Limites.Width - 2), std::max(0.0, Limites.Height - 2)); }
    void OnRender(DrawingContext &dc) override
    {
        dc.Retangulo(Limites, Tema::Recurso("PainelLateral"), Tema::Recurso("RodapeBorda"), Thickness(1));
        Rect c = Corpo();
        TemBarra = Altura() > c.Height + 0.5;
        double larg = c.Width - (TemBarra ? Tema::BarraLargura : 0);
        Rolagem = std::clamp(Rolagem, 0.0, std::max(0.0, Altura() - c.Height));
        dc.Recorta(c);
        FonteTexto f = _c->Fonte();
        const GabaritoBorda &g = Tema::Modelo(Vistas::Gabarito::ComboBoxItem);
        Brush texto = _c->ForegroundEfetivo();
        for (size_t i = 0; i < _c->Items.Count(); i++)
        {
            Rect r(c.X, c.Y + i * AlturaItem - Rolagem, larg, AlturaItem);
            if (r.Bottom() < c.Y || r.Y > c.Bottom()) continue;
            bool sel = (int) i == _c->SelectedIndex();
            DesenhaGabarito(dc, r, g, (int) i == Realce, false, false, true);
            dc.Texto(Texto::ComReticencias(f, _c->Items[i], larg - g.Recuo.Horizontal()), f, r.X + g.Recuo.Left,
                     r.Y + g.Recuo.Top, sel ? g.TextoMarcado : texto);
        }
        dc.Restaura();
        if (TemBarra)
        {
            Barra.Area = Rect(c.X + larg, c.Y, Tema::BarraLargura, c.Height);
            Barra.Maximo = std::max(0.0, Altura() - c.Height);
            Barra.Visivel = c.Height;
            Barra.Valor = Rolagem;
            Barra.Desenha(dc);
        }
    }
    int IndiceEm(Point p) const
    {
        Rect c = Corpo();
        if (!c.Contains(p) || AlturaItem <= 0) return -1;
        int i = (int) ((p.Y - c.Y + Rolagem) / AlturaItem);
        return i >= 0 && i < (int) _c->Items.Count() ? i : -1;
    }
    void OnMouseMove(MouseEventArgs &e) override
    {
        Barra.Move(e.Posicao);
        if (Barra.Arrastando) { Rolagem = Barra.Valor; InvalidateVisual(); return; }
        int i = IndiceEm(e.Posicao);
        if (i >= 0 && i != Realce) Realce = i, InvalidateVisual();
    }
    void OnMouseDown(MouseButtonEventArgs &e) override
    {
        e.Handled = true;
        if (TemBarra && Barra.Aperta(e.Posicao)) { Rolagem = Barra.Valor; CaptureMouse(); InvalidateVisual(); }
    }
    void OnMouseUp(MouseButtonEventArgs &e) override
    {
        e.Handled = true;
        if (Barra.Arrastando || Barra._repetindo) { Barra.Solta(); ReleaseMouseCapture(); return; }
        int i = IndiceEm(e.Posicao);
        if (i >= 0)
        {
            ComboBox *c = _c;
            c->SetIsDropDownOpen(false);
            c->SelectedIndex = i;
        }
    }
    void OnMouseWheel(MouseWheelEventArgs &e) override
    {
        Rolagem = std::clamp(Rolagem - e.Delta / 120.0 * 3 * AlturaItem, 0.0, std::max(0.0, Altura() - Corpo().Height));
        e.Handled = true;
        InvalidateVisual();
    }
    void Mostra(int i)
    {
        Rect c = Corpo();
        if (i < 0) return;
        double y = i * AlturaItem;
        if (y < Rolagem) Rolagem = y;
        else if (y + AlturaItem > Rolagem + c.Height) Rolagem = y + AlturaItem - c.Height;
    }
};

ComboBox::ComboBox() : Items(this) {}

ComboBox::~ComboBox()
{
    if (_aberta)
        if (Window *j = Janela()) j->FechaPopup(_popup.get());
}

std::string ComboBox::SelectedItem() const { return TemSelecao() ? Items[(size_t) SelectedIndex()] : std::string(); }

Size ComboBox::MeasureOverride(Size d)
{
    (void) d;
    FonteTexto f = Fonte();
    Thickness p = PaddingEfetivo();
    double w = 0;
    for (auto &s : Items) w = std::max(w, Texto::Largura(f, s));
    return Size(std::ceil(w) + p.Horizontal() + 2, Texto::AlturaLinha(f) + p.Vertical() + 2);
}

void ComboBox::OnRender(DrawingContext &dc)
{
    // o ToggleButton do gabarito: AbaTarefa, borda Borda, cantos 3; mouse por cima ou aberta acende a borda
    bool sobre = IsMouseOver();
    Brush fundo = sobre ? Brush(Color::De("#FF2C2214")) : BackgroundEfetivo();
    Brush borda = (sobre || _aberta) ? Tema::Recurso("RodapeBorda") : BorderBrushEfetivo();
    dc.Retangulo(Limites, fundo, borda, Thickness(1), Vistas::CornerRadius(3));
    // a seta: Path Data="M0,0 L4,4 L8,0 Z", Margin="0,0,9,0", TextoFraco
    double ax = Limites.Right() - 9 - 8, ay = Arred(Limites.Y + (Limites.Height - 4) / 2);
    nvgBeginPath(dc.vg);
    nvgMoveTo(dc.vg, (float) ax, (float) ay);
    nvgLineTo(dc.vg, (float) (ax + 4), (float) (ay + 4));
    nvgLineTo(dc.vg, (float) (ax + 8), (float) ay);
    nvgClosePath(dc.vg);
    Color c = Tema::Recurso("TextoFraco").Cor();
    nvgFillColor(dc.vg, nvgRGBA(c.R, c.G, c.B, c.A));
    nvgFill(dc.vg);
    if (TemSelecao())
    {
        FonteTexto f = Fonte();
        Thickness p = PaddingEfetivo();
        double larg = Limites.Width - p.Horizontal();
        Brush cor = IsEnabledEfetivo() ? ForegroundEfetivo() : Brush(Color::De("#FF5A4A32"));
        double y = Limites.Y + (Limites.Height - Texto::AlturaLinha(f)) / 2;
        dc.Recorta(Rect(Limites.X + p.Left, Limites.Y, std::max(0.0, larg), Limites.Height));
        dc.Texto(SelectedItem(), f, Limites.X + p.Left, y, cor);
        dc.Restaura();
    }
}

void ComboBox::SetIsDropDownOpen(bool abrir)
{
    if (abrir == _aberta) return;
    Window *j = Janela();
    if (!j) return;
    if (abrir)
    {
        auto lista = std::make_shared<ListaSuspensa>(this);
        _popup = lista;
        lista->Parent = this;     // herda fonte e cor (o Parent logico do Popup)
        lista->Measure(Size(Infinito, Infinito));
        double w = std::max(Limites.Width, lista->DesiredSize.Width);
        double h = std::min(lista->DesiredSize.Height, MaxDropDownHeight);
        Size tela = j->TamanhoCliente();
        double y = Limites.Bottom();
        if (y + h > tela.Height && Limites.Y - h >= 0) y = Limites.Y - h;   // sem lugar embaixo: abre para cima
        double x = std::min(Limites.X, std::max(0.0, tela.Width - w));
        lista->Realce = SelectedIndex();
        _aberta = true;
        j->AbrePopup(_popup, Rect(x, y, w, h), this, [this] {
            _aberta = false;
            _popup.reset();
            RoutedEventArgs a;
            a.Source = a.OriginalSource = this;
            DropDownClosed(a);
            InvalidateVisual();
        });
        lista->Arrange(Rect(x, y, w, h));
        lista->Mostra(SelectedIndex());
        RoutedEventArgs a;
        a.Source = a.OriginalSource = this;
        DropDownOpened(a);
    }
    else
        j->FechaPopup(_popup.get());   // o aoFechar acima faz o resto
    InvalidateVisual();
}

void ComboBox::OnMouseDown(MouseButtonEventArgs &e)
{
    if (e.ChangedButton != MouseButton::Left) return;
    Focus();
    e.Handled = true;
    SetIsDropDownOpen(!_aberta);   // ClickMode="Press"
}

void ComboBox::OnKeyDown(KeyEventArgs &e)
{
    int n = (int) Items.Count();
    auto *lista = dynamic_cast<ListaSuspensa *>(_popup.get());
    if (_aberta && lista)
    {
        switch (e.Key)
        {
        case Key::Up: lista->Realce = std::max(0, lista->Realce - 1); lista->Mostra(lista->Realce); e.Handled = true; break;
        case Key::Down: lista->Realce = std::min(n - 1, lista->Realce + 1); lista->Mostra(lista->Realce); e.Handled = true; break;
        case Key::Home: lista->Realce = 0; lista->Mostra(0); e.Handled = true; break;
        case Key::End: lista->Realce = n - 1; lista->Mostra(n - 1); e.Handled = true; break;
        case Key::Enter:
        {
            int r = lista->Realce;
            SetIsDropDownOpen(false);
            if (r >= 0) SelectedIndex = r;
            e.Handled = true;
            break;
        }
        case Key::Escape: case Key::F4: SetIsDropDownOpen(false); e.Handled = true; break;
        default: break;
        }
        InvalidateVisual();
        return;
    }
    bool alt = Tem(e.Modificadores, ModifierKeys::Alt);
    switch (e.Key)
    {
    case Key::F4: SetIsDropDownOpen(true); e.Handled = true; break;
    case Key::Down:
        if (alt) SetIsDropDownOpen(true);
        else if (n > 0) SelectedIndex = std::min(n - 1, SelectedIndex() + 1);
        e.Handled = true;
        break;
    case Key::Up: if (n > 0) SelectedIndex = std::max(0, SelectedIndex() - 1); e.Handled = true; break;
    case Key::Home: if (n > 0) SelectedIndex = 0; e.Handled = true; break;
    case Key::End: if (n > 0) SelectedIndex = n - 1; e.Handled = true; break;
    default: break;
    }
}

void ComboBox::OnMouseWheel(MouseWheelEventArgs &e)
{
    // o ComboBox do WPF so troca com a roda quando tem o foco
    if (!IsKeyboardFocused() || _aberta) return;
    int n = (int) Items.Count();
    if (n == 0) return;
    SelectedIndex = std::clamp(SelectedIndex() + (e.Delta > 0 ? -1 : 1), 0, n - 1);
    e.Handled = true;
}

void ComboBox::OnLostFocus() { SetIsDropDownOpen(false); }

// =====================================================================================
// DataGrid
// =====================================================================================
DataGrid::DataGrid()
{
    // os padroes do DataGrid do WPF: linhas de grade pretas, borda de 1
    HorizontalGridLinesBrush = Brushes::Black;
    BorderThickness = Thickness(1);
}

DataGrid::~DataGrid()
{
    if (_editor && _editor->Parent == this) _editor->Parent = nullptr;
}

void *DataGrid::ItemDaVista(int i) const
{
    return i >= 0 && i < (int) _vista.size() && !ItemsSource().Vazia() && (size_t) _vista[i] < ItemsSource().Count()
               ? ItemsSource().Item((size_t) _vista[i])
               : nullptr;
}

int DataGrid::IndiceNaVista(int iFonte) const
{
    for (size_t i = 0; i < _vista.size(); i++)
        if (_vista[i] == iFonte) return (int) i;
    return -1;
}

std::vector<int> DataGrid::SelectedIndices() const
{
    std::vector<int> r;
    for (size_t i = 0; i < _sel.size(); i++)
        if (_sel[i]) r.push_back((int) i);
    return r;
}

void DataGrid::FazVista()
{
    if (_editor) CancelEdit();
    // a selecao e lembrada pelos itens (indices na fonte)
    std::set<int> antes;
    int primeiraFonte = IndiceNaFonte(SelectedIndex());
    for (size_t i = 0; i < _sel.size() && i < _vista.size(); i++)
        if (_sel[i]) antes.insert(_vista[i]);
    std::vector<char> selAntes = _sel;
    std::vector<int> vistaAntes = _vista;
    _vista.clear();
    size_t n = ItemsSource().Count();
    for (size_t i = 0; i < n; i++)
        if (!Filter || Filter(ItemsSource().Item(i))) _vista.push_back((int) i);
    _vistaFeita = true;
    Ordena();
    _sel.assign(_vista.size(), 0);
    int nova = -1;
    for (size_t i = 0; i < _vista.size(); i++)
        if (antes.count(_vista[i]))
        {
            _sel[i] = 1;
            if (_vista[i] == primeiraFonte || nova < 0) nova = (int) i;
        }
    if (primeiraFonte >= 0)
        for (size_t i = 0; i < _vista.size(); i++)
            if (_vista[i] == primeiraFonte) nova = (int) i;
    _linhaAtual = std::min(_linhaAtual, (int) _vista.size() - 1);
    // selecao que sumiu da lista: avisa (o WPF dispara SelectionChanged com os removidos)
    bool mudou = antes.size() != (size_t) std::count(_sel.begin(), _sel.end(), 1) || nova != SelectedIndex();
    SelectedIndex.Define(nova);
    if (mudou)
    {
        SelectionChangedEventArgs e;
        e.Source = e.OriginalSource = this;
        for (size_t i = 0; i < selAntes.size() && i < vistaAntes.size(); i++)
            if (selAntes[i] && IndiceNaVista(vistaAntes[i]) < 0) e.RemovedItems.push_back((int) i);
        SelectionChanged(e);
    }
    InvalidateVisual();
}

void DataGrid::Refresh() { FazVista(); }

void DataGrid::Ordena()
{
    if (_colOrdenada < 0 || _colOrdenada >= (int) Columns.size()) return;
    auto &c = Columns[(size_t) _colOrdenada];
    if (!c->SortDirection) return;
    bool desc = *c->SortDirection == ListSortDirection::Descending;
    const FonteDeItens &f = ItemsSource();
    std::stable_sort(_vista.begin(), _vista.end(), [&](int a, int b) {
        void *x = f.Item((size_t) a), *y = f.Item((size_t) b);
        int r = c->Compara ? c->Compara(x, y) : ComparaTexto(c->Binding ? c->Binding(x) : "", c->Binding ? c->Binding(y) : "");
        return desc ? r > 0 : r < 0;
    });
}

void DataGrid::OrdenaPor(int coluna, ListSortDirection direcao)
{
    if (coluna < 0 || coluna >= (int) Columns.size()) return;
    for (auto &c : Columns) c->SortDirection.reset();
    Columns[(size_t) coluna]->SortDirection = direcao;
    _colOrdenada = coluna;
    FazVista();
    RoutedEventArgs a;
    a.Source = a.OriginalSource = this;
    Sorted(a);
}

void DataGrid::CalculaColunas(double largura)
{
    FonteTexto f = Fonte();
    double fixo = 0, pesos = 0;
    for (auto &c : Columns)
    {
        if (c->Visibility != Vistas::Visibility::Visible) { c->ActualWidth = 0; continue; }
        if (c->Width.IsAbsolute()) c->ActualWidth = std::clamp(c->Width.Value, c->MinWidth, std::max(c->MinWidth, c->MaxWidth));
        else if (c->Width.IsAuto())
        {
            // Auto: o cabecalho e as celulas que estao na tela
            double w = Texto::Largura(f, c->Header) + Tema::CabecalhoGradeRecuo.Horizontal() + 1;
            int primeiro = RowHeight > 0 ? (int) (_rolagemV / RowHeight) : 0;
            for (int i = primeiro; i < std::min((int) _vista.size(), primeiro + 60); i++)
                if (c->Binding) w = std::max(w, Texto::Largura(f, c->Binding(ItemDaVista(i))) + 5);
            c->ActualWidth = std::clamp(w, c->MinWidth, std::max(c->MinWidth, c->MaxWidth));
        }
        else
        {
            pesos += c->Width.Value;
            continue;
        }
        fixo += c->ActualWidth;
    }
    double resto = std::max(0.0, largura - fixo);
    for (auto &c : Columns)
        if (c->Visibility == Vistas::Visibility::Visible && c->Width.IsStar())
            c->ActualWidth = std::clamp(pesos > 0 ? std::floor(resto * c->Width.Value / pesos) : 0, c->MinWidth, std::max(c->MinWidth, c->MaxWidth));
    _larguraTotal = 0;
    for (auto &c : Columns) _larguraTotal += c->ActualWidth;
}

double DataGrid::XDaColuna(int col) const
{
    double x = _corpo.X - _rolagemH;
    for (int i = 0; i < col && i < (int) Columns.size(); i++) x += Columns[(size_t) i]->ActualWidth;
    return x;
}

int DataGrid::ColunaEm(double x) const
{
    double cx = _corpo.X - _rolagemH;
    for (size_t i = 0; i < Columns.size(); i++)
    {
        double w = Columns[i]->ActualWidth;
        if (w > 0 && x >= cx && x < cx + w) return (int) i;
        cx += w;
    }
    return -1;
}

int DataGrid::GarraEm(Point p) const
{
    if (!CanUserResizeColumns || p.Y < Limites.Y || p.Y >= _corpo.Y) return -1;
    double cx = _corpo.X - _rolagemH;
    for (size_t i = 0; i < Columns.size(); i++)
    {
        cx += Columns[i]->ActualWidth;
        if (Columns[i]->ActualWidth > 0 && std::fabs(p.X - cx) <= Tema::GarraColuna / 2 + 1 && Columns[i]->CanUserResize)
            return (int) i;
    }
    return -1;
}

int DataGrid::LinhaEm(Point p) const
{
    if (!_corpo.Contains(p) || RowHeight <= 0) return -1;
    double larg = std::min(_corpo.Width, _larguraTotal - _rolagemH);
    if (p.X >= _corpo.X + larg) return -1;
    int i = (int) ((p.Y - _corpo.Y + _rolagemV) / RowHeight);
    return i >= 0 && i < (int) _vista.size() ? i : -1;
}

void DataGrid::Filhos(std::vector<UIElement *> &s) const
{
    if (_editor) s.push_back(_editor.get());
}

Size DataGrid::MeasureOverride(Size d)
{
    if (!_vistaFeita) FazVista();
    Thickness b = BorderThicknessEfetiva();
    FonteTexto f = Fonte();
    _alturaCab = HeadersVisibility == DataGridHeadersVisibility::Column || HeadersVisibility == DataGridHeadersVisibility::All
                     ? (std::isnan(ColumnHeaderHeight) ? Texto::AlturaLinha(f) + Tema::CabecalhoGradeRecuo.Vertical() + 1 : ColumnHeaderHeight)
                     : 0;
    double w = 0;
    for (auto &c : Columns)
        if (c->Visibility == Vistas::Visibility::Visible)
            w += c->Width.IsAbsolute() ? c->Width.Value : std::max(c->MinWidth, Texto::Largura(f, c->Header) + Tema::CabecalhoGradeRecuo.Horizontal());
    double h = _alturaCab + RowHeight * _vista.size();
    if (_editor) _editor->Measure(Size(Infinito, RowHeight));
    return Size(std::min(w + b.Horizontal(), d.Width), std::min(h + b.Vertical(), d.Height));
}

Size DataGrid::ArrangeOverride(Size fs)
{
    Thickness b = BorderThicknessEfetiva();
    Rect in(Limites.X + b.Left, Limites.Y + b.Top, std::max(0.0, fs.Width - b.Horizontal()), std::max(0.0, fs.Height - b.Vertical()));
    double bl = Tema::BarraLargura;
    double altLinhas = RowHeight * _vista.size();
    _temV = _temH = false;
    for (int volta = 0; volta < 3; volta++)
    {
        double w = in.Width - (_temV ? bl : 0), h = in.Height - _alturaCab - (_temH ? bl : 0);
        CalculaColunas(w);
        bool v = altLinhas > h + 0.5, hh = _larguraTotal > w + 0.5;
        if (v == _temV && hh == _temH) break;
        _temV = v;
        _temH = hh;
    }
    _corpo = Rect(in.X, in.Y + _alturaCab, std::max(0.0, in.Width - (_temV ? bl : 0)), std::max(0.0, in.Height - _alturaCab - (_temH ? bl : 0)));
    _rolagemV = std::clamp(_rolagemV, 0.0, std::max(0.0, altLinhas - _corpo.Height));
    _rolagemH = std::clamp(_rolagemH, 0.0, std::max(0.0, _larguraTotal - _corpo.Width));
    _barraV.Area = Rect(_corpo.Right(), in.Y, bl, in.Height - (_temH ? bl : 0));
    _barraV.Maximo = std::max(0.0, altLinhas - _corpo.Height);
    _barraV.Visivel = _corpo.Height;
    _barraV.Valor = _rolagemV;
    _barraH.Area = Rect(in.X, _corpo.Bottom(), _corpo.Width, bl);
    _barraH.Maximo = std::max(0.0, _larguraTotal - _corpo.Width);
    _barraH.Visivel = _corpo.Width;
    _barraH.Valor = _rolagemH;
    if (_mostrarDepois >= 0 && _corpo.Height > 0)
    {
        int i = _mostrarDepois;
        _mostrarDepois = -1;
        ScrollIntoView(i);
        _rolagemV = std::clamp(_rolagemV, 0.0, std::max(0.0, altLinhas - _corpo.Height));
        _barraV.Valor = _rolagemV;
    }
    PosicionaEditor();
    return fs;
}

void DataGrid::PosicionaEditor()
{
    if (!_editor) return;
    int l = IndiceNaVista(_linhaEditada);
    if (l < 0 || _colEditada < 0 || _colEditada >= (int) Columns.size())
    {
        _editor->Arrange(Rect(-1000, -1000, 0, 0));
        return;
    }
    double x = XDaColuna(_colEditada), y = _corpo.Y + l * RowHeight - _rolagemV;
    _editor->Arrange(Rect(x + 2, y, std::max(0.0, Columns[(size_t) _colEditada]->ActualWidth - 4), RowHeight - 1));
}

void DataGrid::OnRender(DrawingContext &dc)
{
    Thickness b = BorderThicknessEfetiva();
    Brush fundo = BackgroundEfetivo();
    dc.Retangulo(Limites, fundo, BorderBrushEfetivo(), b);
    Rect in(Limites.X + b.Left, Limites.Y + b.Top, std::max(0.0, Limites.Width - b.Horizontal()), std::max(0.0, Limites.Height - b.Vertical()));
    FonteTexto f = Fonte();
    Brush tinta = ForegroundEfetivo();
    // ---- linhas ----
    dc.Recorta(_corpo);
    int n = (int) _vista.size();
    int primeiro = RowHeight > 0 ? std::max(0, (int) (_rolagemV / RowHeight)) : 0;
    int ultimo = RowHeight > 0 ? std::min(n - 1, (int) ((_rolagemV + _corpo.Height) / RowHeight)) : -1;
    double larguraLinhas = std::min(_corpo.Width, _larguraTotal - _rolagemH);
    bool linhasH = GridLinesVisibility == DataGridGridLinesVisibility::All || GridLinesVisibility == DataGridGridLinesVisibility::Horizontal;
    bool linhasV = GridLinesVisibility == DataGridGridLinesVisibility::All || GridLinesVisibility == DataGridGridLinesVisibility::Vertical;
    Brush gradeV = !VerticalGridLinesBrush.Nulo() ? VerticalGridLinesBrush : Tema::Recurso("LinhaCabecalho");
    double lh = Texto::AlturaLinha(f);
    for (int i = primeiro; i <= ultimo; i++)
    {
        double y = _corpo.Y + i * RowHeight - _rolagemV;
        Rect linha(_corpo.X, y, larguraLinhas, RowHeight);
        Brush fl = (i % 2 == 1 && !AlternatingRowBackground.Nulo()) ? AlternatingRowBackground : RowBackground;
        if (!fl.Nulo()) dc.Retangulo(linha, fl);
        void *item = ItemDaVista(i);
        double x = _corpo.X - _rolagemH;
        for (size_t c = 0; c < Columns.size(); c++)
        {
            auto &col = Columns[c];
            double w = col->ActualWidth;
            if (w <= 0) continue;
            Rect cel(x, y, w, RowHeight - (linhasH ? 1 : 0));
            bool sel = SelectionUnit == DataGridSelectionUnit::Cell ? (i == _linhaAtual && (int) c == _colAtual && i < (int) _sel.size() && _sel[i])
                                                                     : (i < (int) _sel.size() && _sel[i]);
            bool editando = _editor && i == IndiceNaVista(_linhaEditada) && (int) c == _colEditada;
            if (editando) dc.Retangulo(cel, Brush(Tema::CelulaEditando));
            else if (sel) dc.Retangulo(cel, Brush(Tema::CelulaSelecionada));
            if (!editando && col->Binding && item)
            {
                std::string s = col->Binding(item);
                Brush cor = col->CorDoTexto ? col->CorDoTexto(item) : Brush();
                FonteTexto fc = f;
                if (col->ElementStyle)
                {
                    if (col->ElementStyle->Foreground && cor.Nulo()) cor = *col->ElementStyle->Foreground;
                    if (col->ElementStyle->FontWeight) fc.Peso = *col->ElementStyle->FontWeight;
                    if (col->ElementStyle->FontSize) fc.Tamanho = *col->ElementStyle->FontSize;
                    if (col->ElementStyle->FontFamily) fc.Familia = *col->ElementStyle->FontFamily;
                }
                if (cor.Nulo()) cor = tinta;
                // a TextBlock da DataGridTextColumn: margem 2,0,2,0 e centrada na vertical; o que nao
                // cabe e cortado (sem reticencias, como no WPF)
                dc.Recorta(Rect(x, y, w, RowHeight));
                double tx = x + 2;
                if (col->AlinhamentoTexto != Vistas::TextAlignment::Left)
                {
                    double tw = Texto::Largura(fc, s);
                    tx = col->AlinhamentoTexto == Vistas::TextAlignment::Right ? x + w - 2 - tw : x + (w - tw) / 2;
                }
                dc.Texto(s, fc, tx, y + (RowHeight - (linhasH ? 1 : 0) - lh) / 2, cor);
                dc.Restaura();
            }
            if (linhasV) dc.Retangulo(Rect(x + w - 1, y, 1, RowHeight), gradeV);
            x += w;
        }
        if (linhasH) dc.Retangulo(Rect(_corpo.X, y + RowHeight - 1, larguraLinhas, 1), HorizontalGridLinesBrush);
    }
    dc.Restaura();
    // ---- cabecalhos (DataGridColumnHeader do Tema) ----
    if (_alturaCab > 0)
    {
        const Vistas::Style *s = Tema::EstiloPadrao("DataGridColumnHeader");
        Brush fundoCab = s && s->Background ? *s->Background : Tema::Recurso("CabecalhoColuna");
        Brush bordaCab = s && s->BorderBrush ? *s->BorderBrush : Tema::Recurso("LinhaCabecalho");
        Brush textoCab = s && s->Foreground ? *s->Foreground : Tema::Recurso("TextoFraco");
        FonteTexto fc = f;
        if (s && s->FontSize) fc.Tamanho = *s->FontSize;
        Thickness r = Tema::CabecalhoGradeRecuo;
        Rect faixa(in.X, in.Y, in.Width - (_temV ? Tema::BarraLargura : 0), _alturaCab);
        dc.Recorta(faixa);
        double x = _corpo.X - _rolagemH;
        Point mouse = Janela() ? Janela()->PosicaoDoMouse() : Point(-1, -1);
        for (size_t c = 0; c < Columns.size(); c++)
        {
            auto &col = Columns[c];
            double w = col->ActualWidth;
            if (w <= 0) continue;
            Rect cab(x, in.Y, w, _alturaCab);
            bool sobre = IsMouseOver() && cab.Contains(mouse) && _redimensionando < 0;
            dc.Retangulo(cab, sobre ? Brush(Tema::CabecalhoGradeSobre) : fundoCab, bordaCab, Thickness(0, 0, 1, 1));
            double seta = col->SortDirection ? 8 + 6 : 0;
            dc.Recorta(Rect(cab.X + r.Left, cab.Y, std::max(0.0, w - r.Horizontal() - 1 - seta), cab.Height));
            dc.Texto(col->Header, fc, cab.X + r.Left, cab.Y + r.Top, textoCab);
            dc.Restaura();
            if (col->SortDirection)
            {
                // Path "M0,4 L4,0 L8,4 Z" (crescente) / "M0,0 L4,4 L8,0 Z" (decrescente), DockPanel.Dock=Right
                double ax = cab.Right() - 1 - r.Right - 8, ay = Arred(cab.Y + (cab.Height - 4) / 2);
                bool cresc = *col->SortDirection == ListSortDirection::Ascending;
                nvgBeginPath(dc.vg);
                if (cresc)
                {
                    nvgMoveTo(dc.vg, (float) ax, (float) (ay + 4));
                    nvgLineTo(dc.vg, (float) (ax + 4), (float) ay);
                    nvgLineTo(dc.vg, (float) (ax + 8), (float) (ay + 4));
                }
                else
                {
                    nvgMoveTo(dc.vg, (float) ax, (float) ay);
                    nvgLineTo(dc.vg, (float) (ax + 4), (float) (ay + 4));
                    nvgLineTo(dc.vg, (float) (ax + 8), (float) ay);
                }
                nvgClosePath(dc.vg);
                Color cc = textoCab.Cor();
                nvgFillColor(dc.vg, nvgRGBA(cc.R, cc.G, cc.B, cc.A));
                nvgFill(dc.vg);
            }
            x += w;
        }
        // o cabecalho de enchimento, a direita da ultima coluna
        if (x < faixa.Right())
            dc.Retangulo(Rect(x, in.Y, faixa.Right() - x, _alturaCab), fundoCab, bordaCab, Thickness(0, 0, 0, 1));
        dc.Restaura();
        if (_temV) dc.Retangulo(Rect(_corpo.Right(), in.Y, Tema::BarraLargura, _alturaCab), fundoCab, bordaCab, Thickness(0, 0, 0, 1));
    }
    if (_temV) _barraV.Desenha(dc);
    if (_temH) _barraH.Desenha(dc);
    if (_temV && _temH) dc.Retangulo(Rect(_corpo.Right(), _corpo.Bottom(), Tema::BarraLargura, Tema::BarraLargura), Brush(Tema::SistemaControle));
}

void DataGrid::AvisaSelecao(const std::vector<char> &antes)
{
    SelectionChangedEventArgs e;
    e.Source = e.OriginalSource = this;
    for (size_t i = 0; i < _sel.size(); i++)
    {
        bool a = i < antes.size() && antes[i], n = _sel[i];
        if (n && !a) e.AddedItems.push_back((int) i);
        if (a && !n) e.RemovedItems.push_back((int) i);
    }
    int primeira = -1;
    if (_ancora >= 0 && _ancora < (int) _sel.size() && _sel[_ancora]) primeira = _ancora;
    else
        for (size_t i = 0; i < _sel.size(); i++)
            if (_sel[i]) { primeira = (int) i; break; }
    bool mudouIndice = primeira != SelectedIndex();
    SelectedIndex.Define(primeira);
    if (!e.AddedItems.empty() || !e.RemovedItems.empty() || mudouIndice) SelectionChanged(e);
    InvalidateVisual();
}

void DataGrid::AoMudarSelecao(int antigo, int novo)
{
    (void) antigo;
    std::vector<char> antes = _sel;
    _sel.assign(_vista.size(), 0);
    if (novo >= 0 && novo < (int) _sel.size())
    {
        _sel[novo] = 1;
        _ancora = _linhaAtual = novo;
    }
    SelectedIndex.Define(-2);   // forca o aviso
    AvisaSelecao(antes);
}

void DataGrid::SelectAll()
{
    if (SelectionMode == Vistas::SelectionMode::Single) return;
    std::vector<char> antes = _sel;
    _sel.assign(_vista.size(), 1);
    AvisaSelecao(antes);
}

void DataGrid::UnselectAll()
{
    std::vector<char> antes = _sel;
    _sel.assign(_vista.size(), 0);
    AvisaSelecao(antes);
}

void DataGrid::SelecionaIndices(const std::vector<int> &v)
{
    std::vector<char> antes = _sel;
    _sel.assign(_vista.size(), 0);
    for (int i : v)
        if (i >= 0 && i < (int) _sel.size()) _sel[i] = 1;
    if (!v.empty()) _ancora = _linhaAtual = v.front();
    AvisaSelecao(antes);
}

void DataGrid::ScrollIntoView(int i)
{
    if (i < 0 || i >= (int) _vista.size()) return;
    if (_corpo.Height <= 0)
    {
        _mostrarDepois = i;   // antes do primeiro layout: aplica no Arrange
        return;
    }
    double y = i * RowHeight;
    if (y < _rolagemV) _rolagemV = y;
    else if (y + RowHeight > _rolagemV + _corpo.Height) _rolagemV = y + RowHeight - _corpo.Height;
    InvalidateVisual();
}

void DataGrid::SetCurrentCell(int linha, int coluna)
{
    _linhaAtual = linha;
    _colAtual = std::clamp(coluna, 0, std::max(0, (int) Columns.size() - 1));
    InvalidateVisual();
}

void DataGrid::Clica(int linha, ModifierKeys m, bool arrasto)
{
    std::vector<char> antes = _sel;
    if ((int) _sel.size() != (int) _vista.size()) _sel.assign(_vista.size(), 0);
    bool ctrl = Tem(m, ModifierKeys::Control), shift = Tem(m, ModifierKeys::Shift);
    if (SelectionMode == Vistas::SelectionMode::Single || SelectionUnit == DataGridSelectionUnit::Cell)
    {
        std::fill(_sel.begin(), _sel.end(), 0);
        _sel[linha] = 1;
        _ancora = linha;
    }
    else if (shift || arrasto)
    {
        if (!ctrl) std::fill(_sel.begin(), _sel.end(), 0);
        int a = _ancora >= 0 ? _ancora : linha;
        for (int k = std::min(a, linha); k <= std::max(a, linha); k++) _sel[k] = 1;
    }
    else if (ctrl)
    {
        _sel[linha] = !_sel[linha];
        _ancora = linha;
    }
    else
    {
        std::fill(_sel.begin(), _sel.end(), 0);
        _sel[linha] = 1;
        _ancora = linha;
    }
    _linhaAtual = linha;
    AvisaSelecao(antes);
}

void DataGrid::OnMouseDown(MouseButtonEventArgs &e)
{
    if (_editor && _editor->EhAncestralDe(e.OriginalSource)) return;   // clique dentro da edicao
    if (e.ChangedButton != MouseButton::Left)
    {
        // o botao direito tambem seleciona a linha (o WPF faz)
        int l = LinhaEm(e.Posicao);
        if (l >= 0 && !(l < (int) _sel.size() && _sel[l])) Clica(l, ModifierKeys::None, false);
        return;
    }
    Focus();
    e.Handled = true;
    if (_temV && _barraV.Aperta(e.Posicao)) { _rolagemV = _barraV.Valor; CaptureMouse(); InvalidateVisual(); return; }
    if (_temH && _barraH.Aperta(e.Posicao)) { _rolagemH = _barraH.Valor; CaptureMouse(); InvalidateVisual(); return; }
    int g = GarraEm(e.Posicao);
    if (g >= 0)
    {
        if (_editor) CommitEdit();
        _redimensionando = g;
        _xRedim = e.Posicao.X;
        _larguraRedim = Columns[(size_t) g]->ActualWidth;
        CaptureMouse();
        return;
    }
    if (e.Posicao.Y < _corpo.Y)
    {
        _colSobre = ColunaEm(e.Posicao.X);   // ordena ao soltar, no mesmo cabecalho
        CaptureMouse();
        return;
    }
    int l = LinhaEm(e.Posicao);
    if (l < 0) return;
    int c = ColunaEm(e.Posicao.X);
    bool jaAtual = l == _linhaAtual && c == _colAtual && l < (int) _sel.size() && _sel[l];
    if (_editor) CommitEdit();
    if (SelectionUnit == DataGridSelectionUnit::Cell && c >= 0) _colAtual = c;
    Clica(l, Keyboard::Modifiers(), false);
    // clicar na celula que ja e a atual comeca a edicao (como no WPF)
    if (SelectionUnit == DataGridSelectionUnit::Cell && (jaAtual || e.ClickCount == 2)) BeginEdit();
    else if (SelectionMode == Vistas::SelectionMode::Extended)
    {
        _arrastandoSelecao = true;
        CaptureMouse();
    }
}

void DataGrid::OnMouseMove(MouseEventArgs &e)
{
    bool antes = _barraV.Sobre || _barraH.Sobre;
    _barraV.Move(e.Posicao);
    _barraH.Move(e.Posicao);
    if (_barraV.Arrastando) { _rolagemV = _barraV.Valor; InvalidateVisual(); }
    if (_barraH.Arrastando) { _rolagemH = _barraH.Valor; InvalidateVisual(); }
    if (antes != (_barraV.Sobre || _barraH.Sobre)) InvalidateVisual();
    if (_redimensionando >= 0)
    {
        auto &c = Columns[(size_t) _redimensionando];
        double w = std::clamp(_larguraRedim + e.Posicao.X - _xRedim, c->MinWidth, std::max(c->MinWidth, c->MaxWidth));
        c->Width = DataGridLength(Arred(w));
        InvalidateVisual();
        return;
    }
    if (_arrastandoSelecao)
    {
        Point p(e.Posicao.X, std::clamp(e.Posicao.Y, _corpo.Y, _corpo.Bottom() - 1));
        int l = LinhaEm(Point(_corpo.X + 1, p.Y));
        if (l >= 0 && l != _linhaAtual) Clica(l, Keyboard::Modifiers(), true);
    }
    // o realce do cabecalho segue o mouse
    if (e.Posicao.Y < _corpo.Y + 2) InvalidateVisual();
}

void DataGrid::OnMouseUp(MouseButtonEventArgs &e)
{
    if (e.ChangedButton != MouseButton::Left) return;
    if (_barraV.Arrastando || _barraH.Arrastando || _barraV._repetindo || _barraH._repetindo)
    {
        _barraV.Solta();
        _barraH.Solta();
    }
    if (_redimensionando >= 0) _redimensionando = -1;
    else if (_colSobre >= 0 && e.Posicao.Y < _corpo.Y)
    {
        int c = ColunaEm(e.Posicao.X);
        if (c == _colSobre && CanUserSortColumns && Columns[(size_t) c]->CanUserSort)
        {
            auto d = Columns[(size_t) c]->SortDirection;
            OrdenaPor(c, d && *d == ListSortDirection::Ascending ? ListSortDirection::Descending : ListSortDirection::Ascending);
        }
    }
    _colSobre = -1;
    _arrastandoSelecao = false;
    if (IsMouseCaptured()) ReleaseMouseCapture();
    InvalidateVisual();
}

void DataGrid::OnMouseLeave(MouseEventArgs &e)
{
    (void) e;
    _barraV.Sobre = _barraH.Sobre = false;
    InvalidateVisual();
}

void DataGrid::OnMouseWheel(MouseWheelEventArgs &e)
{
    if (!_temV) return;
    double antes = _rolagemV;
    // CanContentScroll: tres linhas por degrau
    _rolagemV = std::clamp(_rolagemV - e.Delta / 120.0 * 3 * RowHeight, 0.0, _barraV.Maximo);
    if (antes != _rolagemV) e.Handled = true;
    InvalidateVisual();
}

void DataGrid::MoveCelula(int dl, int dc, bool editar)
{
    int n = (int) _vista.size();
    if (n == 0) return;
    int l = std::clamp((_linhaAtual < 0 ? 0 : _linhaAtual) + dl, 0, n - 1);
    int c = _colAtual;
    if (dc != 0)
    {
        // a proxima coluna visivel (e editavel, se for para editar)
        int k = c;
        for (int volta = 0; volta < (int) Columns.size(); volta++)
        {
            k += dc;
            if (k < 0 || k >= (int) Columns.size()) break;
            if (Columns[(size_t) k]->Visibility == Vistas::Visibility::Visible && (!editar || !Columns[(size_t) k]->IsReadOnly)) { c = k; break; }
        }
    }
    _colAtual = c;
    Clica(l, ModifierKeys::None, false);
    ScrollIntoView(l);
    if (editar) BeginEdit();
}

void DataGrid::OnKeyDown(KeyEventArgs &e)
{
    int n = (int) _vista.size();
    bool shift = Tem(e.Modificadores, ModifierKeys::Shift), ctrl = Tem(e.Modificadores, ModifierKeys::Control);
    if (_editor)
    {
        switch (e.Key)
        {
        case Key::Enter: CommitEdit(); MoveCelula(shift ? -1 : 1, 0, false); e.Handled = true; return;
        case Key::Tab: CommitEdit(); MoveCelula(0, shift ? -1 : 1, true); e.Handled = true; return;
        case Key::Escape: CancelEdit(); e.Handled = true; return;
        case Key::Up: CommitEdit(); MoveCelula(-1, 0, false); e.Handled = true; return;
        case Key::Down: CommitEdit(); MoveCelula(1, 0, false); e.Handled = true; return;
        default: return;
        }
    }
    if (n == 0) return;
    int pagina = std::max(1, (int) (_corpo.Height / std::max(1.0, RowHeight)) - 1);
    int l = _linhaAtual < 0 ? -1 : _linhaAtual;
    int novo = l;
    switch (e.Key)
    {
    case Key::Up: novo = std::max(0, l - 1); break;
    case Key::Down: case Key::Enter: novo = std::min(n - 1, l + 1); break;
    case Key::PageUp: novo = std::max(0, l - pagina); break;
    case Key::PageDown: novo = std::min(n - 1, l + pagina); break;
    case Key::Home: if (ctrl || SelectionUnit != DataGridSelectionUnit::Cell) novo = 0; else { _colAtual = 0; InvalidateVisual(); e.Handled = true; return; } break;
    case Key::End: if (ctrl || SelectionUnit != DataGridSelectionUnit::Cell) novo = n - 1; else { _colAtual = (int) Columns.size() - 1; InvalidateVisual(); e.Handled = true; return; } break;
    case Key::Left: if (SelectionUnit == DataGridSelectionUnit::Cell) { MoveCelula(0, -1, false); e.Handled = true; } return;
    case Key::Right: if (SelectionUnit == DataGridSelectionUnit::Cell) { MoveCelula(0, 1, false); e.Handled = true; } return;
    case Key::Tab: if (SelectionUnit == DataGridSelectionUnit::Cell) { MoveCelula(0, shift ? -1 : 1, false); e.Handled = true; } return;
    case Key::F2: BeginEdit(); e.Handled = true; return;
    case Key::A: if (ctrl) { SelectAll(); e.Handled = true; } return;
    default: return;
    }
    if (novo < 0) novo = 0;
    if (shift && SelectionMode == Vistas::SelectionMode::Extended && SelectionUnit != DataGridSelectionUnit::Cell)
        Clica(novo, ModifierKeys::Shift, false);
    else if (ctrl && SelectionUnit != DataGridSelectionUnit::Cell)
        _linhaAtual = novo;    // Ctrl+seta move so o foco, sem selecionar
    else
        Clica(novo, ModifierKeys::None, false);
    ScrollIntoView(novo);
    e.Handled = true;
}

void DataGrid::OnTextInput(TextCompositionEventArgs &e)
{
    // digitar numa celula editavel comeca a edicao com o que foi digitado
    if (_editor || SelectionUnit != DataGridSelectionUnit::Cell || e.Text.empty()) return;
    if (!BeginEdit()) return;
    _editor->Text = e.Text;
    _editor->SetCaretIndex(CaracteresEm(e.Text));
    e.Handled = true;
}

bool DataGrid::BeginEdit()
{
    if (IsReadOnly || _linhaAtual < 0 || _linhaAtual >= (int) _vista.size()) return false;
    if (_colAtual < 0 || _colAtual >= (int) Columns.size()) return false;
    auto &col = Columns[(size_t) _colAtual];
    if (col->IsReadOnly || !col->BindingGrava) return false;
    if (_editor) CommitEdit();
    _linhaEditada = _vista[(size_t) _linhaAtual];
    _colEditada = _colAtual;
    auto ed = std::make_shared<TextBox>();
    ed->Style = col->EditingElementStyle;
    ed->Text = col->Binding ? col->Binding(ItemDaVista(_linhaAtual)) : std::string();
    ed->SelectAll();
    _editor = ed;
    UIElement::Adota(this, ed.get());
    ed->Measure(Size(Infinito, RowHeight));
    PosicionaEditor();
    ed->Focus();
    InvalidateVisual();
    return true;
}

bool DataGrid::CommitEdit()
{
    if (!_editor) return true;
    auto ed = _editor;
    _editor.reset();
    if (_colEditada >= 0 && _colEditada < (int) Columns.size() && _linhaEditada >= 0 && (size_t) _linhaEditada < ItemsSource().Count())
    {
        auto &col = Columns[(size_t) _colEditada];
        if (col->BindingGrava) col->BindingGrava(ItemsSource().Item((size_t) _linhaEditada), ed->Text());
    }
    bool tinhaFoco = ed->IsKeyboardFocused();
    UIElement::Solta(ed.get());
    _linhaEditada = _colEditada = -1;
    if (tinhaFoco) Focus();
    InvalidateVisual();
    return true;
}

void DataGrid::CancelEdit()
{
    if (!_editor) return;
    auto ed = _editor;
    _editor.reset();
    bool tinhaFoco = ed->IsKeyboardFocused();
    UIElement::Solta(ed.get());
    _linhaEditada = _colEditada = -1;
    if (tinhaFoco) Focus();
    InvalidateVisual();
}

Vistas::Cursor DataGrid::CursorEm(Point p) const
{
    if (_redimensionando >= 0 || GarraEm(p) >= 0) return Vistas::Cursor::SizeWE;
    return Cursor;
}

std::string DataGrid::DicaEm(Point p) const
{
    if (p.Y < _corpo.Y && p.Y >= Limites.Y)
    {
        int c = ColunaEm(p.X);
        if (c >= 0 && !Columns[(size_t) c]->HeaderToolTip.empty()) return Columns[(size_t) c]->HeaderToolTip;
    }
    return ToolTip;
}

} // namespace ARCA::Vistas

// ARCA no xOS - Vistas/PainelEncarte (de Vistas\PainelEncarte.xaml e PainelEncarte.xaml.cs).
#include "PainelEncarte.h"

#include "JanelaPagina.h"
#include "Base/Arquivo.h"
#include "Base/Despacho.h"
#include "Base/Texto.h"
#include "Modelo/Formato.h"

#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <set>
#include <thread>

namespace ARCA::Vistas {

namespace {
Brush Pincel(uint8_t r, uint8_t g, uint8_t b) { return SolidColorBrush(Color::FromRgb(r, g, b)); }

// StringComparer.OrdinalIgnoreCase
bool MenorSemCaixa(const std::string &a, const std::string &b)
{
    return Base::Minusculas(a) < Base::Minusculas(b);
}
} // namespace

PainelEncarte::PainelEncarte()
{
    InitializeComponent();
    SizeChanged += [this](SizeChangedEventArgs &) { CarregarVisiveis(); };
}

PainelEncarte::~PainelEncarte()
{
    // os fios de decodificação que ainda estiverem rodando descartam o resultado
    (*_geracao)++;
}

void PainelEncarte::InitializeComponent()
{
    // <UserControl Background="Black">
    Background = Brushes::Black;
    auto d = std::make_shared<DockPanel>();

    auto g = std::make_shared<Grid>();
    DockPanel::SetDock(g, Dock::Top);
    g->Margin = Thickness(2, 0, 0, 6);
    g->ColumnDefinitions = {ColumnDefinition(GridLength::Star(1)), ColumnDefinition(GridLength::Auto())};
    auto t = std::make_shared<TextBlock>("ENCARTE");
    t->Foreground = SolidColorBrush(Color::De("#FF8A7654"));
    t->FontSize = 10;
    t->FontWeight = FontWeights::SemiBold;
    t->VerticalAlignment = Vistas::VerticalAlignment::Center;
    g->Children.Add(t);
    TxtInfo = std::make_shared<TextBlock>();
    TxtInfo->Name = "TxtInfo";
    Grid::SetColumn(TxtInfo, 1);
    TxtInfo->Foreground = SolidColorBrush(Color::De("#FF6A583C"));
    TxtInfo->FontSize = 10;
    TxtInfo->VerticalAlignment = Vistas::VerticalAlignment::Center;
    g->Children.Add(TxtInfo);
    d->Children.Add(g);

    auto b = std::make_shared<Border>();
    b->BorderBrush = SolidColorBrush(Color::De("#FF3A2E1C"));
    b->BorderThickness = Thickness(1);
    b->Background = SolidColorBrush(Color::De("#FF120E08"));
    auto gi = std::make_shared<Grid>();
    AreaLista = std::make_shared<ScrollViewer>();
    AreaLista->Name = "AreaLista";
    AreaLista->VerticalScrollBarVisibility = ScrollBarVisibility::Auto;
    AreaLista->HorizontalScrollBarVisibility = ScrollBarVisibility::Disabled;
    AreaLista->ScrollChanged += [this](ScrollChangedEventArgs &) { CarregarVisiveis(); };   // AoRolar
    Paginas = std::make_shared<StackPanel>();
    Paginas->Name = "Paginas";
    Paginas->Margin = Thickness(6);
    AreaLista->Content = Paginas;
    gi->Children.Add(AreaLista);
    TxtVazio = std::make_shared<TextBlock>("sem encarte");
    TxtVazio->Name = "TxtVazio";
    TxtVazio->Foreground = SolidColorBrush(Color::De("#FF5A4A32"));
    TxtVazio->FontSize = 11;
    TxtVazio->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    TxtVazio->VerticalAlignment = Vistas::VerticalAlignment::Center;
    gi->Children.Add(TxtVazio);
    b->SetChild(gi);
    d->Children.Add(b);
    Content = d;
}

// ===============================================================
void PainelEncarte::Carregar(const std::string &arquivoDaFaixa)
{
    (*_geracao)++;
    Paginas->Children.Clear();
    _paginas.clear();
    _bytes = 0;
    _carregadas = 0;
    TxtVazio->Visibility = Vistas::Visibility::Visible;
    TxtVazio->Text = "sem encarte";
    TxtInfo->Text = "";

    if (arquivoDaFaixa.empty()) return;

    std::string pasta = Base::PastaDe(arquivoDaFaixa);
    if (pasta.empty()) return;

    std::string artwork = Base::Combina(pasta, "Artwork");
    if (!Base::PastaExiste(artwork)) { TxtVazio->Text = "sem pasta Artwork"; return; }

    std::vector<std::string> achados;
    try
    {
        // Padrão de extensão com três letras casa com as extensões que
        // COMEÇAM com elas: "*.tif" já devolve os .tiff, e listar os dois
        // duplicava as páginas. ("*.jpg" não pega .jpeg: "jpeg" não começa
        // com "jpg" — esse continua na lista.) O conjunto garante: cada
        // arquivo uma vez.
        // LINUX: o Directory.GetFiles do Windows não distingue maiúsculas e casa "*.tif" com
        // ".tiff"; aqui a comparação faz o mesmo à mão.
        std::set<std::string> setVistos;
        std::vector<std::string> todos = Base::Arquivos(artwork);
        for (const char *ext : {".jpg", ".jpeg", ".png", ".bmp", ".tif"})
            for (const std::string &f : todos)
            {
                std::string e = Base::Minusculas(Base::Extensao(f));
                bool casa = std::string(ext) == ".tif" ? Base::ComecaCom(e, ".tif") : e == ext;
                if (casa && setVistos.insert(Base::Minusculas(f)).second) achados.push_back(f);
            }
    }
    catch (...) {}
    std::sort(achados.begin(), achados.end(), MenorSemCaixa);

    if (achados.empty()) { TxtVazio->Text = "pasta vazia"; return; }

    TxtVazio->Visibility = Vistas::Visibility::Collapsed;
    for (const std::string &a : achados)
    {
        auto p = MontarPagina(a, (int) _paginas.size());
        _paginas.push_back(p);
        Paginas->Children.Add(p->Moldura);
    }
    AtualizarInfo();

    // Depois do layout, para saber o que está à vista.
    if (Window *j = Janela()) j->DepoisDoLayout([this] { CarregarVisiveis(); });
}

std::shared_ptr<PainelEncarte::Pagina> PainelEncarte::MontarPagina(const std::string &arquivo, int indice)
{
    auto img = std::make_shared<Image>();
    img->Stretch = Vistas::Stretch::Uniform;
    // SnapsToDevicePixels, BitmapScalingMode.HighQuality: a textura do NanoVG é bilinear

    auto b = std::make_shared<Border>();
    b->Background = Pincel(0x12, 0x0E, 0x08);
    b->BorderBrush = Pincel(0x3A, 0x2E, 0x1C);
    b->BorderThickness = Thickness(1);
    b->Margin = Thickness(0, 0, 0, 6);
    b->Padding = Thickness(2);
    b->MinHeight = AlturaProvisoria;
    b->Cursor = Cursors::Hand;
    b->SetChild(img);
    b->ToolTip = Base::NomeDoArquivo(arquivo) + "   (clique para ver em tela cheia)";

    int i = indice;
    b->MouseLeftButtonUp += [this, i](MouseButtonEventArgs &) { AbrirTelaCheia(i); };

    auto p = std::make_shared<Pagina>();
    p->Arquivo = arquivo;
    p->Moldura = b;
    p->Imagem = img;
    return p;
}

// ===============================================================
/// <summary>
/// Pede a decodificação do que está à vista, com uma folga de uma tela
/// para cima e para baixo — assim a página seguinte já está pronta
/// quando a rolagem chega nela.
/// </summary>
void PainelEncarte::CarregarVisiveis()
{
    if (_paginas.empty()) return;

    // Painel escondido (módulo recolhido) mede 0: não há largura de
    // verdade para decodificar. Espera o SizeChanged de quando ele
    // aparecer — decodificar agora daria uma página borrada.
    if (AreaLista->ActualWidth <= 0) return;

    int intLargura = (int) std::max(80.0, AreaLista->ActualWidth - 24);
    double topo = AreaLista->VerticalOffset() - AreaLista->ViewportHeight();
    double base_ = AreaLista->VerticalOffset() + AreaLista->ViewportHeight() * 2;

    double y = 0;
    for (auto &p : _paginas)
    {
        double altura = p->Moldura->ActualHeight > 0 ? p->Moldura->ActualHeight : AlturaProvisoria;
        bool blnPrecisa = p->intLarguraPedida == 0 || intLargura > p->intLarguraPedida * dblFolgaRedecodificar;
        if (blnPrecisa && y + altura >= topo && y <= base_)
        {
            p->intLarguraPedida = intLargura;
            Decodificar(p, intLargura, *_geracao);
        }
        y += altura + 6;
    }
}

/// <summary>
/// Decodifica fora da thread da interface e congela antes de cruzar —
/// congelar é o que permite criar noutra thread e usar aqui. Sem isso,
/// trocar de faixa travaria a tela enquanto JPEGs grandes são abertos.
/// </summary>
void PainelEncarte::Decodificar(std::shared_ptr<Pagina> p, int larguraAlvo, int geracao)
{
    std::shared_ptr<std::atomic<int>> g = _geracao;
    std::string arquivo = p->Arquivo;
    std::thread t([this, p, larguraAlvo, geracao, g, arquivo] {
        // ThreadPriority.BelowNormal: não competir com o áudio
        setpriority(PRIO_PROCESS, 0, 5);
        std::shared_ptr<BitmapSource> bmp = Abrir(arquivo, larguraAlvo);
        if (!bmp || geracao != *g) return;

        Base::Despacha([this, p, larguraAlvo, geracao, g, bmp] {
            if (geracao != *g) return;
            // Um pedido maior pode ter saído depois deste; se este
            // chegar por último, não pode sobrescrever o melhor.
            if (larguraAlvo != p->intLarguraPedida) return;

            std::shared_ptr<BitmapSource> objAnterior = p->Imagem->Source;
            if (objAnterior)
                _bytes -= (int64_t) objAnterior->PixelWidth * objAnterior->PixelHeight * 4;
            else
                _carregadas++;

            p->Imagem->Source = bmp;
            p->Moldura->MinHeight = 0;             // a página dita a altura agora
            _bytes += (int64_t) bmp->PixelWidth * bmp->PixelHeight * 4;
            AtualizarInfo();
        });
    });
    t.detach();   // IsBackground = true
}

std::shared_ptr<BitmapSource> PainelEncarte::Abrir(const std::string &arquivo, int larguraAlvo)
{
    try
    {
        // CacheOption = OnLoad, IgnoreColorProfile, DecodePixelWidth = larguraAlvo
        return BitmapSource::DeArquivo(arquivo, larguraAlvo);
    }
    catch (...) { return nullptr; }
}

void PainelEncarte::AtualizarInfo()
{
    TxtInfo->Text = std::to_string(_paginas.size()) + (_paginas.size() == 1 ? " página" : " páginas") +
                    (_carregadas > 0 ? " · " + Modelo::Formato::Num(_bytes / 1048576.0, 1) + " MB" : "");
    TxtInfo->ToolTip = std::to_string(_carregadas) + " de " + std::to_string(_paginas.size()) + " decodificadas. " +
                       "Só o que entra em vista é aberto.";
}

// ===============================================================
void PainelEncarte::AbrirTelaCheia(int indice)
{
    std::vector<std::string> arquivos;
    for (auto &p : _paginas) arquivos.push_back(p->Arquivo);

    auto j = std::make_shared<JanelaPagina>(arquivos, indice);
    j->Owner = Janela();
    j->ShowDialog();
}

void PainelEncarte::Soltar()
{
    Carregar("");
}

} // namespace ARCA::Vistas

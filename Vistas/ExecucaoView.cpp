// ARCA no xOS - Vistas/ExecucaoView (de Vistas\ExecucaoView.xaml e ExecucaoView.xaml.cs).
#include "ExecucaoView.h"

#include "InstrumentosShader.h"
#include "Base/Arquivo.h"
#include "Base/Despacho.h"
#include "Base/Texto.h"
#include "Interop/EditorFlac.h"
#include "Modelo/EspectrogramaCache.h"
#include "Modelo/Formato.h"
#include "Modelo/OndaCache.h"

#include <sys/resource.h>

#include <algorithm>
#include <cmath>
#include <thread>

namespace ARCA::Vistas {

using Modelo::AnalisadorAudio;
using Modelo::Formato;

namespace {

// Ordem pedida: Forma de onda, Espectro, Histograma, Goniômetro, Painel.
// Forma de onda é o padrão.
const std::vector<std::string> NomesAbas = {"Forma de onda", "Espectro", "Espectrograma", "Histograma de nível",
                                            "Goniômetro", "Medidores", "Painel"};

Brush Hex(const char *h) { return SolidColorBrush(Color::De(h)); }

// Os UserControl.Resources do ExecucaoView.xaml (a AbaInterna já está no Tema)
void RegistrarEstilos()
{
    static bool feito = false;
    if (feito) return;
    feito = true;
    Style r;
    r.Chave = "RotuloInstrumento";
    r.TargetType = "TextBlock";
    r.Foreground = Hex("#FF8A7654");
    r.FontSize = 10;
    r.FontWeight = FontWeight::SemiBold;
    r.Margin = Thickness(2, 0, 0, 4);
    Tema::RegistraEstilo(r);
    Style l;
    l.Chave = "Leitura";
    l.TargetType = "TextBlock";
    l.Foreground = Hex("#FFFFB454");
    l.FontFamily = "Consolas";
    l.FontSize = 13;
    Tema::RegistraEstilo(l);
    Style lr;
    lr.Chave = "LeituraRotulo";
    lr.TargetType = "TextBlock";
    lr.Foreground = Hex("#FF6A583C");
    lr.FontSize = 10;
    lr.Margin = Thickness(0, 0, 0, 1);
    Tema::RegistraEstilo(lr);
}

// ThreadPriority.BelowNormal
void PrioridadeAbaixoDoNormal() { setpriority(PRIO_PROCESS, 0, 5); }

} // namespace

ExecucaoView::ExecucaoView() : _inicioCronometro(Agora())
{
    RegistrarEstilos();
    InitializeComponent();
    MontarLeituras();

    InstalarInstrumentos();
    MontarAbas();

    _rendering.Interval = TimeSpan::FromMilliseconds(16);
    _rendering.Tick += [this] { AoRenderizar(); };
}

ExecucaoView::~ExecucaoView()
{
    (*_geracaoOnda)++;
    (*_geracaoEg)++;
}

double ExecucaoView::Cronometro() const { return Agora() - _inicioCronometro; }

void ExecucaoView::InitializeComponent()
{
    // <UserControl Background="#FF17120B">
    // Chapa sépia escura (não preto): um pouco de luz no fundo tira o brilho
    // ofuscante dos traços finos dos instrumentos, e a distância entre este
    // módulo e os outros três marca a troca de contexto.
    Background = Hex("#FF17120B");

    auto raiz = std::make_shared<Grid>();

    // ===== Palco do vídeo ============================================
    // Tocando vídeo, o módulo é dele: nada de capa, instrumentos, onda nem
    // análise. A janela principal põe o controle do WMP exatamente sobre
    // AreaVideo (ele mora numa camada por cima de tudo).
    PalcoVideo = std::make_shared<Grid>();
    PalcoVideo->Name = "PalcoVideo";
    PalcoVideo->Visibility = Vistas::Visibility::Collapsed;
    PalcoVideo->Background = Hex("#FF0B0906");
    PalcoVideo->RowDefinitions = {RowDefinition(GridLength::Auto()), RowDefinition(GridLength::Star(1))};
    {
        auto d = std::make_shared<DockPanel>();
        d->Margin = Thickness(16, 8, 16, 8);
        // Tela cheia do próprio WMP; Esc volta.
        BtnTelaCheia = std::make_shared<Button>();
        BtnTelaCheia->Name = "BtnTelaCheia";
        DockPanel::SetDock(BtnTelaCheia, Dock::Right);
        BtnTelaCheia->Content = "Tela cheia";
        BtnTelaCheia->Padding = Thickness(12, 4);
        BtnTelaCheia->VerticalAlignment = Vistas::VerticalAlignment::Center;
        BtnTelaCheia->ToolTip = "Mostrar o vídeo na tela inteira. Esc volta ao ARCA.";
        BtnTelaCheia->Click += [this](RoutedEventArgs &) { AoPedirTelaCheia(); };
        d->Children.Add(BtnTelaCheia);
        TxtAvisoVideo = std::make_shared<TextBlock>();
        TxtAvisoVideo->Name = "TxtAvisoVideo";
        DockPanel::SetDock(TxtAvisoVideo, Dock::Right);
        TxtAvisoVideo->Foreground = Hex("#FFE0563A");
        TxtAvisoVideo->FontSize = 11;
        TxtAvisoVideo->VerticalAlignment = Vistas::VerticalAlignment::Center;
        TxtAvisoVideo->Margin = Thickness(0, 0, 12, 0);
        d->Children.Add(TxtAvisoVideo);
        auto sp = std::make_shared<StackPanel>();
        sp->Orientation = Vistas::Orientation::Horizontal;
        sp->VerticalAlignment = Vistas::VerticalAlignment::Center;
        TxtTituloVideo = std::make_shared<TextBlock>("—");
        TxtTituloVideo->Name = "TxtTituloVideo";
        TxtTituloVideo->Foreground = Hex("#FFE3D2B0");
        TxtTituloVideo->FontSize = 16;
        TxtTituloVideo->TextTrimming = TextTrimming::CharacterEllipsis;
        sp->Children.Add(TxtTituloVideo);
        TxtDetalheVideo = std::make_shared<TextBlock>();
        TxtDetalheVideo->Name = "TxtDetalheVideo";
        TxtDetalheVideo->Foreground = Hex("#FF8A7654");
        TxtDetalheVideo->FontSize = 12;
        TxtDetalheVideo->Margin = Thickness(14, 4, 0, 0);
        TxtDetalheVideo->TextTrimming = TextTrimming::CharacterEllipsis;
        sp->Children.Add(TxtDetalheVideo);
        d->Children.Add(sp);
        PalcoVideo->Children.Add(d);
        AreaVideo = std::make_shared<Border>();
        AreaVideo->Name = "AreaVideo";
        Grid::SetRow(AreaVideo, 1);
        AreaVideo->Background = Brushes::Black;
        PalcoVideo->Children.Add(AreaVideo);
    }
    raiz->Children.Add(PalcoVideo);

    Instrumentacao = std::make_shared<DockPanel>();
    Instrumentacao->Name = "Instrumentacao";
    Instrumentacao->Margin = Thickness(16);

    // ===== Cabeçalho: capa, identificação ========================= -->
    {
        auto b = std::make_shared<Border>();
        DockPanel::SetDock(b, Dock::Top);
        b->Margin = Thickness(0, 0, 0, 14);
        auto g = std::make_shared<Grid>();
        g->ColumnDefinitions = {ColumnDefinition(GridLength::Auto()), ColumnDefinition(GridLength::Star(1))};
        auto moldura = std::make_shared<Border>();
        moldura->Width = 150;
        moldura->Height = 150;
        moldura->Background = Hex("#FF120E08");
        moldura->BorderBrush = Hex("#FF3A2E1C");
        moldura->BorderThickness = Thickness(1);
        auto gc = std::make_shared<Grid>();
        // LINUX: o WPF recorta pelo layout a imagem UniformToFill maior que a moldura
        gc->ClipToBounds = true;
        TxtSemCapa = std::make_shared<TextBlock>("sem capa");
        TxtSemCapa->Name = "TxtSemCapa";
        TxtSemCapa->Foreground = Hex("#FF5A4A32");
        TxtSemCapa->FontSize = 11;
        TxtSemCapa->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
        TxtSemCapa->VerticalAlignment = Vistas::VerticalAlignment::Center;
        gc->Children.Add(TxtSemCapa);
        ImgCapa = std::make_shared<Image>();
        ImgCapa->Name = "ImgCapa";
        ImgCapa->Stretch = Vistas::Stretch::UniformToFill;
        gc->Children.Add(ImgCapa);
        moldura->SetChild(gc);
        g->Children.Add(moldura);

        auto sp = std::make_shared<StackPanel>();
        Grid::SetColumn(sp, 1);
        sp->Margin = Thickness(18, 4, 18, 0);
        sp->VerticalAlignment = Vistas::VerticalAlignment::Top;
        TxtTitulo = std::make_shared<TextBlock>("—");
        TxtTitulo->Name = "TxtTitulo";
        TxtTitulo->Foreground = Hex("#FFE3D2B0");
        TxtTitulo->FontSize = 22;
        TxtTitulo->TextTrimming = TextTrimming::CharacterEllipsis;
        sp->Children.Add(TxtTitulo);
        TxtArtista = std::make_shared<TextBlock>();
        TxtArtista->Name = "TxtArtista";
        TxtArtista->Foreground = Hex("#FFE3D2B0");
        TxtArtista->FontSize = 14;
        TxtArtista->Margin = Thickness(0, 6, 0, 0);
        TxtArtista->TextTrimming = TextTrimming::CharacterEllipsis;
        sp->Children.Add(TxtArtista);
        TxtAlbum = std::make_shared<TextBlock>();
        TxtAlbum->Name = "TxtAlbum";
        TxtAlbum->Foreground = Hex("#FF8A7654");
        TxtAlbum->FontSize = 12;
        TxtAlbum->Margin = Thickness(0, 3, 0, 0);
        TxtAlbum->TextTrimming = TextTrimming::CharacterEllipsis;
        sp->Children.Add(TxtAlbum);
        TxtFormato = std::make_shared<TextBlock>();
        TxtFormato->Name = "TxtFormato";
        TxtFormato->Foreground = Hex("#FF6A583C");
        TxtFormato->FontSize = 11;
        TxtFormato->Margin = Thickness(0, 12, 0, 0);
        TxtFormato->FontFamily = "Consolas";
        sp->Children.Add(TxtFormato);
        g->Children.Add(sp);
        b->SetChild(g);
        Instrumentacao->Children.Add(b);
    }

    // ===== Leituras numéricas (sempre visíveis) =================== -->
    {
        auto b = std::make_shared<Border>();
        DockPanel::SetDock(b, Dock::Bottom);
        b->Margin = Thickness(0, 14, 0, 0);
        b->Padding = Thickness(0, 10, 0, 0);
        b->BorderBrush = Hex("#FF3A2E1C");
        b->BorderThickness = Thickness(0, 1, 0, 0);
        PainelLeituras = std::make_shared<StackPanel>();
        PainelLeituras->Name = "PainelLeituras";
        PainelLeituras->Orientation = Vistas::Orientation::Horizontal;
        b->SetChild(PainelLeituras);
        Instrumentacao->Children.Add(b);
    }

    // ===== Encarte, à direita ===================================== -->
    Encarte = std::make_shared<PainelEncarte>();
    Encarte->Name = "Encarte";
    DockPanel::SetDock(Encarte, Dock::Right);
    Encarte->Width = 430;
    Encarte->Margin = Thickness(14, 0, 0, 0);
    Instrumentacao->Children.Add(Encarte);

    // ===== Visualizadores: abas + palco ========================== -->
    {
        auto d = std::make_shared<DockPanel>();
        auto b = std::make_shared<Border>();
        DockPanel::SetDock(b, Dock::Top);
        b->BorderBrush = Hex("#FF3A2E1C");
        b->BorderThickness = Thickness(0, 0, 0, 1);
        b->Margin = Thickness(0, 0, 0, 8);
        Abas = std::make_shared<StackPanel>();
        Abas->Name = "Abas";
        Abas->Orientation = Vistas::Orientation::Horizontal;
        b->SetChild(Abas);
        d->Children.Add(b);
        Palco = std::make_shared<Grid>();
        Palco->Name = "Palco";
        d->Children.Add(Palco);
        Instrumentacao->Children.Add(d);
    }
    raiz->Children.Add(Instrumentacao);
    Content = raiz;
}

/// <summary>
/// Espectro, histograma e forma de onda vão para a placa: são funções
/// por pixel, que é exatamente o que um shader faz bem. Cada um recebe
/// da CPU uma tira de poucos pixels com os valores, e desenha o resto
/// sozinho.
///
/// O goniômetro fica na CPU de propósito: ele não é uma função por
/// pixel, é plotagem de milhares de pontos com acumulação de brilho e
/// rastro. Traduzir isso para shader daria um resultado pior com muito
/// mais trabalho.
///
/// Se o shader não compilar — outra placa, outro sistema —, entra a
/// versão em CPU no lugar, e a leitura DESENHO diz qual está valendo.
/// </summary>
void ExecucaoView::InstalarInstrumentos()
{
    int naPlaca = 0;

    auto es = std::make_shared<EspectroShader>();
    if (es->Montar()) { _espec = es; naPlaca++; }
    else { _espec = std::make_shared<Espectro>(); _falhaShader = es->Falha; }

    auto hs = std::make_shared<HistogramaShader>();
    if (hs->Montar()) { _histo = hs; naPlaca++; }
    else { _histo = std::make_shared<Histograma>(); }

    auto os = std::make_shared<OsciloscopioShader>();
    if (os->Montar()) { _osc = os; naPlaca++; }
    else { _osc = std::make_shared<Osciloscopio>(); }

    _gonio = std::make_shared<Goniometro>();

    _hostOsc = Moldurar(_osc);
    _hostEspec = Moldurar(_espec);
    _hostHisto = Moldurar(_histo);
    _hostGonio = Moldurar(_gonio);

    _onda = std::make_shared<OndaArquivo>();
    _onda->Buscou += [this](double seg) {
        if (Buscar) Buscar(seg);
    };

    // A aba Espectro é o modo Análise; o shader compacto fica no Painel.
    _especAnalise = std::make_shared<EspectroAnalise>();
    _medidores = std::make_shared<Medidores>();

    _espectrograma = std::make_shared<EspectrogramaArquivo>();
    _espectrograma->Buscou += [this](double seg) {
        if (Buscar) Buscar(seg);
    };

    _naPlaca = naPlaca;

    // Preenche já: sem áudio, os dois caminhos desenham preto, e a
    // tela não diria qual está valendo.
    _desenho->Text = naPlaca == 3 ? "3 na placa"
                                  : std::to_string(naPlaca) + " na placa, " + std::to_string(3 - naPlaca) + " na CPU";
    if (naPlaca < 3 && !_falhaShader.empty())
        _desenho->ToolTip = _falhaShader;
}

std::shared_ptr<Border> ExecucaoView::Moldurar(const std::shared_ptr<IInstrumento> &inst)
{
    auto b = std::make_shared<Border>();
    b->BorderBrush = SolidColorBrush(Color::FromRgb(0x3A, 0x2E, 0x1C));
    b->BorderThickness = Thickness(1);
    b->SetChild(inst->Elemento());
    return b;
}

void ExecucaoView::MontarAbas()
{
    for (size_t i = 0; i < NomesAbas.size(); i++)
    {
        auto r = std::make_shared<RadioButton>();
        r->Style = Tema::Estilo("AbaInterna");
        r->GroupName = "visualizadores";
        r->Content = NomesAbas[i];
        r->Tag = (int) i;
        RadioButton *rp = r.get();
        r->Checked += [this, rp](RoutedEventArgs &) { Mostrar(std::any_cast<int>(rp->Tag)); };   // AoTrocarAba
        Abas->Children.Add(r);
        if (i == 0) r->IsChecked = true;   // Forma de onda por padrão
    }

    // Escala vertical da onda de arquivo: linear (padrão) ou dBFS.
    _caixaDb = std::make_shared<CheckBox>();
    _caixaDb->Content = "escala em dB";
    _caixaDb->Foreground = SolidColorBrush(Color::FromRgb(0x8A, 0x76, 0x54));
    _caixaDb->FontSize = 11;
    _caixaDb->VerticalAlignment = Vistas::VerticalAlignment::Center;
    _caixaDb->Margin = Thickness(16, 0, 0, 0);
    _caixaDb->ToolTip = "Altura da onda em decibéis (dBFS) em vez de linear. "
                        "Realça os trechos baixos — a parte fraca do sinal "
                        "cresce e fica visível; desligada, a altura é "
                        "proporcional à amplitude.";
    _caixaDb->Checked += [this](RoutedEventArgs &) { if (_onda) _onda->Db(true); };
    _caixaDb->Unchecked += [this](RoutedEventArgs &) { if (_onda) _onda->Db(false); };
    Abas->Children.Add(_caixaDb);
}

/// <summary>
/// Põe no palco só o visualizador escolhido — ou os quatro, na aba
/// Painel. Reparenta as molduras e refaz a lista de ativos, que é o que
/// o laço de atualização percorre.
/// </summary>
void ExecucaoView::Mostrar(int aba)
{
    Soltar(_hostOsc); Soltar(_hostEspec); Soltar(_hostHisto); Soltar(_hostGonio);
    Soltar(_onda); Soltar(_especAnalise); Soltar(_medidores); Soltar(_espectrograma);
    Palco->Children.Clear();
    _ativos.clear();
    _abaAtual = aba;

    std::string nome = (aba >= 0 && aba < (int) NomesAbas.size()) ? NomesAbas[aba] : "";

    // A escala dB só faz sentido na onda de arquivo; some nas outras abas.
    if (_caixaDb)
        _caixaDb->Visibility = (nome == "Forma de onda") ? Vistas::Visibility::Visible : Vistas::Visibility::Collapsed;

    // A onda do arquivo (categoria A): vive do cache e do playhead,
    // então _ativos fica vazia — nem mais leve que isso.
    if (nome == "Forma de onda")
    {
        Palco->Children.Add(_onda);
        if (ObterPosicao) _onda->PosicaoSegundos(ObterPosicao());
        return;
    }
    else if (nome == "Espectro")
    {
        Palco->Children.Add(_especAnalise);
        _ativos.push_back(_especAnalise.get());
    }
    // Categoria A: vive do cache e do playhead, _ativos fica vazia.
    else if (nome == "Espectrograma")
    {
        Palco->Children.Add(_espectrograma);
        if (ObterPosicao) _espectrograma->PosicaoSegundos(ObterPosicao());
        return;
    }
    else if (nome == "Medidores")
    {
        Palco->Children.Add(_medidores);
        _ativos.push_back(_medidores.get());
    }
    else if (nome == "Histograma de nível")
    {
        Palco->Children.Add(_hostHisto);
        _ativos.push_back(_histo.get());
    }
    else if (nome == "Goniômetro")
    {
        Palco->Children.Add(_hostGonio);
        _ativos.push_back(_gonio.get());
    }
    else if (nome == "Painel")
    {
        Palco->Children.Add(MontarPainel());
        _ativos.push_back(_gonio.get()); _ativos.push_back(_espec.get());
        _ativos.push_back(_histo.get()); _ativos.push_back(_osc.get());
    }
    else
    {
        Palco->Children.Add(_hostOsc);
        _ativos.push_back(_osc.get());
    }

    // Feedback imediato ao trocar: se há áudio, desenha uma vez já, sem
    // esperar o próximo quadro.
    if (_analisador.Pronto())
        for (IInstrumento *inst : _ativos)
            inst->Atualizar(_analisador, _esq, _dir);
}

/// <summary>O mosaico 2x2 do Painel: os quatro instrumentos ao vivo.</summary>
std::shared_ptr<Grid> ExecucaoView::MontarPainel()
{
    auto g = std::make_shared<Grid>();
    g->ColumnDefinitions = {ColumnDefinition(), ColumnDefinition(GridLength(14)), ColumnDefinition(GridLength(1.25, GridUnitType::Star))};
    g->RowDefinitions = {RowDefinition(), RowDefinition(GridLength(14)), RowDefinition()};

    Colocar(g, Celula("GONIÔMETRO", _hostGonio), 0, 0);
    Colocar(g, Celula("ESPECTRO", _hostEspec), 0, 2);
    Colocar(g, Celula("HISTOGRAMA DE NÍVEL", _hostHisto), 2, 0);
    Colocar(g, Celula("FORMA DE ONDA POR CANAL", _hostOsc), 2, 2);
    return g;
}

UIElementPtr ExecucaoView::Celula(const std::string &rotulo, const std::shared_ptr<Border> &host)
{
    auto d = std::make_shared<DockPanel>();
    auto t = std::make_shared<TextBlock>(rotulo);
    // O estilo do rótulo vem dos recursos; aplicado no host pai já basta,
    // mas para a célula ser autossuficiente, define-se aqui.
    t->Foreground = SolidColorBrush(Color::FromRgb(0x8A, 0x76, 0x54));
    t->FontSize = 10; t->FontWeight = FontWeights::SemiBold;
    t->Margin = Thickness(2, 0, 0, 4);
    DockPanel::SetDock(t, Dock::Top);
    d->Children.Add(t);
    d->Children.Add(host);
    return d;
}

void ExecucaoView::Colocar(const std::shared_ptr<Grid> &g, const UIElementPtr &fe, int linha, int coluna)
{
    Grid::SetRow(fe, linha);
    Grid::SetColumn(fe, coluna);
    g->Children.Add(fe);
}

/// <summary>Tira uma moldura de onde quer que ela esteja, para reancorar.</summary>
void ExecucaoView::Soltar(const UIElementPtr &fe)
{
    UIElement *pai = fe ? fe->Parent : nullptr;
    if (auto *pnl = dynamic_cast<Panel *>(pai)) { pnl->Children.Remove(fe); return; }
    if (auto *dec = dynamic_cast<Border *>(pai)) { dec->SetChild(nullptr); return; }
    if (auto *cc = dynamic_cast<ContentControl *>(pai)) { cc->Content = nullptr; }
}

void ExecucaoView::Ativar(bool ligado)
{
    _ligado = ligado;
    AtualizarAssinatura();
}

/// <summary>
/// Inscrito no Rendering, o WPF redesenha a janela a cada quadro, mesmo
/// que o tratador não faça nada. Só vale com o módulo à vista e sem
/// vídeo no palco: vendo filme, não há instrumento para desenhar.
/// </summary>
void ExecucaoView::AtualizarAssinatura()
{
    bool blnQuer = _ligado && !ModoVideo();
    if (blnQuer == _blnAssinado) return;
    _blnAssinado = blnQuer;
    if (blnQuer) _rendering.Start();
    else _rendering.Stop();
}

/// <summary>
/// Sincronizado com o ciclo de composição do WPF, e não num
/// DispatcherTimer.
///
/// O DispatcherTimer roda em prioridade Background — abaixo da
/// renderização —, então só bate quando o dispatcher não tem nada
/// melhor a fazer. O resultado são quadros **irregulares**, e
/// irregularidade se percebe como irrealismo mesmo quando a média está
/// boa. Aqui cada quadro chega junto com o ciclo de vídeo.
/// </summary>
// LINUX: aqui é um DispatcherTimer de 16 ms: o laço do xOS não tem prioridades, o relógio bate na
// hora e a janela redesenha logo depois (Janela.cpp); a regra dos 28 ms abaixo segura os 30 qps.
void ExecucaoView::AoRenderizar()
{
    // Vídeo na tela: os instrumentos estão escondidos e sem análise.
    if (!ObterPosicao || ModoVideo()) return;

    // O evento pode disparar mais de uma vez para o mesmo quadro.
    // (RenderingEventArgs.RenderingTime: o relógio só bate uma vez por volta do laço)

    // Uma atualização a cada dois ciclos de vídeo: a 60 Hz dá 30
    // quadros por segundo, e regulares, que é o que importa.
    double agora = Cronometro();
    if (agora - _segundosDoUltimoQuadro < 0.028) return;
    _segundosDoUltimoQuadro = agora;

    Atualizar(ObterPosicao());
}

/// <summary>
/// Cada leitura recebe um gabarito: o maior texto que ela pode vir a
/// mostrar. A largura do bloco é travada nesse gabarito, e não no valor
/// do momento — sem isso a linha inteira se desloca toda vez que um
/// número passa de um para dois dígitos e volta. Num painel de
/// instrumento, número que anda de lugar é ilegível.
/// </summary>
void ExecucaoView::MontarLeituras()
{
    _picoE = Leitura("PICO ESQ", "-100,0 dB");
    _picoD = Leitura("PICO DIR", "-100,0 dB");
    _rms = Leitura("RMS", "-100,0 dB");
    _correl = Leitura("CORRELAÇÃO", "-1,00");
    _taxa = Leitura("FORMATO", "2ch 192k");
    _quadros = Leitura("QUADROS/S", "60");
    _custo = Leitura("CUSTO", "999,9 ms");
    _desenho = Leitura("DESENHO", "0 na placa, 3 na CPU");
}

std::shared_ptr<TextBlock> ExecucaoView::Leitura(const std::string &rotulo, const std::string &gabarito)
{
    auto sp = std::make_shared<StackPanel>();
    sp->Margin = Thickness(0, 0, 26, 0);
    auto r = std::make_shared<TextBlock>(rotulo);
    r->Style = Tema::Estilo("LeituraRotulo");
    auto v = std::make_shared<TextBlock>("—");
    v->Style = Tema::Estilo("Leitura");
    sp->Children.Add(r);
    sp->Children.Add(v);

    // Medido, não chutado: pixels fixos errariam com outra fonte ou
    // outro DPI, e o rótulo é de largura variável (Segoe UI) enquanto o
    // valor é monoespaçado (Consolas).
    FonteTexto fr, fv;
    fr.Tamanho = 10;
    fv.Familia = "Consolas";
    fv.Tamanho = 13;
    sp->Width = std::ceil(std::max(Texto::Largura(fr, rotulo), Texto::Largura(fv, gabarito)));

    PainelLeituras->Children.Add(sp);
    return v;
}

/// <summary>
/// Alinha o número à direita num campo de largura fixa. Consolas é
/// monoespaçada, então isto prende a vírgula decimal numa coluna só —
/// travar a largura do bloco impede a linha de andar, mas sem isto os
/// dígitos ainda dançariam dentro do bloco.
/// </summary>
std::string ExecucaoView::Coluna(const std::string &numero, int largura)
{
    // numero.PadLeft(largura): conta caracteres, não bytes ("—" é um só)
    int n = (int) Base::Caracteres(numero);
    return n >= largura ? numero : std::string((size_t) (largura - n), ' ') + numero;
}

void ExecucaoView::RecarregarEncarteSeDoAlbum(const std::string &strPastaAlbum)
{
    if (_arquivoAtual.empty() || strPastaAlbum.empty()) return;
    std::string strPastaAtual = Base::PastaDe(_arquivoAtual);
    // LINUX: TrimEnd('\\') do Windows vira o da barra do Linux
    std::string pasta = strPastaAlbum;
    while (pasta.size() > 1 && pasta.back() == '/') pasta.pop_back();
    if (Base::IgualSemCaixa(strPastaAtual, pasta))
        Encarte->Carregar(_arquivoAtual);
}

// ===============================================================
void ExecucaoView::DefinirVideo(const std::string &caminho, const std::string &titulo, const std::string &artista)
{
    (*_geracaoOnda)++;
    (*_geracaoEg)++;
    _analisador.Fechar();
    Encarte->Soltar();
    LimparLeituras();
    _arquivoAtual = caminho;

    TxtTituloVideo->Text = titulo.empty() ? "—" : titulo;
    TxtDetalheVideo->Text = artista;
    MostrarPalcoDeVideo(true);
}

void ExecucaoView::AvisarVideo(const std::string &strTexto)
{
    TxtAvisoVideo->Text = strTexto;
}

void ExecucaoView::AoPedirTelaCheia()
{
    std::string strFalha = PedirTelaCheia ? PedirTelaCheia() : "o player não está disponível";
    TxtAvisoVideo->Text = strFalha;
}

void ExecucaoView::MostrarPalcoDeVideo(bool blnVideo)
{
    TxtAvisoVideo->Text = "";
    _modoVideo = blnVideo;
    PalcoVideo->Visibility = blnVideo ? Vistas::Visibility::Visible : Vistas::Visibility::Collapsed;
    Instrumentacao->Visibility = blnVideo ? Vistas::Visibility::Collapsed : Vistas::Visibility::Visible;
    AtualizarAssinatura();
}

void ExecucaoView::DefinirFaixa(const std::string &caminho, const std::string &titulo, const std::string &artista,
                                const std::string &album)
{
    // Vindo de um vídeo: a mesma URL não é a mesma situação — a análise
    // nunca foi aberta para ele.
    if (ModoVideo()) { MostrarPalcoDeVideo(false); _arquivoAtual.clear(); }
    TxtTitulo->Text = titulo.empty() ? "—" : titulo;
    TxtArtista->Text = artista;
    TxtAlbum->Text = album;

    if (!_arquivoAtual.empty() && Base::IgualSemCaixa(caminho, _arquivoAtual)) return;
    if (caminho.empty() && _arquivoAtual.empty()) return;   // string.Equals(null, null)
    _arquivoAtual = caminho;
    LimparLeituras();

    CarregarCapa(caminho);
    Encarte->Carregar(caminho);

    _analisador.Fechar();
    if (!caminho.empty() && _analisador.Abrir(caminho))
        TxtFormato->Text = std::to_string(_analisador.Canais()) + " canais · " + Formato::Inteiro(_analisador.Taxa()) + " Hz";
    else
        TxtFormato->Text = !_analisador.Falha().empty() ? _analisador.Falha() : "sem análise";

    _taxa->Text = _analisador.Pronto()
                      ? std::to_string(_analisador.Canais()) + "ch " + std::to_string(_analisador.Taxa() / 1000) + "k"
                      : "—";

    GerarOnda(caminho);
    GerarEspectrograma(caminho);
}

/// <summary>
/// Prepara a onda do arquivo: do cache se já existir (instantâneo);
/// senão, decodifica em segundo plano num núcleo do processador, salva
/// o cache e mostra quando pronto. O contador de geração descarta um
/// resultado que chegue depois de o usuário ter trocado de faixa.
/// </summary>
void ExecucaoView::GerarOnda(const std::string &caminho)
{
    (*_geracaoOnda)++;
    int geracao = *_geracaoOnda;
    _onda->Definir(nullptr);
    _medidores->DefinirArquivo(-Infinito, 0, -Infinito);

    if (caminho.empty() || !Base::ArquivoExiste(caminho)) return;

    // Tudo fora da interface, inclusive ler o cache do disco: um cache
    // grande, lido aqui, travava a tela na troca de faixa.
    auto g = _geracaoOnda;
    std::thread t([this, caminho, geracao, g] {
        PrioridadeAbaixoDoNormal();
        std::shared_ptr<Modelo::OndaCache> cache = Modelo::OndaCache::Carregar(caminho);
        if (cache)
        {
            Base::Despacha([this, cache, geracao, g] {
                if (geracao != *g) return;
                _onda->Definir(cache);
                _medidores->DefinirArquivo(cache->IntegradoLufs, cache->LraLu, cache->PicoArquivoDb);
            });
            return;
        }
        std::string strFalha;
        std::shared_ptr<Modelo::OndaCache> oc =
            Modelo::OndaCache::Gerar(caminho, nullptr, [geracao, g] { return geracao != *g; }, strFalha);
        if (geracao != *g) return;
        if (!oc)
        {
            if (!strFalha.empty())
                Base::Despacha([this, strFalha, geracao, g] { if (geracao == *g) _onda->MostrarFalha(strFalha); });
            return;
        }
        oc->Salvar(caminho);
        Base::Despacha([this, oc, geracao, g] {
            if (geracao != *g) return;
            _onda->Definir(oc);
            _medidores->DefinirArquivo(oc->IntegradoLufs, oc->LraLu, oc->PicoArquivoDb);
        });
    });
    t.detach();   // IsBackground = true
}

/// <summary>
/// Prepara o espectrograma do arquivo, mesma mecânica da onda: cache
/// instantâneo se existir; senão STFT em segundo plano, salva e mostra.
/// </summary>
void ExecucaoView::GerarEspectrograma(const std::string &caminho)
{
    (*_geracaoEg)++;
    int geracao = *_geracaoEg;
    _espectrograma->Definir(nullptr);

    if (caminho.empty() || !Base::ArquivoExiste(caminho)) return;

    auto g = _geracaoEg;
    std::thread t([this, caminho, geracao, g] {
        PrioridadeAbaixoDoNormal();
        std::shared_ptr<Modelo::EspectrogramaCache> cache = Modelo::EspectrogramaCache::Carregar(caminho);
        if (cache)
        {
            Base::Despacha([this, cache, geracao, g] { if (geracao == *g) _espectrograma->Definir(cache); });
            return;
        }
        std::string strFalha;
        std::shared_ptr<Modelo::EspectrogramaCache> eg =
            Modelo::EspectrogramaCache::Gerar(caminho, nullptr, [geracao, g] { return geracao != *g; }, strFalha);
        if (geracao != *g) return;
        if (!eg)
        {
            if (!strFalha.empty())
                Base::Despacha([this, strFalha, geracao, g] { if (geracao == *g) _espectrograma->MostrarFalha(strFalha); });
            return;
        }
        eg->Salvar(caminho);
        Base::Despacha([this, eg, geracao, g] {
            if (geracao == *g) _espectrograma->Definir(eg);
        });
    });
    t.detach();
}

void ExecucaoView::Atualizar(double posicao)
{
    // Posição negativa: nada carregado (parado). Pausado, a posição
    // vem: o cursor da onda e do espectrograma acompanha (clicar na onda
    // pausada move o cursor; trocar de aba não o esconde).
    if (posicao < 0) return;
    std::string nomeAtual = (_abaAtual >= 0 && _abaAtual < (int) NomesAbas.size()) ? NomesAbas[_abaAtual] : "";
    if (nomeAtual == "Forma de onda" && _onda) _onda->PosicaoSegundos(posicao);
    else if (nomeAtual == "Espectrograma" && _espectrograma) _espectrograma->PosicaoSegundos(posicao);

    // Os instrumentos só andam tocando: pausado, ficam como estão em
    // vez de rodar em falso.
    if (!_analisador.Pronto() || (EstaTocando && !EstaTocando())) return;

    // Mede o que é nosso. Se o CUSTO for baixo e os QUADROS também,
    // o limite está no ciclo de vídeo, não no nosso trabalho — e são
    // problemas com soluções opostas.
    double t0 = Cronometro() * 1000.0;

    _analisador.Acompanhar(posicao);
    _analisador.Copiar(_esq, _dir);

    // Só o que está à vista. Na aba Painel são os quatro; nas demais, um.
    for (size_t i = 0; i < _ativos.size(); i++)
        _ativos[i]->Atualizar(_analisador, _esq, _dir);

    // A onda de arquivo não é redesenhada por quadro; só o playhead
    // corre (acima), e é uma linha leve por cima.

    _picoE->Text = Coluna(Fixo(AnalisadorAudio::Db(_analisador.PicoEsq), 1), 6) + " dB";
    _picoD->Text = Coluna(Fixo(AnalisadorAudio::Db(_analisador.PicoDir), 1), 6) + " dB";
    _rms->Text = Coluna(Fixo(AnalisadorAudio::Db((_analisador.RmsEsq + _analisador.RmsDir) / 2.0), 1), 6) + " dB";

    double c = _analisador.Correlacao;
    _correl->Text = (c >= 0 ? "+" : "") + Fixo(c, 2);
    // Correlação negativa avisa que somar os canais em mono cancelaria
    // parte do som. Vale uma cor diferente.
    _correl->Foreground = c < 0 ? SolidColorBrush(Color::FromRgb(0xE0, 0x56, 0x3A))
                                : SolidColorBrush(Color::FromRgb(0xFF, 0xB4, 0x54));

    // Quadros e custo medidos, não prometidos.
    _somaCusto += Cronometro() * 1000.0 - t0;
    _contaQuadros++;
    double agora = Cronometro();
    if (agora - _marcaQuadros >= 1.0)
    {
        _quadros->Text = Coluna(Fixo(_contaQuadros / (agora - _marcaQuadros), 0), 2);
        _custo->Text = Coluna(Fixo(_somaCusto / std::max(1, _contaQuadros), 1), 5) + " ms";
        _contaQuadros = 0;
        _somaCusto = 0;
        _marcaQuadros = agora;
    }
}

void ExecucaoView::Parar()
{
    (*_geracaoOnda)++;
    (*_geracaoEg)++;
    _analisador.Fechar();
    Encarte->Soltar();
    _arquivoAtual.clear();
    LimparLeituras();
    // Parado, não há vídeo a mostrar: o módulo volta ao normal.
    if (ModoVideo()) MostrarPalcoDeVideo(false);
}

/// <summary>Leituras ao vivo de volta ao "—": não mostram a faixa anterior.</summary>
void ExecucaoView::LimparLeituras()
{
    if (!_picoE) return;
    _picoE->Text = Coluna("—", 6) + " dB";
    _picoD->Text = Coluna("—", 6) + " dB";
    _rms->Text = Coluna("—", 6) + " dB";
    _correl->Text = "—";
}

// ===============================================================
void ExecucaoView::CarregarCapa(const std::string &caminho)
{
    ImgCapa->Source = nullptr;
    TxtSemCapa->Visibility = Vistas::Visibility::Visible;
    if (caminho.empty()) return;

    try
    {
        // LINUX: EditorAsf vira EditorFlac (a capa é o bloco PICTURE)
        Interop::EditorFlac ed;
        if (!ed.Abrir(caminho)) return;
        std::string mime, descricao;
        std::vector<uint8_t> bytes = ed.LerImagem(mime, descricao);
        if (bytes.empty()) return;

        std::shared_ptr<BitmapSource> bmp = BitmapSource::DeBytes(bytes);
        ImgCapa->Source = bmp;
        TxtSemCapa->Visibility = Vistas::Visibility::Collapsed;
    }
    catch (...) {}
}

} // namespace ARCA::Vistas

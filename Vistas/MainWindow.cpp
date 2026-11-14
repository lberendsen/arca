// ARCA no xOS - Vistas/MainWindow (de MainWindow.xaml e MainWindow.xaml.cs): o casco.
// InitializeComponent e o MainWindow.xaml traduzido elemento por elemento (os comentarios do XAML
// vem junto); o resto e a parte do MainWindow.xaml.cs que e do casco. O que e da BIBLIOTECA e do
// tocador ficou nos ganchos declarados em MainWindow.h.
#include "MainWindow.h"

namespace ARCA::Vistas {

namespace {
const char *const SetaEsq = "\xE2\x97\x80";     // ◀ (&#9664;)
const char *const Quadrado = "\xE2\x96\xA0";    // ■ (&#9632;)
const char *const SetaDir = "\xE2\x96\xB6";     // ▶ (&#9654;)
const char *const Barra = "\xE2\x9D\x99";       // ❙ (&#10073;)
} // namespace

MainWindow::MainWindow()
{
    InitializeComponent();
    Loaded += [this](RoutedEventArgs &) { AoCarregar(); };
    Closed += [this](RoutedEventArgs &) { AoFechar(); };
    Closing += [this](CancelEventArgs &e) { AoPedirFechar(e); };
    // 250 ms. Cheguei a subir para 600 supondo que a consulta fosse
    // cara; medido, ela leva 13 ms. O adiamento tinha virado o maior
    // custo do conjunto — mitigação do problema errado.
    _adiadorBusca.Interval = TimeSpan::FromMilliseconds(250);
    _adiadorBusca.Tick += [this] {
        _adiadorBusca.Stop();
        if (AplicarBusca) AplicarBusca();
    };

    CaixaBusca->TextChanged += [this](TextChangedEventArgs &) {
        DicaBusca->Visibility = CaixaBusca->Text().empty() ? Vistas::Visibility::Visible : Vistas::Visibility::Collapsed;
        // Adiar: consultar a cada tecla faria uma consulta por letra.
        _adiadorBusca.Stop();
        _adiadorBusca.Start();
    };

    // Enter busca na hora, sem esperar o adiamento.
    CaixaBusca->KeyDown += [this](KeyEventArgs &k) {
        if (k.Key == Key::Enter)
        {
            _adiadorBusca.Stop();
            if (AplicarBusca) AplicarBusca();
        }
        else if (k.Key == Key::Escape)
        {
            CaixaBusca->Text = std::string();
        }
    };

    // LINUX: a CaixaAlbum, a DicaAlbum e a ListaAlbuns sao do Grid ModuloBiblioteca: os seus
    // manipuladores (TextChanged, KeyDown, PreviewMouseLeftButtonUp) vao com a frente da BIBLIOTECA.

    BtnAnterior->Click += [this](RoutedEventArgs &) {
        if (Pular) Pular(-1);
    };
    BtnProximo->Click += [this](RoutedEventArgs &) {
        if (Pular) Pular(+1);
    };
}

void MainWindow::InitializeComponent()
{
    // <Window Title="ARCA" Width="1400" Height="880" MinWidth="1080" MinHeight="660"
    //         WindowState="Maximized" WindowStartupLocation="CenterScreen" Icon="arca.ico"
    //         Background="{StaticResource Canvas}" FontFamily="{StaticResource Fonte}"
    //         TextOptions.TextFormattingMode="Display" UseLayoutRounding="True">
    Title = "ARCA";
    Width = 1400;
    Height = 880;
    MinWidth = 1080;
    MinHeight = 660;
    WindowState = Vistas::WindowState::Maximized;
    WindowStartupLocation = Vistas::WindowStartupLocation::CenterScreen;
    Background = Tema::Recurso("Canvas");
    FontFamily = Tema::Fonte;
    ClasseX = "arca";

    auto raiz = std::make_shared<Grid>();
    auto doca = std::make_shared<DockPanel>();
    raiz->Children.Add(doca);

    // ===== Abas de módulo ========================================
    {
        auto b = std::make_shared<Border>();
        DockPanel::SetDock(b, Dock::Top);
        b->Background = Tema::Recurso("BarraFerramentas");
        b->BorderBrush = Tema::Recurso("RodapeBorda");
        b->BorderThickness = Thickness(0, 0, 0, 1);
        b->Padding = Thickness(10, 8, 10, 0);
        auto g = std::make_shared<Grid>();
        g->ColumnDefinitions = {ColumnDefinition(GridLength::Auto()), ColumnDefinition(GridLength::Star(1)),
                                ColumnDefinition(GridLength::Auto())};
        AbasModulos = std::make_shared<StackPanel>();
        AbasModulos->Name = "AbasModulos";
        AbasModulos->Orientation = Orientation::Horizontal;
        g->Children.Add(AbasModulos);
        TxtMotor = std::make_shared<TextBlock>();
        TxtMotor->Name = "TxtMotor";
        Grid::SetColumn(TxtMotor, 2);
        TxtMotor->Style = Tema::Estilo("Fraco");
        TxtMotor->VerticalAlignment = Vistas::VerticalAlignment::Center;
        TxtMotor->Margin = Thickness(0, 0, 4, 6);
        g->Children.Add(TxtMotor);
        b->SetChild(g);
        doca->Children.Add(b);
    }

    // ===== Trilha de navegação e busca ===========================
    {
        auto b = std::make_shared<Border>();
        DockPanel::SetDock(b, Dock::Top);
        b->Background = Tema::Recurso("BarraTrilha");
        b->BorderBrush = Tema::Recurso("LinhaCabecalho");
        b->BorderThickness = Thickness(0, 0, 0, 1);
        b->Padding = Thickness(12, 7);
        auto g = std::make_shared<Grid>();
        g->ColumnDefinitions = {ColumnDefinition(GridLength::Star(1)), ColumnDefinition(GridLength::Auto())};
        TxtTrilha = std::make_shared<TextBlock>();
        TxtTrilha->Name = "TxtTrilha";
        TxtTrilha->VerticalAlignment = Vistas::VerticalAlignment::Center;
        TxtTrilha->Foreground = Tema::Recurso("Texto");
        TxtTrilha->FontSize = 13;
        g->Children.Add(TxtTrilha);
        auto gb = std::make_shared<Grid>();
        Grid::SetColumn(gb, 1);
        gb->Width = 250;
        CaixaBusca = std::make_shared<TextBox>();
        CaixaBusca->Name = "CaixaBusca";
        gb->Children.Add(CaixaBusca);
        DicaBusca = std::make_shared<TextBlock>("Pesquisar");
        DicaBusca->Name = "DicaBusca";
        DicaBusca->IsHitTestVisible = false;
        DicaBusca->Foreground = Tema::Recurso("TextoFraco");
        DicaBusca->FontSize = 12;
        DicaBusca->VerticalAlignment = Vistas::VerticalAlignment::Center;
        DicaBusca->Margin = Thickness(8, 0, 0, 0);
        gb->Children.Add(DicaBusca);
        g->Children.Add(gb);
        b->SetChild(g);
        doca->Children.Add(b);
    }

    // ===== Rodapé de reprodução ==================================
    {
        Rodape = std::make_shared<Border>();
        Rodape->Name = "Rodape";
        DockPanel::SetDock(Rodape, Dock::Bottom);
        Rodape->Background = Tema::Recurso("Rodape");
        Rodape->BorderBrush = Tema::Recurso("RodapeBorda");
        Rodape->BorderThickness = Thickness(0, 1, 0, 0);
        auto d = std::make_shared<DockPanel>();

        // Barra de progresso colada no topo do rodapé, como no WMP12
        // IsMoveToPointEnabled: clicar na trilha leva ao ponto clicado. Sem
        // ele, o clique andava um LargeChange (=1, a faixa inteira) e
        // pulava para o fim ou para o começo.
        SliderPosicao = std::make_shared<Slider>();
        SliderPosicao->Name = "SliderPosicao";
        DockPanel::SetDock(SliderPosicao, Dock::Top);
        SliderPosicao->Minimum = 0;
        SliderPosicao->Maximum = 1;
        SliderPosicao->IsMoveToPointEnabled = true;
        SliderPosicao->SmallChange = 0.01;
        SliderPosicao->LargeChange = 0.05;
        SliderPosicao->Margin = Thickness(0, -6, 0, 0);
        SliderPosicao->PreviewMouseLeftButtonUp += [this](MouseButtonEventArgs &) {
            if (AoSoltarPosicao) AoSoltarPosicao();
        };
        d->Children.Add(SliderPosicao);

        auto g = std::make_shared<Grid>();
        g->Margin = Thickness(12, 4, 12, 10);
        g->ColumnDefinitions = {ColumnDefinition(GridLength::Auto()), ColumnDefinition(GridLength::Star(1)),
                                ColumnDefinition(GridLength::Auto()), ColumnDefinition(GridLength::Star(1)),
                                ColumnDefinition(GridLength::Auto())};

        // Vaga da miniatura do vídeo. O controle do WMP não mora
        // aqui: mora na CamadaVideo, por cima da janela, e é
        // posicionado sobre esta vaga (fora de EM EXECUÇÃO) ou
        // sobre o palco do EM EXECUÇÃO (tocando vídeo).
        auto sp = std::make_shared<StackPanel>();
        sp->Orientation = Orientation::Horizontal;
        sp->VerticalAlignment = Vistas::VerticalAlignment::Center;
        VagaVideo = std::make_shared<Border>();
        VagaVideo->Name = "VagaVideo";
        VagaVideo->Width = 0;
        VagaVideo->Height = 62;
        VagaVideo->Margin = Thickness(0, 0, 10, 0);
        sp->Children.Add(VagaVideo);
        auto textos = std::make_shared<StackPanel>();
        textos->VerticalAlignment = Vistas::VerticalAlignment::Center;
        textos->MaxWidth = 280;
        TxtTitulo = std::make_shared<TextBlock>("\xE2\x80\x94");   // —
        TxtTitulo->Name = "TxtTitulo";
        TxtTitulo->FontSize = 13;
        TxtTitulo->Foreground = Tema::Recurso("Texto");
        TxtTitulo->TextTrimming = TextTrimming::CharacterEllipsis;
        textos->Children.Add(TxtTitulo);
        TxtArtista = std::make_shared<TextBlock>();
        TxtArtista->Name = "TxtArtista";
        TxtArtista->Style = Tema::Estilo("Fraco");
        TxtArtista->TextTrimming = TextTrimming::CharacterEllipsis;
        TxtArtista->TextWrapping = TextWrapping::NoWrap;
        textos->Children.Add(TxtArtista);
        sp->Children.Add(textos);
        g->Children.Add(sp);

        TxtTempo = std::make_shared<TextBlock>("0:00 / 0:00");
        TxtTempo->Name = "TxtTempo";
        Grid::SetColumn(TxtTempo, 1);
        TxtTempo->Style = Tema::Estilo("Fraco");
        TxtTempo->HorizontalAlignment = Vistas::HorizontalAlignment::Right;
        TxtTempo->VerticalAlignment = Vistas::VerticalAlignment::Center;
        TxtTempo->Margin = Thickness(0, 0, 18, 0);
        g->Children.Add(TxtTempo);

        // Transporte centralizado, simétrico em volta do orbe:
        // ◀◀  ■  (▶)  ❙❙  ▶▶. Cada botão tem um significado só e
        // nunca troca de ícone: Tocar obedece à seleção, Pausar
        // segura e solta a faixa atual, Parar a encerra.
        auto tr = std::make_shared<StackPanel>();
        Grid::SetColumn(tr, 2);
        tr->Orientation = Orientation::Horizontal;
        tr->VerticalAlignment = Vistas::VerticalAlignment::Center;
        auto transporte = [](const std::string &glifo, double largura, double fonte, Thickness margem, const std::string &dica) {
            auto b = std::make_shared<Button>();
            b->Content = glifo;
            b->Width = largura;
            b->Padding = Thickness(0, 3);
            b->Style = Tema::Estilo("BotaoTransporte");
            b->FontSize = fonte;
            b->Margin = margem;
            b->ToolTip = dica;
            return b;
        };
        BtnAnterior = transporte(std::string(SetaEsq) + SetaEsq, 38, 10, Thickness(0, 0, 6, 0), "Anterior");
        BtnAnterior->Name = "BtnAnterior";
        tr->Children.Add(BtnAnterior);
        BtnParar = transporte(Quadrado, 30, 11, Thickness(0), "Parar");
        BtnParar->Name = "BtnParar";
        BtnParar->IsEnabled = false;
        BtnParar->Click += [this](RoutedEventArgs &) {
            if (AoParar) AoParar();
        };
        tr->Children.Add(BtnParar);
        BtnTocar = std::make_shared<Button>();
        BtnTocar->Name = "BtnTocar";
        BtnTocar->Width = 46;
        BtnTocar->Height = 46;
        BtnTocar->Margin = Thickness(8, 0);
        BtnTocar->ToolTip = "Reproduzir a faixa selecionada";
        BtnTocar->Template = Gabarito::OrbePlay;
        BtnTocar->Click += [this](RoutedEventArgs &) {
            if (AoTocar) AoTocar();
        };
        IconeTocar = std::make_shared<TextBlock>(SetaDir);
        IconeTocar->Name = "IconeTocar";
        IconeTocar->Foreground = SolidColorBrush(Color::De("#FFFFF4E0"));
        IconeTocar->FontSize = 17;
        IconeTocar->Margin = Thickness(3, 0, 0, 0);
        BtnTocar->Content = IconeTocar;
        tr->Children.Add(BtnTocar);
        BtnPausar = transporte(std::string(Barra) + Barra, 30, 11, Thickness(0), "Pausar");
        BtnPausar->Name = "BtnPausar";
        BtnPausar->IsEnabled = false;
        BtnPausar->Click += [this](RoutedEventArgs &) {
            if (AoPausar) AoPausar();
        };
        tr->Children.Add(BtnPausar);
        BtnProximo = transporte(std::string(SetaDir) + SetaDir, 38, 10, Thickness(6, 0, 0, 0), "Próximo");
        BtnProximo->Name = "BtnProximo";
        tr->Children.Add(BtnProximo);
        g->Children.Add(tr);

        auto vol = std::make_shared<StackPanel>();
        Grid::SetColumn(vol, 4);
        vol->Orientation = Orientation::Horizontal;
        vol->VerticalAlignment = Vistas::VerticalAlignment::Center;
        auto rv = std::make_shared<TextBlock>("Volume");
        rv->Style = Tema::Estilo("Fraco");
        rv->VerticalAlignment = Vistas::VerticalAlignment::Center;
        rv->Margin = Thickness(0, 0, 8, 0);
        vol->Children.Add(rv);
        SliderVolume = std::make_shared<Slider>();
        SliderVolume->Name = "SliderVolume";
        SliderVolume->Width = 110;
        SliderVolume->Minimum = 0;
        SliderVolume->Maximum = 100;
        SliderVolume->Value = 70;
        SliderVolume->VerticalAlignment = Vistas::VerticalAlignment::Center;
        SliderVolume->ValueChanged += [this](RoutedPropertyChangedEventArgs<double> &e) {
            if (AoMudarVolume) AoMudarVolume(e.NewValue);
        };
        vol->Children.Add(SliderVolume);
        g->Children.Add(vol);

        d->Children.Add(g);
        Rodape->SetChild(d);
        doca->Children.Add(Rodape);
    }

    // ===== Corpo: o módulo ativo =================================
    Corpo = std::make_shared<Grid>();
    doca->Children.Add(Corpo);

    // LINUX: a CamadaVideo (a ilha do controle do WMP, por cima de tudo) e do tocador: o video do
    // xOS sera desenhado pela frente do EM EXECUCAO no lugar que PosicionarVideo calcular.
    Content = raiz;
}

void MainWindow::EncaixaModulos(std::shared_ptr<VistaModulo> aquisicao, std::shared_ptr<VistaModulo> biblioteca,
                                std::shared_ptr<VistaModulo> execucao)
{
    Corpo->Children.Clear();
    // a ordem do XAML: BIBLIOTECA, EM EXECUCAO (Collapsed), AQUISICAO (Collapsed)
    ModuloBiblioteca = biblioteca;
    ModuloExecucao = execucao;
    ModuloAquisicao = aquisicao;
    if (ModuloBiblioteca)
    {
        ModuloBiblioteca->Name = "ModuloBiblioteca";
        Corpo->Children.Add(ModuloBiblioteca);
    }
    if (ModuloExecucao)
    {
        ModuloExecucao->Name = "ModuloExecucao";
        ModuloExecucao->Visibility = Vistas::Visibility::Collapsed;
        Corpo->Children.Add(ModuloExecucao);
    }
    if (ModuloAquisicao)
    {
        ModuloAquisicao->Name = "ModuloAquisicao";
        ModuloAquisicao->Visibility = Vistas::Visibility::Collapsed;
        Corpo->Children.Add(ModuloAquisicao);
    }
}

// ===============================================================
void MainWindow::AoCarregar()
{
    if (_iniciado) return;
    _iniciado = true;

    MontarAbas();

    // LINUX: daqui em diante o original hospeda o WMP (TxtMotor, Avisar, _player, ModuloExecucao
    // .ObterPosicao/.EstaTocando/.Buscar/.PedirTelaCheia), cria a Biblioteca, liga
    // ModuloAquisicao.AcrescentarNaBiblioteca, preenche ListaCategorias e liga o _relogio de
    // 300 ms (AoBater). Tudo isso e do tocador e da BIBLIOTECA, que se ligam neste evento.
    Carregado();
}

/// <summary>
/// Fechar no meio de uma extração, identificação ou integração deixa
/// arquivo pela metade (na área de trabalho ou, na integração, no
/// acervo). Pergunta antes, com "Não" como padrão.
/// </summary>
void MainWindow::AoPedirFechar(CancelEventArgs &e)
{
    if (!ModuloAquisicao || !ModuloAquisicao->Ocupado()) return;
    if (MessageBox::Show(this,
                         "A AQUISIÇÃO está trabalhando (extração, identificação ou integração no acervo).\r\n\r\n"
                         "Fechar agora pode deixar arquivos pela metade. Fechar mesmo assim?",
                         "ARCA", MessageBoxButton::YesNo, MessageBoxImage::Warning, MessageBoxResult::No) != MessageBoxResult::Yes)
        e.Cancel = true;
}

void MainWindow::AoFechar()
{
    // _relogio.Stop(); _player.Parar(): do tocador
    _adiadorBusca.Stop();
    Fechou();
}

// ===============================================================
void MainWindow::MontarAbas()
{
    std::vector<Modulo> modulos;

    modulos.push_back(Modulo{"AQUISIÇÃO", "Aquisição", false, true, false});
    modulos.push_back(Modulo{"BIBLIOTECA", "Biblioteca", false, false, true});
    modulos.push_back(Modulo{"EM EXECUÇÃO", "Em execução", true, false, false});

    for (const Modulo &m : modulos)
    {
        auto r = std::make_shared<RadioButton>();
        r->Style = Tema::Estilo("AbaModulo");
        r->GroupName = "modulos";
        r->Content = m.Nome;
        r->Tag = m;
        // Ligar o evento ANTES de marcar: no inicializador, o IsChecked
        // dispara o Checked antes de haver quem escute, e a aba padrão
        // acaba selecionada sem nunca ter configurado a tela.
        RadioButton *rp = r.get();
        r->Checked += [this, rp](RoutedEventArgs &) { AoTrocarModulo(*rp); };
        AbasModulos->Children.Add(r);

        // Só o módulo inicial se marca. Antes era "todo módulo pronto",
        // e quando EM EXECUÇÃO ficou pronto passaram a ser dois no mesmo
        // grupo — o último marcado vencia, e o app abria no lugar errado.
        if (m.Inicial) r->IsChecked = true;
        if (m.Execucao) _abaExecucao = r;
    }
}

void MainWindow::AoTrocarModulo(RadioButton &r)
{
    const Modulo &m = std::any_cast<const Modulo &>(r.Tag);
    TxtTrilha->Text = m.Trilha;
    _trilhaAtual = m.Trilha;

    _emExecucao = m.Execucao;
    AplicarTomRodape(m.Execucao);
    if (ModuloExecucao) ModuloExecucao->Ativar(m.Execucao);

    bool naBiblioteca = !m.Execucao && !m.Aquisicao;

    if (ModuloBiblioteca) ModuloBiblioteca->Visibility = naBiblioteca ? Vistas::Visibility::Visible : Vistas::Visibility::Collapsed;
    if (ModuloExecucao) ModuloExecucao->Visibility = m.Execucao ? Vistas::Visibility::Visible : Vistas::Visibility::Collapsed;
    if (ModuloAquisicao) ModuloAquisicao->Visibility = m.Aquisicao ? Vistas::Visibility::Visible : Vistas::Visibility::Collapsed;

    // O vídeo muda de lugar com o módulo: palco no EM EXECUÇÃO, vaga do
    // rodapé nos outros. Depois do layout, quando as posições valem.
    DepoisDoLayout([this] {
        if (PosicionarVideo) PosicionarVideo();
    });

    // LINUX: o bloco "if (naBiblioteca)" do original (reler os albuns desatualizados e pôr na trilha
    // "Biblioteca  ›  categoria  ›  album") e da BIBLIOTECA: vai no Ativar(true) dela, que le
    // TrilhaDoModulo() e escreve TxtTrilha. A AQUISICAO recebe Ativar tambem (o original nao tinha).
    if (ModuloBiblioteca) ModuloBiblioteca->Ativar(naBiblioteca);
    if (ModuloAquisicao) ModuloAquisicao->Ativar(m.Aquisicao);
}

/// <summary>
/// O rodapé acompanha o módulo. Sem isto, a faixa clara embaixo do
/// módulo preto quebra a ideia de troca de contexto — e o próprio WMP12
/// escurece os controles quando entra no Reproduzindo Agora.
/// </summary>
void MainWindow::AplicarTomRodape(bool escuro)
{
    if (escuro)
    {
        Rodape->Background = SolidColorBrush(Color::FromRgb(0x17, 0x12, 0x0B));
        Rodape->BorderBrush = SolidColorBrush(Color::FromRgb(0x3A, 0x2E, 0x1C));
        // Creme, nao branco: no tom escuro o contraste tem de cair, nao subir.
        TxtTitulo->Foreground = Tema::Recurso("ChapaTexto");
        TxtArtista->Foreground = SolidColorBrush(Color::FromRgb(0x8A, 0x76, 0x54));
        TxtTempo->Foreground = SolidColorBrush(Color::FromRgb(0x8A, 0x76, 0x54));
        TomTransporte(Tema::Recurso("ChapaRotulo"));
    }
    else
    {
        Rodape->Background = Tema::Recurso("Rodape");
        Rodape->BorderBrush = Tema::Recurso("RodapeBorda");
        TxtTitulo->Foreground = Tema::Recurso("Texto");
        TxtArtista->Foreground = Tema::Recurso("TextoFraco");
        TxtTempo->Foreground = Tema::Recurso("TextoFraco");
        TomTransporte(Tema::Recurso("TextoFraco"));
    }
}

/// <summary>
/// Parar, pausar, anterior e próximo são só o glifo — sem moldura, para
/// não virarem blocos claros disputando a atenção com o orbe. A cor
/// deles acompanha o tom do rodapé (o Pausar, aceso, sobrepõe o ocre).
/// </summary>
void MainWindow::TomTransporte(const Brush &cor)
{
    _objCorTransporte = cor;
    BtnParar->Foreground = cor;
    BtnAnterior->Foreground = cor;
    BtnProximo->Foreground = cor;
    if (PlayerVivo && PlayerVivo() && AtualizarTransporteAgora) AtualizarTransporteAgora();
    else BtnPausar->Foreground = cor;
}

/// <summary>
/// Pausar e Parar só valem com uma faixa em curso (tocando ou
/// pausada); fora disso ficam apagados, para não oferecer o que não
/// faz nada. Pausado, o Pausar fica aceso no ocre vivo e passa a dizer
/// "Retomar" — o estado fica visível no próprio botão, sem que o orbe
/// troque de ícone.
/// </summary>
void MainWindow::AtualizarTransporte(bool blnPausado, bool blnEmCurso)
{
    BtnParar->IsEnabled = blnEmCurso;
    BtnPausar->IsEnabled = blnEmCurso;
    if (blnPausado) BtnPausar->Foreground = Tema::Recurso("AcentoVivo");
    else if (!_objCorTransporte.Nulo()) BtnPausar->Foreground = _objCorTransporte;
    else BtnPausar->Foreground = Brush();   // volta ao do estilo
    BtnPausar->ToolTip = blnPausado ? "Retomar" : "Pausar";
}

} // namespace ARCA::Vistas

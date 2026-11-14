// ARCA no xOS - Vistas/BibliotecaView (de MainWindow.xaml, o Grid ModuloBiblioteca, e de
// MainWindow.xaml.cs, de AoTrocarCategoria a Pular). Ver BibliotecaView.h.
#include "BibliotecaView.h"

#include "AquisicaoView.h"
#include "Base/Acervo.h"
#include "Base/Arquivo.h"
#include "Base/Despacho.h"
#include "Base/Texto.h"
#include "ImagensDoEncarte.h"
#include "Interop/ColecaoMidia.h"
#include "JanelaArtworkAlbum.h"
#include "JanelaConferenciaAlbum.h"
#include "JanelaConteudoFaixas.h"
#include "JanelaEnviarObra.h"
#include "Modelo/Aquisicao.h"
#include "Modelo/CatalogoAsf.h"
#include "Modelo/ClasseMidia.h"
#include "Modelo/Formato.h"
#include "Modelo/OrdemFaixas.h"
#include "Modelo/RenomeacaoFaixa.h"

#include <algorithm>
#include <chrono>
#include <set>

namespace ARCA::Vistas {

using namespace ARCA::Modelo;
using Interop::AtributoFlac;
using Interop::EditorFlac;
using Interop::EstadoPlayer;
using Interop::TipoAtributoFlac;

namespace {

/// <summary>
/// Os campos que valem para o álbum inteiro. Título, faixa e compositor
/// são de cada faixa e ficam de fora.
/// </summary>
// ARCA-ORIGINAL: no .cs este <summary> ficou sem dono (logo abaixo dele vem o do
// strCampoNomeArquivo, e so depois o s_arrCamposAlbum, que fica sem documentacao propria).
const char *const s_arrCamposAlbum[][2] = {
    {"WM/AlbumTitle", "Álbum"},
    {"WM/AlbumArtist", "Artista do álbum"},
    {"Author", "Artista das faixas"},
    {"WM/Year", "Ano"},
    {"WM/Genre", "Gênero"},
    {"WM/PartOfSet", "Disco do conjunto"},
    {"WM/Publisher", "Gravadora"},
};

// Mouse.OverrideCursor = Cursors.Wait ... finally { Mouse.OverrideCursor = null; }
struct CursorEspera
{
    CursorEspera() { Mouse::SetOverrideCursor(Cursors::Wait); }
    ~CursorEspera() { Mouse::SetOverrideCursor(Cursor::Herda); }
};

double Milissegundos(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

std::string Aspas(const std::string &s) { return "“" + s + "”"; }

bool ExisteSemCaixa(const std::vector<std::string> &l, const std::string &s)
{
    return std::any_of(l.begin(), l.end(), [&](const std::string &x) { return Base::IgualSemCaixa(x, s); });
}

} // namespace

// ===============================================================
//  Montagem (o Grid ModuloBiblioteca do MainWindow.xaml)
// ===============================================================
BibliotecaView::BibliotecaView(MainWindow &janela) : _janela(janela)
{
    InitializeComponent();

    // A caixa de álbuns filtra só a lista de álbuns, na hora: é uma
    // lista em memória, não há consulta a adiar.
    CaixaAlbum->TextChanged += [this](TextChangedEventArgs &) {
        DicaAlbum->Visibility = CaixaAlbum->Text().empty() ? Vistas::Visibility::Visible : Vistas::Visibility::Collapsed;
        FiltrarAlbuns();
    };
    CaixaAlbum->KeyDown += [this](KeyEventArgs &k) {
        // Enter escolhe o primeiro álbum que sobrou; Esc limpa a caixa.
        if (k.Key == Key::Enter) EscolherPrimeiroAlbum();
        else if (k.Key == Key::Escape) CaixaAlbum->Text = std::string();
    };
    // Clicar em <todos> já selecionado não muda a seleção, então não
    // passa pelo AoEscolherAlbum — mas tem de limpar a caixa do mesmo
    // jeito.
    ListaAlbuns->PreviewMouseLeftButtonUp += [this](MouseButtonEventArgs &m) {
        ListBoxItem *objItem = ListBox::ContainerFromElement(ListaAlbuns.get(), m.OriginalSource);
        AlbumDaLista *a = objItem != nullptr ? ListaAlbuns->Item<AlbumDaLista>(objItem->Indice) : nullptr;
        if (a != nullptr && a->Todos) CaixaAlbum->Text = std::string();
    };

    // os ganchos do casco (ver o fim de MainWindow.h)
    _janela.AplicarBusca = [this] { AplicarBusca(); };
    _janela.Pular = [this](int passo) { Pular(passo); };
    _janela.Avisar = [this](const std::string &t, const std::string &x) { Avisar(t, x); };
    _janela.AtualizarEstadoEdicaoSeFaixa = [this] {
        if (!_arquivoSel.empty()) AtualizarEstadoEdicao();
    };
    // O começo do AoTocar: "Tocar obedece à seleção" — se a faixa escolhida
    // na lista não é a que está carregada, toca a escolhida.
    _janela.TocarSelecionadaSeOutra = [this] {
        if (!_janela._player || !_janela._player->Vivo()) return false;
        ItemMidia *objSelecionado = ListaItens->SelectedItem<ItemMidia>();
        if (objSelecionado != nullptr && !EhACarregada(*objSelecionado))
        {
            Tocar(*objSelecionado);
            return true;
        }
        return false;
    };
    _janela.Carregado += [this] { AoCarregar(); };
}

void BibliotecaView::InitializeComponent()
{
    // <Grid x:Name="ModuloBiblioteca">
    auto g = std::make_shared<Grid>();
    g->RowDefinitions = {RowDefinition(GridLength::Auto()), RowDefinition(GridLength::Star(1))};
    g->ColumnDefinitions = {ColumnDefinition(GridLength(240)), ColumnDefinition(GridLength::Star(1)),
                            ColumnDefinition(GridLength::Auto())};

    // Tipos de mídia como abas internas, no topo — o mesmo
    // vocabulário das abas de visualizador do EM EXECUÇÃO.
    // Continua sendo um ListBox: só mudou o desenho, a seleção
    // e o evento são os mesmos.
    {
        auto b = std::make_shared<Border>();
        Grid::SetRow(b, 0);
        Grid::SetColumnSpan(b, 3);
        b->Background = Tema::Recurso("Canvas");
        b->BorderBrush = Tema::Recurso("Borda");
        b->BorderThickness = Thickness(0, 0, 0, 1);
        b->Padding = Thickness(12, 8, 12, 0);
        ListaCategorias = std::make_shared<ListBox>();
        ListaCategorias->Name = "ListaCategorias";
        ListaCategorias->Background = Brushes::Transparent;
        ListaCategorias->BorderThickness = Thickness(0);
        ListaCategorias->HorizontalScrollBarVisibility = ScrollBarVisibility::Disabled;
        ListaCategorias->VerticalScrollBarVisibility = ScrollBarVisibility::Disabled;
        ListaCategorias->ItemContainerStyle = Tema::Estilo("AbaInternaLista");
        ListaCategorias->ItemsPanelOrientation = Vistas::Orientation::Horizontal;
        ListaCategorias->DefineTexto<Categoria>([](Categoria &c) { return c.ToString(); });
        ListaCategorias->SelectionChanged += [this](SelectionChangedEventArgs &) { AoTrocarCategoria(); };
        b->SetChild(ListaCategorias);
        g->Children.Add(b);
    }

    // ===== Álbuns ===========================================
    // Escolher um álbum filtra a lista de faixas ao lado; a
    // caixa no topo filtra só esta lista, pelo nome do álbum.
    // <todos> fica sempre em primeiro e tira os dois filtros.
    {
        auto b = std::make_shared<Border>();
        Grid::SetRow(b, 1);
        Grid::SetColumn(b, 0);
        b->Background = Tema::Recurso("PainelLateral");
        b->BorderBrush = Tema::Recurso("Borda");
        b->BorderThickness = Thickness(0, 0, 1, 0);
        auto d = std::make_shared<DockPanel>();
        auto r = std::make_shared<TextBlock>("ÁLBUNS");
        DockPanel::SetDock(r, Dock::Top);
        r->Style = Tema::Estilo("Rotulo");
        r->Margin = Thickness(12, 12, 0, 6);
        d->Children.Add(r);
        auto gc = std::make_shared<Grid>();
        DockPanel::SetDock(gc, Dock::Top);
        gc->Margin = Thickness(10, 0, 10, 8);
        CaixaAlbum = std::make_shared<TextBox>();
        CaixaAlbum->Name = "CaixaAlbum";
        gc->Children.Add(CaixaAlbum);
        DicaAlbum = std::make_shared<TextBlock>("Procurar álbum");
        DicaAlbum->Name = "DicaAlbum";
        DicaAlbum->IsHitTestVisible = false;
        DicaAlbum->Foreground = Tema::Recurso("TextoFraco");
        DicaAlbum->FontSize = 12;
        DicaAlbum->VerticalAlignment = Vistas::VerticalAlignment::Center;
        DicaAlbum->Margin = Thickness(8, 0, 0, 0);
        gc->Children.Add(DicaAlbum);
        d->Children.Add(gc);
        ListaAlbuns = std::make_shared<ListBox>();
        ListaAlbuns->Name = "ListaAlbuns";
        ListaAlbuns->Background = Brushes::Transparent;
        ListaAlbuns->BorderThickness = Thickness(0);
        ListaAlbuns->HorizontalScrollBarVisibility = ScrollBarVisibility::Disabled;
        ListaAlbuns->IsVirtualizing = true;
        ListaAlbuns->ItemContainerStyle = Tema::Estilo("ItemAlbum");
        ListaAlbuns->DefineItemTemplate<AlbumDaLista>([](AlbumDaLista &a, int) -> UIElementPtr {
            auto t = std::make_shared<TextBlock>(a.Nome);
            t->TextTrimming = Vistas::TextTrimming::CharacterEllipsis;
            t->ToolTip = a.Nome;
            return t;
        });
        // <DataTrigger Binding="{Binding Todos}" Value="True"> FontStyle Italic
        ListaAlbuns->PreparaContainer = [](ListBoxItem &c, void *p) {
            if (static_cast<AlbumDaLista *>(p)->Todos) c.FontStyle = FontStyles::Italic;
        };
        ListaAlbuns->SelectionChanged += [this](SelectionChangedEventArgs &) { AoEscolherAlbum(); };
        d->Children.Add(ListaAlbuns);
        b->SetChild(d);
        g->Children.Add(b);
    }

    {
        auto d = std::make_shared<DockPanel>();
        Grid::SetRow(d, 1);
        Grid::SetColumn(d, 1);
        d->Background = Tema::Recurso("FundoLista");
        auto br = std::make_shared<Border>();
        DockPanel::SetDock(br, Dock::Bottom);
        br->Background = Tema::Recurso("CabecalhoColuna");
        br->BorderBrush = Tema::Recurso("LinhaCabecalho");
        br->BorderThickness = Thickness(0, 1, 0, 0);
        br->Padding = Thickness(12, 5);
        TxtRodape = std::make_shared<TextBlock>();
        TxtRodape->Name = "TxtRodape";
        TxtRodape->Style = Tema::Estilo("Fraco");
        br->SetChild(TxtRodape);
        d->Children.Add(br);

        auto gi = std::make_shared<Grid>();
        // DataGrid e não ListView: largura proporcional de coluna
        // e ordenação vêm de graça. Era assim no ARCA antigo.
        ListaItens = std::make_shared<DataGrid>();
        ListaItens->Name = "ListaItens";
        ListaItens->Background = Brushes::Transparent;
        ListaItens->BorderThickness = Thickness(0);
        ListaItens->AutoGenerateColumns = false;
        ListaItens->IsReadOnly = true;
        ListaItens->HeadersVisibility = DataGridHeadersVisibility::Column;
        ListaItens->SelectionMode = Vistas::SelectionMode::Extended;
        ListaItens->GridLinesVisibility = DataGridGridLinesVisibility::Horizontal;
        ListaItens->HorizontalGridLinesBrush = Tema::Recurso("LinhaCabecalho");
        ListaItens->RowBackground = Brushes::Transparent;
        ListaItens->AlternatingRowBackground = Tema::Recurso("LinhaAlternada");
        ListaItens->RowHeight = 24;
        auto cNum = DataGridTextColumn::Nova<ItemMidia>("#", DataGridLength(52), [](ItemMidia &i) { return std::to_string(i.Numero()); });
        cNum->OrdenaPor<ItemMidia, int>([](ItemMidia &i) { return i.Numero(); });
        ListaItens->Columns.push_back(cNum);
        ListaItens->Columns.push_back(DataGridTextColumn::Nova<ItemMidia>("Título", DataGridLength::Star(2.4), [](ItemMidia &i) { return i.Titulo(); }));
        ListaItens->Columns.push_back(DataGridTextColumn::Nova<ItemMidia>("Artista", DataGridLength::Star(1.6), [](ItemMidia &i) { return i.Artista(); }));
        ListaItens->Columns.push_back(DataGridTextColumn::Nova<ItemMidia>("Álbum", DataGridLength::Star(1.6), [](ItemMidia &i) { return i.Album(); }));
        ListaItens->Columns.push_back(DataGridTextColumn::Nova<ItemMidia>("Duração", DataGridLength(90), [](ItemMidia &i) { return i.Duracao(); }));
        ListaItens->MouseDoubleClick += [this](MouseButtonEventArgs &e) { AoAbrirItem(e); };
        ListaItens->SelectionChanged += [this](SelectionChangedEventArgs &) { AoSelecionarItem(); };
        gi->Children.Add(ListaItens);

        PainelAviso = std::make_shared<Border>();
        PainelAviso->Name = "PainelAviso";
        PainelAviso->Visibility = Vistas::Visibility::Collapsed;
        PainelAviso->Background = Tema::Recurso("FundoLista");
        auto sp = std::make_shared<StackPanel>();
        sp->VerticalAlignment = Vistas::VerticalAlignment::Center;
        sp->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
        sp->MaxWidth = 520;
        TxtAvisoTitulo = std::make_shared<TextBlock>();
        TxtAvisoTitulo->Name = "TxtAvisoTitulo";
        TxtAvisoTitulo->Style = Tema::Estilo("Titulo");
        TxtAvisoTitulo->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
        TxtAvisoTitulo->TextAlignment = Vistas::TextAlignment::Center;
        TxtAvisoTitulo->TextWrapping = Vistas::TextWrapping::Wrap;
        sp->Children.Add(TxtAvisoTitulo);
        TxtAvisoTexto = std::make_shared<TextBlock>();
        TxtAvisoTexto->Name = "TxtAvisoTexto";
        TxtAvisoTexto->Style = Tema::Estilo("Fraco");
        TxtAvisoTexto->Margin = Thickness(0, 10, 0, 0);
        TxtAvisoTexto->TextAlignment = Vistas::TextAlignment::Center;
        TxtAvisoTexto->LineHeight = 20;
        sp->Children.Add(TxtAvisoTexto);
        PainelAviso->SetChild(sp);
        gi->Children.Add(PainelAviso);
        d->Children.Add(gi);
        g->Children.Add(d);
    }

    // ===== Metadados do arquivo selecionado ==============
    // Lidos do contêiner ASF pelo Windows Media Format SDK, e
    // não da biblioteca do player: a biblioteca devolve o que o
    // banco dela guardou, o arquivo devolve o que ele tem. Para
    // um acervo, a diferença importa. =====================
    // (LINUX: do FLAC, pela libFLAC: Interop/EditorFlac)
    {
        auto b = std::make_shared<Border>();
        Grid::SetRow(b, 1);
        Grid::SetColumn(b, 2);
        b->Width = 372;
        b->Background = Tema::Recurso("PainelLateral");
        b->BorderBrush = Tema::Recurso("Borda");
        b->BorderThickness = Thickness(1, 0, 0, 0);
        auto d = std::make_shared<DockPanel>();
        d->Margin = Thickness(14, 12, 10, 10);
        auto r = std::make_shared<TextBlock>("METADADOS DO ARQUIVO");
        DockPanel::SetDock(r, Dock::Top);
        r->Style = Tema::Estilo("Rotulo");
        d->Children.Add(r);
        TxtArquivoSel = std::make_shared<TextBlock>();
        TxtArquivoSel->Name = "TxtArquivoSel";
        DockPanel::SetDock(TxtArquivoSel, Dock::Top);
        TxtArquivoSel->Style = Tema::Estilo("Fraco");
        TxtArquivoSel->FontSize = 11;
        TxtArquivoSel->TextTrimming = Vistas::TextTrimming::CharacterEllipsis;
        TxtArquivoSel->TextWrapping = Vistas::TextWrapping::NoWrap;
        TxtArquivoSel->Margin = Thickness(0, 0, 0, 10);
        d->Children.Add(TxtArquivoSel);

        auto bb = std::make_shared<Border>();
        DockPanel::SetDock(bb, Dock::Bottom);
        bb->Margin = Thickness(0, 10, 0, 0);
        auto pilha = std::make_shared<StackPanel>();
        auto botao = [](const char *nome, const char *texto, const char *dica) {
            auto x = std::make_shared<Button>();
            x->Name = nome;
            x->Content = texto;
            x->IsEnabled = false;
            x->HorizontalAlignment = Vistas::HorizontalAlignment::Stretch;
            x->Padding = Thickness(0, 5);
            x->Margin = Thickness(0, 0, 0, 10);
            x->ToolTip = dica;
            return x;
        };
        // Capa e encarte do álbum desta faixa, na pasta Artwork:
        // para o disco que entrou no acervo sem eles.
        BtnArtworkAlbum = botao("BtnArtworkAlbum", "Capa e encarte do álbum…",
                                "Digitalizar a capa e as páginas do encarte para a pasta Artwork do álbum");
        BtnArtworkAlbum->Click += [this](RoutedEventArgs &) { AoArtworkAlbum(); };
        pilha->Children.Add(BtnArtworkAlbum);
        // Bit-perfect do acervo antigo: confere o álbum no
        // AccurateRip pelo índice do CD gravado nas faixas.
        BtnConferirAlbum = botao("BtnConferirAlbum", "Conferir álbum (bit-perfect)…",
                                 "Confere cada faixa do álbum no AccurateRip, sem o disco, pelo índice do CD gravado nas faixas");
        BtnConferirAlbum->Click += [this](RoutedEventArgs &) { AoConferirAlbum(); };
        pilha->Children.Add(BtnConferirAlbum);
        // Audiolivro e fala para o que já está no acervo: a mesma
        // marca por faixa que a AQUISIÇÃO grava antes de aprovar.
        BtnConteudoAlbum = botao("BtnConteudoAlbum", "Conteúdo das faixas do álbum…",
                                 "Marcar faixas deste álbum como música, audiolivro ou fala (entrevista, humor)");
        BtnConteudoAlbum->Click += [this](RoutedEventArgs &) { AoConteudoAlbum(); };
        pilha->Children.Add(BtnConteudoAlbum);
        // Obras: o álbum inteiro (sem faixa selecionada) ou a faixa
        // vai para uma obra do banco do ARCA.
        BtnEnviarObra = botao("BtnEnviarObra", "Enviar para uma obra…",
                              "Pôr este álbum (ou esta faixa) numa obra — existente ou nova, com título, autor e capa");
        BtnEnviarObra->Click += [this](RoutedEventArgs &) { AoEnviarObra(); };
        pilha->Children.Add(BtnEnviarObra);
        TxtEstadoEdicao = std::make_shared<TextBlock>();
        TxtEstadoEdicao->Name = "TxtEstadoEdicao";
        TxtEstadoEdicao->Style = Tema::Estilo("Fraco");
        TxtEstadoEdicao->FontSize = 11;
        TxtEstadoEdicao->Margin = Thickness(0, 0, 0, 7);
        pilha->Children.Add(TxtEstadoEdicao);
        BtnSalvarTags = std::make_shared<Button>();
        BtnSalvarTags->Name = "BtnSalvarTags";
        BtnSalvarTags->Content = "Salvar alterações";
        BtnSalvarTags->IsEnabled = false;
        BtnSalvarTags->HorizontalAlignment = Vistas::HorizontalAlignment::Stretch;
        BtnSalvarTags->Padding = Thickness(0, 7);
        BtnSalvarTags->Click += [this](RoutedEventArgs &) { AoSalvarTags(); };
        pilha->Children.Add(BtnSalvarTags);
        bb->SetChild(pilha);
        d->Children.Add(bb);

        auto sv = std::make_shared<ScrollViewer>();
        sv->VerticalScrollBarVisibility = ScrollBarVisibility::Auto;
        sv->HorizontalScrollBarVisibility = ScrollBarVisibility::Disabled;
        PainelMetadados = std::make_shared<StackPanel>();
        PainelMetadados->Name = "PainelMetadados";
        PainelMetadados->Margin = Thickness(0, 0, 8, 0);
        sv->Content = PainelMetadados;
        d->Children.Add(sv);
        b->SetChild(d);
        g->Children.Add(b);
    }

    Content = g;
}

// O que o AoCarregar do original fazia pela BIBLIOTECA, depois de hospedar o player.
void BibliotecaView::AoCarregar()
{
    // LINUX: "_biblioteca = new Biblioteca(_player)" vinha depois de o WMP subir, e sem ele a
    // BIBLIOTECA nao abria. No xOS a biblioteca e do ARCA (Interop/ColecaoMidia), nao do tocador:
    // abre mesmo sem o player.
    Interop::ColecaoMidia &objColecao = Interop::ColecaoMidia::Instancia();
    _biblioteca = std::make_unique<Biblioteca>(&objColecao);
    objColecao.Iniciar([this](int n) { AoAtualizarColecao(n); });

    // Fecha o ciclo: o que AQUISIÇÃO aprova entra na biblioteca na hora.
    if (auto objAquisicao = std::dynamic_pointer_cast<AquisicaoView>(_janela.ModuloAquisicao))
        objAquisicao->AcrescentarNaBiblioteca = [this](const std::string &caminho) {
            if (!_biblioteca || !_biblioteca->Acrescentar(caminho)) return false;
            _blnAlbunsDesatualizados = true;   // relida ao voltar à BIBLIOTECA
            return true;
        };
    _lstCategorias = Biblioteca::Categorias();
    ListaCategorias->ItemsSource = FonteDeItens::De(&_lstCategorias);
    ListaCategorias->SelectedIndex = 0;
}

// LINUX: nao havia. A colecao terminou de conferir as pastas do acervo e achou o que mudou por
// fora do ARCA (ou, na primeira vez, o acervo inteiro): os albuns ficam para reler, e a lista e
// relida ja, se a BIBLIOTECA esta a vista e nada esta em uso (edicao, busca, selecao).
void BibliotecaView::AoAtualizarColecao(int intMudancas)
{
    if (intMudancas == 0) return;
    _blnAlbunsDesatualizados = true;
    if (!IsVisible() || !Alterados().empty() || !_janela.CaixaBusca->Text().empty() || ListaItens->SelectedIndex() >= 0) return;
    CarregarAlbuns();
    CarregarCategoria();
}

std::string BibliotecaView::RaizAcervo() const
{
    if (auto objAquisicao = std::dynamic_pointer_cast<AquisicaoView>(_janela.ModuloAquisicao))
        if (!objAquisicao->RaizAcervo.empty()) return objAquisicao->RaizAcervo;
    return Base::PastaAcervo();
}

/// <summary>O banco de obras, na raiz do acervo. Criado no primeiro uso.</summary>
TBancoObras &BibliotecaView::BancoObras()
{
    if (!_objBancoObras) _objBancoObras = std::make_unique<TBancoObras>(RaizAcervo());
    return *_objBancoObras;
}

const Categoria *BibliotecaView::CategoriaSelecionada() const
{
    return ListaCategorias->SelectedItem<Categoria>();
}

// LINUX: "ListaItens.ItemsSource = itens". No WPF, trocar o ItemsSource tira a selecao (com
// SelectionChanged); a DataGrid do xOS guarda a selecao pela posicao, que na lista nova seria
// outra faixa. Entao a selecao sai antes, pelo mesmo SelectionChanged, e a lista nova entra limpa.
void BibliotecaView::DefinirItens(std::shared_ptr<std::vector<ItemMidia>> itens)
{
    if (ListaItens->SelectedIndex() >= 0)
    {
        _lstItens = nullptr;
        ListaItens->ItemsSource = FonteDeItens();
    }
    _lstItens = itens;
    ListaItens->ItemsSource = itens ? FonteDeItens::De(itens) : FonteDeItens();
}

void BibliotecaView::Ativar(bool ativo)
{
    if (!ativo) return;
    // if (naBiblioteca) { ... } do AoTrocarModulo
    if (_blnAlbunsDesatualizados) CarregarAlbuns();
    const Categoria *c = CategoriaSelecionada();
    if (c != nullptr) _janela.TxtTrilha->Text = _janela.TrilhaDoModulo() + "  ›  " + c->Nome + TrilhaAlbum();
}

// ===============================================================
void BibliotecaView::AoTrocarCategoria()
{
    // Os álbuns são os da categoria: Vídeos tem os seus. O álbum
    // escolhido continua, se existir na categoria nova.
    CarregarAlbuns();
    // Trocar de categoria abandona a busca em curso.
    if (!_janela.CaixaBusca->Text().empty()) { _janela.CaixaBusca->Text = std::string(); return; }
    CarregarCategoria();
}

// ===============================================================
/// <summary>
/// Relê os álbuns da categoria. Se o álbum escolhido não existe nela,
/// o filtro cai para &lt;todos&gt;.
/// </summary>
void BibliotecaView::CarregarAlbuns()
{
    if (!_biblioteca) return;
    const Categoria *c = CategoriaSelecionada();
    auto lstAlbuns = std::make_shared<std::vector<AlbumDaLista>>();
    if (c != nullptr && c->PorObra)
    {
        // Em Obras, a coluna lista as obras do banco do ARCA.
        lstAlbuns->push_back(AlbumDaLista::CriarTodos());
        try
        {
            for (const TObra &o : BancoObras().ListarObras()) lstAlbuns->push_back(AlbumDaLista::DeObra(o));
            std::stable_sort(lstAlbuns->begin(), lstAlbuns->end(), [](const AlbumDaLista &a, const AlbumDaLista &b) {
                if (a.Todos != b.Todos) return a.Todos;
                return ComparaPtBrSemCaixa(a.Nome, b.Nome) < 0;
            });
        }
        catch (const std::exception &ex) { Avisar("Não consegui abrir o banco de obras", ex.what()); }
        // A obra escolhida vale pelo número; o nome acompanha (pode ter mudado).
        const AlbumDaLista *objObra = nullptr;
        if (_intObraFiltro != 0)
            for (const AlbumDaLista &a : *lstAlbuns)
                if (a.IdObra == _intObraFiltro) { objObra = &a; break; }
        if (objObra == nullptr) _intObraFiltro = 0;
        _strAlbumFiltro = objObra != nullptr ? objObra->Nome : "";
    }
    else
    {
        if (c == nullptr || c->Disponivel) *lstAlbuns = _biblioteca->Albuns(c);
        else lstAlbuns->push_back(AlbumDaLista::CriarTodos());
        _intObraFiltro = 0;
    }
    _blnAlbunsDesatualizados = false;

    if (!_strAlbumFiltro.empty() &&
        std::none_of(lstAlbuns->begin(), lstAlbuns->end(), [this](const AlbumDaLista &a) { return !a.Todos && a.Nome == _strAlbumFiltro; }))
        _strAlbumFiltro.clear();

    _blnAjustandoAlbuns = true;
    try
    {
        _vistaAlbuns = lstAlbuns;
        ListaAlbuns->ItemsSource = FonteDeItens::De(_vistaAlbuns);
        FiltrarAlbuns();
    }
    catch (...)
    {
        _blnAjustandoAlbuns = false;
        throw;
    }
    _blnAjustandoAlbuns = false;
}

/// <summary>
/// Aplica a caixa de busca à lista de álbuns: trecho em qualquer
/// lugar do nome, sem distinguir maiúscula nem acento. &lt;todos&gt;
/// nunca some.
/// </summary>
void BibliotecaView::FiltrarAlbuns()
{
    if (!_vistaAlbuns) return;
    std::string strTrecho = Base::Trim(CaixaAlbum->Text());
    bool blnAntes = _blnAjustandoAlbuns;
    _blnAjustandoAlbuns = true;
    if (strTrecho.empty()) ListaAlbuns->DefineFiltro<AlbumDaLista>(nullptr);
    else
        ListaAlbuns->DefineFiltro<AlbumDaLista>([strTrecho](AlbumDaLista &a) { return a.Todos || ContemSemCaixaNemAcento(a.Nome, strTrecho); });
    ListaAlbuns->Refresh();
    MarcarAlbumEscolhido();
    _blnAjustandoAlbuns = blnAntes;
}

/// <summary>Seleciona na lista o álbum do filtro, se ele estiver à vista.</summary>
void BibliotecaView::MarcarAlbumEscolhido()
{
    int intAlvo = -1;
    for (int i = 0; i < ListaAlbuns->Count(); i++)
    {
        AlbumDaLista *a = ListaAlbuns->Item<AlbumDaLista>(i);
        if (_strAlbumFiltro.empty() ? a->Todos : (!a->Todos && a->Nome == _strAlbumFiltro && a->IdObra == _intObraFiltro))
        {
            intAlvo = i;
            break;
        }
    }
    ListaAlbuns->SelectedIndex = intAlvo;
    if (intAlvo >= 0) ListaAlbuns->ScrollIntoView(intAlvo);
}

void BibliotecaView::EscolherPrimeiroAlbum()
{
    if (!_vistaAlbuns) return;
    for (int i = 0; i < ListaAlbuns->Count(); i++)
        if (!ListaAlbuns->Item<AlbumDaLista>(i)->Todos)
        {
            ListaAlbuns->SelectedIndex = i;
            ListaAlbuns->ScrollIntoView(i);
            return;
        }
}

void BibliotecaView::AoEscolherAlbum()
{
    if (_blnAjustandoAlbuns) return;
    AlbumDaLista *a = ListaAlbuns->SelectedItem<AlbumDaLista>();
    if (a == nullptr) return;

    std::string strNovo = a->Todos ? "" : a->Nome;
    bool blnMudou = strNovo != _strAlbumFiltro || a->IdObra != _intObraFiltro;
    // O filtro muda antes de limpar a caixa: limpar refiltra a lista e
    // remarca o álbum do filtro — que tem de ser já o novo.
    _strAlbumFiltro = strNovo;
    _intObraFiltro = a->IdObra;
    bool blnTodos = a->Todos;

    // <todos> tira os dois filtros: o das faixas e o da caixa de álbuns.
    if (blnTodos && !CaixaAlbum->Text().empty()) CaixaAlbum->Text = std::string();
    if (!blnMudou) return;

    // O álbum filtra só pelo campo Álbum. A busca livre (título,
    // artista, álbum) não se mistura com ele: escolher um álbum a
    // limpa, e as faixas são exatamente as do álbum.
    if (!_janela.CaixaBusca->Text().empty())
    {
        _janela.CaixaBusca->Text = std::string();
        _janela._adiadorBusca.Stop();   // o TextChanged acabou de armar a busca vazia
    }
    CarregarCategoria();
}

std::string BibliotecaView::TrilhaAlbum() const
{
    return _strAlbumFiltro.empty() ? "" : "  ›  " + _strAlbumFiltro;
}

void BibliotecaView::AplicarBusca()
{
    if (!_biblioteca) return;
    std::string texto = Base::Trim(_janela.CaixaBusca->Text());
    const Categoria *c = CategoriaSelecionada();

    if (texto.empty()) { CarregarCategoria(); return; }
    // Um caractere devolveria quase o acervo inteiro sem ajudar ninguém.
    if (Base::Caracteres(texto) < 2) return;
    if (c != nullptr && !c->Disponivel) return;

    // A busca livre é no acervo da categoria inteiro: o álbum escolhido
    // volta para <todos>, em vez de virar um E escondido com o texto.
    if (!_strAlbumFiltro.empty())
    {
        _strAlbumFiltro.clear();
        _intObraFiltro = 0;
        _blnAjustandoAlbuns = true;
        MarcarAlbumEscolhido();
        _blnAjustandoAlbuns = false;
    }

    // Em Obras, a busca é nas faixas das obras, não no acervo inteiro:
    // carrega todas as obras e fica com o que casa — são poucas faixas.
    if (c != nullptr && c->PorObra)
    {
        CarregarObras(*c);
        std::shared_ptr<std::vector<ItemMidia>> lstTodas = _lstItens;
        if (!lstTodas) return;
        auto lstAchadas = std::make_shared<std::vector<ItemMidia>>();
        for (const ItemMidia &i : *lstTodas)
            if (ContemSemCaixaNemAcento(i.Titulo(), texto) || ContemSemCaixaNemAcento(i.Artista(), texto) ||
                ContemSemCaixaNemAcento(i.Album(), texto))
                lstAchadas->push_back(i);
        DefinirItens(lstAchadas);
        _janela.TxtTrilha->Text = "Biblioteca  ›  Obras  ›  busca por " + Aspas(texto);
        TxtRodape->Text = lstAchadas->empty()
            ? "nada encontrado nas obras para " + Aspas(texto)
            : Formato::Itens((int) lstAchadas->size()) + " nas obras para " + Aspas(texto);
        return;
    }

    EsconderAviso();
    CursorEspera objEspera;
    TxtRodape->Text = "buscando " + Aspas(texto) + "…";
    // Medir os dois lados separadamente: se a demora está na
    // consulta, o caminho é mudar a consulta; se está em pôr na
    // tela, é a carga preguiçosa acordando as linhas visíveis. São
    // problemas diferentes e não adianta chutar qual é.
    auto tConsulta = std::chrono::steady_clock::now();
    auto itens = std::make_shared<std::vector<ItemMidia>>(_biblioteca->Buscar(c, texto));
    double dblConsulta = Milissegundos(tConsulta);

    auto tTela = std::chrono::steady_clock::now();
    DefinirItens(itens);
    ListaItens->UpdateLayout();   // força realizar as linhas visíveis
    double dblTela = Milissegundos(tTela);

    _janela.TxtTrilha->Text = "Biblioteca  ›  busca por " + Aspas(texto);

    if (!_biblioteca->Falha.empty())
    {
        Avisar("A busca falhou", _biblioteca->Falha);
        TxtRodape->Text = "";
    }
    else
    {
        std::string tempos = "consulta " + Formato::Num(dblConsulta, 0) + " ms · tela " + Formato::Num(dblTela, 0) + " ms";
        TxtRodape->Text = (itens->empty() ? "nada encontrado para " + Aspas(texto)
                                          : Formato::Itens((int) itens->size()) + " para " + Aspas(texto)) +
                          "   —   " + tempos;
    }
}

void BibliotecaView::CarregarCategoria()
{
    const Categoria *c = CategoriaSelecionada();
    if (c == nullptr || !_biblioteca) return;

    _janela.TxtTrilha->Text = "Biblioteca  ›  " + c->Nome + TrilhaAlbum();

    if (!c->Disponivel)
    {
        DefinirItens(nullptr);
        Avisar(c->Nome + " — ainda não construído", c->Observacao);
        TxtRodape->Text = "";
        return;
    }

    EsconderAviso();
    if (c->PorObra) { CarregarObras(*c); return; }
    CursorEspera objEspera;
    auto itens = std::make_shared<std::vector<ItemMidia>>(_biblioteca->Carregar(c, _strAlbumFiltro));
    DefinirItens(itens);
    // Álbum escolhido e nada selecionado: o painel passa a ser o do álbum.
    if (ListaItens->SelectedIndex() < 0) MostrarAlbum();

    if (!_biblioteca->Falha.empty())
    {
        Avisar("Não consegui ler a biblioteca", _biblioteca->Falha);
        TxtRodape->Text = "";
    }
    else
    {
        TxtRodape->Text = Formato::Itens((int) itens->size()) + " em " + Base::Minusculas(c->Nome) +
                          (_strAlbumFiltro.empty() ? "" : " · álbum " + Aspas(_strAlbumFiltro));
        // LINUX: nao havia (o WMP ja tinha a biblioteca pronta): na primeira abertura a colecao
        // do ARCA ainda esta lendo o acervo
        Interop::ColecaoMidia &objColecao = Interop::ColecaoMidia::Instancia();
        if (objColecao.Varrendo() && objColecao.Quantidade() == 0) TxtRodape->Text += " · lendo o acervo pela primeira vez…";
    }
}

/// <summary>
/// A categoria Obras: as faixas da obra escolhida, na ordem dela (parte
/// por parte; dentro do álbum, pelo número da faixa). Em &lt;todos&gt;,
/// todas as obras, uma depois da outra.
/// </summary>
void BibliotecaView::CarregarObras(const Categoria &c)
{
    (void) c;
    CursorEspera objEspera;
    try
    {
        std::vector<int> lstObras;
        if (_intObraFiltro != 0) lstObras.push_back(_intObraFiltro);
        else if (_vistaAlbuns)
            for (const AlbumDaLista &a : *_vistaAlbuns)
                if (a.IdObra != 0) lstObras.push_back(a.IdObra);

        if (lstObras.empty())
        {
            DefinirItens(nullptr);
            MostrarAlbum();
            Avisar("Nenhuma obra ainda",
                   "Para criar uma: em outra categoria, escolha um álbum (sem faixa selecionada) ou selecione uma faixa, "
                   "e use “Enviar para uma obra…” no painel da direita.");
            TxtRodape->Text = "";
            return;
        }

        int intPartesPerdidas = 0;
        std::vector<std::string> lstArquivos;
        std::set<std::string> setVistos;    // OrdinalIgnoreCase
        for (int intIdObra : lstObras)
            for (const std::string &strArquivo : ArquivosDaObra(intIdObra, intPartesPerdidas))
                if (setVistos.insert(Base::Minusculas(strArquivo)).second) lstArquivos.push_back(strArquivo);

        auto itens = std::make_shared<std::vector<ItemMidia>>(_biblioteca->CarregarArquivos(lstArquivos));
        DefinirItens(itens);
        if (ListaItens->SelectedIndex() < 0) MostrarAlbum();

        if (!_biblioteca->Falha.empty())
        {
            Avisar("Não consegui ler a biblioteca", _biblioteca->Falha);
            TxtRodape->Text = "";
            return;
        }
        std::string strRodape = Formato::Itens((int) itens->size()) +
                                (_intObraFiltro == 0 ? " em todas as obras" : " na obra " + Aspas(_strAlbumFiltro));
        if (intPartesPerdidas > 0) strRodape += " · " + std::to_string(intPartesPerdidas) + " parte(s) não encontrada(s) no acervo";
        // LINUX: "fora da biblioteca do WMP" no original
        if (_biblioteca->ForaDaBiblioteca > 0)
            strRodape += " · " + std::to_string(_biblioteca->ForaDaBiblioteca) + " faixa(s) ainda fora da biblioteca do ARCA";
        TxtRodape->Text = strRodape;
    }
    catch (const std::exception &ex)
    {
        DefinirItens(nullptr);
        Avisar("Não consegui abrir o banco de obras", ex.what());
        TxtRodape->Text = "";
    }
}

/// <summary>
/// Os arquivos de uma obra, na ordem: as partes na ordem da obra; um
/// álbum são as WMA da pasta dele pelo número da faixa. Parte cuja pasta
/// ou arquivo não existe mais conta em intPerdidas.
/// </summary>
// LINUX: as FLAC da pasta (o acervo no xOS e FLAC)
std::vector<std::string> BibliotecaView::ArquivosDaObra(int intIdObra, int &intPerdidas)
{
    std::vector<std::string> lstArquivos;
    for (const TParteObra &p : BancoObras().Partes(intIdObra))
    {
        if (p.eTipo == TipoParteObra::Faixa)
        {
            if (Base::ArquivoExiste(p.strCaminho)) lstArquivos.push_back(p.strCaminho);
            else intPerdidas++;
            continue;
        }
        std::vector<std::string> arrFlac;
        try
        {
            if (Base::PastaExiste(p.strCaminho)) arrFlac = Base::Arquivos(p.strCaminho, ".flac");
        }
        catch (...) {}
        if (arrFlac.empty()) { intPerdidas++; continue; }
        std::vector<std::string> lstNaOrdem = TOrdemFaixas::Ordenar(arrFlac, nullptr);
        lstArquivos.insert(lstArquivos.end(), lstNaOrdem.begin(), lstNaOrdem.end());
    }
    return lstArquivos;
}

/// <summary>
/// Edições ainda não gravadas — da faixa, do álbum inteiro ou da obra —
/// não somem caladas quando o painel vai mudar: a pessoa decide.
/// </summary>
void BibliotecaView::ResolverPendentes()
{
    std::vector<std::shared_ptr<CampoEditor>> lstPendentes = Alterados();
    if (lstPendentes.empty()) return;

    if (_intObraPainel != 0)
    {
        if (MessageBox::Show(&_janela,
                             std::to_string(lstPendentes.size()) + " campo(s) da obra foram alterados e não gravados.\r\n\r\nGravar agora?",
                             "Alterações não gravadas", MessageBoxButton::YesNo, MessageBoxImage::Question,
                             MessageBoxResult::Yes) == MessageBoxResult::Yes)
        {
            std::optional<std::string> strResumo = SalvarObra(false);
            if (strResumo) TxtRodape->Text = *strResumo;
        }
    }
    else if (_lstArquivosAlbum)
    {
        // ARCA-ORIGINAL: com uma faixa do album tocando, as alteracoes do album somem caladas aqui
        // (nem pergunta nem grava), contra o que o resumo acima promete.
        if (TocandoAlgumDoAlbum()) return;
        if (MessageBox::Show(&_janela,
                             std::to_string(lstPendentes.size()) + " campo(s) do álbum foram alterados e não gravados.\r\n\r\nGravar agora nas " +
                                 std::to_string(_lstArquivosAlbum->size()) + " faixa(s) do álbum?",
                             "Alterações não gravadas", MessageBoxButton::YesNo, MessageBoxImage::Question,
                             MessageBoxResult::Yes) == MessageBoxResult::Yes)
        {
            // Sem reler as listas: estamos no meio de uma troca de seleção.
            std::optional<std::string> strResumo = GravarAlbum(lstPendentes, false);
            if (strResumo) TxtRodape->Text = *strResumo;
        }
    }
    // ARCA-ORIGINAL: o mesmo na faixa: tocando, as alteracoes dela somem caladas.
    else if (!_arquivoSel.empty() && !TocandoEsteArquivo())
    {
        if (MessageBox::Show(&_janela,
                             std::to_string(lstPendentes.size()) + " campo(s) de \"" + Base::NomeDoArquivo(_arquivoSel) +
                                 "\" foram alterados e não gravados.\r\n\r\nGravar agora?",
                             "Alterações não gravadas", MessageBoxButton::YesNo, MessageBoxImage::Question,
                             MessageBoxResult::Yes) == MessageBoxResult::Yes)
        {
            bool blnFalhou;
            std::string strResumo = SalvarFaixa(_arquivoSel, lstPendentes, false, blnFalhou);
            if (blnFalhou)
                MessageBox::Show(&_janela, strResumo, "Alterações não gravadas", MessageBoxButton::OK, MessageBoxImage::Warning);
            else
                TxtRodape->Text = strResumo;
        }
    }
}

/// <summary>
/// Mostra o que o arquivo realmente tem no cabeçalho ASF, agrupado
/// pelas categorias do estudo em Referência\Sheet_2.
/// </summary>
void BibliotecaView::AoSelecionarItem()
{
    // Edições ainda não gravadas não somem caladas ao trocar de faixa
    // (à mão ou pelo avanço automático): a pessoa decide.
    ResolverPendentes();
    LimparPainel();

    ItemMidia *item = ListaItens->SelectedItem<ItemMidia>();
    if (!_biblioteca) return;
    if (item == nullptr) { MostrarAlbum(); return; }

    std::string caminho = item->Arquivo();
    if (caminho.empty()) return;
    _arquivoSel = caminho;
    BtnArtworkAlbum->IsEnabled = Base::ArquivoExiste(caminho);
    BtnConferirAlbum->IsEnabled = BtnArtworkAlbum->IsEnabled;
    BtnConteudoAlbum->IsEnabled = BtnArtworkAlbum->IsEnabled;
    BtnEnviarObra->IsEnabled = BtnArtworkAlbum->IsEnabled;
    TxtArquivoSel->Text = Base::NomeDoArquivo(caminho);
    TxtArquivoSel->ToolTip = caminho;

    {
        EditorFlac editor;
        if (!editor.Abrir(caminho))
        {
            // LINUX: "Arquivos que não são ASF (MP3, MP4, FLAC) ... do contêiner da Microsoft" no
            // original; aqui a leitura e a do FLAC
            auto t = std::make_shared<TextBlock>("Não consegui ler o cabeçalho deste arquivo.\r\n" + editor.Falha() +
                                                 "\r\n\r\nArquivos que não são FLAC (MP3, MP4, WMA) não passam "
                                                 "por aqui — esta leitura é do contêiner FLAC.");
            t->Style = Tema::Estilo("Fraco");
            t->TextWrapping = Vistas::TextWrapping::Wrap;
            PainelMetadados->Children.Add(t);
            AtualizarEstadoEdicao();
            return;
        }

        PainelMetadados->Children.Add(LinhaNomeArquivo(caminho));
        MostrarAtributos(editor.LerTudo());
    }
    AtualizarEstadoEdicao();
}

/// <summary>
/// "Nome do arquivo", no topo do painel da faixa. O nome é livre; só a
/// extensão fica fora da caixa, porque diz o que o arquivo é.
/// </summary>
UIElementPtr BibliotecaView::LinhaNomeArquivo(const std::string &strCaminho)
{
    std::string strSemExtensao = Base::NomeSemExtensao(strCaminho);

    auto objPilha = std::make_shared<StackPanel>();
    auto r = std::make_shared<TextBlock>("ARQUIVO");
    r->Style = Tema::Estilo("Rotulo");
    r->Margin = Thickness(0, 12, 0, 4);
    objPilha->Children.Add(r);

    // LINUX: "a biblioteca do WMP" no original
    std::shared_ptr<Grid> g = LinhaBase("Nome do arquivo", "Renomeia o arquivo e atualiza a biblioteca do ARCA, os caches, as playlists e o ledger");
    auto objLinha = std::make_shared<DockPanel>();
    auto objExtensao = std::make_shared<TextBlock>(Base::Extensao(strCaminho));
    objExtensao->Foreground = Tema::Recurso("TextoFraco");
    objExtensao->FontSize = 11;
    objExtensao->VerticalAlignment = Vistas::VerticalAlignment::Center;
    objExtensao->Margin = Thickness(2, 0, 0, 0);
    DockPanel::SetDock(objExtensao, Dock::Right);
    auto cx = std::make_shared<TextBox>();
    cx->Text = strSemExtensao;
    cx->FontSize = 11;
    cx->Padding = Thickness(4, 2, 4, 2);
    cx->TextChanged += [this](TextChangedEventArgs &) { AtualizarEstadoEdicao(); };
    objLinha->Children.Add(objExtensao);
    objLinha->Children.Add(cx);
    Grid::SetColumn(objLinha, 1);
    g->Children.Add(objLinha);
    objPilha->Children.Add(g);

    _campos.push_back(std::make_shared<CampoEditor>(CampoEditor{strCampoNomeArquivo, strSemExtensao, cx}));
    return objPilha;
}

void BibliotecaView::LimparPainel()
{
    PainelMetadados->Children.Clear();
    _campos.clear();
    _arquivoSel.clear();
    _lstArquivosAlbum.reset();
    _intObraPainel = 0;
    TxtArquivoSel->Text = "";
    TxtArquivoSel->ToolTip = "";
    TxtEstadoEdicao->Text = "";
    BtnSalvarTags->IsEnabled = false;
    BtnArtworkAlbum->IsEnabled = false;
    BtnConferirAlbum->IsEnabled = false;
    BtnConteudoAlbum->IsEnabled = false;
    BtnEnviarObra->IsEnabled = false;
}

// ===============================================================
//  OBRAS
// ===============================================================
/// <summary>
/// O painel da obra escolhida: título e autor (gravados por "Salvar
/// alterações"), a capa e as partes — reordenar, retirar e excluir
/// valem na hora. Nada aqui toca os arquivos do acervo.
/// </summary>
void BibliotecaView::MostrarObra()
{
    if (_intObraFiltro == 0)
    {
        auto t = std::make_shared<TextBlock>(
            "Escolha uma obra na lista para editar título, autor, capa e partes.\r\n\r\n"
            "Para criar uma obra: em outra categoria, escolha um álbum (sem faixa selecionada) ou "
            "selecione uma faixa, e use “Enviar para uma obra…”.");
        t->Style = Tema::Estilo("Fraco");
        t->TextWrapping = Vistas::TextWrapping::Wrap;
        t->Margin = Thickness(0, 12, 0, 0);
        PainelMetadados->Children.Add(t);
        return;
    }

    TObra objObra;
    std::vector<TParteObra> lstPartes;
    try
    {
        objObra = BancoObras().Obter(_intObraFiltro);
        lstPartes = BancoObras().Partes(_intObraFiltro);
    }
    catch (const std::exception &ex)
    {
        auto t = std::make_shared<TextBlock>(std::string("Não consegui ler a obra: ") + ex.what());
        t->Style = Tema::Estilo("Fraco");
        t->TextWrapping = Vistas::TextWrapping::Wrap;
        PainelMetadados->Children.Add(t);
        return;
    }
    if (objObra.intId == 0) return;

    _intObraPainel = objObra.intId;
    TxtArquivoSel->Text = "Obra — " + (lstPartes.size() == 1 ? std::string("1 parte") : std::to_string(lstPartes.size()) + " partes");
    TxtArquivoSel->ToolTip = BancoObras().Arquivo();

    PainelMetadados->Children.Add(Rotulo("OBRA"));
    PainelMetadados->Children.Add(LinhaEditavel(strCampoObraTitulo, "Título", objObra.strTitulo));
    PainelMetadados->Children.Add(LinhaEditavel(strCampoObraAutor, "Autor", objObra.strAutor));

    // Capa: troca vale na hora.
    PainelMetadados->Children.Add(Rotulo("CAPA DA OBRA"));
    auto objCapa = std::make_shared<Grid>();
    objCapa->ColumnDefinitions.push_back(ColumnDefinition(GridLength(130)));
    objCapa->ColumnDefinitions.push_back(ColumnDefinition());
    auto objMoldura = std::make_shared<Border>();
    objMoldura->Width = 120;
    objMoldura->Height = 120;
    objMoldura->Background = Tema::Recurso("PainelLateral");
    objMoldura->BorderBrush = Tema::Recurso("Borda");
    objMoldura->BorderThickness = Thickness(1);
    objMoldura->HorizontalAlignment = Vistas::HorizontalAlignment::Left;
    std::shared_ptr<BitmapSource> objImg = ImagensDoEncarte::Miniatura(objObra.arrCapa, 240);
    if (objImg)
    {
        auto i = std::make_shared<Image>();
        i->Source = objImg;
        i->Stretch = Vistas::Stretch::Uniform;
        objMoldura->SetChild(i);
    }
    else
    {
        auto t = std::make_shared<TextBlock>("sem capa");
        t->Style = Tema::Estilo("Fraco");
        t->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
        t->VerticalAlignment = Vistas::VerticalAlignment::Center;
        objMoldura->SetChild(t);
    }
    objCapa->Children.Add(objMoldura);
    auto objBotoesCapa = std::make_shared<StackPanel>();
    objBotoesCapa->Children.Add(BotaoPequeno("De arquivo…", [this] {
        std::string strErro;
        std::shared_ptr<BitmapSource> objImagem = ImagensDoEncarte::DeArquivo(&_janela, "Escolher a capa da obra", strErro);
        TrocarCapaDaObra(objImagem, strErro);
    }));
    objBotoesCapa->Children.Add(BotaoPequeno("Digitalizar…", [this] {
        std::string strErro;
        std::shared_ptr<BitmapSource> objImagem = ImagensDoEncarte::DoScanner(strErro);
        TrocarCapaDaObra(objImagem, strErro);
    }));
    objBotoesCapa->Children.Add(BotaoPequeno("Sem capa", [this] {
        AcaoNaObra([this] { BancoObras().AlterarCapa(_intObraPainel, {}); }, "Capa retirada da obra.");
    }));
    Grid::SetColumn(objBotoesCapa, 1);
    objCapa->Children.Add(objBotoesCapa);
    PainelMetadados->Children.Add(objCapa);

    // Partes, na ordem em que a obra toca.
    PainelMetadados->Children.Add(Rotulo("PARTES   (" + std::to_string(lstPartes.size()) + ")"));
    auto objNota = std::make_shared<TextBlock>("Na ordem em que a obra toca. Retirar uma parte não apaga nada do acervo.");
    objNota->Style = Tema::Estilo("Fraco");
    objNota->FontSize = 10;
    objNota->FontStyle = FontStyles::Italic;
    objNota->TextWrapping = Vistas::TextWrapping::Wrap;
    objNota->Margin = Thickness(0, 0, 0, 6);
    PainelMetadados->Children.Add(objNota);
    for (size_t i = 0; i < lstPartes.size(); i++)
        PainelMetadados->Children.Add(LinhaParte(lstPartes[i], (int) i, (int) lstPartes.size()));

    auto objExcluir = std::make_shared<Button>();
    objExcluir->Content = "Excluir esta obra…";
    objExcluir->HorizontalAlignment = Vistas::HorizontalAlignment::Left;
    objExcluir->Padding = Thickness(12, 4, 12, 4);
    objExcluir->Margin = Thickness(0, 14, 0, 0);
    int intId = objObra.intId;
    std::string strTitulo = objObra.strTitulo;
    objExcluir->Click += [this, intId, strTitulo](RoutedEventArgs &) {
        if (MessageBox::Show(&_janela,
                             "Excluir a obra " + Aspas(strTitulo) + "?\r\n\r\nSó o agrupamento sai do banco do ARCA. "
                             "Nenhum álbum nem faixa do acervo é apagado.",
                             "Excluir obra", MessageBoxButton::OKCancel, MessageBoxImage::Question,
                             MessageBoxResult::Cancel) != MessageBoxResult::OK)
            return;
        try { BancoObras().ExcluirObra(intId); }
        catch (const std::exception &ex)
        {
            TxtEstadoEdicao->Text = std::string("Não consegui excluir: ") + ex.what();
            return;
        }
        _campos.clear();           // nada a perguntar: a obra não existe mais
        _intObraFiltro = 0;
        _strAlbumFiltro.clear();
        CarregarAlbuns();
        CarregarCategoria();
        TxtRodape->Text = "Obra " + Aspas(strTitulo) + " excluída.";
    };
    PainelMetadados->Children.Add(objExcluir);
    AtualizarEstadoEdicao();
}

std::shared_ptr<TextBlock> BibliotecaView::Rotulo(const std::string &strTexto)
{
    auto t = std::make_shared<TextBlock>(strTexto);
    t->Style = Tema::Estilo("Rotulo");
    t->Margin = Thickness(0, 12, 0, 4);
    return t;
}

std::shared_ptr<Button> BibliotecaView::BotaoPequeno(const std::string &strTexto, std::function<void()> objAcao)
{
    auto b = std::make_shared<Button>();
    b->Content = strTexto;
    b->Padding = Thickness(0, 3, 0, 3);
    b->Margin = Thickness(0, 0, 0, 5);
    b->Click += [objAcao](RoutedEventArgs &) { objAcao(); };
    return b;
}

/// <summary>Uma parte: posição, rótulo e ↑ ↓ ✕. Pasta ou arquivo que sumiu aparece marcado, nunca escondido.</summary>
UIElementPtr BibliotecaView::LinhaParte(const TParteObra &p, int intIndice, int intTotal)
{
    bool blnAlbum = p.eTipo == TipoParteObra::Album;
    bool blnExiste = blnAlbum ? Base::PastaExiste(p.strCaminho) : Base::ArquivoExiste(p.strCaminho);
    std::string strRotulo = blnAlbum ? (p.strAlbum.empty() ? Base::NomeDoArquivo(p.strCaminho) : p.strAlbum)
                                     : Base::NomeSemExtensao(p.strCaminho);
    int intIdParte = p.intId;

    auto objLinha = std::make_shared<DockPanel>();
    objLinha->Margin = Thickness(0, 0, 0, 3);
    auto objBotoes = std::make_shared<StackPanel>();
    objBotoes->Orientation = Vistas::Orientation::Horizontal;
    objBotoes->Children.Add(BotaoParte("↑", "Tocar antes", intIndice > 0, [this, intIdParte] {
        AcaoNaObra([this, intIdParte] { BancoObras().Mover(_intObraPainel, intIdParte, -1); }, "");
    }));
    objBotoes->Children.Add(BotaoParte("↓", "Tocar depois", intIndice < intTotal - 1, [this, intIdParte] {
        AcaoNaObra([this, intIdParte] { BancoObras().Mover(_intObraPainel, intIdParte, +1); }, "");
    }));
    objBotoes->Children.Add(BotaoParte("✕", "Retirar da obra (nada é apagado do acervo)", true, [this, intIdParte, strRotulo] {
        AcaoNaObra([this, intIdParte] { BancoObras().RetirarParte(intIdParte); }, Aspas(strRotulo) + " retirado da obra.");
    }));
    DockPanel::SetDock(objBotoes, Dock::Right);
    objLinha->Children.Add(objBotoes);

    auto objTextos = std::make_shared<StackPanel>();
    objTextos->VerticalAlignment = Vistas::VerticalAlignment::Center;
    objTextos->ToolTip = p.strCaminho;
    auto t1 = std::make_shared<TextBlock>(std::to_string(intIndice + 1) + ". " + strRotulo);
    t1->Foreground = Tema::Recurso("Texto");
    t1->FontSize = 11;
    t1->TextTrimming = Vistas::TextTrimming::CharacterEllipsis;
    objTextos->Children.Add(t1);
    auto t2 = std::make_shared<TextBlock>(std::string(blnAlbum ? "álbum" : "faixa") + (blnExiste ? "" : " — não encontrado no acervo"));
    t2->Foreground = Tema::Recurso(blnExiste ? "TextoFraco" : "Alarme");
    t2->FontSize = 10;
    objTextos->Children.Add(t2);
    objLinha->Children.Add(objTextos);
    return objLinha;
}

std::shared_ptr<Button> BibliotecaView::BotaoParte(const std::string &strTexto, const std::string &strDica, bool blnAtivo,
                                                   std::function<void()> objAcao)
{
    auto b = std::make_shared<Button>();
    b->Content = strTexto;
    b->Width = 24;
    b->Padding = Thickness(0, 1, 0, 1);
    b->Margin = Thickness(3, 0, 0, 0);
    b->ToolTip = strDica;
    b->IsEnabled = blnAtivo;
    b->Click += [objAcao](RoutedEventArgs &) { objAcao(); };
    return b;
}

/// <summary>Faz a mudança na obra e relê obras e faixas; o painel volta a ser o da obra.</summary>
void BibliotecaView::AcaoNaObra(std::function<void()> objAcao, const std::string &strResumo)
{
    try { objAcao(); }
    catch (const std::exception &ex)
    {
        TxtEstadoEdicao->Text = std::string("Não consegui: ") + ex.what();
        return;
    }
    CarregarAlbuns();
    CarregarCategoria();
    if (!strResumo.empty()) TxtEstadoEdicao->Text = strResumo;
}

void BibliotecaView::TrocarCapaDaObra(const std::shared_ptr<BitmapSource> &objImagem, const std::string &strErro)
{
    if (!strErro.empty()) { TxtEstadoEdicao->Text = strErro; return; }
    if (!objImagem) return;
    std::shared_ptr<BitmapSource> objCortada = ImagensDoEncarte::Cortar(&_janela, objImagem, "Cortar a capa da obra", true);
    if (!objCortada) return;
    std::vector<uint8_t> arrJpeg = ImagensDoEncarte::CodificarPlena(objCortada);
    if (arrJpeg.empty()) { TxtEstadoEdicao->Text = "Não consegui preparar a capa."; return; }
    AcaoNaObra([this, arrJpeg] { BancoObras().AlterarCapa(_intObraPainel, arrJpeg); }, "Capa da obra trocada.");
}

/// <summary>Grava título e autor da obra do painel. Devolve o resumo, ou o motivo de não ter gravado.</summary>
std::optional<std::string> BibliotecaView::SalvarObra(bool blnReler)
{
    std::shared_ptr<CampoEditor> objTitulo, objAutor;
    for (auto &c : _campos)
    {
        if (!objTitulo && c->Atributo == strCampoObraTitulo) objTitulo = c;
        if (!objAutor && c->Atributo == strCampoObraAutor) objAutor = c;
    }
    if (!objTitulo || !objAutor || _intObraPainel == 0) return std::nullopt;
    std::string strTitulo = Base::Trim(objTitulo->Caixa->Text());
    if (strTitulo.empty()) return std::string("A obra precisa de um título; nada foi gravado.");
    try { BancoObras().AlterarTituloEAutor(_intObraPainel, strTitulo, Base::Trim(objAutor->Caixa->Text())); }
    catch (const std::exception &ex) { return std::string("Não consegui gravar a obra: ") + ex.what(); }
    objTitulo->Original = objTitulo->Caixa->Text();
    objAutor->Original = objAutor->Caixa->Text();
    if (blnReler) { CarregarAlbuns(); CarregarCategoria(); }
    return "Obra gravada: " + Aspas(strTitulo) + ".";
}

/// <summary>
/// Manda o álbum inteiro (painel de álbum) ou a faixa (painel de faixa)
/// para uma obra. Álbum: uma parte por pasta. A janela escolhe ou cria
/// a obra; aqui as partes são acrescentadas — repetida não duplica.
/// </summary>
void BibliotecaView::AoEnviarObra()
{
    std::vector<TParteObra> lstPartes;
    std::string strOQue, strReferencia;
    if (_lstArquivosAlbum && !_lstArquivosAlbum->empty())
    {
        std::vector<std::string> lstPastas;
        for (const std::string &strArquivo : *_lstArquivosAlbum)
        {
            std::string strPasta = Base::PastaDe(strArquivo);
            if (ExisteSemCaixa(lstPastas, strPasta)) continue;
            lstPastas.push_back(strPasta);
            std::string strAlbum, strArtista, strMcdi;
            int intFaixa;
            LerIdentidade(strArquivo, strAlbum, strArtista, strMcdi, intFaixa);
            TParteObra p;
            p.eTipo = TipoParteObra::Album;
            p.strCaminho = strPasta;
            p.strAlbum = _strAlbumFiltro;
            p.strMcdi = strMcdi;
            lstPartes.push_back(p);
        }
        strOQue = "o álbum " + Aspas(_strAlbumFiltro);
        strReferencia = (*_lstArquivosAlbum)[0];
    }
    else if (!_arquivoSel.empty())
    {
        std::string strAlbum, strArtista, strMcdi;
        int intFaixa;
        LerIdentidade(_arquivoSel, strAlbum, strArtista, strMcdi, intFaixa);
        TParteObra p;
        p.eTipo = TipoParteObra::Faixa;
        p.strCaminho = _arquivoSel;
        p.strAlbum = strAlbum;
        p.strMcdi = strMcdi;
        p.intFaixa = intFaixa;
        lstPartes.push_back(p);
        strOQue = "a faixa " + Aspas(Base::NomeSemExtensao(_arquivoSel));
        strReferencia = _arquivoSel;
    }
    else return;

    std::string strA, strArtistaRef, strM;
    int intF;
    LerIdentidade(strReferencia, strA, strArtistaRef, strM, intF);
    std::vector<uint8_t> arrCapaAlbum = CapaDoAlbum(strReferencia);

    auto objJanela = std::make_shared<JanelaEnviarObra>(BancoObras(), strOQue, strArtistaRef, arrCapaAlbum);
    objJanela->Owner = &_janela;
    objJanela->ShowDialog();
    if (objJanela->IdObra() == 0) return;

    int intNovas = 0, intJa = 0;
    try
    {
        for (const TParteObra &p : lstPartes)
            if (BancoObras().AcrescentarParte(objJanela->IdObra(), p)) intNovas++;
            else intJa++;
    }
    catch (const std::exception &ex)
    {
        TxtEstadoEdicao->Text = std::string("Não consegui enviar para a obra: ") + ex.what();
        return;
    }

    std::string strResumo = "Enviado para a obra " + Aspas(objJanela->TituloObra());
    if (intNovas > 0) strResumo += ": " + (intNovas == 1 ? std::string("1 parte acrescentada") : std::to_string(intNovas) + " partes acrescentadas");
    if (intJa > 0)
        strResumo += std::string(intNovas > 0 ? "; " : ": ") +
                     (intJa == 1 ? std::string("já fazia parte dela") : std::to_string(intJa) + " já faziam parte dela");
    TxtEstadoEdicao->Text = strResumo + ".";
    TxtRodape->Text = strResumo + ".";
}

/// <summary>Álbum, artista (do álbum, senão da faixa), índice do CD e número da faixa, lidos do arquivo.</summary>
void BibliotecaView::LerIdentidade(const std::string &strArquivo, std::string &strAlbum, std::string &strArtista, std::string &strMcdi,
                                   int &intFaixa)
{
    strAlbum.clear();
    strArtista.clear();
    strMcdi.clear();
    intFaixa = -1;
    EditorFlac objEditor;
    if (!objEditor.Abrir(strArquivo)) return;
    std::vector<AtributoFlac> lstAtributos = objEditor.LerTudo();
    std::string strAutor;
    bool blnAlbum = false, blnArtista = false, blnAutor = false;     // "== null" do original
    for (const AtributoFlac &a : lstAtributos)
    {
        if (a.Nome == "WM/AlbumTitle" && !blnAlbum) { strAlbum = a.Valor; blnAlbum = true; }
        else if (a.Nome == "WM/AlbumArtist" && !a.Valor.empty() && !blnArtista) { strArtista = a.Valor; blnArtista = true; }
        else if (a.Nome == "Author" && !blnAutor) { strAutor = a.Valor; blnAutor = true; }
    }
    if (!blnArtista) strArtista = strAutor;
    strMcdi = objEditor.LerMcdi();
    intFaixa = TOrdemFaixas::NumeroDaFaixa(lstAtributos);
}

/// <summary>A capa do álbum do arquivo, para "Usar a capa do álbum": Artwork\capa.jpg, Folder.jpg, ou a embutida na faixa.</summary>
std::vector<uint8_t> BibliotecaView::CapaDoAlbum(const std::string &strArquivo)
{
    std::string strPasta = Base::PastaDe(strArquivo);
    for (const std::string &strCandidata :
         {Base::Combina(Base::Combina(strPasta, Integracao::PastaArtwork), "capa.jpg"), Base::Combina(strPasta, "Folder.jpg")})
    {
        try
        {
            if (Base::ArquivoExiste(strCandidata)) return Base::LeBytes(strCandidata);
        }
        catch (...) {}
    }
    EditorFlac objEditor;
    if (!objEditor.Abrir(strArquivo)) return {};
    std::string strMime, strDescricao;
    return objEditor.LerImagem(strMime, strDescricao);
}

/// <summary>
/// Álbum escolhido e nenhuma faixa selecionada: o painel edita o álbum
/// inteiro. É para corrigir de uma vez o que veio errado em todas as
/// faixas ("vol 1" sem o ponto), em vez de faixa por faixa.
///
/// As faixas são as WMA das pastas do que está na lista com aquele
/// título de álbum — e não só as da lista: em Música, o Thriller não
/// mostra as entrevistas, mas elas são do álbum e mudam junto.
/// </summary>
// LINUX: as FLAC das pastas
void BibliotecaView::MostrarAlbum()
{
    // Trocar de álbum, obra ou categoria com o painel alterado: a pessoa
    // decide, como na troca de faixa.
    ResolverPendentes();
    LimparPainel();

    // Em Obras, sem faixa selecionada, o painel é o da obra.
    const Categoria *c = CategoriaSelecionada();
    if (c != nullptr && c->PorObra) { MostrarObra(); return; }

    if (_strAlbumFiltro.empty() || !_janela.CaixaBusca->Text().empty()) return;
    std::shared_ptr<std::vector<ItemMidia>> lstItens = _lstItens;
    if (!lstItens || lstItens->empty()) return;

    std::vector<std::string> lstPastas;
    for (const ItemMidia &objItem : *lstItens)
    {
        std::string strArquivo = objItem.Arquivo();
        if (strArquivo.empty() || !Base::ArquivoExiste(strArquivo)) continue;
        std::string strPasta = Base::PastaDe(strArquivo);
        if (!ExisteSemCaixa(lstPastas, strPasta)) lstPastas.push_back(strPasta);
    }

    // Valor de cada campo em cada faixa: igual em todas vira o texto da
    // caixa; diferente, a caixa fica vazia e só grava se for preenchida.
    std::vector<std::string> lstArquivos;
    std::map<std::string, std::set<std::string>> dicValores;
    // LINUX: o HashSet do .NET devolve os valores na ordem em que entraram (o "Nas faixas: ..."
    // da dica); a ordem vai guardada a parte
    std::map<std::string, std::vector<std::string>> dicOrdem;
    for (const auto &arrCampo : s_arrCamposAlbum) dicValores[arrCampo[0]] = {};
    for (const std::string &strPasta : lstPastas)
    {
        std::vector<std::string> arrFlac;
        try { arrFlac = Base::Arquivos(strPasta, ".flac"); }
        catch (...) { continue; }
        std::sort(arrFlac.begin(), arrFlac.end(), [](const std::string &a, const std::string &b) { return Base::Minusculas(a) < Base::Minusculas(b); });
        for (const std::string &strFlac : arrFlac)
        {
            EditorFlac objEditor;
            if (!objEditor.Abrir(strFlac)) continue;
            std::map<std::string, std::string> dicDoArquivo;      // OrdinalIgnoreCase: chave em minusculas
            for (const AtributoFlac &a : objEditor.LerTudo())
                if (a.Tipo == TipoAtributoFlac::Texto && !dicDoArquivo.count(Base::Minusculas(a.Nome)))
                    dicDoArquivo[Base::Minusculas(a.Nome)] = a.Valor;
            auto itAlbum = dicDoArquivo.find(Base::Minusculas("WM/AlbumTitle"));
            if (itAlbum == dicDoArquivo.end() || itAlbum->second != _strAlbumFiltro) continue;
            lstArquivos.push_back(strFlac);
            for (const auto &arrCampo : s_arrCamposAlbum)
            {
                auto it = dicDoArquivo.find(Base::Minusculas(arrCampo[0]));
                std::string strValor = it != dicDoArquivo.end() ? it->second : "";
                if (dicValores[arrCampo[0]].insert(strValor).second) dicOrdem[arrCampo[0]].push_back(strValor);
            }
        }
    }
    if (lstArquivos.empty()) return;

    _lstArquivosAlbum = lstArquivos;
    BtnEnviarObra->IsEnabled = true;
    TxtArquivoSel->Text = "Álbum inteiro — " + std::to_string(lstArquivos.size()) + " faixa(s)";
    TxtArquivoSel->ToolTip = Base::Junta(lstPastas, "\n");

    auto r = std::make_shared<TextBlock>("ÁLBUM INTEIRO   (" + std::to_string(lstArquivos.size()) + " faixas)");
    r->Style = Tema::Estilo("Rotulo");
    r->Margin = Thickness(0, 12, 0, 4);
    PainelMetadados->Children.Add(r);
    auto n = std::make_shared<TextBlock>(
        "Nenhuma faixa selecionada: o que se grava aqui vai para todas as faixas do álbum, "
        "inclusive as que estão em outra categoria. Campo marcado “varia” está diferente "
        "entre as faixas e só é gravado se você preenchê-lo.");
    n->Style = Tema::Estilo("Fraco");
    n->FontSize = 10;
    n->FontStyle = FontStyles::Italic;
    n->TextWrapping = Vistas::TextWrapping::Wrap;
    n->Margin = Thickness(0, 0, 0, 7);
    PainelMetadados->Children.Add(n);
    if (lstPastas.size() > 1)
    {
        std::vector<std::string> lstNomes;
        for (const std::string &p : lstPastas) lstNomes.push_back(Base::NomeDoArquivo(p));
        auto a = std::make_shared<TextBlock>("Atenção: este nome de álbum está em " + std::to_string(lstPastas.size()) + " pastas (" +
                                             Base::Junta(lstNomes, "; ") + "). A gravação vale para todas.");
        a->Foreground = Tema::Recurso("Alarme");
        a->FontSize = 11;
        a->TextWrapping = Vistas::TextWrapping::Wrap;
        a->Margin = Thickness(0, 0, 0, 7);
        PainelMetadados->Children.Add(a);
    }

    for (const auto &arrCampo : s_arrCamposAlbum)
    {
        const std::vector<std::string> &lstValores = dicOrdem[arrCampo[0]];
        bool blnVaria = lstValores.size() > 1;
        std::string strValor = blnVaria ? "" : lstValores[0];
        PainelMetadados->Children.Add(LinhaEditavel(arrCampo[0], std::string(arrCampo[1]) + (blnVaria ? " (varia)" : ""), strValor));
        if (blnVaria) _campos.back()->Caixa->ToolTip = "Nas faixas: " + Base::Junta(lstValores, " · ");
    }
    AtualizarEstadoEdicao();
}

/// <summary>Alguma faixa do álbum em edição está tocando (o player a segura aberta)?</summary>
bool BibliotecaView::TocandoAlgumDoAlbum() const
{
    const auto &p = _janela._player;
    if (!p || !p->Vivo() || !_lstArquivosAlbum) return false;
    EstadoPlayer objEstado = p->Estado();
    if (objEstado != EstadoPlayer::Tocando && objEstado != EstadoPlayer::Pausado) return false;
    std::string strUrl = p->Url();
    return !strUrl.empty() && ExisteSemCaixa(*_lstArquivosAlbum, strUrl);
}

/// <summary>
/// Grava os campos alterados em todas as faixas do álbum e leva os
/// valores ao banco do WMP. Com blnReler, relê álbuns e faixas (e segue
/// o álbum se o nome mudou). Devolve o resumo para a pessoa.
/// </summary>
std::optional<std::string> BibliotecaView::GravarAlbum(const std::vector<std::shared_ptr<CampoEditor>> &lstMudou, bool blnReler)
{
    if (!_lstArquivosAlbum || lstMudou.empty()) return std::nullopt;
    std::vector<std::string> lstArquivos = *_lstArquivosAlbum;

    std::map<std::string, std::string> dicCampos;
    for (const auto &c : lstMudou) dicCampos[c->Atributo] = c->Caixa->Text();

    std::vector<std::string> lstGravados;
    std::vector<std::string> lstFalhas;
    {
        // LINUX: o acervo so de leitura abre durante a gravacao
        std::unique_ptr<Base::GravacaoNoAcervo> objGravacao;
        std::string strFalhaAcervo;
        try { objGravacao = std::make_unique<Base::GravacaoNoAcervo>(); }
        catch (const std::exception &ex) { strFalhaAcervo = ex.what(); }
        for (const std::string &strArquivo : lstArquivos)
        {
            EditorFlac objEditor;
            bool blnOk = strFalhaAcervo.empty() && objEditor.Abrir(strArquivo);
            if (blnOk)
                for (const auto &objCampo : dicCampos) blnOk &= objEditor.DefinirTexto(objCampo.first, objCampo.second);
            if (blnOk && objEditor.Salvar()) lstGravados.push_back(strArquivo);
            else
                lstFalhas.push_back(Base::NomeDoArquivo(strArquivo) + " (" +
                                    (!strFalhaAcervo.empty() ? strFalhaAcervo : !objEditor.Falha().empty() ? objEditor.Falha() : "?") + ")");
        }
    }
    // ARCA-ORIGINAL: os campos passam a valer como gravados mesmo quando nenhuma faixa gravou
    // (todas em lstFalhas); a GravarCampos, da faixa, so marca os que gravaram de fato.
    for (const auto &c : lstMudou) c->Original = c->Caixa->Text();

    int intNoPlayer = !lstGravados.empty() ? _biblioteca->AtualizarNoPlayer(lstGravados, dicCampos) : 0;

    // O nome do álbum guardado nas partes de obra acompanha (é o rótulo
    // que o painel da obra mostra).
    auto itAlbumNovo = dicCampos.find("WM/AlbumTitle");
    if (!lstGravados.empty() && itAlbumNovo != dicCampos.end() && BancoObras().Existe())
    {
        try
        {
            std::set<std::string> setPastas;
            std::vector<std::string> lstPastasNaOrdem;
            for (const std::string &strArquivo : lstGravados)
                if (setPastas.insert(Base::Minusculas(Base::PastaDe(strArquivo))).second) lstPastasNaOrdem.push_back(Base::PastaDe(strArquivo));
            for (const std::string &strPasta : lstPastasNaOrdem) BancoObras().AtualizarAlbumDaPasta(strPasta, itAlbumNovo->second);
        }
        catch (...) {}   // é só o rótulo: a parte continua apontando para a pasta
    }

    // LINUX: "na biblioteca do WMP" e "quando o WMP reler a pasta" no original
    std::string strResumo = "Álbum: " + std::to_string(lstGravados.size()) + " de " + std::to_string(lstArquivos.size()) +
                            " faixa(s) gravada(s); " + std::to_string(intNoPlayer) + " atualizada(s) na biblioteca do ARCA.";
    if (intNoPlayer < (int) lstGravados.size()) strResumo += " As outras aparecem atualizadas quando a biblioteca reler a pasta.";
    if (!lstFalhas.empty()) strResumo += " Não gravei: " + Base::Junta(lstFalhas, ", ") + ".";

    if (blnReler)
    {
        if (itAlbumNovo != dicCampos.end() && !Base::Trim(itAlbumNovo->second).empty() && intNoPlayer > 0)
            _strAlbumFiltro = itAlbumNovo->second;
        CarregarAlbuns();
        CarregarCategoria();
    }
    return strResumo;
}

void BibliotecaView::MostrarAtributos(const std::vector<AtributoFlac> &atributos)
{
    std::map<std::string, const AtributoFlac *> existentes;     // OrdinalIgnoreCase: chave em minusculas
    std::map<std::string, int> dicOcorrencias;
    for (const AtributoFlac &a : atributos)
    {
        std::string k = Base::Minusculas(a.Nome);
        if (!existentes.count(k)) existentes[k] = &a;
        dicOcorrencias[k]++;
    }

    std::map<std::string, std::vector<const AtributoFlac *>> grupos;
    for (const AtributoFlac &a : atributos) grupos[CatalogoAsf::CategoriaDe(a.Nome)].push_back(&a);

    for (const std::string &cat : CatalogoAsf::Ordem())
    {
        bool editavel = cat == CatalogoAsf::Basico || cat == CatalogoAsf::Musical || cat == CatalogoAsf::Curadoria;

        std::vector<const AtributoFlac *> itens = grupos.count(cat) ? grupos[cat] : std::vector<const AtributoFlac *>();

        // Nas categorias editáveis, os campos conhecidos que o arquivo
        // ainda não tem aparecem vazios — senão não haveria como criar
        // um campo de curadoria, que por definição nunca existe antes.
        std::vector<const CampoAsf *> ausentes;
        if (editavel)
            for (const CampoAsf *c : CatalogoAsf::Editaveis())
                if (c->Categoria() == cat && !existentes.count(Base::Minusculas(c->Atributo()))) ausentes.push_back(c);

        if (itens.empty() && ausentes.empty()) continue;

        auto t = std::make_shared<TextBlock>(Base::Maiusculas(cat) + "   (" + std::to_string(itens.size() + ausentes.size()) + ")");
        t->Style = Tema::Estilo("Rotulo");
        t->Margin = Thickness(0, 12, 0, 4);
        PainelMetadados->Children.Add(t);

        std::string nota = CatalogoAsf::NotaDe(cat);
        if (!nota.empty())
        {
            auto tn = std::make_shared<TextBlock>(nota);
            tn->Style = Tema::Estilo("Fraco");
            tn->FontSize = 10;
            tn->FontStyle = FontStyles::Italic;
            tn->Margin = Thickness(0, 0, 0, 7);
            PainelMetadados->Children.Add(tn);
        }

        for (const AtributoFlac *a : itens)
        {
            const CampoAsf *conhecido = CatalogoAsf::Conhecido(a->Nome);
            // Atributo com vários valores (dois WM/Genre, por exemplo): a
            // gravação por nome só alcança o primeiro — editar o segundo
            // escrevia no primeiro. Fica só para leitura.
            bool podeEditar = editavel && !a->Binario() && a->Tipo == TipoAtributoFlac::Texto && conhecido != nullptr &&
                              conhecido->Editavel() && dicOcorrencias[Base::Minusculas(a->Nome)] == 1;
            PainelMetadados->Children.Add(podeEditar ? LinhaEditavel(a->Nome, CatalogoAsf::RotuloDe(a->Nome), a->Valor) : LinhaLeitura(*a));
        }

        for (const CampoAsf *c : ausentes) PainelMetadados->Children.Add(LinhaEditavel(c->Atributo(), c->Rotulo(), ""));
    }

    if (PainelMetadados->Children.Count() == 0)
    {
        auto t = std::make_shared<TextBlock>("O arquivo não declara nenhum atributo.");
        t->Style = Tema::Estilo("Fraco");
        PainelMetadados->Children.Add(t);
    }
}

std::shared_ptr<Grid> BibliotecaView::LinhaBase(const std::string &rotulo, const std::string &dica)
{
    auto g = std::make_shared<Grid>();
    g->Margin = Thickness(0, 0, 0, 4);
    g->ColumnDefinitions.push_back(ColumnDefinition(GridLength(126)));
    g->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Star(1)));
    auto t = std::make_shared<TextBlock>(rotulo);
    t->Foreground = Tema::Recurso("TextoFraco");
    t->FontSize = 11;
    t->VerticalAlignment = Vistas::VerticalAlignment::Center;
    t->TextTrimming = Vistas::TextTrimming::CharacterEllipsis;
    t->ToolTip = dica;
    g->Children.Add(t);
    return g;
}

UIElementPtr BibliotecaView::LinhaLeitura(const AtributoFlac &a)
{
    // LINUX: a dica leva tambem o nome com que o campo esta gravado no FLAC ("WM/AlbumTitle = ALBUM")
    std::string strDica = a.Nome + (a.NomeVorbis.empty() || a.NomeVorbis == a.Nome ? "" : " = " + a.NomeVorbis) +
                          "   (" + a.TipoTexto() + ")";
    std::shared_ptr<Grid> g = LinhaBase(CatalogoAsf::RotuloDe(a.Nome), strDica);
    auto val = std::make_shared<TextBlock>(a.Valor.empty() ? "—" : a.Valor);
    val->Foreground = Tema::Recurso(a.Binario() ? "TextoFraco" : "Texto");
    val->FontSize = 11;
    val->TextWrapping = Vistas::TextWrapping::Wrap;
    val->MaxHeight = 66;
    val->VerticalAlignment = Vistas::VerticalAlignment::Center;
    val->FontStyle = a.Binario() ? FontStyles::Italic : FontStyles::Normal;
    // (o WPF recorta o que passa do MaxHeight; aqui o recorte e pedido)
    val->ClipToBounds = true;
    Grid::SetColumn(val, 1);
    g->Children.Add(val);
    return g;
}

UIElementPtr BibliotecaView::LinhaEditavel(const std::string &atributo, const std::string &rotulo, const std::string &valor)
{
    // LINUX: a dica leva tambem o nome Vorbis do campo (os OBRA: e o nome do arquivo nao tem)
    std::string strVorbis = Base::ComecaCom(atributo, "OBRA:") || atributo == strCampoNomeArquivo ? "" : EditorFlac::NomeVorbis(atributo);
    std::shared_ptr<Grid> g = LinhaBase(rotulo, atributo + (strVorbis.empty() ? "" : " = " + strVorbis));
    auto cx = std::make_shared<TextBox>();
    cx->Text = valor;
    cx->FontSize = 11;
    cx->Padding = Thickness(4, 2, 4, 2);
    cx->TextChanged += [this](TextChangedEventArgs &) { AtualizarEstadoEdicao(); };
    Grid::SetColumn(cx, 1);
    g->Children.Add(cx);

    _campos.push_back(std::make_shared<CampoEditor>(CampoEditor{atributo, valor, cx}));
    return g;
}

// ===============================================================
bool BibliotecaView::TocandoEsteArquivo() const
{
    const auto &p = _janela._player;
    if (!p || !p->Vivo() || _arquivoSel.empty()) return false;
    // Parado, o player solta o arquivo: comparar só o endereço deixava
    // a edição bloqueada depois de Parar.
    EstadoPlayer objEstado = p->Estado();
    if (objEstado != EstadoPlayer::Tocando && objEstado != EstadoPlayer::Pausado) return false;
    std::string url = p->Url();
    return !url.empty() && Base::IgualSemCaixa(url, _arquivoSel);
}

std::vector<std::shared_ptr<BibliotecaView::CampoEditor>> BibliotecaView::Alterados()
{
    std::vector<std::shared_ptr<CampoEditor>> l;
    for (const auto &c : _campos)
        if (c->Caixa->Text() != c->Original) l.push_back(c);
    return l;
}

void BibliotecaView::AtualizarEstadoEdicao()
{
    size_t n = Alterados().size();

    // O WMP mantém o arquivo aberto enquanto toca, e o editor de
    // metadados precisa de acesso de escrita. Avisar antes vale mais
    // que deixar falhar depois.
    if (TocandoEsteArquivo())
    {
        BtnSalvarTags->IsEnabled = false;
        TxtEstadoEdicao->Text = "Esta faixa está tocando — o arquivo fica aberto pelo "
                                "player e não aceita gravação. Pare a reprodução para editar.";
        return;
    }
    if (TocandoAlgumDoAlbum())
    {
        BtnSalvarTags->IsEnabled = false;
        TxtEstadoEdicao->Text = "Uma faixa deste álbum está tocando — o player a mantém aberta "
                                "e ela não aceitaria gravação. Pare a reprodução para editar o álbum.";
        return;
    }

    BtnSalvarTags->IsEnabled = n > 0;
    TxtEstadoEdicao->Text = n == 0 ? (!_campos.empty() ? "Sem alterações." : "")
                                   : (n == 1 ? std::string("1 campo alterado.") : std::to_string(n) + " campos alterados.");
}

/// <summary>
/// Capa e encarte do álbum da faixa selecionada: a pasta do álbum é a
/// do arquivo. Gravado algo, o encarte em EM EXECUÇÃO é relido se a
/// faixa tocando for desse álbum.
/// </summary>
void BibliotecaView::AoArtworkAlbum()
{
    if (_arquivoSel.empty() || !Base::ArquivoExiste(_arquivoSel)) return;
    ItemMidia *item = ListaItens->SelectedItem<ItemMidia>();
    std::string strPastaAlbum = Base::PastaDe(_arquivoSel);
    std::string strArtista = item != nullptr && !item->Artista().empty() ? item->Artista() : Base::NomeDoArquivo(Base::PastaDe(strPastaAlbum));
    std::string strAlbum = item != nullptr && !item->Album().empty() ? item->Album() : Base::NomeDoArquivo(strPastaAlbum);

    auto objJanela = std::make_shared<JanelaArtworkAlbum>(strPastaAlbum, strArtista, strAlbum, RaizAcervo());
    objJanela->Owner = &_janela;
    objJanela->ShowDialog();
    if (objJanela->Gravou() && _janela.RecarregarEncarteSeDoAlbum) _janela.RecarregarEncarteSeDoAlbum(strPastaAlbum);
}

/// <summary>
/// Confere no AccurateRip o álbum da faixa selecionada, sem o disco,
/// pelo índice do CD que as faixas guardam (WM/MCDI).
/// </summary>
void BibliotecaView::AoConferirAlbum()
{
    if (_arquivoSel.empty() || !Base::ArquivoExiste(_arquivoSel)) return;
    ItemMidia *item = ListaItens->SelectedItem<ItemMidia>();
    std::string strPastaAlbum = Base::PastaDe(_arquivoSel);
    std::string strArtista = item != nullptr && !item->Artista().empty() ? item->Artista() : Base::NomeDoArquivo(Base::PastaDe(strPastaAlbum));
    std::string strAlbum = item != nullptr && !item->Album().empty() ? item->Album() : Base::NomeDoArquivo(strPastaAlbum);
    auto objJanela = std::make_shared<JanelaConferenciaAlbum>(strPastaAlbum, strArtista, strAlbum, RaizAcervo());
    objJanela->Owner = &_janela;
    objJanela->ShowDialog();
}

/// <summary>
/// Música, audiolivro ou fala para as faixas de um álbum que já está no
/// acervo — para o disco que entrou sem a marca. As faixas são as WMA
/// da pasta da faixa selecionada, como no Conferir álbum.
///
/// Grava a classe de mídia no arquivo (o áudio não muda) e reinscreve
/// a faixa na biblioteca do WMP: é na inscrição que ele decide o
/// MediaType, e audiolivro/fala vão para "other".
/// </summary>
// LINUX: as FLAC da pasta; a reinscricao e na colecao do ARCA, que decide o MediaType do mesmo jeito
void BibliotecaView::AoConteudoAlbum()
{
    if (_arquivoSel.empty() || !Base::ArquivoExiste(_arquivoSel)) return;
    std::string strPastaAlbum = Base::PastaDe(_arquivoSel);

    std::vector<std::string> lstArquivos;
    try { lstArquivos = Base::Arquivos(strPastaAlbum, ".flac"); }
    catch (const std::exception &ex)
    {
        TxtEstadoEdicao->Text = std::string("Não consegui ler a pasta do álbum: ") + ex.what();
        return;
    }
    if (lstArquivos.empty()) return;
    // Na ordem do disco (número da faixa), não do nome do arquivo.
    lstArquivos = TOrdemFaixas::Ordenar(lstArquivos, nullptr);

    // O player segura o arquivo que toca, e a gravação falharia nele.
    const auto &p = _janela._player;
    if (p && p->Vivo() && !p->Url().empty() && Base::IgualSemCaixa(Base::PastaDe(p->Url()), strPastaAlbum))
    {
        if (MessageBox::Show(&_janela, "Uma faixa deste álbum está carregada no player. Parar a reprodução para gravar?",
                             "Conteúdo das faixas", MessageBoxButton::YesNo, MessageBoxImage::Question,
                             MessageBoxResult::Yes) != MessageBoxResult::Yes)
            return;
        if (_janela.MarcarParadoPeloUsuario) _janela.MarcarParadoPeloUsuario();   // _paradoPeloUsuario = true
        p->Parar();
        p->Url("");
    }

    auto objJanela = std::make_shared<JanelaConteudoFaixas>(lstArquivos, "");
    objJanela->Owner = &_janela;
    objJanela->ShowDialog();
    if (!objJanela->Alteradas()) return;

    CursorEspera objEspera;
    std::vector<std::string> lstGravadas;
    std::vector<std::string> lstFalhas;
    {
        // LINUX: o acervo so de leitura abre durante a gravacao
        std::unique_ptr<Base::GravacaoNoAcervo> objGravacao;
        std::string strFalhaAcervo;
        try { objGravacao = std::make_unique<Base::GravacaoNoAcervo>(); }
        catch (const std::exception &ex) { strFalhaAcervo = ex.what(); }
        for (const auto &objPar : *objJanela->Alteradas())
        {
            EditorFlac objEditor;
            if (strFalhaAcervo.empty() && objEditor.Abrir(objPar.first) && TClasseMidia::Gravar(&objEditor, objPar.second) && objEditor.Salvar())
                lstGravadas.push_back(objPar.first);
            else
                lstFalhas.push_back(Base::NomeDoArquivo(objPar.first) + " (" +
                                    (!strFalhaAcervo.empty() ? strFalhaAcervo : !objEditor.Falha().empty() ? objEditor.Falha() : "?") + ")");
        }
    }

    int intReinscritas = !lstGravadas.empty() ? _biblioteca->Reinscrever(lstGravadas) : 0;

    // A faixa pode ter mudado de categoria: listas e álbuns relidos.
    CarregarAlbuns();
    CarregarCategoria();

    std::string strResumo = "Conteúdo: " + std::to_string(lstGravadas.size()) + " faixa(s) marcada(s); " +
                            std::to_string(intReinscritas) + " reinscrita(s) na biblioteca.";
    if (!lstFalhas.empty()) strResumo += " Não consegui marcar: " + Base::Junta(lstFalhas, ", ") + ".";
    TxtEstadoEdicao->Text = strResumo;
    TxtRodape->Text = strResumo;
}

void BibliotecaView::AoSalvarTags()
{
    std::vector<std::shared_ptr<CampoEditor>> mudou = Alterados();
    if (!mudou.empty() && _intObraPainel != 0)
    {
        std::optional<std::string> strResumoObra = SalvarObra(true);
        // Depois de reler: o recarregamento refaz o painel.
        TxtEstadoEdicao->Text = strResumoObra.value_or("");
        return;
    }
    if (!mudou.empty() && _lstArquivosAlbum)
    {
        if (MessageBox::Show(&_janela,
                             "Gravar " + std::to_string(mudou.size()) + " campo(s) em todas as " + std::to_string(_lstArquivosAlbum->size()) +
                                 " faixa(s) do álbum " + Aspas(_strAlbumFiltro) + "?",
                             "Editar o álbum inteiro", MessageBoxButton::OKCancel, MessageBoxImage::Question,
                             MessageBoxResult::OK) != MessageBoxResult::OK)
            return;
        BtnSalvarTags->IsEnabled = false;
        std::optional<std::string> strResumo;
        {
            CursorEspera objEspera;
            strResumo = GravarAlbum(mudou, true);
        }
        // Depois de reler: o recarregamento limpa o painel.
        TxtEstadoEdicao->Text = strResumo.value_or("");
        TxtRodape->Text = strResumo.value_or("");
        return;
    }
    if (mudou.empty() || _arquivoSel.empty()) return;

    BtnSalvarTags->IsEnabled = false;
    TxtEstadoEdicao->Text = "Gravando…";

    bool blnFalhou;
    std::string strResumoFaixa;
    {
        CursorEspera objEspera;
        strResumoFaixa = SalvarFaixa(_arquivoSel, mudou, true, blnFalhou);
    }
    BtnSalvarTags->IsEnabled = !Alterados().empty();
    // A mensagem fica: antes, o AtualizarEstadoEdicao chamado logo
    // depois a trocava por "Sem alterações." na hora. Depois de uma
    // renomeação a faixa foi selecionada de novo, e o painel é outro.
    TxtEstadoEdicao->Text = strResumoFaixa;
    if (!blnFalhou) TxtRodape->Text = strResumoFaixa;
}

/// <summary>
/// Grava a faixa: as tags alteradas e, se o nome mudou, renomeia — e
/// leva o caminho novo a tudo o que aponta para o arquivo (biblioteca do
/// WMP, caches, playlists, ledger). O nome é conferido antes de
/// qualquer gravação: se não servir, nada é tocado.
///
/// Com blnReler, relê a lista e volta a selecionar a faixa; sem ele
/// (troca de seleção em curso), a releitura vem logo depois.
/// Devolve o texto para a pessoa; blnFalhou diz se algo não foi feito.
/// </summary>
std::string BibliotecaView::SalvarFaixa(const std::string &strArquivo, const std::vector<std::shared_ptr<CampoEditor>> &lstMudou,
                                        bool blnReler, bool &blnFalhou)
{
    blnFalhou = false;
    std::shared_ptr<CampoEditor> objNome;
    std::vector<std::shared_ptr<CampoEditor>> lstTags;
    for (const auto &c : lstMudou)
    {
        if (c->Atributo == strCampoNomeArquivo) { if (!objNome) objNome = c; }
        else lstTags.push_back(c);
    }

    std::string strNovoSemExtensao;
    if (objNome)
    {
        strNovoSemExtensao = objNome->Caixa->Text();
        std::string strErro = TRenomeacaoFaixa::Validar(strArquivo, strNovoSemExtensao);
        if (!strErro.empty()) { blnFalhou = true; return "Nada foi gravado. " + strErro; }
    }

    std::string strResumo;
    if (!lstTags.empty())
    {
        std::string strFalhaTags = GravarCampos(strArquivo, lstTags);
        if (!strFalhaTags.empty())
        {
            blnFalhou = true;
            return strFalhaTags + (objNome ? " O arquivo não foi renomeado." : "");
        }
        strResumo = std::to_string(lstTags.size()) + " " + (lstTags.size() == 1 ? "campo gravado" : "campos gravados") + " no arquivo. ";
    }
    if (!objNome) return Base::Trim(strResumo);

    // Artista e álbum para o ledger, do próprio arquivo: na troca de
    // seleção, a faixa selecionada já é outra.
    std::string strArtista, strAlbum;
    {
        EditorFlac objLeitor;
        if (objLeitor.Abrir(strArquivo))
            for (const AtributoFlac &a : objLeitor.LerTudo())
            {
                if (a.Nome == "WM/AlbumTitle" && strAlbum.empty()) strAlbum = a.Valor;
                else if (a.Nome == "WM/AlbumArtist" && !a.Valor.empty()) strArtista = a.Valor;
                else if (a.Nome == "Author" && strArtista.empty()) strArtista = a.Valor;
            }
    }
    // Parado, o player solta o arquivo, mas continua apontando para o
    // caminho antigo: um Tocar depois tentaria abrir o que não existe.
    const auto &p = _janela._player;
    if (p && p->Vivo() && Base::IgualSemCaixa(p->Url(), strArquivo)) p->Url("");

    TResultadoRenomeacao objR = TRenomeacaoFaixa::Renomear(strArquivo, strNovoSemExtensao, RaizAcervo(), strArtista, strAlbum);
    if (!objR.blnOk) { blnFalhou = true; return strResumo + "Não renomeei: " + objR.strFalha; }

    objNome->Original = objNome->Caixa->Text();
    std::string strWmp = _biblioteca->TrocarCaminho(strArquivo, objR.strCaminhoNovo);
    if (Base::IgualSemCaixa(_arquivoSel, strArquivo)) _arquivoSel = objR.strCaminhoNovo;

    strResumo += "Renomeado para " + Aspas(Base::NomeDoArquivo(objR.strCaminhoNovo)) + " — atualizados: " + Base::Junta(objR.lstFeito, ", ") +
                 ". " + strWmp;
    if (!objR.lstAvisos.empty()) strResumo += " Atenção: " + Base::Junta(objR.lstAvisos, "; ") + ".";

    // A lista guarda o caminho antigo: relê, e reencontra a faixa pelo novo.
    std::string strNovo = objR.strCaminhoNovo;
    if (blnReler) RelerESelecionar(strNovo);
    else Base::Despacha([this, strNovo] { RelerESelecionar(strNovo); });
    return strResumo;
}

/// <summary>Relê a categoria e seleciona a faixa deste caminho, se ela estiver na lista.</summary>
void BibliotecaView::RelerESelecionar(const std::string &strCaminho)
{
    CarregarCategoria();
    std::shared_ptr<std::vector<ItemMidia>> lstItens = _lstItens;
    // Cada item lido acorda uma consulta ao WMP: numa lista enorme
    // (Tudo, sem álbum) não vale procurar.
    if (!lstItens || lstItens->size() > 2000) return;
    for (size_t i = 0; i < lstItens->size(); i++)
        if (Base::IgualSemCaixa((*lstItens)[i].Arquivo(), strCaminho))
        {
            int intVista = ListaItens->IndiceNaVista((int) i);
            if (intVista < 0) return;
            ListaItens->SelectedIndex = intVista;
            ListaItens->ScrollIntoView(intVista);
            return;
        }
}

/// <summary>
/// Grava os campos no arquivo. Só os que gravaram de fato passam a
/// valer como "original" — antes, um campo cuja gravação falhou ficava
/// marcado como gravado. null se tudo deu certo; senão, o que faltou.
/// </summary>
std::string BibliotecaView::GravarCampos(const std::string &strArquivo, const std::vector<std::shared_ptr<CampoEditor>> &lstCampos)
{
    // LINUX: o acervo so de leitura abre durante a gravacao
    std::unique_ptr<Base::GravacaoNoAcervo> objGravacao;
    try { objGravacao = std::make_unique<Base::GravacaoNoAcervo>(); }
    catch (const std::exception &ex) { return std::string("Não consegui abrir para gravação: ") + ex.what(); }

    EditorFlac editor;
    if (!editor.Abrir(strArquivo))
        return "Não consegui abrir para gravação: " + (!editor.Falha().empty() ? editor.Falha() : std::string("motivo desconhecido"));

    std::vector<std::shared_ptr<CampoEditor>> lstOk;
    std::vector<std::string> lstFalhou;
    for (const auto &c : lstCampos)
    {
        if (c->Atributo == strCampoNomeArquivo) continue;   // não é tag: é renomear (SalvarFaixa)
        if (editor.DefinirTexto(c->Atributo, c->Caixa->Text())) lstOk.push_back(c);
        else lstFalhou.push_back(CatalogoAsf::RotuloDe(c->Atributo));
    }

    if (!editor.Salvar()) return "Falhou ao salvar: " + (!editor.Falha().empty() ? editor.Falha() : std::string("?"));

    for (const auto &c : lstOk) c->Original = c->Caixa->Text();
    return lstFalhou.empty() ? ""
                             : std::to_string(lstOk.size()) + " campo(s) gravado(s); não gravei: " + Base::Junta(lstFalhou, ", ") + ".";
}

/// <summary>
/// Duplo clique numa faixa toca **e** muda para EM EXECUÇÃO.
///
/// A troca de módulo está amarrada ao gesto, não ao fato de a
/// reprodução começar: o Tocar da barra de baixo também toca a faixa
/// selecionada, mas deixa você onde está.
/// </summary>
void BibliotecaView::AoAbrirItem(MouseButtonEventArgs &e)
{
    // Só duplo clique numa linha: no cabeçalho (ordenar) ou na barra de
    // rolagem, tocava a faixa selecionada.
    if (ListaItens->LinhaEm(e.Posicao) < 0) return;

    ItemMidia *objSelecionado = ListaItens->SelectedItem<ItemMidia>();
    if (objSelecionado == nullptr) return;
    Tocar(*objSelecionado);
    if (_janela.AbaExecucao()) _janela.AbaExecucao()->IsChecked = true;
}

/// <summary>O item é o arquivo que está carregado no player?</summary>
bool BibliotecaView::EhACarregada(const ItemMidia &objItem) const
{
    if (!_janela._player) return false;
    std::string strCaminho = objItem.Arquivo();
    return !strCaminho.empty() && Base::IgualSemCaixa(strCaminho, _janela._player->Url());
}

void BibliotecaView::Tocar(const ItemMidia &objItem)
{
    const auto &p = _janela._player;
    if (!p || !p->Vivo()) return;

    std::string caminho = objItem.Arquivo();
    if (caminho.empty()) return;

    _objItemTocando = objItem;
    // _paradoPeloUsuario = false; _ultimaPosicao = 0; _ultimaDuracao = 0; (do tocador)
    if (_janela.AoComecarFaixa) _janela.AoComecarFaixa();
    p->Url(caminho);
    p->Tocar();
}

/// <summary>
/// Anda na lista que está na tela, e não numa fila paralela: o que você
/// vê é o que toca em seguida — **na ordem em que aparece**, inclusive
/// com a grade ordenada por uma coluna (ListaItens.Items é a vista
/// ordenada; o ItemsSource é a ordem original).
///
/// O ponto de partida é a faixa que está tocando. Se a lista mudou
/// (busca, outra categoria) e ela não está mais na tela, parte da
/// seleção; sem seleção, não toca nada — melhor parar que tocar uma
/// faixa qualquer.
/// </summary>
void BibliotecaView::Pular(int passo)
{
    int intQtd = ListaItens->Count();
    if (intQtd == 0) return;

    int origem = -1;
    if (_objItemTocando)
        for (int i = 0; i < intQtd; i++)
            if (ListaItens->Item<ItemMidia>(i)->MesmoItem(*_objItemTocando)) { origem = i; break; }
    if (origem < 0 && ListaItens->SelectedIndex() >= 0) origem = ListaItens->SelectedIndex();
    if (origem < 0) return;

    int destino = origem + passo;
    if (destino < 0 || destino >= intQtd) return;

    ItemMidia *objDestino = ListaItens->Item<ItemMidia>(destino);
    if (objDestino == nullptr) return;
    ItemMidia objCopia = *objDestino;
    Tocar(objCopia);
    ListaItens->SelectedIndex = destino;
    ListaItens->ScrollIntoView(destino);
}

// ===============================================================
void BibliotecaView::Avisar(const std::string &titulo, const std::string &texto)
{
    TxtAvisoTitulo->Text = titulo;
    TxtAvisoTexto->Text = texto;
    PainelAviso->Visibility = Vistas::Visibility::Visible;
}

void BibliotecaView::EsconderAviso()
{
    PainelAviso->Visibility = Vistas::Visibility::Collapsed;
}

// ---- o encaixe na MainWindow (ver o fim de MainWindow.h) ----
std::shared_ptr<VistaModulo> CriaModuloBiblioteca(MainWindow &janela)
{
    return std::make_shared<BibliotecaView>(janela);
}

} // namespace ARCA::Vistas

// ARCA no xOS - Vistas/Dialogos: MessageBox e OpenFileDialog (ver Dialogos.h).
#include "Dialogos.h"

#include "../Base/Arquivo.h"
#include "../Base/Texto.h"
#include "../Modelo/Formato.h"

#ifndef NANOVG_GLES2
#define NANOVG_GLES2
#endif
#include "nanovg/nanovg.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <sys/stat.h>

namespace ARCA::Vistas {

namespace fs = std::filesystem;

namespace {

Window *DonoPadrao()
{
    // o MessageBox sem dono do WPF fica com a janela ativa
    for (Window *j : Application::Janelas())
        if (j->IsActive()) return j;
    if (Window *m = Application::JanelaModalAtual()) return m;
    return Application::MainWindow();
}

// o icone da caixa de mensagem: os quatro do Windows, nas cores do Tema
class IconeMensagem : public UIElement
{
public:
    explicit IconeMensagem(MessageBoxImage i) : _i(i) {}
    Size MeasureOverride(Size d) override
    {
        (void) d;
        return Size(32, 32);
    }
    void OnRender(DrawingContext &dc) override
    {
        NVGcontext *vg = dc.vg;
        float x = (float) Limites.X, y = (float) Limites.Y;
        Color fundo = _i == MessageBoxImage::Error ? Tema::Recurso("Alarme").Cor()
                      : _i == MessageBoxImage::Warning ? Tema::Recurso("Acento").Cor() : Tema::Recurso("TextoFraco").Cor();
        Color tinta = Tema::Recurso("Canvas").Cor();
        nvgBeginPath(vg);
        if (_i == MessageBoxImage::Warning)
        {
            nvgMoveTo(vg, x + 16, y + 2);
            nvgLineTo(vg, x + 31, y + 29);
            nvgLineTo(vg, x + 1, y + 29);
            nvgClosePath(vg);
            nvgLineJoin(vg, NVG_ROUND);
        }
        else
            nvgCircle(vg, x + 16, y + 16, 15);
        nvgFillColor(vg, nvgRGBA(fundo.R, fundo.G, fundo.B, 255));
        nvgFill(vg);
        FonteTexto f;
        f.Peso = FontWeight::Bold;
        f.Tamanho = _i == MessageBoxImage::Warning ? 17 : 20;
        const char *g = _i == MessageBoxImage::Error ? "\xC3\x97" : _i == MessageBoxImage::Warning ? "!" : _i == MessageBoxImage::Question ? "?" : "i";
        double w = Texto::Largura(f, g);
        double topo = _i == MessageBoxImage::Warning ? 8 : 16 - Texto::AlturaLinha(f) / 2;
        if (_i == MessageBoxImage::Warning) topo = 29 - Texto::AlturaLinha(f) + 1;
        dc.Texto(g, f, x + 16 - w / 2, y + topo, Brush(tinta));
    }

private:
    MessageBoxImage _i;
};

} // namespace

MessageBoxResult MessageBox::Show(const std::string &texto, const std::string &titulo, MessageBoxButton botoes,
                                  MessageBoxImage imagem, MessageBoxResult padrao)
{
    return Show(nullptr, texto, titulo, botoes, imagem, padrao);
}

MessageBoxResult MessageBox::Show(Window *dono, const std::string &texto, const std::string &titulo, MessageBoxButton botoes,
                                  MessageBoxImage imagem, MessageBoxResult padrao)
{
    if (!dono) dono = DonoPadrao();
    auto j = std::make_shared<Window>();
    j->Title = titulo;
    j->Owner = dono;
    j->WindowStartupLocation = dono ? WindowStartupLocation::CenterOwner : WindowStartupLocation::CenterScreen;
    j->SizeToContent = SizeToContent::WidthAndHeight;
    j->ResizeMode = ResizeMode::NoResize;
    j->ShowInTaskbar = false;
    j->Background = Tema::Recurso("Canvas");
    j->MinWidth = 300;

    MessageBoxResult resultado = MessageBoxResult::None;
    std::vector<std::pair<std::string, MessageBoxResult>> lista;
    switch (botoes)
    {
    case MessageBoxButton::OK: lista = {{"OK", MessageBoxResult::OK}}; break;
    case MessageBoxButton::OKCancel: lista = {{"OK", MessageBoxResult::OK}, {"Cancelar", MessageBoxResult::Cancel}}; break;
    case MessageBoxButton::YesNo: lista = {{"Sim", MessageBoxResult::Yes}, {"Não", MessageBoxResult::No}}; break;
    case MessageBoxButton::YesNoCancel:
        lista = {{"Sim", MessageBoxResult::Yes}, {"Não", MessageBoxResult::No}, {"Cancelar", MessageBoxResult::Cancel}};
        break;
    }
    // o botao padrao: o pedido, se existir na caixa; senao o primeiro
    MessageBoxResult def = lista.front().second;
    for (auto &b : lista)
        if (b.second == padrao) def = padrao;

    auto raiz = std::make_shared<DockPanel>();
    auto barra = std::make_shared<Border>();
    barra->Background = Tema::Recurso("BarraFerramentas");
    barra->BorderBrush = Tema::Recurso("Borda");
    barra->BorderThickness = Thickness(0, 1, 0, 0);
    barra->Padding = Thickness(12, 10);
    DockPanel::SetDock(barra, Dock::Bottom);
    auto pilha = std::make_shared<StackPanel>();
    pilha->Orientation = Orientation::Horizontal;
    pilha->HorizontalAlignment = HorizontalAlignment::Right;
    std::shared_ptr<Button> padraoBtn;
    for (auto &b : lista)
    {
        auto btn = std::make_shared<Button>();
        btn->Content = b.first;
        btn->MinWidth = 75;
        btn->Margin = Thickness(8, 0, 0, 0);
        MessageBoxResult r = b.second;
        Window *jp = j.get();
        btn->Click += [&resultado, r, jp](RoutedEventArgs &) {
            resultado = r;
            jp->Close();
        };
        if (r == def)
        {
            btn->IsDefault = true;
            padraoBtn = btn;
        }
        pilha->Children.Add(btn);
    }
    barra->SetChild(pilha);
    raiz->Children.Add(barra);

    auto corpo = std::make_shared<Grid>();
    corpo->Margin = Thickness(20, 22, 24, 22);
    corpo->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));
    corpo->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Star(1)));
    if (imagem != MessageBoxImage::None)
    {
        auto ic = std::make_shared<IconeMensagem>(imagem);
        ic->Margin = Thickness(0, 0, 14, 0);
        ic->VerticalAlignment = VerticalAlignment::Top;
        corpo->Children.Add(ic);
    }
    auto t = std::make_shared<TextBlock>(texto);
    t->TextWrapping = TextWrapping::Wrap;
    t->MaxWidth = 440;
    t->Foreground = Tema::Recurso("Texto");
    t->VerticalAlignment = VerticalAlignment::Center;
    Grid::SetColumn(t, 1);
    corpo->Children.Add(t);
    raiz->Children.Add(corpo);
    j->Content = raiz;

    // Esc: Cancelar; com OK sozinho, o OK (como no Windows). Sim/Nao nao tem Esc.
    j->PreviewKeyDown += [&](KeyEventArgs &k) {
        if (k.Key != Key::Escape) return;
        k.Handled = true;
        if (botoes == MessageBoxButton::YesNo) return;
        resultado = botoes == MessageBoxButton::OK ? MessageBoxResult::OK : MessageBoxResult::Cancel;
        j->Close();
    };
    j->Loaded += [&](RoutedEventArgs &) {
        if (padraoBtn) padraoBtn->Focus();
    };
    j->ShowDialog();
    if (resultado == MessageBoxResult::None)
    {
        // fechada pela barra de titulo
        switch (botoes)
        {
        case MessageBoxButton::OK: resultado = MessageBoxResult::OK; break;
        case MessageBoxButton::YesNo: resultado = MessageBoxResult::No; break;
        default: resultado = MessageBoxResult::Cancel; break;
        }
    }
    return resultado;
}

// =====================================================================================
// OpenFileDialog
// =====================================================================================
namespace {

struct Entrada
{
    std::string Nome, Caminho, Data, Tipo, Tamanho;
    bool Pasta = false;
    int64_t Bytes = 0;
    int64_t Quando = 0;
};

std::string TipoDe(const std::string &ext)
{
    std::string e = Base::Minusculas(ext);
    if (e == ".jpg" || e == ".jpeg") return "Imagem JPEG";
    if (e == ".png") return "Imagem PNG";
    if (e == ".bmp") return "Imagem de bitmap";
    if (e == ".tif" || e == ".tiff") return "Imagem TIFF";
    if (e == ".gif") return "Imagem GIF";
    if (e == ".flac") return "Áudio FLAC";
    if (e == ".wma") return "Arquivo de Áudio do Windows Media";
    if (e == ".mp3") return "Formato MP3";
    if (e == ".txt") return "Documento de Texto";
    if (e == ".pdf") return "Documento PDF";
    if (e.empty()) return "Arquivo";
    return "Arquivo " + Base::Maiusculas(e.substr(1));
}

std::string DataDe(int64_t t)
{
    time_t tt = (time_t) t;
    struct tm l;
    localtime_r(&tt, &l);
    char b[32];
    strftime(b, sizeof b, "%d/%m/%Y %H:%M", &l);
    return b;
}

struct Filtro
{
    std::string Descricao;
    std::vector<std::string> Extensoes;   // vazio = todos
};

std::vector<Filtro> LeFiltro(const std::string &f)
{
    std::vector<Filtro> r;
    auto partes = Base::Divide(f, "|");
    for (size_t i = 0; i + 1 < partes.size(); i += 2)
    {
        Filtro x;
        x.Descricao = partes[i];
        for (auto &p : Base::Divide(partes[i + 1], ";", true))
        {
            std::string q = Base::Trim(p);
            if (q == "*.*" || q == "*") { x.Extensoes.clear(); break; }
            if (q.rfind("*.", 0) == 0) x.Extensoes.push_back(Base::Minusculas(q.substr(1)));
        }
        r.push_back(x);
    }
    if (r.empty()) r.push_back({"Todos os arquivos (*.*)", {}});
    return r;
}

std::string s_ultimaPasta;

class JanelaArquivo : public Window
{
public:
    OpenFileDialog &D;
    std::vector<Filtro> Filtros;
    std::string Pasta;
    std::vector<Entrada> Itens;
    std::shared_ptr<TextBox> CaixaPasta, CaixaNome;
    std::shared_ptr<DataGrid> Grade;
    std::shared_ptr<ComboBox> ListaFiltros;
    std::shared_ptr<ListBox> Locais;
    std::shared_ptr<TextBlock> Aviso;
    std::vector<std::pair<std::string, std::string>> LocaisCaminhos;
    bool Ok = false;

    explicit JanelaArquivo(OpenFileDialog &d) : D(d)
    {
        Title = d.Title.empty() ? "Abrir" : d.Title;
        Width = 780;
        Height = 520;
        MinWidth = 560;
        MinHeight = 380;
        WindowStartupLocation = Vistas::WindowStartupLocation::CenterOwner;
        ShowInTaskbar = false;
        Background = Tema::Recurso("Canvas");
        Filtros = LeFiltro(d.Filter);

        auto raiz = std::make_shared<DockPanel>();

        // ---- a pasta atual, no alto (a trilha) ----
        auto topo = std::make_shared<Border>();
        topo->Background = Tema::Recurso("BarraTrilha");
        topo->BorderBrush = Tema::Recurso("LinhaCabecalho");
        topo->BorderThickness = Thickness(0, 0, 0, 1);
        topo->Padding = Thickness(12, 7);
        DockPanel::SetDock(topo, Dock::Top);
        auto gTopo = std::make_shared<Grid>();
        gTopo->ColumnDefinitions = {ColumnDefinition(GridLength::Auto()), ColumnDefinition(GridLength::Star(1))};
        auto subir = std::make_shared<Button>();
        subir->Content = "Subir";
        subir->ToolTip = "A pasta de cima (Backspace)";
        subir->Margin = Thickness(0, 0, 8, 0);
        subir->Click += [this](RoutedEventArgs &) { Sobe(); };
        gTopo->Children.Add(subir);
        CaixaPasta = std::make_shared<TextBox>();
        CaixaPasta->ToolTip = "Digite uma pasta e tecle Enter";
        CaixaPasta->KeyDown += [this](KeyEventArgs &k) {
            if (k.Key == Key::Enter)
            {
                k.Handled = true;
                Vai(Base::Trim(CaixaPasta->Text()));
            }
        };
        Grid::SetColumn(CaixaPasta, 1);
        gTopo->Children.Add(CaixaPasta);
        topo->SetChild(gTopo);
        raiz->Children.Add(topo);

        // ---- nome, filtro e botoes, embaixo ----
        auto baixo = std::make_shared<Border>();
        baixo->Background = Tema::Recurso("BarraFerramentas");
        baixo->BorderBrush = Tema::Recurso("Borda");
        baixo->BorderThickness = Thickness(0, 1, 0, 0);
        baixo->Padding = Thickness(14, 8);
        DockPanel::SetDock(baixo, Dock::Bottom);
        auto gBaixo = std::make_shared<Grid>();
        gBaixo->ColumnDefinitions = {ColumnDefinition(GridLength::Auto()), ColumnDefinition(GridLength::Star(1)),
                                     ColumnDefinition(GridLength::Auto()), ColumnDefinition(GridLength::Auto()),
                                     ColumnDefinition(GridLength::Auto())};
        gBaixo->RowDefinitions = {RowDefinition(GridLength::Auto()), RowDefinition(GridLength::Auto())};
        auto rotNome = std::make_shared<TextBlock>("Nome:");
        rotNome->Style = Tema::Estilo("Fraco");
        rotNome->VerticalAlignment = Vistas::VerticalAlignment::Center;
        rotNome->Margin = Thickness(0, 0, 8, 0);
        gBaixo->Children.Add(rotNome);
        CaixaNome = std::make_shared<TextBox>();
        Grid::SetColumn(CaixaNome, 1);
        gBaixo->Children.Add(CaixaNome);
        ListaFiltros = std::make_shared<ComboBox>();
        ListaFiltros->Width = 230;
        ListaFiltros->Margin = Thickness(8, 0, 0, 0);
        for (auto &f : Filtros) ListaFiltros->Items.Add(f.Descricao);
        ListaFiltros->SelectedIndex = std::clamp(d.FilterIndex - 1, 0, (int) Filtros.size() - 1);
        ListaFiltros->SelectionChanged += [this](SelectionChangedEventArgs &) { Vai(Pasta); };
        Grid::SetColumn(ListaFiltros, 2);
        gBaixo->Children.Add(ListaFiltros);
        auto abrir = std::make_shared<Button>();
        abrir->Content = "Abrir";
        abrir->IsDefault = true;
        abrir->MinWidth = 75;
        abrir->Margin = Thickness(12, 0, 0, 0);
        abrir->Click += [this](RoutedEventArgs &) { Abrir(); };
        Grid::SetColumn(abrir, 3);
        gBaixo->Children.Add(abrir);
        auto cancelar = std::make_shared<Button>();
        cancelar->Content = "Cancelar";
        cancelar->IsCancel = true;
        cancelar->MinWidth = 75;
        cancelar->Margin = Thickness(8, 0, 0, 0);
        cancelar->Click += [this](RoutedEventArgs &) { Close(); };
        Grid::SetColumn(cancelar, 4);
        gBaixo->Children.Add(cancelar);
        Aviso = std::make_shared<TextBlock>();
        Aviso->Foreground = Tema::Recurso("Alarme");
        Aviso->TextWrapping = TextWrapping::Wrap;
        Aviso->Margin = Thickness(0, 6, 0, 0);
        Aviso->Visibility = Vistas::Visibility::Collapsed;
        Grid::SetRow(Aviso, 1);
        Grid::SetColumnSpan(Aviso, 5);
        gBaixo->Children.Add(Aviso);
        baixo->SetChild(gBaixo);
        raiz->Children.Add(baixo);

        // ---- locais, a esquerda ----
        auto lado = std::make_shared<Border>();
        lado->Background = Tema::Recurso("PainelLateral");
        lado->BorderBrush = Tema::Recurso("Borda");
        lado->BorderThickness = Thickness(0, 0, 1, 0);
        lado->Width = 180;
        DockPanel::SetDock(lado, Dock::Left);
        auto pLado = std::make_shared<DockPanel>();
        auto rot = std::make_shared<TextBlock>("LOCAIS");
        rot->Style = Tema::Estilo("Rotulo");
        rot->Margin = Thickness(12, 12, 0, 6);
        DockPanel::SetDock(rot, Dock::Top);
        pLado->Children.Add(rot);
        const char *home = getenv("HOME");
        LocaisCaminhos = {{"Músicas", Base::PastaAcervo()}, {"Pasta pessoal", home ? home : "/root"}, {"Discos", "/mnt"}, {"Raiz", "/"}};
        Locais = std::make_shared<ListBox>();
        Locais->Background = Brushes::Transparent;
        Locais->BorderThickness = Thickness(0);
        Locais->ItemContainerStyle = Tema::Estilo("ItemAlbum");
        for (auto &l : LocaisCaminhos) Locais->AddItem(l.first);
        Locais->SelectionChanged += [this](SelectionChangedEventArgs &) {
            int i = Locais->SelectedIndex();
            if (i >= 0) Vai(LocaisCaminhos[(size_t) i].second);
        };
        pLado->Children.Add(Locais);
        lado->SetChild(pLado);
        raiz->Children.Add(lado);

        // ---- a lista da pasta ----
        Grade = std::make_shared<DataGrid>();
        Grade->Background = Tema::Recurso("FundoLista");
        Grade->BorderThickness = Thickness(0);
        Grade->IsReadOnly = true;
        Grade->HeadersVisibility = DataGridHeadersVisibility::Column;
        Grade->SelectionMode = d.Multiselect ? SelectionMode::Extended : SelectionMode::Single;
        Grade->GridLinesVisibility = DataGridGridLinesVisibility::Horizontal;
        Grade->HorizontalGridLinesBrush = Tema::Recurso("LinhaCabecalho");
        Grade->RowBackground = Brushes::Transparent;
        Grade->AlternatingRowBackground = Tema::Recurso("LinhaAlternada");
        Grade->RowHeight = 24;
        auto nome = DataGridTextColumn::Nova<Entrada>("Nome", DataGridLength::Star(2.6), [](Entrada &e) { return e.Nome; });
        // as pastas primeiro, como no Explorer
        nome->Compara = [](void *a, void *b) {
            auto *x = static_cast<Entrada *>(a), *y = static_cast<Entrada *>(b);
            if (x->Pasta != y->Pasta) return x->Pasta ? -1 : 1;
            std::string p = Base::Minusculas(Base::SemAcentos(x->Nome)), q = Base::Minusculas(Base::SemAcentos(y->Nome));
            return p < q ? -1 : (q < p ? 1 : 0);
        };
        auto data = DataGridTextColumn::Nova<Entrada>("Data de modificação", DataGridLength(130), [](Entrada &e) { return e.Data; });
        data->OrdenaPor<Entrada, int64_t>([](Entrada &e) { return e.Quando; });
        auto tipo = DataGridTextColumn::Nova<Entrada>("Tipo", DataGridLength(150), [](Entrada &e) { return e.Tipo; });
        auto tam = DataGridTextColumn::Nova<Entrada>("Tamanho", DataGridLength(90), [](Entrada &e) { return e.Tamanho; });
        tam->AlinhamentoTexto = TextAlignment::Right;
        tam->OrdenaPor<Entrada, int64_t>([](Entrada &e) { return e.Bytes; });
        Grade->Columns = {nome, data, tipo, tam};
        Grade->SelectionChanged += [this](SelectionChangedEventArgs &) {
            Entrada *e = Grade->SelectedItem<Entrada>();
            if (e && !e->Pasta) CaixaNome->Text = e->Nome;
        };
        Grade->MouseDoubleClick += [this](MouseButtonEventArgs &m) {
            int l = Grade->LinhaEm(m.Posicao);
            if (l < 0) return;
            m.Handled = true;
            Entrar(*Grade->Item<Entrada>(l));
        };
        Grade->PreviewKeyDown += [this](KeyEventArgs &k) {
            if (k.Key == Key::Enter)
            {
                k.Handled = true;
                if (Entrada *e = Grade->SelectedItem<Entrada>()) Entrar(*e);
            }
            else if (k.Key == Key::Back)
            {
                k.Handled = true;
                Sobe();
            }
        };
        raiz->Children.Add(Grade);
        Content = raiz;

        std::string ini = !d.InitialDirectory.empty() ? d.InitialDirectory : !s_ultimaPasta.empty() ? s_ultimaPasta : (home ? home : "/");
        if (!d.FileName.empty() && d.FileName.find('/') != std::string::npos)
        {
            ini = Base::PastaDe(d.FileName);
            CaixaNome->Text = Base::NomeDoArquivo(d.FileName);
        }
        Vai(ini);
        Loaded += [this](RoutedEventArgs &) { Grade->Focus(); };
    }

    void Avisa(const std::string &s)
    {
        Aviso->Text = s;
        Aviso->Visibility = s.empty() ? Vistas::Visibility::Collapsed : Vistas::Visibility::Visible;
    }

    void Vai(const std::string &pasta)
    {
        std::error_code ec;
        fs::path p(pasta.empty() ? "/" : pasta);
        if (!fs::is_directory(p, ec))
        {
            Avisa("A pasta \"" + pasta + "\" não existe ou não pode ser aberta.");
            CaixaPasta->Text = Pasta;
            return;
        }
        Avisa("");
        Pasta = fs::absolute(p, ec).lexically_normal().string();
        if (Pasta.size() > 1 && Pasta.back() == '/') Pasta.pop_back();
        CaixaPasta->Text = Pasta;
        const Filtro &f = Filtros[(size_t) std::clamp(ListaFiltros->SelectedIndex(), 0, (int) Filtros.size() - 1)];
        std::vector<Entrada> v;
        for (auto it = fs::directory_iterator(p, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            std::string n = it->path().filename().string();
            if (n.empty() || n[0] == '.') continue;
            Entrada e;
            e.Nome = n;
            e.Caminho = it->path().string();
            struct stat st;
            if (stat(e.Caminho.c_str(), &st) != 0) continue;
            e.Pasta = S_ISDIR(st.st_mode);
            e.Quando = (int64_t) st.st_mtime;
            e.Data = DataDe(e.Quando);
            if (e.Pasta) e.Tipo = "Pasta de arquivos";
            else
            {
                std::string ext = Base::Minusculas(it->path().extension().string());
                if (!f.Extensoes.empty() && std::find(f.Extensoes.begin(), f.Extensoes.end(), ext) == f.Extensoes.end()) continue;
                e.Tipo = TipoDe(ext);
                e.Bytes = (int64_t) st.st_size;
                e.Tamanho = Modelo::Formato::Inteiro(std::ceil(e.Bytes / 1024.0)) + " KB";
            }
            v.push_back(e);
        }
        std::sort(v.begin(), v.end(), [](const Entrada &a, const Entrada &b) {
            if (a.Pasta != b.Pasta) return a.Pasta;
            return Base::Minusculas(Base::SemAcentos(a.Nome)) < Base::Minusculas(Base::SemAcentos(b.Nome));
        });
        Itens = std::move(v);
        for (auto &c : Grade->Columns) c->SortDirection.reset();
        Grade->ItemsSource = FonteDeItens::De(&Itens);
        if (!Itens.empty()) Grade->SelectedIndex = 0;
        Grade->ScrollIntoView(0);
    }

    void Sobe()
    {
        fs::path p(Pasta);
        if (p.has_parent_path() && p.parent_path() != p) Vai(p.parent_path().string());
    }

    void Entrar(const Entrada &e)
    {
        if (e.Pasta) Vai(e.Caminho);
        else
        {
            CaixaNome->Text = e.Nome;
            Abrir();
        }
    }

    void Abrir()
    {
        std::string s = Base::Trim(CaixaNome->Text());
        if (s.empty())
        {
            if (Entrada *e = Grade->SelectedItem<Entrada>())
                if (e->Pasta) Vai(e->Caminho);
            return;
        }
        std::string caminho = s[0] == '/' ? s : Base::Combina(Pasta, s);
        std::error_code ec;
        if (fs::is_directory(caminho, ec))
        {
            CaixaNome->Text = std::string();
            Vai(caminho);
            return;
        }
        if (D.CheckFileExists && !fs::exists(caminho, ec))
        {
            MessageBox::Show(this, caminho + "\nArquivo não encontrado.\nVerifique se o nome do arquivo está correto e tente novamente.",
                             Title, MessageBoxButton::OK, MessageBoxImage::Warning);
            return;
        }
        D.FileNames.clear();
        if (D.Multiselect)
            for (Entrada *e : Grade->SelectedItems<Entrada>())
                if (e && !e->Pasta) D.FileNames.push_back(e->Caminho);
        if (D.FileNames.empty()) D.FileNames.push_back(caminho);
        D.FileName = D.FileNames.front();
        D.FilterIndex = ListaFiltros->SelectedIndex() + 1;
        s_ultimaPasta = Pasta;
        Ok = true;
        SetDialogResult(true);
    }
};

} // namespace

std::optional<bool> OpenFileDialog::ShowDialog(Window *dono)
{
    auto j = std::make_shared<JanelaArquivo>(*this);
    j->Owner = dono ? dono : DonoPadrao();
    j->ShowDialog();
    return j->Ok;
}

} // namespace ARCA::Vistas

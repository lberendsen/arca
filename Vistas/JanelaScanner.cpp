// ARCA no xOS - Vistas/JanelaScanner. Ver JanelaScanner.h.
//
// LINUX: o arquivo inteiro substitui o dialogo comum do WIA (o "Digitalizar" do Windows), que o
// ScannerWia.cs abria e o Linux nao tem. Nao vem de um .cs do ARCA.
#include "JanelaScanner.h"

#include "ImagensDoEncarte.h"

#include "../Base/Despacho.h"
#include "../Base/Excecoes.h"
#include "../Interop/ScannerSane.h"
#include "../Modelo/Formato.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <thread>

namespace ARCA::Vistas {

using Interop::ScannerSane;

// LINUX: o mesmo veu da JanelaCorteImagem (a mesa menos a area escolhida)
class VeuDaArea : public UIElement
{
public:
    Brush Fill;
    Rect Fora, Dentro;    // em coordenadas do palco
    VeuDaArea() { IsHitTestVisible = false; }
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
        pinta(Fora.X, Fora.Y, Fora.Width, d.Y - Fora.Y);
        pinta(Fora.X, d.Y + d.Height, Fora.Width, Fora.Y + Fora.Height - (d.Y + d.Height));
        pinta(Fora.X, d.Y, d.X - Fora.X, d.Height);
        pinta(d.X + d.Width, d.Y, Fora.X + Fora.Width - (d.X + d.Width), d.Height);
    }
};

// a leitura em segundo plano: o fio so fala com a janela pelo Despacha, e so se ela ainda existir
struct JanelaScanner::Trabalho
{
    std::atomic<bool> blnCancelar{false};
    std::atomic<bool> blnJanelaViva{true};
};

namespace {

Point TopLeft(const Rect &r) { return Point(r.X, r.Y); }
Point TopRight(const Rect &r) { return Point(r.X + r.Width, r.Y); }
Point BottomRight(const Rect &r) { return Point(r.X + r.Width, r.Y + r.Height); }
Point BottomLeft(const Rect &r) { return Point(r.X, r.Y + r.Height); }
Rect Entre(Point a, Point b) { return Rect(std::min(a.X, b.X), std::min(a.Y, b.Y), std::fabs(a.X - b.X), std::fabs(a.Y - b.Y)); }
bool Contem(const Rect &r, Point p) { return p.X >= r.X && p.X <= r.X + r.Width && p.Y >= r.Y && p.Y <= r.Y + r.Height; }

void Apaga(const std::string &strArquivo)
{
    std::error_code ec;
    if (!strArquivo.empty()) std::filesystem::remove(strArquivo, ec);
}

// Onde está o que foi posto na mesa, na prévia (em px dela); nullopt = nada achado. É a ideia da
// detecção da JanelaCorteImagem (linhas e colunas com mais de 2% de pixels de conteúdo), mas a
// prévia da mesa inteira da G7000 pede outro critério: a tampa é cinza, com faixas de luz (de 61%
// a 77% de brilho conforme a coluna, e mais escura embaixo) e sombra nas bordas da mesa, e nenhuma
// "cor de fundo" separa isso do livreto. Conta como conteúdo o pixel onde a imagem muda de repente
// (as bordas do que foi posto e o que está impresso nele); a luz da tampa muda devagar. Linhas ou
// colunas "cheias" de ponta a ponta (mais de 80%) são a borda da própria mesa.
std::optional<Rect> DetectarNaMesa(const std::shared_ptr<BitmapSource> &objImagem)
{
    double dblReducao = std::min(1.0, 300.0 / std::max(objImagem->PixelWidth, objImagem->PixelHeight));
    std::shared_ptr<BitmapSource> objP = objImagem->Escalada(dblReducao);
    int intL = objP->PixelWidth, intA = objP->PixelHeight;
    if (intL < 16 || intA < 16) return std::nullopt;
    const std::vector<uint8_t> &px = objP->Pixels;
    auto p = [&](int x, int y, int c) { return (int) px[((size_t) y * intL + x) * 4 + c]; };
    std::vector<char> conteudo((size_t) intL * intA);
    std::vector<int> porColuna(intL, 0), porLinha(intA, 0);
    for (int y = 1; y < intA - 1; y++)
        for (int x = 1; x < intL - 1; x++)
        {
            int g = 0;
            for (int c = 0; c < 3; c++) g += std::abs(p(x + 1, y, c) - p(x - 1, y, c)) + std::abs(p(x, y + 1, c) - p(x, y - 1, c));
            if (g > 60) { conteudo[(size_t) y * intL + x] = 1; porColuna[x]++; porLinha[y]++; }
        }
    std::vector<char> colunaBorda(intL), linhaBorda(intA);
    for (int x = 0; x < intL; x++) colunaBorda[x] = porColuna[x] > intA * 8 / 10;
    for (int y = 0; y < intA; y++) linhaBorda[y] = porLinha[y] > intL * 8 / 10;
    std::fill(porColuna.begin(), porColuna.end(), 0);
    std::fill(porLinha.begin(), porLinha.end(), 0);
    for (int y = 0; y < intA; y++)
        for (int x = 0; x < intL; x++)
            if (conteudo[(size_t) y * intL + x] && !colunaBorda[x] && !linhaBorda[y]) { porColuna[x]++; porLinha[y]++; }
    auto primeiro = [](const std::vector<int> &v, int m) { for (int i = 0; i < (int) v.size(); i++) if (v[i] > std::max(1, m)) return i; return -1; };
    auto ultimo = [](const std::vector<int> &v, int m) { for (int i = (int) v.size() - 1; i >= 0; i--) if (v[i] > std::max(1, m)) return i; return -1; };
    int y0 = primeiro(porLinha, intL / 50), y1 = ultimo(porLinha, intL / 50);
    int x0 = primeiro(porColuna, intA / 50), x1 = ultimo(porColuna, intA / 50);
    if (y0 < 0 || x0 < 0) return std::nullopt;
    Rect r(x0 / dblReducao, y0 / dblReducao, (x1 - x0 + 1) / dblReducao, (y1 - y0 + 1) / dblReducao);
    r = r.Intersecao(Rect(0, 0, objImagem->PixelWidth, objImagem->PixelHeight));
    if (r.Width * r.Height / ((double) objImagem->PixelWidth * objImagem->PixelHeight) > 0.92) return std::nullopt;
    return r;
}

} // namespace

const int JanelaScanner::arrResolucoes[5] = {75, 150, 300, 600, 1200};
int JanelaScanner::s_intUltimaResolucao = ScannerSane::ResolucaoPadrao;

JanelaScanner::JanelaScanner(const std::string &strNomeScanner)
{
    Title = "Digitalizar usando " + strNomeScanner;
    Width = 900;
    Height = 760;
    MinWidth = 600;
    MinHeight = 500;
    WindowStartupLocation = Vistas::WindowStartupLocation::CenterOwner;
    ShowInTaskbar = false;
    Background = Tema::Recurso("Canvas");
    _objArea = Rect(0, 0, _dblMesaL, _dblMesaA);

    _objMesa = std::make_shared<Rectangle>();
    _objMesa->Fill = Tema::Recurso("PainelLateral");
    _objMesa->Stroke = Tema::Recurso("Borda");
    _objMesa->StrokeThickness = 1;
    _objMesa->IsHitTestVisible = false;
    _objVista = std::make_shared<Image>();
    _objVista->Stretch = Stretch::Fill;
    _objVista->IsHitTestVisible = false;
    _objVeu = std::make_shared<VeuDaArea>();
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
    _objPalco->Children.Add(_objMesa);
    _objPalco->Children.Add(_objVista);
    _objPalco->Children.Add(_objVeu);
    _objPalco->Children.Add(_objMoldura);
    for (auto &objAlca : _arrAlcas)
    {
        objAlca = std::make_shared<Rectangle>();
        objAlca->Width = 10;
        objAlca->Height = 10;
        objAlca->Fill = Tema::Recurso("AcentoVivo");
        objAlca->Stroke = Brushes::White;
        objAlca->StrokeThickness = 1;
        objAlca->IsHitTestVisible = false;
        _objPalco->Children.Add(objAlca);
    }
    _txtAguarde = std::make_shared<TextBlock>();
    _txtAguarde->Style = Tema::Estilo("Fraco");
    _txtAguarde->HorizontalAlignment = HorizontalAlignment::Center;
    _txtAguarde->VerticalAlignment = VerticalAlignment::Center;
    _txtAguarde->IsHitTestVisible = false;

    auto objSobreposto = std::make_shared<Grid>();
    objSobreposto->Children.Add(_objPalco);
    objSobreposto->Children.Add(_txtAguarde);
    auto objAreaPalco = std::make_shared<Border>();
    objAreaPalco->Background = Tema::Recurso("ChapaFunda");
    objAreaPalco->BorderBrush = Tema::Recurso("Borda");
    objAreaPalco->BorderThickness = Thickness(1);
    objAreaPalco->Margin = Thickness(14);
    objAreaPalco->SetChild(objSobreposto);
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
    Closing += [this](CancelEventArgs &e) { AoFechar(e); };

    // a prévia começa sozinha: sem ela não há onde escolher a área (17 s na G7000)
    IniciarPrevia();
}

JanelaScanner::~JanelaScanner()
{
    if (_objTrabalho)
    {
        _objTrabalho->blnJanelaViva = false;
        _objTrabalho->blnCancelar = true;
    }
}

// ===============================================================
UIElementPtr JanelaScanner::MontarBarra()
{
    auto objBarra = std::make_shared<WrapPanel>();
    auto objRotulo = std::make_shared<TextBlock>("Resolução");
    objRotulo->VerticalAlignment = VerticalAlignment::Center;
    objRotulo->Margin = Thickness(0, 0, 6, 0);
    objBarra->Children.Add(objRotulo);
    _cmbResolucao = std::make_shared<ComboBox>();
    _cmbResolucao->Width = 110;
    _cmbResolucao->VerticalAlignment = VerticalAlignment::Center;
    _cmbResolucao->Margin = Thickness(0, 0, 14, 0);
    _cmbResolucao->ToolTip = "Pontos por polegada. 600 dpi é o padrão do acervo; a prévia é sempre a 75.";
    int intEscolhida = 3;
    for (int i = 0; i < 5; i++)
    {
        _cmbResolucao->Items.Add(std::to_string(arrResolucoes[i]) + " dpi");
        if (arrResolucoes[i] == s_intUltimaResolucao) intEscolhida = i;
    }
    _cmbResolucao->SelectedIndex = intEscolhida;
    _cmbResolucao->SelectionChanged += [this](SelectionChangedEventArgs &) { Desenhar(); };
    objBarra->Children.Add(_cmbResolucao);
    _btnMesaInteira = std::make_shared<Button>();
    _btnMesaInteira->Content = "Mesa inteira";
    _btnMesaInteira->Padding = Thickness(10, 3, 10, 3);
    _btnMesaInteira->Margin = Thickness(0, 0, 14, 0);
    _btnMesaInteira->ToolTip = "A área volta a ser a mesa toda.";
    _btnMesaInteira->Click += [this](RoutedEventArgs &) {
        _objArea = Rect(0, 0, _dblMesaL, _dblMesaA);
        Desenhar();
    };
    objBarra->Children.Add(_btnMesaInteira);
    _txtArea = std::make_shared<TextBlock>();
    _txtArea->Style = Tema::Estilo("Fraco");
    _txtArea->VerticalAlignment = VerticalAlignment::Center;
    objBarra->Children.Add(_txtArea);

    auto objBorda = std::make_shared<Border>();
    objBorda->Background = Tema::Recurso("BarraFerramentas");
    objBorda->BorderBrush = Tema::Recurso("Borda");
    objBorda->BorderThickness = Thickness(0, 0, 0, 1);
    objBorda->Padding = Thickness(14, 8, 14, 8);
    objBorda->SetChild(objBarra);
    DockPanel::SetDock(objBorda, Dock::Top);
    return objBorda;
}

UIElementPtr JanelaScanner::MontarRodape()
{
    auto objGrade = std::make_shared<Grid>();
    objGrade->ColumnDefinitions.push_back(ColumnDefinition());
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));

    auto objEstado = std::make_shared<StackPanel>();
    objEstado->VerticalAlignment = VerticalAlignment::Center;
    _txtEstado = std::make_shared<TextBlock>();
    _txtEstado->Style = Tema::Estilo("Fraco");
    _txtEstado->TextWrapping = TextWrapping::Wrap;
    objEstado->Children.Add(_txtEstado);
    _pbProgresso = std::make_shared<ProgressBar>();
    _pbProgresso->Width = 260;
    _pbProgresso->Height = 5;
    _pbProgresso->HorizontalAlignment = HorizontalAlignment::Left;
    _pbProgresso->Margin = Thickness(0, 5, 0, 0);
    _pbProgresso->Foreground = Tema::Recurso("AcentoVivo");
    _pbProgresso->Background = Tema::Recurso("CabecalhoColuna");
    _pbProgresso->BorderBrush = Tema::Recurso("Borda");
    _pbProgresso->Visibility = Visibility::Collapsed;
    objEstado->Children.Add(_pbProgresso);
    objGrade->Children.Add(objEstado);

    _btnVisualizar = std::make_shared<Button>();
    _btnVisualizar->Content = "Visualizar";
    _btnVisualizar->Padding = Thickness(18, 6, 18, 6);
    _btnVisualizar->Margin = Thickness(12, 0, 0, 0);
    _btnVisualizar->ToolTip = "Lê a mesa inteira a 75 dpi, para ver onde está o que vai ser digitalizado.";
    _btnVisualizar->Click += [this](RoutedEventArgs &) { IniciarPrevia(); };
    auto objCancelar = std::make_shared<Button>();
    objCancelar->Content = "Cancelar";
    objCancelar->IsCancel = true;
    objCancelar->Padding = Thickness(18, 6, 18, 6);
    objCancelar->Margin = Thickness(8, 0, 0, 0);
    objCancelar->Click += [this](RoutedEventArgs &) { Close(); };
    _btnDigitalizar = std::make_shared<Button>();
    _btnDigitalizar->Content = "Digitalizar";
    _btnDigitalizar->IsDefault = true;
    _btnDigitalizar->Padding = Thickness(18, 6, 18, 6);
    _btnDigitalizar->Margin = Thickness(8, 0, 0, 0);
    _btnDigitalizar->Click += [this](RoutedEventArgs &) { Digitalizar(); };
    Grid::SetColumn(_btnVisualizar, 1);
    Grid::SetColumn(objCancelar, 2);
    Grid::SetColumn(_btnDigitalizar, 3);
    objGrade->Children.Add(_btnVisualizar);
    objGrade->Children.Add(objCancelar);
    objGrade->Children.Add(_btnDigitalizar);

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
int JanelaScanner::ResolucaoEscolhida() const
{
    int i = _cmbResolucao->SelectedIndex();
    return i >= 0 && i < 5 ? arrResolucoes[i] : ScannerSane::ResolucaoPadrao;
}

void JanelaScanner::Ocupar(bool blnOcupado)
{
    _btnVisualizar->IsEnabled = !blnOcupado;
    _btnDigitalizar->IsEnabled = !blnOcupado;
    _btnMesaInteira->IsEnabled = !blnOcupado;
    _cmbResolucao->IsEnabled = !blnOcupado;
    _pbProgresso->Visibility = blnOcupado ? Visibility::Visible : Visibility::Collapsed;
    if (!blnOcupado) _objTrabalho = nullptr;
}

void JanelaScanner::Progresso(const std::string &strTexto, double dblFracao)
{
    _txtEstado->Foreground = Brush();
    _txtEstado->Text = strTexto + " " + std::to_string((int) (dblFracao * 100)) + "%";
    _pbProgresso->Value = dblFracao * 100;
}

void JanelaScanner::Ajuda()
{
    _txtEstado->Foreground = Brush();
    _txtEstado->Text = "Arraste sobre a prévia para escolher a área: por dentro move, pelos cantos redimensiona, "
                       "fora desenha uma nova.";
}

void JanelaScanner::IniciarPrevia()
{
    if (_objTrabalho) return;
    auto t = std::make_shared<Trabalho>();
    _objTrabalho = t;
    Ocupar(true);
    Progresso("Visualizando a mesa…", 0);
    if (!_objPrevia) _txtAguarde->Text = "Visualizando a mesa…";
    std::thread([this, t] {
        auto progresso = [this, t, intUltimo = -1](double p) mutable {
            int intPc = (int) (p * 100);
            if (intPc == intUltimo) return;
            intUltimo = intPc;
            Base::Despacha([this, t, p] {
                if (t->blnJanelaViva && !t->blnCancelar) Progresso("Visualizando a mesa…", p);
            });
        };
        auto cancelou = [t] { return t->blnCancelar.load(); };
        try
        {
            double dblL = 0, dblA = 0;
            ScannerSane::Mesa(dblL, dblA);
            Base::Despacha([this, t, dblL, dblA] {
                if (!t->blnJanelaViva) return;
                bool blnEraMesa = _objArea.X == 0 && _objArea.Y == 0 && _objArea.Width == _dblMesaL && _objArea.Height == _dblMesaA;
                _dblMesaL = dblL;
                _dblMesaA = dblA;
                if (blnEraMesa) _objArea = Rect(0, 0, dblL, dblA);
                Diagramar();
            });
            std::string strArquivo = ScannerSane::Previa(progresso, cancelou);
            Base::Despacha([this, t, strArquivo] {
                if (!t->blnJanelaViva) { Apaga(strArquivo); return; }
                AoTerminarPrevia(strArquivo);
            });
        }
        catch (const Base::OperationCanceledException &)
        {
            Base::Despacha([this, t] { if (t->blnJanelaViva) AoCancelado(); });
        }
        catch (const std::exception &ex)
        {
            std::string strErro = ex.what();
            Base::Despacha([this, t, strErro] {
                if (!t->blnJanelaViva) return;
                Ocupar(false);
                if (_blnFecharAoTerminar) { Close(); return; }
                _txtAguarde->Text = _objPrevia ? "" : "Sem prévia.";
                _txtEstado->Foreground = Tema::Recurso("Alarme");
                _txtEstado->Text = strErro;
            });
        }
    }).detach();
}

void JanelaScanner::AoTerminarPrevia(const std::string &strArquivo)
{
    Ocupar(false);
    std::shared_ptr<BitmapSource> objImagem = ImagensDoEncarte::Abrir(strArquivo);
    Apaga(strArquivo);
    if (_blnFecharAoTerminar) { Close(); return; }
    if (!objImagem)
    {
        _txtAguarde->Text = _objPrevia ? "" : "Sem prévia.";
        _txtEstado->Foreground = Tema::Recurso("Alarme");
        _txtEstado->Text = "O scanner entregou uma imagem que não consegui abrir.";
        return;
    }
    _objPrevia = objImagem;
    _objVista->Source = objImagem;
    _txtAguarde->Text = "";
    // a área nasce em volta do que está na mesa, com 10 mm de folga: a JanelaCorteImagem acha a capa
    // contra o fundo da mesa ("a capa num canto, branco em volta"), e sem fundo em volta ela cai no
    // quadrado central. Nada achado: a mesa inteira.
    std::optional<Rect> objAchado = DetectarNaMesa(objImagem);
    if (objAchado)
    {
        const double dblFolga = 10;
        double dblMmPorPx = _dblMesaL / objImagem->PixelWidth, dblMmPorPy = _dblMesaA / objImagem->PixelHeight;
        double x0 = objAchado->X * dblMmPorPx - dblFolga, y0 = objAchado->Y * dblMmPorPy - dblFolga;
        double x1 = (objAchado->X + objAchado->Width) * dblMmPorPx + dblFolga, y1 = (objAchado->Y + objAchado->Height) * dblMmPorPy + dblFolga;
        // o que se põe na mesa vai encostado no canto da marca: perto da borda (10 mm), vai até ela
        if (x0 < 10) x0 = 0;
        if (y0 < 10) y0 = 0;
        if (x1 > _dblMesaL - 10) x1 = _dblMesaL;
        if (y1 > _dblMesaA - 10) y1 = _dblMesaA;
        _objArea = Rect(x0, y0, x1 - x0, y1 - y0).Intersecao(Rect(0, 0, _dblMesaL, _dblMesaA));
    }
    else
        _objArea = Rect(0, 0, _dblMesaL, _dblMesaA);
    Ajuda();
    Diagramar();
}

void JanelaScanner::Digitalizar()
{
    if (_objTrabalho) return;
    int intDpi = ResolucaoEscolhida();
    s_intUltimaResolucao = intDpi;
    auto t = std::make_shared<Trabalho>();
    _objTrabalho = t;
    Ocupar(true);
    std::string strTexto = "Digitalizando a " + std::to_string(intDpi) + " dpi…";
    Progresso(strTexto, 0);
    Rect objArea = _objArea;
    // a mesa inteira não vai como "área": o scanner usa os próprios limites
    bool blnMesa = objArea.X <= 0 && objArea.Y <= 0 && objArea.Width >= _dblMesaL && objArea.Height >= _dblMesaA;
    std::thread([this, t, objArea, blnMesa, intDpi, strTexto] {
        auto progresso = [this, t, strTexto, intUltimo = -1](double p) mutable {
            int intPc = (int) (p * 100);
            if (intPc == intUltimo) return;
            intUltimo = intPc;
            Base::Despacha([this, t, p, strTexto] {
                if (t->blnJanelaViva && !t->blnCancelar) Progresso(strTexto, p);
            });
        };
        auto cancelou = [t] { return t->blnCancelar.load(); };
        try
        {
            std::string strArquivo = blnMesa ? ScannerSane::DigitalizarArea(0, 0, 0, 0, intDpi, progresso, cancelou)
                                             : ScannerSane::DigitalizarArea(objArea.X, objArea.Y, objArea.Width,
                                                                            objArea.Height, intDpi, progresso, cancelou);
            Base::Despacha([this, t, strArquivo, intDpi] {
                if (!t->blnJanelaViva) { Apaga(strArquivo); return; }
                Ocupar(false);
                if (_blnFecharAoTerminar) { Apaga(strArquivo); Close(); return; }
                Arquivo = strArquivo;
                Dpi = intDpi;
                SetDialogResult(true);
            });
        }
        catch (const Base::OperationCanceledException &)
        {
            Base::Despacha([this, t] { if (t->blnJanelaViva) AoCancelado(); });
        }
        catch (const std::exception &ex)
        {
            // como o ScannerWia: a falha sai do Digitalizar como InvalidOperationException
            std::string strErro = ex.what();
            Base::Despacha([this, t, strErro] {
                if (!t->blnJanelaViva) return;
                Ocupar(false);
                Erro = strErro;
                _blnFecharAoTerminar = true;
                Close();
            });
        }
    }).detach();
}

void JanelaScanner::AoCancelado()
{
    Ocupar(false);
    if (_blnFecharAoTerminar) { Close(); return; }
    _txtAguarde->Text = _objPrevia ? "" : "Sem prévia.";
    Ajuda();
}

/// Fechar com o scanner lendo: pede para parar e fecha quando ele parar (o fio não pode ficar
/// solto com a janela já destruída, nem o scanner preso no meio de uma leitura).
void JanelaScanner::AoFechar(CancelEventArgs &e)
{
    if (!_objTrabalho) return;
    _objTrabalho->blnCancelar = true;
    _blnFecharAoTerminar = true;
    _txtEstado->Foreground = Brush();
    _txtEstado->Text = "Cancelando…";
    e.Cancel = true;
}

// ===============================================================
void JanelaScanner::Diagramar()
{
    double dblLargura = _objPalco->ActualWidth, dblAltura = _objPalco->ActualHeight;
    if (dblLargura <= 0 || dblAltura <= 0) return;
    _dblEscala = std::min(dblLargura / _dblMesaL, dblAltura / _dblMesaA);
    double dblL = _dblMesaL * _dblEscala, dblA = _dblMesaA * _dblEscala;
    _dblX0 = (dblLargura - dblL) / 2;
    _dblY0 = (dblAltura - dblA) / 2;
    Canvas::SetLeft(_objMesa, _dblX0);
    Canvas::SetTop(_objMesa, _dblY0);
    _objMesa->Width = dblL;
    _objMesa->Height = dblA;
    Canvas::SetLeft(_objVista, _dblX0);
    Canvas::SetTop(_objVista, _dblY0);
    _objVista->Width = dblL;
    _objVista->Height = dblA;
    Desenhar();
}

Point JanelaScanner::ParaTela(Point objMm) const
{
    return Point(_dblX0 + objMm.X * _dblEscala, _dblY0 + objMm.Y * _dblEscala);
}

Point JanelaScanner::ParaMesa(Point objTela) const
{
    double x = (objTela.X - _dblX0) / _dblEscala, y = (objTela.Y - _dblY0) / _dblEscala;
    return Point(std::max(0.0, std::min(_dblMesaL, x)), std::max(0.0, std::min(_dblMesaA, y)));
}

Rect JanelaScanner::Limitado(Rect objR) const
{
    double x = std::max(0.0, std::min(_dblMesaL - objR.Width, objR.X));
    double y = std::max(0.0, std::min(_dblMesaA - objR.Height, objR.Y));
    return Rect(x, y, objR.Width, objR.Height);
}

void JanelaScanner::Desenhar()
{
    if (_objPalco->ActualWidth <= 0) return;
    Rect objNaTela = Entre(ParaTela(TopLeft(_objArea)), ParaTela(BottomRight(_objArea)));
    _objVeu->Fora = Rect(_dblX0, _dblY0, _dblMesaL * _dblEscala, _dblMesaA * _dblEscala);
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
    int intDpi = ResolucaoEscolhida();
    int intPxL = (int) std::lround(_objArea.Width / 25.4 * intDpi), intPxA = (int) std::lround(_objArea.Height / 25.4 * intDpi);
    double dblMb = (double) intPxL * intPxA * 3 / (1024.0 * 1024.0);
    _txtArea->Text = "Área: " + Modelo::Formato::Num(_objArea.Width / 10, 1) + " x " + Modelo::Formato::Num(_objArea.Height / 10, 1) +
                     " cm   ·   " + std::to_string(intPxL) + " x " + std::to_string(intPxA) + " px a " + std::to_string(intDpi) +
                     " dpi (" + Modelo::Formato::Inteiro(dblMb) + " MB sem compressão)";
    InvalidateVisual();
}

// ===============================================================
void JanelaScanner::AoApertar(MouseButtonEventArgs &e)
{
    if (_objTrabalho) return;   // com o scanner lendo, a área não muda
    Point objTela = e.GetPosition(_objPalco.get());
    Point objMm = ParaMesa(objTela);
    _objAreaNoInicio = _objArea;
    Point arrCantos[4] = {TopLeft(_objArea), TopRight(_objArea), BottomRight(_objArea), BottomLeft(_objArea)};
    for (int i = 0; i < 4; i++)
    {
        Point objC = ParaTela(arrCantos[i]);
        if (std::fabs(objC.X - objTela.X) <= dblRaioAlca && std::fabs(objC.Y - objTela.Y) <= dblRaioAlca)
        {
            _objModo = Modo::Redimensionar;
            _objAncora = arrCantos[(i + 2) % 4];
            _objPalco->CaptureMouse();
            return;
        }
    }
    _objModo = Contem(_objArea, objMm) ? Modo::Mover : Modo::Desenhar;
    _objAncora = objMm;
    _objPalco->CaptureMouse();
}

void JanelaScanner::AoMover(MouseEventArgs &e)
{
    Point objTela = e.GetPosition(_objPalco.get());
    if (_objModo == Modo::Nenhum)
    {
        _objPalco->Cursor = CursorPara(objTela);
        return;
    }
    if (e.LeftButton != MouseButtonState::Pressed) { Soltar(); return; }
    Point objMm = ParaMesa(objTela);
    if (_objModo == Modo::Mover)
        _objArea = Limitado(Rect(_objAreaNoInicio.X + objMm.X - _objAncora.X, _objAreaNoInicio.Y + objMm.Y - _objAncora.Y,
                                 _objAreaNoInicio.Width, _objAreaNoInicio.Height));
    else
    {
        Rect objNova = Entre(_objAncora, objMm);
        if (objNova.Width >= dblLadoMinimoMm && objNova.Height >= dblLadoMinimoMm) _objArea = objNova;
    }
    Desenhar();
}

void JanelaScanner::Soltar()
{
    _objModo = Modo::Nenhum;
    if (_objPalco->IsMouseCaptured()) _objPalco->ReleaseMouseCapture();
}

Vistas::Cursor JanelaScanner::CursorPara(Point objTela) const
{
    if (_objTrabalho) return Cursors::Arrow;
    Point arrCantos[4] = {TopLeft(_objArea), TopRight(_objArea), BottomRight(_objArea), BottomLeft(_objArea)};
    for (int i = 0; i < 4; i++)
    {
        Point objC = ParaTela(arrCantos[i]);
        if (std::fabs(objC.X - objTela.X) <= dblRaioAlca && std::fabs(objC.Y - objTela.Y) <= dblRaioAlca)
            return i % 2 == 0 ? Cursors::SizeNWSE : Cursors::SizeNESW;
    }
    return Contem(_objArea, ParaMesa(objTela)) ? Cursors::SizeAll : Cursors::Cross;
}

// ===============================================================
void RegistrarJanelaScanner()
{
    ScannerSane::DialogoComum = [](double &dblDpi) -> std::string {
        dblDpi = 0;
        // o WIA dizia "nenhum dispositivo" antes de abrir o dialogo (WIA_S_NO_DEVICE_AVAILABLE)
        std::string strNome;
        if (ScannerSane::Dispositivo(&strNome).empty())
            throw Base::InvalidOperationException("Nenhum scanner encontrado. Confira se está ligado e conectado, e tente de novo.");
        auto objJanela = std::make_shared<JanelaScanner>(strNome);
        // o dialogo do WIA ficava sobre a janela ativa
        Window *objDono = Application::JanelaModalAtual();
        objJanela->Owner = objDono ? objDono : Application::MainWindow();
        objJanela->ShowDialog();
        if (!objJanela->Erro.empty()) throw Base::InvalidOperationException(objJanela->Erro);
        dblDpi = objJanela->Dpi;
        return objJanela->Arquivo;
    };
}

} // namespace ARCA::Vistas

// ARCA no xOS - Vistas/EspectrogramaArquivo (de Vistas\EspectrogramaArquivo.cs).
#include "EspectrogramaArquivo.h"

#include "Instrumentos.h"
#include "Janela.h"

#include <algorithm>
#include <cmath>

namespace ARCA::Vistas {

using Modelo::EspectrogramaCache;

namespace {

Brush Pincel(uint8_t r, uint8_t g, uint8_t b) { return SolidColorBrush(Color::FromRgb(r, g, b)); }

const Brush Ocre = Pincel(0xC9, 0xA8, 0x6A);
const Brush OcreFraco = Pincel(0x8A, 0x76, 0x54);

const char *const NotasPt[] = {"Dó", "Dó#", "Ré", "Ré#", "Mi", "Fá", "Fá#", "Sol", "Sol#", "Lá", "Lá#", "Si"};

std::vector<int> Gradiente(const std::vector<int> &paradas)
{
    std::vector<int> m(256);
    int seg = (int) paradas.size() - 1;
    for (int i = 0; i < 256; i++)
    {
        double t = i / 255.0 * seg;
        int k = (int) t;
        if (k >= seg) k = seg - 1;
        double f = t - k;
        int a = paradas[k], b = paradas[k + 1];
        int r = (int) (((a >> 16) & 0xFF) + (((b >> 16) & 0xFF) - ((a >> 16) & 0xFF)) * f);
        int g = (int) (((a >> 8) & 0xFF) + (((b >> 8) & 0xFF) - ((a >> 8) & 0xFF)) * f);
        int c = (int) ((a & 0xFF) + ((b & 0xFF) - (a & 0xFF)) * f);
        m[i] = Tela::Cor((uint8_t) r, (uint8_t) g, (uint8_t) c);
    }
    return m;
}

/// <summary>Três mapas de cor de 256 níveis, do silêncio ao pico.</summary>
std::vector<std::vector<int>> MontarPaletas()
{
    std::vector<std::vector<int>> p(3);
    p[0] = Gradiente({0x0A0805, 0x3A2410, 0xC9902F, 0xFF9E2C, 0xFFF0CC});
    p[1] = Gradiente({0x000000, 0x404040, 0x909090, 0xE0E0E0, 0xFFFFFF});
    p[2] = Gradiente({0x000000, 0x40060A, 0xB01A20, 0xF06010, 0xFFF060});
    return p;
}

const std::vector<std::vector<int>> Paletas = MontarPaletas();

} // namespace

// ===============================================================
std::string TempoSegundos(double s)
{
    if (s < 0) s = 0;
    int m = (int) (s / 60);
    double seg = s - m * 60;
    return std::to_string(m) + ":" + Fixo(seg, 1, 4);
}

std::string TextoFreq(double f)
{
    if (f >= 1000) return Fixo(f / 1000.0, 2) + " kHz";
    return Fixo(f, 0) + " Hz";
}

std::string Nota(double f)
{
    if (f <= 0) return "";
    double midi = 69 + 12.0 * (std::log(f / 440.0) / std::log(2.0));
    // Math.Round do .NET arredonda o meio para o par (banker's rounding): std::nearbyint também
    int m = (int) std::nearbyint(midi);
    int pc = ((m % 12) + 12) % 12;
    int oit = m / 12 - 1;
    return std::string(NotasPt[pc]) + std::to_string(oit);
}

// ===============================================================
EspectrogramaArquivo::EspectrogramaArquivo()
{
    Background = Pincel(0x0A, 0x08, 0x05);
    RowDefinitions = {RowDefinition(GridLength::Auto()), RowDefinition()};

    auto barra = std::make_shared<WrapPanel>();
    barra->Margin = Thickness(4, 4, 4, 2);
    barra->Children.Add(Grupo("Frequência", {"Log", "Linear"}, 0, "esc", [this](RadioButton &r) { AoEscala(r); }));
    barra->Children.Add(Grupo("Cores", {"Quente", "Cinza", "Fogo"}, 0, "cor", [this](RadioButton &r) { AoCor(r); }));
    SetRow(barra, 0);
    Children.Add(barra);

    // RenderOptions.SetBitmapScalingMode(_img, LowQuality): a textura do NanoVG é bilinear
    _img->Stretch = Vistas::Stretch::Fill;
    _sobre->IsHitTestVisible = false;

    _playhead->Stroke = SolidColorBrush(Color::FromRgb(0xF0, 0x2A, 0x1E));
    _playhead->StrokeThickness = 2;
    _playhead->Visibility = Vistas::Visibility::Collapsed;

    _leitura->Foreground = Ocre;
    _leitura->FontSize = 12;
    _leitura->FontFamily = "Consolas";
    _leitura->Visibility = Vistas::Visibility::Collapsed;
    Canvas::SetTop(_leitura, 6);

    _aviso->Foreground = OcreFraco;
    _aviso->FontSize = 12;
    _aviso->Text = "analisando o espectrograma…";
    _aviso->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    _aviso->VerticalAlignment = Vistas::VerticalAlignment::Center;

    _sobre->Children.Add(_playhead);
    _sobre->Children.Add(_leitura);

    // LINUX: no WPF o Grid sem Background não recebe clique; o _area precisa receber (roda, arrasto)
    _area->Background = Brushes::Transparent;
    _area->ClipToBounds = true;
    _area->Children.Add(_img);
    _area->Children.Add(_sobre);
    _area->Children.Add(_aviso);
    SetRow(_area, 1);
    Children.Add(_area);

    _area->SizeChanged += [this](SizeChangedEventArgs &) { RecriarTela(); Redesenhar(); PosicionarPlayhead(); };
    Loaded += [this](RoutedEventArgs &) { RecriarTela(); Redesenhar(); PosicionarPlayhead(); };
    _area->LostMouseCapture += [this](RoutedEventArgs &) { _arrastando = false; };
    _area->MouseWheel += [this](MouseWheelEventArgs &e) { AoRoda(e); };
    _area->MouseLeftButtonDown += [this](MouseButtonEventArgs &e) { AoApertar(e); };
    _area->MouseMove += [this](MouseEventArgs &e) { AoMover(e); };
    _area->MouseLeftButtonUp += [this](MouseButtonEventArgs &e) { AoSoltar(e); };
    _area->MouseLeave += [this](MouseEventArgs &) { _leitura->Visibility = Vistas::Visibility::Collapsed; };
}

void EspectrogramaArquivo::Definir(std::shared_ptr<EspectrogramaCache> eg)
{
    _eg = eg;
    if (eg && eg->Colunas > 0)
    {
        _ci = 0; _cf = eg->Colunas;
        _aviso->Visibility = Vistas::Visibility::Collapsed;
    }
    else
    {
        _aviso->Text = "analisando o espectrograma…";
        _aviso->Visibility = Vistas::Visibility::Visible;
    }
    RecriarTela();
    // O mapa de linhas depende do espectrograma (taxa de amostragem).
    // RecriarTela só o refaz quando o tamanho muda — na troca de faixa
    // o tamanho é o mesmo, e o mapa vazio deixava a tela preta.
    if (_tela) MontarMapaLinhas(_tela->Altura);
    Redesenhar();
}

void EspectrogramaArquivo::MostrarFalha(const std::string &strMotivo)
{
    _aviso->Text = "Não consegui analisar o espectrograma: " + strMotivo + ".\nNada foi guardado em cache; a próxima abertura tenta de novo.";
    _aviso->Visibility = Vistas::Visibility::Visible;
}

void EspectrogramaArquivo::PosicaoSegundos(double valor)
{
    _posSeg = valor;
    PosicionarPlayhead();
}

// ===============================================================
void EspectrogramaArquivo::RecriarTela()
{
    double ex = 1, ey = 1;
    // Fora da tela, a escala é desconhecida: espera o Loaded (ver OndaArquivo).
    if (!Janela()) return;
    int w = (int) std::lround(_area->ActualWidth * ex);
    int h = (int) std::lround(_area->ActualHeight * ey);
    if (w < 1 || h < 1) return;
    if (_tela && _tela->Largura == w && _tela->Altura == h) return;

    _tela = std::make_unique<Tela>(w, h);
    _img->Source = _tela->Bitmap;
    MontarMapaLinhas(h);
    if (_colRed.empty() || _colRedLarg != w)
    {
        _colRed.assign((size_t) w * EspectrogramaCache::Bins, 0);
        _colRedLarg = w;
    }
}

/// <summary>Por linha de tela, a faixa de raias que ela cobre (log ou linear).</summary>
void EspectrogramaArquivo::MontarMapaLinhas(int h)
{
    _rb0.assign(h, 0); _rb1.assign(h, 0);
    if (!_eg) return;

    double fMax = _eg->Taxa / 2.0;
    double fMin = 20.0;
    double logMin = std::log10(fMin), logMax = std::log10(fMax);

    for (int y = 0; y < h; y++)
    {
        double f0 = FreqDeFrac((double) y / h, fMin, fMax, logMin, logMax);
        double f1 = FreqDeFrac((double) (y + 1) / h, fMin, fMax, logMin, logMax);
        // y cresce para baixo = frequência menor; ordena.
        int b0 = (int) (std::min(f0, f1) * EspectrogramaCache::FftTam / _eg->Taxa);
        int b1 = (int) (std::max(f0, f1) * EspectrogramaCache::FftTam / _eg->Taxa) + 1;
        if (b0 < 0) b0 = 0;
        if (b1 > EspectrogramaCache::Bins) b1 = EspectrogramaCache::Bins;
        if (b1 <= b0) b1 = b0 + 1;
        _rb0[y] = b0; _rb1[y] = b1;
    }
}

double EspectrogramaArquivo::FreqDeFrac(double frac, double fMin, double fMax, double logMin, double logMax) const
{
    // frac 0 = topo (aguda), 1 = base (grave).
    if (_log) return std::pow(10, logMax - frac * (logMax - logMin));
    return fMax - frac * (fMax - fMin);
}

void EspectrogramaArquivo::Redesenhar()
{
    if (!_tela) RecriarTela();
    if (!_tela) return;

    _tela->Limpar(Paletas[_mapa][0]);
    if (!_eg || _eg->Colunas <= 0) { _tela->Aplicar(); return; }

    int w = _tela->Largura, h = _tela->Altura;
    const std::vector<int> &pal = Paletas[_mapa];
    const int Bins = EspectrogramaCache::Bins;

    // 1) Reduz no tempo: para cada coluna de tela, o máximo das colunas
    //    do cache que nela caem — nenhum evento breve some ao afastar.
    double colsPorX = (_cf - _ci) / w;
    for (int x = 0; x < w; x++)
    {
        int c0 = (int) (_ci + x * colsPorX);
        int c1 = (int) (_ci + (x + 1) * colsPorX);
        if (c1 <= c0) c1 = c0 + 1;
        if (c0 < 0) c0 = 0;
        if (c1 > _eg->Colunas) c1 = _eg->Colunas;
        size_t baseX = (size_t) x * Bins;
        if (c0 >= c1) { for (int b = 0; b < Bins; b++) _colRed[baseX + b] = 0; continue; }
        for (int b = 0; b < Bins; b++)
        {
            uint8_t m = 0;
            for (int c = c0; c < c1; c++) { uint8_t v = _eg->Valor(c, b); if (v > m) m = v; }
            _colRed[baseX + b] = m;
        }
    }

    // 2) Reduz na frequência conforme o mapa de linhas e colore.
    for (int y = 0; y < h; y++)
    {
        int b0 = _rb0[y], b1 = _rb1[y];
        for (int x = 0; x < w; x++)
        {
            size_t baseX = (size_t) x * Bins;
            uint8_t m = 0;
            for (int b = b0; b < b1; b++) { uint8_t v = _colRed[baseX + b]; if (v > m) m = v; }
            _tela->Ponto(x, y, pal[m]);
        }
    }

    _tela->Aplicar();
}

// ===============================================================
void EspectrogramaArquivo::PosicionarPlayhead()
{
    if (!_eg || _cf <= _ci || _area->ActualWidth < 1)
    {
        _playhead->Visibility = Vistas::Visibility::Collapsed;
        return;
    }
    double col = _eg->ColunaDeSegundos(_posSeg);
    if (col < _ci || col > _cf) { _playhead->Visibility = Vistas::Visibility::Collapsed; return; }
    double x = (col - _ci) / (_cf - _ci) * _area->ActualWidth;
    _playhead->X1 = x; _playhead->X2 = x;
    _playhead->Y1 = 0; _playhead->Y2 = _area->ActualHeight;
    _playhead->Visibility = Vistas::Visibility::Visible;
}

// ===============================================================
void EspectrogramaArquivo::AoRoda(MouseWheelEventArgs &e)
{
    if (!_eg || _cf <= _ci) return;
    double fx = _area->ActualWidth > 0 ? e.GetPosition(_area.get()).X / _area->ActualWidth : 0.5;
    if (fx < 0) fx = 0;
    if (fx > 1) fx = 1;
    double span = _cf - _ci;
    double foco = _ci + fx * span;

    if (Tem(Keyboard::Modifiers(), ModifierKeys::Shift))
    {
        Deslocar((e.Delta > 0 ? -1 : 1) * span * 0.15);
    }
    else
    {
        double fator = e.Delta > 0 ? 1.0 / 1.3 : 1.3;
        double novo = span * fator;
        if (novo < 8) novo = 8;
        if (novo > _eg->Colunas) novo = _eg->Colunas;
        _ci = foco - fx * novo; _cf = _ci + novo;
        Ajustar(); Redesenhar(); PosicionarPlayhead();
    }
    e.Handled = true;
}

void EspectrogramaArquivo::Deslocar(double cols)
{
    double span = _cf - _ci;
    _ci += cols; _cf = _ci + span;
    Ajustar(); Redesenhar(); PosicionarPlayhead();
}

void EspectrogramaArquivo::Ajustar()
{
    double span = _cf - _ci;
    if (span > _eg->Colunas) { _ci = 0; _cf = _eg->Colunas; return; }
    if (_ci < 0) { _ci = 0; _cf = span; }
    if (_cf > _eg->Colunas) { _cf = _eg->Colunas; _ci = _cf - span; }
    if (_ci < 0) _ci = 0;
}

void EspectrogramaArquivo::AoApertar(MouseButtonEventArgs &e)
{
    _arrastando = true; _moveu = false;
    _mouseIni = e.GetPosition(_area.get());
    _mouseUltimoX = _mouseIni.X;
    _area->CaptureMouse();
}

void EspectrogramaArquivo::AoMover(MouseEventArgs &e)
{
    Point p = e.GetPosition(_area.get());
    if (_arrastando && _eg)
    {
        if (_moveu || std::fabs(p.X - _mouseIni.X) >= 4 || std::fabs(p.Y - _mouseIni.Y) >= 4)
        {
            _moveu = true;
            double dxPx = p.X - _mouseUltimoX;
            _mouseUltimoX = p.X;
            if (_area->ActualWidth > 0)
                Deslocar(-(dxPx * (_cf - _ci) / _area->ActualWidth));
        }
    }
    AtualizarLeitura(p);
}

void EspectrogramaArquivo::AtualizarLeitura(Point p)
{
    double w = _area->ActualWidth, h = _area->ActualHeight;
    if (!_eg || w < 2 || h < 2) return;

    double fx = p.X / w;
    if (fx < 0) fx = 0;
    if (fx > 1) fx = 1;
    double col = _ci + fx * (_cf - _ci);
    double seg = _eg->SegundosDeColuna(col);

    double fMax = _eg->Taxa / 2.0, fMin = 20.0;
    double freq = FreqDeFrac(p.Y / h, fMin, fMax, std::log10(fMin), std::log10(fMax));

    int c = (int) col, b = (int) (freq * EspectrogramaCache::FftTam / _eg->Taxa);
    double db = EspectrogramaCache::DbDeByte(_eg->Valor(c, b));

    _leitura->Text = TempoSegundos(seg) + "  " + TextoFreq(freq) + "  " + Nota(freq) + "  " + Fixo(db, 0) + " dB";
    _leitura->Visibility = Vistas::Visibility::Visible;
    _leitura->Measure(Size(Infinito, Infinito));
    Canvas::SetLeft(_leitura, std::max(4.0, w - _leitura->DesiredSize.Width - 8));
}

void EspectrogramaArquivo::AoSoltar(MouseButtonEventArgs &e)
{
    if (_arrastando && !_moveu && _eg && _area->ActualWidth > 0)
    {
        double fx = e.GetPosition(_area.get()).X / _area->ActualWidth;
        if (fx < 0) fx = 0;
        if (fx > 1) fx = 1;
        double seg = _eg->SegundosDeColuna(_ci + fx * (_cf - _ci));
        Buscou(seg);
    }
    _arrastando = false;
    if (_area->IsMouseCaptured()) _area->ReleaseMouseCapture();
}

// ===============================================================
void EspectrogramaArquivo::AoEscala(RadioButton &r)
{
    if (!r.IsChecked()) return;
    _log = std::any_cast<int>(r.Tag) == 0;
    if (_tela) MontarMapaLinhas(_tela->Altura);
    Redesenhar();
}

void EspectrogramaArquivo::AoCor(RadioButton &r)
{
    if (!r.IsChecked()) return;
    _mapa = std::any_cast<int>(r.Tag);
    Redesenhar();
}

UIElementPtr EspectrogramaArquivo::Grupo(const std::string &rotulo, const std::vector<std::string> &opcoes, int marcado,
                                         const std::string &grupo, std::function<void(RadioButton &)> ao)
{
    auto p = std::make_shared<StackPanel>();
    p->Orientation = Vistas::Orientation::Horizontal;
    p->VerticalAlignment = Vistas::VerticalAlignment::Center;
    p->Margin = Thickness(0, 0, 14, 0);
    auto t = std::make_shared<TextBlock>(rotulo + ":");
    t->Foreground = OcreFraco; t->FontSize = 11;
    t->VerticalAlignment = Vistas::VerticalAlignment::Center; t->Margin = Thickness(0, 0, 6, 0);
    p->Children.Add(t);
    for (size_t i = 0; i < opcoes.size(); i++)
    {
        auto r = std::make_shared<RadioButton>();
        r->Content = opcoes[i]; r->GroupName = grupo; r->Tag = (int) i;
        r->Foreground = Ocre; r->FontSize = 11;
        r->VerticalAlignment = Vistas::VerticalAlignment::Center;
        r->Margin = Thickness(0, 0, 8, 0); r->Cursor = Cursors::Hand;
        // no inicializador do C#, o IsChecked vem antes do "+= ao": a marca inicial não dispara
        r->IsChecked = ((int) i == marcado);
        RadioButton *rp = r.get();
        r->Checked += [ao, rp](RoutedEventArgs &) { ao(*rp); };
        p->Children.Add(r);
    }
    return p;
}

} // namespace ARCA::Vistas

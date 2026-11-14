// ARCA no xOS - Vistas/EspectroAnalise (de Vistas\EspectroAnalise.cs).
#include "EspectroAnalise.h"

#include "EspectrogramaArquivo.h"     // TextoFreq, Nota
#include "Janela.h"

#include <algorithm>
#include <cmath>

namespace ARCA::Vistas {

using Modelo::AnalisadorAudio;
using Modelo::Fft;

namespace {

Brush Pincel(uint8_t a, uint8_t r, uint8_t g, uint8_t b) { return SolidColorBrush(Color::FromArgb(a, r, g, b)); }

const Brush CorHarm = Pincel(0x90, 0xC9, 0x90, 0x2F);
const Brush CorHarm1 = Pincel(0xFF, 0xFF, 0xF0, 0xCC);

// --- cores (chapa sépia) ---
const int Fundo = Tela::Cor(0x17, 0x12, 0x0B);
const int CorA = Tela::Cor(0xFF, 0x9E, 0x2C);   // esq / mid / mono
const int CorB = Tela::Cor(0xE8, 0xC0, 0x78);   // dir / side
const int CorAreaA = Tela::Cor(0x7A, 0x50, 0x18);
const int CorAreaB = Tela::Cor(0x5E, 0x4A, 0x24);
const int CorPico = Tela::Cor(0xFF, 0xF0, 0xCC);

const Brush Ocre = SolidColorBrush(Color::FromRgb(0xC9, 0xA8, 0x6A));
const Brush OcreFraco = SolidColorBrush(Color::FromRgb(0x8A, 0x76, 0x54));

const double RefFreq[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};

std::shared_ptr<Line> LinhaGrade(double x1, double y1, double x2, double y2)
{
    auto l = std::make_shared<Line>();
    l->X1 = x1; l->Y1 = y1; l->X2 = x2; l->Y2 = y2;
    l->Stroke = SolidColorBrush(Color::FromArgb(0x50, 0x3E, 0x31, 0x20));
    l->StrokeThickness = 1;
    return l;
}

std::shared_ptr<TextBlock> Rotulo(const std::string &texto, const Brush &cor)
{
    auto t = std::make_shared<TextBlock>(texto);
    t->Foreground = cor;
    t->FontSize = 10;
    return t;
}

} // namespace

EspectroAnalise::EspectroAnalise()
{
    Background = SolidColorBrush(Color::FromRgb(0x17, 0x12, 0x0B));

    RowDefinitions = {RowDefinition(GridLength::Auto()), RowDefinition()};

    // --- barra de controles: tudo à vista, nada escondido atrás de um
    //     botão que cicla. Cada parâmetro é um grupo de opções visíveis.
    auto barra = std::make_shared<WrapPanel>();
    barra->Margin = Thickness(4, 4, 4, 2);

    barra->Children.Add(Grupo("Canais", {"L/R", "Mono", "Mid/Side"}, _modoCanal, "canal", [this](RadioButton &r) { AoCanal(r); }));
    barra->Children.Add(Grupo("Faixa", {"60 dB", "90 dB", "120 dB"}, 1, "faixa", [this](RadioButton &r) { AoFaixa(r); }));
    barra->Children.Add(Grupo("Traço", {"Linha", "Área", "Barras"}, _traco, "traco", [this](RadioButton &r) { AoTraco(r); }));

    auto congelar = std::make_shared<CheckBox>();
    congelar->Content = "Congelar";
    congelar->Foreground = Ocre;
    congelar->FontSize = 11;
    congelar->VerticalAlignment = Vistas::VerticalAlignment::Center;
    congelar->Margin = Thickness(6, 0, 0, 0);
    congelar->Cursor = Cursors::Hand;
    CheckBox *cp = congelar.get();
    congelar->Checked += [this, cp](RoutedEventArgs &) { _congelado = cp->IsChecked(); };     // AoCongelar
    congelar->Unchecked += [this, cp](RoutedEventArgs &) { _congelado = cp->IsChecked(); };
    barra->Children.Add(congelar);

    auto harm = std::make_shared<CheckBox>();
    harm->Content = "Harmônicos";
    harm->Foreground = Ocre;
    harm->FontSize = 11;
    harm->VerticalAlignment = Vistas::VerticalAlignment::Center;
    harm->Margin = Thickness(12, 0, 0, 0);
    harm->Cursor = Cursors::Hand;
    harm->ToolTip = "Detecta a nota fundamental e marca a série harmônica "
                    "(f0, 2·f0, 3·f0…) sobre o espectro.";
    CheckBox *hp = harm.get();
    harm->Checked += [this, hp](RoutedEventArgs &) { AoHarmonicos(*hp); };
    harm->Unchecked += [this, hp](RoutedEventArgs &) { AoHarmonicos(*hp); };
    barra->Children.Add(harm);

    SetRow(barra, 0);
    Children.Add(barra);

    // --- área do espectro: imagem + grade + sobreposição ---
    // RenderOptions.SetBitmapScalingMode(_img, LowQuality): a textura do NanoVG é bilinear
    _img->Stretch = Vistas::Stretch::Fill;
    _grade->IsHitTestVisible = false;
    _sobre->IsHitTestVisible = false;
    _harm->IsHitTestVisible = false;

    _cursor->Stroke = SolidColorBrush(Color::FromRgb(0xF0, 0x2A, 0x1E));
    _cursor->StrokeThickness = 1;
    _cursor->Visibility = Vistas::Visibility::Collapsed;

    _leitura->Foreground = Ocre;
    _leitura->FontSize = 12;
    _leitura->FontFamily = "Consolas";
    _leitura->Visibility = Vistas::Visibility::Collapsed;
    Canvas::SetTop(_leitura, 6);

    _sobre->Children.Add(_cursor);
    _sobre->Children.Add(_leitura);

    ConstruirHarm();

    // LINUX: no WPF o Grid sem Background não recebe o MouseMove; aqui o _area precisa receber
    _area->Background = Brushes::Transparent;
    _area->ClipToBounds = true;
    _area->Children.Add(_img);
    _area->Children.Add(_grade);
    _area->Children.Add(_harm);
    _area->Children.Add(_sobre);
    SetRow(_area, 1);
    Children.Add(_area);

    _area->MouseMove += [this](MouseEventArgs &e) { AoMover(e); };
    _area->MouseLeave += [this](MouseEventArgs &) { AoSair(); };
    _area->SizeChanged += [this](SizeChangedEventArgs &) { ConstruirGrade(); PosicionarLeitura(); };
}

/// <summary>Pool de marcadores de harmônicos, reaproveitados a cada
/// quadro para não gerar lixo enquanto a nota muda.</summary>
void EspectroAnalise::ConstruirHarm()
{
    _hLin.resize(MaxHarm);
    _hRot.resize(MaxHarm);
    for (int i = 0; i < MaxHarm; i++)
    {
        auto l = std::make_shared<Line>();
        l->StrokeThickness = 1;
        l->Visibility = Vistas::Visibility::Collapsed;
        l->Stroke = (i == 0) ? CorHarm1 : CorHarm;
        _hLin[i] = l;
        _harm->Children.Add(l);

        auto t = std::make_shared<TextBlock>();
        t->Foreground = (i == 0) ? Ocre : OcreFraco;
        t->FontSize = 9;
        t->Visibility = Vistas::Visibility::Collapsed;
        _hRot[i] = t;
        _harm->Children.Add(t);
    }
    _f0Texto = std::make_shared<TextBlock>();
    _f0Texto->Foreground = Ocre;
    _f0Texto->FontSize = 12;
    _f0Texto->FontFamily = "Consolas";
    _f0Texto->Visibility = Vistas::Visibility::Collapsed;
    Canvas::SetLeft(_f0Texto, 6);
    Canvas::SetTop(_f0Texto, 6);
    _harm->Children.Add(_f0Texto);
}

// ==================================================================
//  ENTRADA POR QUADRO
// ==================================================================
void EspectroAnalise::Atualizar(AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir)
{
    if (_congelado) return;
    if (a.Taxa() <= 0) return;

    if (a.Taxa() != _taxa) { _taxa = a.Taxa(); MontarMapa(); ConstruirGrade(); }
    Garantir();

    // Só as FFTs que o modo atual pede — no máximo duas por quadro.
    if (_modoCanal == 0)          // L / R
    {
        Fft::Espectro(esq, _raiaA);
        Fft::Espectro(dir, _raiaB);
    }
    else if (_modoCanal == 1)     // Mono
    {
        for (size_t i = 0; i < _mono.size(); i++) _mono[i] = (esq[i] + dir[i]) * 0.5f;
        Fft::Espectro(_mono, _raiaA);
    }
    else                          // Mid / Side
    {
        for (size_t i = 0; i < _mono.size(); i++)
        {
            _mono[i] = (esq[i] + dir[i]) * 0.5f;
            _side[i] = (esq[i] - dir[i]) * 0.5f;
        }
        Fft::Espectro(_mono, _raiaA);
        Fft::Espectro(_side, _raiaB);
    }

    Reduzir(_raiaA, _colA);
    if (_modoCanal != 1) Reduzir(_raiaB, _colB);

    Desenhar();

    if (_harmonicos) AtualizarHarmonicos();
}

void EspectroAnalise::AoHarmonicos(CheckBox &c)
{
    _harmonicos = c.IsChecked();
    if (!_harmonicos) EsconderHarm();
}

// ==================================================================
//  HARMÔNICOS: fundamental por Harmonic Product Spectrum
// ==================================================================
/// <summary>
/// O maior pico nem sempre é a fundamental — pode ser um harmônico. O
/// HPS multiplica o espectro por cópias comprimidas de si (÷2, ÷3, ÷4):
/// só na fundamental os harmônicos coincidem e o produto dispara. É o
/// método clássico de detecção de altura, e é barato.
/// </summary>
double EspectroAnalise::DetectarF0()
{
    int half = Fft::Tamanho / 2;
    if (_mag.empty()) _mag.assign(half, 0.0);
    for (int i = 0; i < half; i++)
        _mag[i] = std::pow(10.0, _raiaA[i] / 20.0);   // dB -> linear

    int iMin = std::max(2, (int) (40.0 * Fft::Tamanho / _taxa));
    int iMax = std::min((half - 1) / 4, (int) (1000.0 * Fft::Tamanho / _taxa));
    if (iMax <= iMin) return 0;

    int melhor = 0; double top = 0;
    for (int i = iMin; i <= iMax; i++)
    {
        double p = _mag[i] * _mag[2 * i] * _mag[3 * i] * _mag[4 * i];
        if (p > top) { top = p; melhor = i; }
    }
    if (melhor == 0) return 0;

    // Sinal fraco demais: sem altura confiável.
    if (_raiaA[melhor] < -70.0) return 0;

    // Interpolação parabólica no log do produto, para altura sub-raia.
    double di = 0;
    if (melhor > iMin && melhor < iMax)
    {
        double a1 = LogProd(melhor - 1), b = LogProd(melhor), c = LogProd(melhor + 1);
        double den = a1 - 2 * b + c;
        if (std::fabs(den) > 1e-9) di = 0.5 * (a1 - c) / den;
        if (di < -1) di = -1;
        if (di > 1) di = 1;
    }
    return (melhor + di) * _taxa / (double) Fft::Tamanho;
}

double EspectroAnalise::LogProd(int i) const
{
    double p = _mag[i] * _mag[2 * i] * _mag[3 * i] * _mag[4 * i];
    return std::log(p + 1e-30);
}

void EspectroAnalise::AtualizarHarmonicos()
{
    _f0 = DetectarF0();
    double w = _area->ActualWidth, h = _area->ActualHeight;
    if (_f0 <= 0 || w < 2 || h < 2) { EsconderHarm(); return; }

    double logMin = std::log10(20.0);
    double logMax = std::log10(std::min(20000.0, _taxa / 2.0));
    double teto = std::min(20000.0, _taxa / 2.0);

    int usados = 0;
    for (int k = 0; k < MaxHarm; k++)
    {
        double f = _f0 * (k + 1);
        double xf = (std::log10(f) - logMin) / (logMax - logMin);
        if (f > teto || xf < 0 || xf > 1)
        {
            _hLin[k]->Visibility = Vistas::Visibility::Collapsed;
            _hRot[k]->Visibility = Vistas::Visibility::Collapsed;
            continue;
        }
        double x = xf * w;
        Line &l = *_hLin[k];
        l.X1 = x; l.X2 = x; l.Y1 = 0; l.Y2 = h;
        l.Visibility = Vistas::Visibility::Visible;

        auto &t = _hRot[k];
        t->Text = std::to_string(k + 1);
        Canvas::SetLeft(t, x + 2);
        Canvas::SetTop(t, 22);
        t->Visibility = Vistas::Visibility::Visible;
        usados++;
    }

    _f0Texto->Text = "f0: " + TextoFreq(_f0) + "  " + Nota(_f0);
    _f0Texto->Visibility = usados > 0 ? Vistas::Visibility::Visible : Vistas::Visibility::Collapsed;
}

void EspectroAnalise::EsconderHarm()
{
    if (_hLin.empty()) return;
    for (int k = 0; k < MaxHarm; k++)
    {
        _hLin[k]->Visibility = Vistas::Visibility::Collapsed;
        _hRot[k]->Visibility = Vistas::Visibility::Collapsed;
    }
    if (_f0Texto) _f0Texto->Visibility = Vistas::Visibility::Collapsed;
}

// ==================================================================
//  MAPA COLUNA -> RAIA (log de frequência)
// ==================================================================
void EspectroAnalise::MontarMapa()
{
    _mapaIni.assign(LargInterna, 0);
    _mapaFim.assign(LargInterna, 0);
    _colA.assign(LargInterna, DbPiso);
    _colB.assign(LargInterna, DbPiso);
    _pico.assign(LargInterna, DbPiso);

    double logMin = std::log10(20.0);
    double logMax = std::log10(std::min(20000.0, _taxa / 2.0));
    for (int x = 0; x < LargInterna; x++)
    {
        double f0 = std::pow(10, logMin + (logMax - logMin) * x / LargInterna);
        double f1 = std::pow(10, logMin + (logMax - logMin) * (x + 1.0) / LargInterna);
        int i0 = (int) (f0 * Fft::Tamanho / _taxa);
        int i1 = (int) (f1 * Fft::Tamanho / _taxa);
        if (i1 <= i0) i1 = i0 + 1;
        if (i0 < 0) i0 = 0;
        if (i1 > (int) _raiaA.size()) i1 = (int) _raiaA.size();
        _mapaIni[x] = i0;
        _mapaFim[x] = i1;
    }
    _taxaDoMapa = _taxa;
}

/// <summary>
/// Condensa as raias que caem em cada coluna pelo MÁXIMO (destaca tons
/// estreitos) e suaviza no tempo, para a curva não tremer.
/// </summary>
void EspectroAnalise::Reduzir(const std::vector<double> &raias, std::vector<double> &col)
{
    const double alpha = 0.5;
    for (int x = 0; x < LargInterna; x++)
    {
        int i0 = _mapaIni[x], i1 = _mapaFim[x];
        double db = DbPiso;
        for (int i = i0; i < i1; i++) if (raias[i] > db) db = raias[i];
        col[x] += (db - col[x]) * alpha;
    }
}

// ==================================================================
//  DESENHO DA CURVA
// ==================================================================
void EspectroAnalise::Garantir()
{
    if (_tela) return;
    _tela = std::make_unique<Tela>(LargInterna, AltInterna);
    _img->Source = _tela->Bitmap;
    if (_mapaIni.empty()) MontarMapa();
}

int EspectroAnalise::Y(double db) const
{
    double f = (-db) / _faixa;         // 0 dB no topo, -faixa embaixo
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    int y = (int) (f * (AltInterna - 1));
    return y;
}

void EspectroAnalise::Desenhar()
{
    _tela->Limpar(Fundo);

    bool doisCanais = (_modoCanal != 1);
    if (doisCanais)
    {
        DesenharCurva(_colB, _traco, CorB, CorAreaB);   // secundária por baixo
        DesenharCurva(_colA, _traco, CorA, CorAreaA);
    }
    else
    {
        DesenharCurva(_colA, _traco, CorA, CorAreaA);
    }

    // Retenção de pico, só na curva primária.
    for (int x = 0; x < LargInterna; x++)
    {
        if (_colA[x] > _pico[x]) _pico[x] = _colA[x];
        else _pico[x] -= 0.5;
        if (_pico[x] < DbPiso) _pico[x] = DbPiso;
        if (_pico[x] > -_faixa) _tela->Ponto(x, Y(_pico[x]), CorPico);
    }

    _tela->Aplicar();
}

void EspectroAnalise::DesenharCurva(const std::vector<double> &col, int traco, int cor, int corArea)
{
    int baseo = AltInterna - 1;

    if (traco == 2)   // barras agrupadas (visual clássico)
    {
        const int bandas = 72;
        for (int b = 0; b < bandas; b++)
        {
            int x0 = b * LargInterna / bandas;
            int x1 = (b + 1) * LargInterna / bandas;
            if (x1 <= x0) x1 = x0 + 1;
            double db = DbPiso;
            for (int x = x0; x < x1; x++) if (col[x] > db) db = col[x];
            int y = Y(db);
            for (int x = x0; x < x1 - 1; x++) _tela->ColunaVertical(x, y, baseo, cor);
        }
        return;
    }

    int yAnt = Y(col[0]);
    for (int x = 0; x < LargInterna; x++)
    {
        int y = Y(col[x]);
        if (traco == 1) _tela->ColunaVertical(x, y, baseo, corArea);   // área
        if (x > 0) _tela->Linha(x - 1, yAnt, x, y, cor);              // contorno
        yAnt = y;
    }
}

// ==================================================================
//  GRADE ROTULADA (camada WPF, nítida)
// ==================================================================
void EspectroAnalise::ConstruirGrade()
{
    _grade->Children.Clear();
    double w = _area->ActualWidth, h = _area->ActualHeight;
    if (w < 2 || h < 2) return;

    double logMin = std::log10(20.0);
    double logMax = std::log10(std::min(20000.0, _taxa / 2.0));

    // Verticais de frequência.
    for (double f : RefFreq)
    {
        if (f > _taxa / 2.0) continue;
        double x = (std::log10(f) - logMin) / (logMax - logMin) * w;
        _grade->Children.Add(LinhaGrade(x, 0, x, h));
        auto t = Rotulo(TextoFreq(f), OcreFraco);
        Canvas::SetLeft(t, x + 2);
        Canvas::SetTop(t, h - 15);
        _grade->Children.Add(t);
    }

    // Horizontais de dB (a cada 10, rótulo a cada 20).
    for (int db = 0; db >= -(int) _faixa; db -= 10)
    {
        double y = (-db) / _faixa * h;
        _grade->Children.Add(LinhaGrade(0, y, w, y));
        if (db % 20 == 0)
        {
            auto t = Rotulo(std::to_string(db), OcreFraco);
            Canvas::SetLeft(t, 2);
            Canvas::SetTop(t, y + 1);
            _grade->Children.Add(t);
        }
    }
}

// ==================================================================
//  CURSOR: Hz + dB + nota
// ==================================================================
void EspectroAnalise::AoMover(MouseEventArgs &e)
{
    double w = _area->ActualWidth, h = _area->ActualHeight;
    if (w < 2 || h < 2) return;
    Point p = e.GetPosition(_area.get());
    double fx = p.X / w;
    if (fx < 0) fx = 0;
    if (fx > 1) fx = 1;

    double logMin = std::log10(20.0);
    double logMax = std::log10(std::min(20000.0, _taxa / 2.0));
    double freq = std::pow(10, logMin + (logMax - logMin) * fx);
    double db = -(p.Y / h) * _faixa;

    _cursor->X1 = p.X; _cursor->X2 = p.X;
    _cursor->Y1 = 0; _cursor->Y2 = h;
    _cursor->Visibility = Vistas::Visibility::Visible;

    _leitura->Text = TextoFreq(freq) + "  " + Fixo(db, 1) + " dB  " + Nota(freq);
    _leitura->Visibility = Vistas::Visibility::Visible;
    _temCursor = true;
    PosicionarLeitura();
}

void EspectroAnalise::AoSair()
{
    _temCursor = false;
    _cursor->Visibility = Vistas::Visibility::Collapsed;
    _leitura->Visibility = Vistas::Visibility::Collapsed;
}

void EspectroAnalise::PosicionarLeitura()
{
    if (!_temCursor) return;
    _leitura->Measure(Size(Infinito, Infinito));
    Canvas::SetLeft(_leitura, std::max(4.0, _area->ActualWidth - _leitura->DesiredSize.Width - 8));
}

// ==================================================================
//  CONTROLES
// ==================================================================
/// <summary>
/// Um grupo de opções sempre visíveis: rótulo + rádios. É a diferença
/// entre um enfeite e uma ferramenta — o operador vê todas as escolhas
/// e qual está ativa de relance, sem clicar para descobrir.
/// </summary>
UIElementPtr EspectroAnalise::Grupo(const std::string &rotulo, const std::vector<std::string> &opcoes, int marcado,
                                    const std::string &grupo, std::function<void(RadioButton &)> ao)
{
    auto p = std::make_shared<StackPanel>();
    p->Orientation = Vistas::Orientation::Horizontal;
    p->VerticalAlignment = Vistas::VerticalAlignment::Center;
    p->Margin = Thickness(0, 0, 14, 0);
    auto t = std::make_shared<TextBlock>(rotulo + ":");
    t->Foreground = OcreFraco;
    t->FontSize = 11;
    t->VerticalAlignment = Vistas::VerticalAlignment::Center;
    t->Margin = Thickness(0, 0, 6, 0);
    p->Children.Add(t);
    for (size_t i = 0; i < opcoes.size(); i++)
    {
        auto r = std::make_shared<RadioButton>();
        r->Content = opcoes[i];
        r->GroupName = grupo;
        r->Tag = (int) i;
        r->Foreground = Ocre;
        r->FontSize = 11;
        r->VerticalAlignment = Vistas::VerticalAlignment::Center;
        r->Margin = Thickness(0, 0, 8, 0);
        r->Cursor = Cursors::Hand;
        r->IsChecked = ((int) i == marcado);     // antes do "+= ao", como no inicializador do C#
        RadioButton *rp = r.get();
        r->Checked += [ao, rp](RoutedEventArgs &) { ao(*rp); };
        p->Children.Add(r);
    }
    return p;
}

void EspectroAnalise::AoCanal(RadioButton &r)
{
    if (!r.IsChecked()) return;
    _modoCanal = std::any_cast<int>(r.Tag);
}

void EspectroAnalise::AoFaixa(RadioButton &r)
{
    if (!r.IsChecked()) return;
    int idx = std::any_cast<int>(r.Tag);
    _faixa = idx == 0 ? 60 : (idx == 1 ? 90 : 120);
    ConstruirGrade();
    // Congelado, o laço não redesenha: a curva ficava na escala antiga,
    // descasada da grade nova.
    if (_congelado && !_colA.empty()) Desenhar();
}

void EspectroAnalise::AoTraco(RadioButton &r)
{
    if (!r.IsChecked()) return;
    _traco = std::any_cast<int>(r.Tag);
}

} // namespace ARCA::Vistas

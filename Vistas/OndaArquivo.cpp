// ARCA no xOS - Vistas/OndaArquivo (de Vistas\OndaArquivo.cs).
#include "OndaArquivo.h"

#include "Instrumentos.h"
#include "Janela.h"
#include "Modelo/AnalisadorAudio.h"

#include <algorithm>
#include <cmath>

namespace ARCA::Vistas {

using Modelo::AnalisadorAudio;
using Modelo::OndaCache;

namespace {
// Cores (chapa sépia do módulo).
const int Fundo = Tela::Cor(0x17, 0x12, 0x0B);
const int Zero = Tela::Cor(0x3A, 0x2E, 0x1C);
const int CorCanal[] = {
    Tela::Cor(0xFF, 0x9E, 0x2C),   // esquerdo: âmbar
    Tela::Cor(0xE8, 0xC0, 0x78),   // direito: palha
    Tela::Cor(0xC9, 0x90, 0x2F),
    Tela::Cor(0xB4, 0x76, 0x1C),
};
const int CorRms = Tela::Cor(0xFF, 0xE0, 0xB0);
const int CorClip = Tela::Cor(0xE0, 0x56, 0x3A);
const Brush CorMarca = SolidColorBrush(Color::FromRgb(0x7F, 0xC8, 0xD0));
} // namespace

OndaArquivo::OndaArquivo()
{
    Background = SolidColorBrush(Color::FromRgb(0x17, 0x12, 0x0B));
    ClipToBounds = true;
    // RenderOptions.SetBitmapScalingMode(_img, NearestNeighbor): a Tela tem o tamanho exato do
    // elemento, então não há ampliação a filtrar
    _img->Stretch = Vistas::Stretch::Fill;
    _sobre->IsHitTestVisible = false;

    // Seleção fica no fundo da sobreposição; o playhead entra depois,
    // por cima. Marcadores são inseridos no índice 0 (abaixo de tudo).
    _selRect->Fill = SolidColorBrush(Color::FromArgb(0x34, 0xFF, 0xE0, 0xB0));
    _selRect->Stroke = SolidColorBrush(Color::FromArgb(0x90, 0xFF, 0xE0, 0xB0));
    _selRect->StrokeThickness = 1;
    _selRect->Visibility = Vistas::Visibility::Collapsed;
    _sobre->Children.Add(_selRect);

    // Vermelho e mais grosso: some no meio da onda se for discreto.
    _playhead->Stroke = SolidColorBrush(Color::FromRgb(0xF0, 0x2A, 0x1E));
    _playhead->StrokeThickness = 2;
    _sobre->Children.Add(_playhead);

    _selTexto->Foreground = SolidColorBrush(Color::FromRgb(0xC9, 0xA8, 0x6A));
    _selTexto->FontSize = 11;
    _selTexto->FontFamily = "Consolas";
    _selTexto->Visibility = Vistas::Visibility::Collapsed;
    Canvas::SetTop(_selTexto, 4); Canvas::SetLeft(_selTexto, 6);
    _sobre->Children.Add(_selTexto);

    _hintV->Foreground = SolidColorBrush(Color::FromRgb(0x8A, 0x76, 0x54));
    _hintV->FontSize = 11;
    _hintV->Visibility = Vistas::Visibility::Collapsed;
    _sobre->Children.Add(_hintV);

    _aviso->Foreground = SolidColorBrush(Color::FromRgb(0x8A, 0x76, 0x54));
    _aviso->FontSize = 12;
    _aviso->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    _aviso->VerticalAlignment = Vistas::VerticalAlignment::Center;
    _aviso->Text = "analisando a onda…";

    Children.Add(_img);
    Children.Add(_sobre);
    Children.Add(_aviso);

    SizeChanged += [this](SizeChangedEventArgs &) { RecriarTela(); Redesenhar(); PosicionarPlayhead(); AtualizarMarcasSelecao(); AtualizarHintV(); };
    // Volta à tela (troca de aba): refaz na escala do monitor.
    Loaded += [this](RoutedEventArgs &) { RecriarTela(); Redesenhar(); PosicionarPlayhead(); AtualizarMarcasSelecao(); };
    // A captura do mouse pode se perder (outra janela, Alt+Tab): sem
    // isto o arrasto ficava "grudado".
    LostMouseCapture += [this](RoutedEventArgs &) { _arrastando = false; _selecionando = false; };
    MouseWheel += [this](MouseWheelEventArgs &e) { AoRoda(e); };
    MouseLeftButtonDown += [this](MouseButtonEventArgs &e) { AoApertar(e); };
    MouseMove += [this](MouseEventArgs &e) { AoMover(e); };
    MouseLeftButtonUp += [this](MouseButtonEventArgs &e) { AoSoltar(e); };
    MouseRightButtonDown += [this](MouseButtonEventArgs &e) { AoDireito(e); };
}

void OndaArquivo::Db(bool valor)
{
    if (_db != valor) { _db = valor; Redesenhar(); }
}

void OndaArquivo::Definir(std::shared_ptr<OndaCache> oc)
{
    _oc = oc;
    _marcas.clear();
    LimparSelecao();
    _ganhoV = 1.0;
    if (oc && oc->TotalAmostras > 0)
    {
        _ini = 0; _fim = oc->TotalAmostras;
        _aviso->Visibility = Vistas::Visibility::Collapsed;
    }
    else
    {
        _aviso->Text = "analisando a onda…";
        _aviso->Visibility = Vistas::Visibility::Visible;
    }
    RecriarTela();
    Redesenhar();
    AtualizarMarcasSelecao();
    AtualizarHintV();
}

void OndaArquivo::MostrarFalha(const std::string &strMotivo)
{
    _aviso->Text = "Não consegui analisar a onda: " + strMotivo + ".\nNada foi guardado em cache; a próxima abertura tenta de novo.";
    _aviso->Visibility = Vistas::Visibility::Visible;
}

void OndaArquivo::Limpar()
{
    _oc = nullptr;
    _aviso->Text = "sem onda";
    _aviso->Visibility = Vistas::Visibility::Visible;
    if (_tela) { _tela->Limpar(Fundo); _tela->Aplicar(); }
}

void OndaArquivo::PosicaoSegundos(double valor)
{
    _posSeg = valor;
    PosicionarPlayhead();
}

// ===============================================================
void OndaArquivo::RecriarTela()
{
    // Em pixels REAIS do dispositivo, não em DIPs. Se o monitor está a
    // 125%, um DIP vale 1,25 pixel: uma Tela medida em DIPs sairia
    // esticada pelo Stretch.Fill e a coluna de 1 DIP viraria um bloco
    // irregular de 1 ou 2 pixels (o "muitas colunas enfileiradas"). Ao
    // criar a Tela com o total de pixels físicos, cada coluna de dados
    // ocupa exatamente 1 pixel — a onda fica nítida em qualquer DPI.
    // LINUX: no xOS um DIP é um pixel (escala 1).
    double ex = 1, ey = 1;
    // Fora da tela (aba oculta) não se sabe a escala do monitor: criar
    // agora daria uma Tela de 100% que ficaria borrada a 125%. Espera
    // voltar à tela (Loaded refaz).
    if (!Janela()) return;
    int w = (int) std::lround(ActualWidth * ex);
    int h = (int) std::lround(ActualHeight * ey);
    if (w < 1 || h < 1) return;
    if (_tela && _tela->Largura == w && _tela->Altura == h) return;

    _tela = std::make_unique<Tela>(w, h);
    _img->Source = _tela->Bitmap;
    _mn.assign(w, 0); _mx.assign(w, 0); _rms.assign(w, 0); _clip.assign(w, false);
}

void OndaArquivo::Redesenhar()
{
    if (!_tela) RecriarTela();
    if (!_tela) return;

    _tela->Limpar(Fundo);
    if (!_oc || _oc->TotalAmostras <= 0) { _tela->Aplicar(); return; }

    int w = _tela->Largura, h = _tela->Altura;
    int nc = std::max(1, _oc->Canais);
    int bandaH = h / nc;
    // Folga em cima e embaixo: um master alto (picos perto de 0 dB)
    // encostaria nas bordas e a onda pareceria espremida. 86% da
    // meia-faixa deixa a onda respirar.
    int semi = (int) (bandaH / 2 * 0.86);
    if (semi < 1) semi = 1;

    for (int c = 0; c < nc; c++)
    {
        _oc->Colunas(c, _ini, _fim, w, _mn, _mx, _rms, _clip);

        int y0 = c * bandaH;
        int meio = y0 + bandaH / 2;
        int corCanal = CorCanal[c % 4];

        // linha zero
        _tela->Linha(0, meio, w - 1, meio, Zero);

        for (int x = 0; x < w; x++)
        {
            int yMax = meio - (int) (Mapear(_mx[x]) * semi);
            int yMin = meio - (int) (Mapear(_mn[x]) * semi);
            int topo = std::min(yMax, yMin);
            int baseo = std::max(yMax, yMin);
            if (topo < y0) topo = y0;
            if (baseo > y0 + bandaH - 1) baseo = y0 + bandaH - 1;

            _tela->ColunaVertical(x, topo, baseo, _clip[x] ? CorClip : corCanal);

            // RMS por dentro, em torno do zero
            int yr = (int) (Mapear(_rms[x]) * semi);
            if (yr > 0 && !_clip[x])
                _tela->ColunaVertical(x, meio - yr, meio + yr, CorRms);
        }

        if (c > 0) _tela->Linha(0, y0, w - 1, y0, Zero);   // divisória entre canais
    }

    _tela->Aplicar();
}

/// <summary>
/// Amplitude normalizada (-1..1) para fração da meia-altura, conforme a
/// escala. Em dBFS, comprime a faixa: -60 dB no zero, 0 dB no extremo,
/// preservando o sinal (para cima/para baixo).
/// </summary>
double OndaArquivo::Mapear(float a) const
{
    if (!_db)
    {
        double v = a * _ganhoV;               // zoom vertical (linear)
        return v < -1 ? -1 : (v > 1 ? 1 : v);
    }
    double m = std::fabs(a);
    if (m < 1e-5) return 0;
    double db = 20.0 * std::log10(m);          // <= 0
    double norm = (db + 60.0) / 60.0;          // -60 dB -> 0 ; 0 dB -> 1
    if (norm < 0) norm = 0;
    if (norm > 1) norm = 1;
    return a < 0 ? -norm : norm;
}

// ===============================================================
void OndaArquivo::PosicionarPlayhead()
{
    if (!_oc || _oc->Taxa <= 0 || _fim <= _ini || ActualWidth < 1)
    {
        _playhead->Visibility = Vistas::Visibility::Collapsed;
        return;
    }
    int64_t amostra = (int64_t) (_posSeg * _oc->Taxa);
    if (amostra < _ini || amostra > _fim) { _playhead->Visibility = Vistas::Visibility::Collapsed; return; }

    double x = (double) (amostra - _ini) / (_fim - _ini) * ActualWidth;
    _playhead->X1 = x; _playhead->X2 = x;
    _playhead->Y1 = 0; _playhead->Y2 = ActualHeight;
    _playhead->Visibility = Vistas::Visibility::Visible;
}

// ===============================================================
void OndaArquivo::AoRoda(MouseWheelEventArgs &e)
{
    if (!_oc || _fim <= _ini) return;
    ModifierKeys mods = Keyboard::Modifiers();

    // Ctrl+roda: zoom vertical (amplitude), para inspecionar trechos baixos.
    // Em dB a escala já abre os trechos baixos e o ganho não se aplica:
    // ali ele mostrava "vert ×4" sem efeito nenhum.
    if (Tem(mods, ModifierKeys::Control) && _db) { e.Handled = true; return; }
    if (Tem(mods, ModifierKeys::Control))
    {
        _ganhoV *= e.Delta > 0 ? 1.3 : 1.0 / 1.3;
        if (_ganhoV < 1) _ganhoV = 1;
        if (_ganhoV > 64) _ganhoV = 64;
        Redesenhar(); AtualizarHintV();
        e.Handled = true; return;
    }

    double fx = ActualWidth > 0 ? e.GetPosition(this).X / ActualWidth : 0.5;
    if (fx < 0) fx = 0;
    if (fx > 1) fx = 1;

    int64_t span = _fim - _ini;
    int64_t foco = _ini + (int64_t) (fx * span);

    // Shift+roda desliza; roda pura dá zoom ancorado no cursor.
    if (Tem(mods, ModifierKeys::Shift))
    {
        int64_t passo = (int64_t) (span * 0.15) * (e.Delta > 0 ? -1 : 1);
        Deslocar(passo);
    }
    else
    {
        double fator = e.Delta > 0 ? 1.0 / 1.3 : 1.3;
        int64_t novo = (int64_t) (span * fator);
        // O cache tem um bin a cada BinBase amostras: abaixo de um bin
        // por coluna, o zoom só repete blocos e vira degraus.
        int64_t minSpan = std::max<int64_t>(16, (int64_t) (_tela ? _tela->Largura : 256) * OndaCache::BinBase);
        if (novo < minSpan) novo = minSpan;
        if (novo > _oc->TotalAmostras) novo = _oc->TotalAmostras;

        _ini = foco - (int64_t) (fx * novo);
        _fim = _ini + novo;
        Ajustar();
        Redesenhar(); PosicionarPlayhead(); AtualizarMarcasSelecao();
    }
    e.Handled = true;
}

void OndaArquivo::Deslocar(int64_t amostras)
{
    int64_t span = _fim - _ini;
    _ini += amostras; _fim = _ini + span;
    Ajustar();
    Redesenhar(); PosicionarPlayhead(); AtualizarMarcasSelecao();
}

void OndaArquivo::Ajustar()
{
    int64_t span = _fim - _ini;
    if (span > _oc->TotalAmostras) { _ini = 0; _fim = _oc->TotalAmostras; return; }
    if (_ini < 0) { _ini = 0; _fim = span; }
    if (_fim > _oc->TotalAmostras) { _fim = _oc->TotalAmostras; _ini = _fim - span; }
    if (_ini < 0) _ini = 0;
}

void OndaArquivo::AoApertar(MouseButtonEventArgs &e)
{
    if (!_oc) return;

    // Duplo-clique: marca no tempo do cursor.
    if (e.ClickCount == 2)
    {
        if (_oc->Taxa > 0) AdicionarMarca((double) PosParaAmostra(e.GetPosition(this).X) / _oc->Taxa);
        e.Handled = true;
        return;
    }

    _arrastando = true; _moveu = false;
    _mouseIni = e.GetPosition(this);
    _mouseUltimoX = _mouseIni.X;
    _selecionando = Tem(Keyboard::Modifiers(), ModifierKeys::Shift);
    if (_selecionando) _selAncora = PosParaAmostra(_mouseIni.X);
    CaptureMouse();
}

void OndaArquivo::AoMover(MouseEventArgs &e)
{
    if (!_arrastando || !_oc) return;
    Point p = e.GetPosition(this);
    if (!_moveu && std::fabs(p.X - _mouseIni.X) < 4 && std::fabs(p.Y - _mouseIni.Y) < 4) return;
    _moveu = true;

    if (_selecionando)
    {
        int64_t a = PosParaAmostra(p.X);
        _selIni = std::min(_selAncora, a);
        _selFim = std::max(_selAncora, a);
        PosicionarSelecao();
        return;
    }

    double dxPx = p.X - _mouseUltimoX;
    _mouseUltimoX = p.X;
    if (ActualWidth > 0)
    {
        double amostrasPorPx = (double) (_fim - _ini) / ActualWidth;
        Deslocar(-(int64_t) (dxPx * amostrasPorPx));
    }
}

void OndaArquivo::AoSoltar(MouseButtonEventArgs &e)
{
    if (_arrastando && !_moveu && _oc && _oc->Taxa > 0 && ActualWidth > 0)
    {
        // Clique sem arrasto: limpa a seleção e busca a posição.
        LimparSelecao();
        double seg = (double) PosParaAmostra(e.GetPosition(this).X) / _oc->Taxa;
        Buscou(seg);
    }
    _arrastando = false; _selecionando = false;
    if (IsMouseCaptured()) ReleaseMouseCapture();
}

void OndaArquivo::AoDireito(MouseButtonEventArgs &e)
{
    if (!_oc) { return; }
    Point p = e.GetPosition(this);

    // Remove a marca mais próxima (até 6 px); senão, limpa a seleção.
    int melhor = -1; double best = 6;
    if (_oc->Taxa > 0)
        for (size_t i = 0; i < _marcas.size(); i++)
        {
            double x = AmostraParaX((int64_t) (_marcas[i] * _oc->Taxa));
            double d = std::fabs(x - p.X);
            if (d < best) { best = d; melhor = (int) i; }
        }

    if (melhor >= 0) { _marcas.erase(_marcas.begin() + melhor); PosicionarMarcas(); }
    else LimparSelecao();
    e.Handled = true;
}

// ===============================================================
//  SELEÇÃO E MARCAS
// ===============================================================
int64_t OndaArquivo::PosParaAmostra(double x) const
{
    double fx = ActualWidth > 0 ? x / ActualWidth : 0;
    if (fx < 0) fx = 0;
    if (fx > 1) fx = 1;
    return _ini + (int64_t) (fx * (_fim - _ini));
}

double OndaArquivo::AmostraParaX(int64_t a) const
{
    if (_fim <= _ini) return 0;
    return (double) (a - _ini) / (_fim - _ini) * ActualWidth;
}

void OndaArquivo::LimparSelecao()
{
    _selIni = _selFim = -1;
    _selRect->Visibility = Vistas::Visibility::Collapsed;
    _selTexto->Visibility = Vistas::Visibility::Collapsed;
}

void OndaArquivo::AtualizarMarcasSelecao()
{
    PosicionarSelecao();
    PosicionarMarcas();
}

void OndaArquivo::PosicionarSelecao()
{
    if (!_oc || _selIni < 0 || _selFim <= _selIni || _fim <= _ini)
    {
        _selRect->Visibility = Vistas::Visibility::Collapsed;
        _selTexto->Visibility = Vistas::Visibility::Collapsed;
        return;
    }

    int64_t a0 = std::max(_selIni, _ini), a1 = std::min(_selFim, _fim);
    if (a1 > a0)
    {
        double x0 = AmostraParaX(a0), x1 = AmostraParaX(a1);
        Canvas::SetLeft(_selRect, x0);
        Canvas::SetTop(_selRect, 0);
        _selRect->Width = std::max(1.0, x1 - x0);
        _selRect->Height = ActualHeight;
        _selRect->Visibility = Vistas::Visibility::Visible;
    }
    else _selRect->Visibility = Vistas::Visibility::Collapsed;

    double taxa = _oc->Taxa;
    double s0 = _selIni / taxa, s1 = _selFim / taxa;
    float pk = 0;
    double energia = 0;
    for (int c = 0; c < _oc->Canais; c++)
    {
        _oc->Colunas(c, _selIni, _selFim, 1, _s1mn, _s1mx, _s1rms, _s1cl);
        float ap = std::max(std::fabs(_s1mn[0]), std::fabs(_s1mx[0]));
        if (ap > pk) pk = ap;
        // RMS do trecho, e não o do bloco mais forte (era o que saía).
        double r = _oc->RmsDoTrecho(c, _selIni, _selFim);
        energia += r * r;
    }
    float rms = (float) std::sqrt(energia / std::max(1, _oc->Canais));
    _selTexto->Text = "sel " + Tempo(s0) + "–" + Tempo(s1) +
                      " (" + Fixo(s1 - s0, 2) + " s)   pico " +
                      Fixo(AnalisadorAudio::Db(pk), 1) + " dB   rms " +
                      Fixo(AnalisadorAudio::Db(rms), 1) + " dB";
    _selTexto->Visibility = Vistas::Visibility::Visible;
}

void OndaArquivo::AdicionarMarca(double seg)
{
    _marcas.push_back(seg);
    PosicionarMarcas();
}

void OndaArquivo::PosicionarMarcas()
{
    for (size_t i = 0; i < _marcaLinhas.size(); i++) _sobre->Children.Remove(_marcaLinhas[i]);
    _marcaLinhas.clear();
    if (!_oc || _oc->Taxa <= 0 || _fim <= _ini) return;

    for (size_t i = 0; i < _marcas.size(); i++)
    {
        int64_t a = (int64_t) (_marcas[i] * _oc->Taxa);
        if (a < _ini || a > _fim) continue;
        double x = AmostraParaX(a);
        auto l = std::make_shared<Line>();
        l->Stroke = CorMarca; l->StrokeThickness = 1;
        l->X1 = x; l->X2 = x; l->Y1 = 0; l->Y2 = ActualHeight;
        _sobre->Children.Insert(0, l);   // abaixo do playhead
        _marcaLinhas.push_back(l);
    }
}

void OndaArquivo::AtualizarHintV()
{
    if (_ganhoV > 1.01)
    {
        _hintV->Text = "vert ×" + Fixo(_ganhoV, 1);
        Canvas::SetLeft(_hintV, 6);
        Canvas::SetTop(_hintV, std::max(0.0, ActualHeight - 18));
        _hintV->Visibility = Vistas::Visibility::Visible;
    }
    else _hintV->Visibility = Vistas::Visibility::Collapsed;
}

std::string OndaArquivo::Tempo(double s)
{
    if (s < 0) s = 0;
    int m = (int) (s / 60);
    double seg = s - m * 60;
    return std::to_string(m) + ":" + Fixo(seg, 1, 4);
}

} // namespace ARCA::Vistas

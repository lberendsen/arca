// ARCA no xOS - Vistas/MedidoresPainel (de Vistas\MedidoresPainel.cs).
#include "MedidoresPainel.h"

#include <cmath>

namespace ARCA::Vistas {

using Modelo::AnalisadorAudio;

namespace {
Brush Congelar(uint8_t r, uint8_t g, uint8_t b) { return SolidColorBrush(Color::FromRgb(r, g, b)); }

const Brush Ambar = Congelar(0xFF, 0x9E, 0x2C);
const Brush Palha = Congelar(0xE8, 0xC0, 0x78);
const Brush Trilho = Congelar(0x2A, 0x21, 0x14);
const Brush Brilho = Congelar(0xFF, 0xF0, 0xCC);
const Brush Vermelho = Congelar(0xF0, 0x2A, 0x1E);
const Brush LuzApagada = Congelar(0x3A, 0x2E, 0x1C);
const Brush Ocre = Congelar(0xC9, 0xA8, 0x6A);
const Brush OcreFraco = Congelar(0x8A, 0x76, 0x54);
} // namespace

Medidores::Medidores()
{
    Background = Congelar(0x17, 0x12, 0x0B);

    // Um Grid de quatro faixas proporcionais: cada grupo é centralizado
    // na sua faixa, então o espaço livre vira espaçamento entre grupos e
    // o conjunto preenche a área conforme ela cresce.
    auto raiz = std::make_shared<Grid>();
    raiz->Margin = Thickness(20, 12, 20, 12);
    for (int i = 0; i < 5; i++) raiz->RowDefinitions.push_back(RowDefinition());

    auto nivel = Secao();
    nivel->Children.Add(Cabecalho("NÍVEL  (dBFS)"));
    _picoE = Linha(nivel, "Pico L", Ambar, -60, 0, true, true);
    _picoD = Linha(nivel, "Pico R", Palha, -60, 0, true, true);
    _rmsE = Linha(nivel, "RMS L", Ambar, -60, 0, false, false);
    _rmsD = Linha(nivel, "RMS R", Palha, -60, 0, false, false);
    SetRow(nivel, 0);
    raiz->Children.Add(nivel);

    auto dinamica = Secao();
    dinamica->Children.Add(Cabecalho("DINÂMICA"));
    _cristaE = Linha(dinamica, "Crest L", Ambar, 0, 30, false, false);
    _cristaD = Linha(dinamica, "Crest R", Palha, 0, 30, false, false);
    _dr = Linha(dinamica, "DR (janela)", Brilho, 0, 30, false, false);
    dinamica->Children.Add(LinhaClipping());
    SetRow(dinamica, 1);
    raiz->Children.Add(dinamica);

    auto estereo = Secao();
    estereo->Children.Add(Cabecalho("ESTÉREO"));
    _corr = LinhaBipolar(estereo, "Correlação");
    _bal = LinhaBipolar(estereo, "Balanço  (−E / +D)");
    _mid = Linha(estereo, "Mid", Ambar, -60, 0, false, false);
    _side = Linha(estereo, "Side", Palha, -60, 0, false, false);
    SetRow(estereo, 2);
    raiz->Children.Add(estereo);

    auto loud = Secao();
    loud->Children.Add(CabecalhoLoudness());
    _lufsM = Linha(loud, "M (400 ms)", Ambar, -40, 0, false, false);
    _lufsS = Linha(loud, "S (3 s)", Palha, -40, 0, false, false);
    _lufsI = Linha(loud, "Integrado", Brilho, -40, 0, false, false);
    SetRow(loud, 3);
    raiz->Children.Add(loud);

    auto arq = Secao();
    arq->Children.Add(Cabecalho("ARQUIVO  (R128, medido uma vez)"));
    _arqI = Linha(arq, "Integrado", Brilho, -40, 0, false, false);
    _arqLra = Linha(arq, "LRA", Ambar, 0, 20, false, false);
    _arqPico = Linha(arq, "Pico", Palha, -40, 0, false, false);
    SetRow(arq, 4);
    raiz->Children.Add(arq);

    Children.Add(raiz);
}

void Medidores::DefinirArquivo(double integrado, double lra, double pico)
{
    _fI = integrado; _fLra = lra; _fPico = pico;
}

std::shared_ptr<StackPanel> Medidores::Secao()
{
    auto s = std::make_shared<StackPanel>();
    s->VerticalAlignment = Vistas::VerticalAlignment::Center;
    return s;
}

// ==================================================================
void Medidores::Atualizar(AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir)
{
    _a = &a;
    double picoE = AnalisadorAudio::Db(a.PicoEsq);
    double picoD = AnalisadorAudio::Db(a.PicoDir);
    double rmsE = AnalisadorAudio::Db(a.RmsEsq);
    double rmsD = AnalisadorAudio::Db(a.RmsDir);

    _picoE->Def(picoE, FmtDb(picoE));
    _picoD->Def(picoD, FmtDb(picoD));
    _rmsE->Def(rmsE, FmtDb(rmsE));
    _rmsD->Def(rmsD, FmtDb(rmsD));

    double crE = picoE - rmsE;
    double crD = picoD - rmsD;
    _cristaE->Def(crE, Fixo(crE, 1) + " dB");
    _cristaD->Def(crD, Fixo(crD, 1) + " dB");

    double dr = (crE + crD) * 0.5;
    _drSuav += (dr - _drSuav) * 0.05;
    _dr->Def(_drSuav, Fixo(_drSuav, 1) + " dB");

    // Clipping com retenção: um estouro breve fica visível ~1 s.
    if (a.ClipouEsq) _holdE = 30; else if (_holdE > 0) _holdE--;
    if (a.ClipouDir) _holdD = 30; else if (_holdD > 0) _holdD--;
    _luzE->Fill = _holdE > 0 ? Vermelho : LuzApagada;
    _luzD->Fill = _holdD > 0 ? Vermelho : LuzApagada;

    _corr->Def(a.Correlacao, FmtSinal(a.Correlacao));
    _bal->Def(a.Balanco, FmtSinal(a.Balanco));
    double mid = AnalisadorAudio::Db(a.RmsMid);
    double side = AnalisadorAudio::Db(a.RmsSide);
    _mid->Def(mid, FmtDb(mid));
    _side->Def(side, FmtDb(side));

    _lufsM->Def(a.LufsM, FmtLufs(a.LufsM));
    _lufsS->Def(a.LufsS, FmtLufs(a.LufsS));
    _lufsI->Def(a.LufsI, FmtLufs(a.LufsI));

    _arqI->Def(_fI, FmtLufs(_fI));
    _arqLra->Def(_fLra, Fixo(_fLra, 1) + " LU");
    _arqPico->Def(_fPico, FmtDb(_fPico));
}

// ==================================================================
//  MONTAGEM
// ==================================================================
UIElementPtr Medidores::CabecalhoLoudness()
{
    auto g = std::make_shared<Grid>();
    g->Margin = Thickness(0, 10, 0, 3);
    g->ColumnDefinitions = {ColumnDefinition(), ColumnDefinition(GridLength::Auto())};

    auto t = std::make_shared<TextBlock>("LOUDNESS  (LUFS)");
    t->Foreground = OcreFraco; t->FontSize = 10;
    t->FontWeight = FontWeights::SemiBold; t->VerticalAlignment = Vistas::VerticalAlignment::Center;
    SetColumn(t, 0);
    g->Children.Add(t);

    auto b = std::make_shared<Button>();
    b->Content = "zerar integrado"; b->Foreground = Ocre;
    b->Background = Congelar(0x22, 0x1B, 0x10); b->BorderBrush = Congelar(0x3A, 0x2E, 0x1C);
    b->BorderThickness = Thickness(1); b->Padding = Thickness(6, 0, 6, 0);
    b->FontSize = 10; b->Cursor = Cursors::Hand;
    b->Click += [this](RoutedEventArgs &) { AoZerar(); };
    SetColumn(b, 1);
    g->Children.Add(b);
    return g;
}

void Medidores::AoZerar()
{
    if (_a) _a->ReiniciarLoudness();
}

std::shared_ptr<TextBlock> Medidores::Cabecalho(const std::string &t)
{
    auto tb = std::make_shared<TextBlock>(t);
    tb->Foreground = OcreFraco;
    tb->FontSize = 10;
    tb->FontWeight = FontWeights::SemiBold;
    tb->Margin = Thickness(0, 10, 0, 3);
    return tb;
}

std::shared_ptr<Medidores::Barra> Medidores::Linha(const std::shared_ptr<StackPanel> &host, const std::string &rotulo,
                                                   const Brush &cor, double escMin, double escMax, bool comHold, bool alerta)
{
    auto g = LinhaBase(rotulo);
    auto trilha = std::make_shared<Grid>();
    trilha->Height = 16; trilha->VerticalAlignment = Vistas::VerticalAlignment::Center;
    auto fundo = std::make_shared<Rectangle>();
    fundo->Fill = Trilho;
    trilha->Children.Add(fundo);
    auto enche = std::make_shared<Rectangle>();
    enche->Fill = cor; enche->HorizontalAlignment = Vistas::HorizontalAlignment::Left;
    trilha->Children.Add(enche);
    std::shared_ptr<Rectangle> marca;
    if (comHold)
    {
        marca = std::make_shared<Rectangle>();
        marca->Width = 2; marca->Fill = Brilho; marca->HorizontalAlignment = Vistas::HorizontalAlignment::Left;
        trilha->Children.Add(marca);
    }
    SetColumn(trilha, 1);
    g->Children.Add(trilha);

    auto val = Valor();
    SetColumn(val, 2);
    g->Children.Add(val);
    host->Children.Add(g);

    auto b = std::make_shared<Barra>();
    b->Trilha = trilha; b->Enche = enche; b->Marca = marca; b->Valor = val;
    b->EscMin = escMin; b->EscMax = escMax; b->ComHold = comHold;
    b->AlertaTopo = alerta; b->CorNormal = cor;
    return b;
}

std::shared_ptr<Medidores::Barra> Medidores::LinhaBipolar(const std::shared_ptr<StackPanel> &host, const std::string &rotulo)
{
    auto g = LinhaBase(rotulo);
    auto trilha = std::make_shared<Grid>();
    trilha->Height = 16; trilha->VerticalAlignment = Vistas::VerticalAlignment::Center;
    auto fundo = std::make_shared<Rectangle>();
    fundo->Fill = Trilho;
    trilha->Children.Add(fundo);
    // Traço central de referência.
    auto centro = std::make_shared<Rectangle>();
    centro->Width = 1; centro->Fill = OcreFraco; centro->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    trilha->Children.Add(centro);
    auto enche = std::make_shared<Rectangle>();
    enche->Fill = Ambar; enche->HorizontalAlignment = Vistas::HorizontalAlignment::Left;
    trilha->Children.Add(enche);
    SetColumn(trilha, 1);
    g->Children.Add(trilha);

    auto val = Valor();
    SetColumn(val, 2);
    g->Children.Add(val);
    host->Children.Add(g);

    auto b = std::make_shared<Barra>();
    b->Trilha = trilha; b->Enche = enche; b->Valor = val; b->Bipolar = true;
    return b;
}

std::shared_ptr<Grid> Medidores::LinhaClipping()
{
    auto g = LinhaBase("Clipping (agora)");
    auto p = std::make_shared<StackPanel>();
    p->Orientation = Vistas::Orientation::Horizontal; p->VerticalAlignment = Vistas::VerticalAlignment::Center;
    _luzE = Luz(); _luzD = Luz();
    p->Children.Add(Etiqueta("L"));
    p->Children.Add(_luzE);
    p->Children.Add(Etiqueta("R"));
    p->Children.Add(_luzD);
    SetColumn(p, 1);
    g->Children.Add(p);
    return g;
}

std::shared_ptr<Grid> Medidores::LinhaBase(const std::string &rotulo)
{
    auto g = std::make_shared<Grid>();
    g->Margin = Thickness(0, 3, 0, 3);
    g->ColumnDefinitions = {ColumnDefinition(GridLength(100)), ColumnDefinition(), ColumnDefinition(GridLength(84))};

    auto r = std::make_shared<TextBlock>(rotulo);
    r->Foreground = Ocre; r->FontSize = 11;
    r->VerticalAlignment = Vistas::VerticalAlignment::Center;
    SetColumn(r, 0);
    g->Children.Add(r);
    return g;
}

std::shared_ptr<TextBlock> Medidores::Valor()
{
    auto t = std::make_shared<TextBlock>();
    t->Foreground = Ocre; t->FontSize = 11; t->FontFamily = "Consolas";
    t->HorizontalAlignment = Vistas::HorizontalAlignment::Right;
    t->VerticalAlignment = Vistas::VerticalAlignment::Center;
    t->Margin = Thickness(8, 0, 0, 0);
    return t;
}

std::shared_ptr<TextBlock> Medidores::Etiqueta(const std::string &t)
{
    auto tb = std::make_shared<TextBlock>(t);
    tb->Foreground = OcreFraco; tb->FontSize = 11;
    tb->VerticalAlignment = Vistas::VerticalAlignment::Center;
    tb->Margin = Thickness(0, 0, 4, 0);
    return tb;
}

std::shared_ptr<Rectangle> Medidores::Luz()
{
    auto r = std::make_shared<Rectangle>();
    r->Width = 12; r->Height = 12; r->Fill = LuzApagada;
    r->Margin = Thickness(0, 0, 12, 0);
    r->VerticalAlignment = Vistas::VerticalAlignment::Center;
    return r;
}

std::string Medidores::FmtDb(double db)
{
    if (db <= -140) return "-∞ dB";
    return Fixo(db, 1) + " dB";
}

std::string Medidores::FmtSinal(double v)
{
    // ToString("+0.00;-0.00;0.00"): o zero (já arredondado) sai sem sinal
    std::string s = Fixo(std::fabs(v), 2);
    if (s == "0,00") return s;
    return (v < 0 ? "-" : "+") + s;
}

std::string Medidores::FmtLufs(double v)
{
    if ((std::isinf(v) && v < 0) || v <= -100)
        return "-∞ LUFS";
    return Fixo(v, 1) + " LUFS";
}

// ==================================================================
void Medidores::Barra::Def(double v, const std::string &txt)
{
    Valor->Text = txt;
    double w = Trilha->ActualWidth;
    if (w < 1) return;

    if (Bipolar)
    {
        double centro = w / 2.0;
        double vv = v < -1 ? -1 : (v > 1 ? 1 : v);
        double larg = std::fabs(vv) * centro;
        Enche->Width = larg;
        Enche->Margin = Thickness(vv >= 0 ? centro : centro - larg, 0, 0, 0);
        return;
    }

    double frac = (v - EscMin) / (EscMax - EscMin);
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    Enche->Width = frac * w;

    if (AlertaTopo)
        Enche->Fill = v > -3 ? Vermelho : CorNormal;

    if (ComHold && Marca)
    {
        if (frac > Hold) Hold = frac; else Hold -= 0.012;
        if (Hold < 0) Hold = 0;
        Marca->Margin = Thickness(Hold * w - 1, 0, 0, 0);
    }
}

} // namespace ARCA::Vistas

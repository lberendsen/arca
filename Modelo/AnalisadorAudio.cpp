// ARCA no xOS - Modelo/AnalisadorAudio (de Modelo\AnalisadorAudio.cs).
#include "AnalisadorAudio.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ARCA::Modelo {

namespace {
constexpr double MenosInfinito = -std::numeric_limits<double>::infinity();
}

AnalisadorAudio::AnalisadorAudio()
    : LufsM(MenosInfinito), LufsS(MenosInfinito), LufsI(MenosInfinito),
      _esq(Janela, 0.0f), _dir(Janela, 0.0f), _histEnergia(HistBins, 0.0), _histN(HistBins, 0)
{
}

AnalisadorAudio::~AnalisadorAudio() = default;   // Dispose: _leitor.Dispose()

bool AnalisadorAudio::Abrir(const std::string &caminho)
{
    Zerar();
    bool ok = _leitor.Abrir(caminho);
    if (ok) ReiniciarLoudness();
    return ok;
}

void AnalisadorAudio::Fechar()
{
    _leitor.Fechar();
    Zerar();
}

void AnalisadorAudio::Zerar()
{
    std::fill(_esq.begin(), _esq.end(), 0.0f);
    std::fill(_dir.begin(), _dir.end(), 0.0f);
    _escrita = 0;
    PicoEsq = PicoDir = RmsEsq = RmsDir = Correlacao = 0;
    RmsMid = RmsSide = Balanco = 0;
    ClipouEsq = ClipouDir = false;
    LufsM = LufsS = LufsI = MenosInfinito;
}

void AnalisadorAudio::Acompanhar(double posicaoDoPlayer)
{
    if (!_leitor.Aberto()) return;

    double nossa = _leitor.TempoDoUltimoBloco();

    // Saltou para trás, ou ficou muito para trás: reposiciona em vez de
    // decodificar tudo que há no meio.
    if (nossa > posicaoDoPlayer + 1.0 || nossa < posicaoDoPlayer - 3.0)
    {
        _leitor.Posicionar(posicaoDoPlayer);
        std::fill(_esq.begin(), _esq.end(), 0.0f);
        std::fill(_dir.begin(), _dir.end(), 0.0f);
        // Saltar rompe a continuidade que a loudness pressupõe: reinicia.
        ReiniciarLoudness();
        nossa = posicaoDoPlayer;
    }

    // Puxa até alcançar, com teto: se o arquivo for lento de decodificar
    // numa máquina lenta, é melhor ficar um pouco atrás do que travar a
    // interface tentando recuperar tudo de uma vez.
    int puxados = 0;
    while (_leitor.TempoDoUltimoBloco() <= posicaoDoPlayer && puxados < 8)
    {
        std::vector<std::vector<float>> bloco = _leitor.ProximoBloco();
        if (bloco.empty()) break;
        Empilhar(bloco);
        ProcessarLoudness(bloco);
        puxados++;
    }

    if (puxados > 0) Medir();
}

void AnalisadorAudio::Empilhar(const std::vector<std::vector<float>> &bloco)
{
    const std::vector<float> &e = bloco[0];
    const std::vector<float> &d = bloco.size() > 1 ? bloco[1] : bloco[0];
    int n = (int) e.size();

    // Bloco maior que a janela: só o fim interessa.
    int inicio = n > Janela ? n - Janela : 0;
    for (int i = inicio; i < n; i++)
    {
        _esq[_escrita] = e[i];
        _dir[_escrita] = d[i];
        _escrita = (_escrita + 1) % Janela;
    }
}

void AnalisadorAudio::Copiar(std::vector<float> &destinoEsq, std::vector<float> &destinoDir) const
{
    int n = std::min((int) destinoEsq.size(), Janela);
    int inicio = (_escrita - n + Janela) % Janela;
    int ate = std::min(n, Janela - inicio);

    std::copy(_esq.begin() + inicio, _esq.begin() + inicio + ate, destinoEsq.begin());
    std::copy(_dir.begin() + inicio, _dir.begin() + inicio + ate, destinoDir.begin());
    if (ate < n)
    {
        std::copy(_esq.begin(), _esq.begin() + (n - ate), destinoEsq.begin() + ate);
        std::copy(_dir.begin(), _dir.begin() + (n - ate), destinoDir.begin() + ate);
    }
}

void AnalisadorAudio::Medir()
{
    double pe = 0, pd = 0, se = 0, sd = 0, sed = 0, sm = 0, ss = 0;
    bool ce = false, cd = false;
    for (int i = 0; i < Janela; i++)
    {
        float e = _esq[i], d = _dir[i];
        double ae = std::fabs(e), ad = std::fabs(d);
        if (ae > pe) pe = ae;
        if (ad > pd) pd = ad;
        if (ae >= LimiarClip) ce = true;
        if (ad >= LimiarClip) cd = true;
        se += e * e;
        sd += d * d;
        sed += e * d;
        double mid = (e + d) * 0.5, lat = (e - d) * 0.5;
        sm += mid * mid;
        ss += lat * lat;
    }
    PicoEsq = pe;
    PicoDir = pd;
    RmsEsq = std::sqrt(se / Janela);
    RmsDir = std::sqrt(sd / Janela);
    RmsMid = std::sqrt(sm / Janela);
    RmsSide = std::sqrt(ss / Janela);
    ClipouEsq = ce;
    ClipouDir = cd;

    // Balanço por energia: onde está o peso do som entre os canais.
    double tot = se + sd;
    Balanco = tot > 1e-12 ? (sd - se) / tot : 0;

    // Correlação de fase: o número que um goniômetro resume. Negativo
    // avisa que somar os canais em mono cancelaria o som.
    double den = std::sqrt(se * sd);
    Correlacao = den > 1e-12 ? sed / den : 0;
}

// ==================================================================
//  LOUDNESS (BS.1770 / R128)
// ==================================================================
void AnalisadorAudio::ReiniciarLoudness()
{
    int fs = _leitor.Taxa() > 0 ? _leitor.Taxa() : 44100;
    _amostras100 = std::max(1, fs / 10);
    _shE = PonderacaoK::Shelf(fs); _hpE = PonderacaoK::PassaAlta(fs);
    _shD = PonderacaoK::Shelf(fs); _hpD = PonderacaoK::PassaAlta(fs);
    _accE = _accD = 0; _accN = 0;
    _subPos = _subCount = 0;
    std::fill(_subE, _subE + MaxSub, 0.0);
    std::fill(_subD, _subD + MaxSub, 0.0);
    std::fill(_histEnergia.begin(), _histEnergia.end(), 0.0);
    std::fill(_histN.begin(), _histN.end(), 0);
    _blnMono = _leitor.Canais() == 1;
    LufsM = LufsS = LufsI = MenosInfinito;
}

void AnalisadorAudio::ProcessarLoudness(const std::vector<std::vector<float>> &bloco)
{
    if (!_shE) return;
    const std::vector<float> &e = bloco[0];
    const std::vector<float> &d = bloco.size() > 1 ? bloco[1] : bloco[0];
    int n = (int) e.size();
    for (int i = 0; i < n; i++)
    {
        double ke = _hpE->P(_shE->P(e[i]));
        double kd = _hpD->P(_shD->P(d[i]));
        _accE += ke * ke;
        _accD += kd * kd;
        if (++_accN >= _amostras100) FinalizarSub();
    }
}

void AnalisadorAudio::FinalizarSub()
{
    double msE = _accE / _accN, msD = _accD / _accN;
    _accE = _accD = 0; _accN = 0;

    _subE[_subPos] = msE; _subD[_subPos] = msD;
    _subPos = (_subPos + 1) % MaxSub;
    if (_subCount < MaxSub) _subCount++;

    double zm = MediaUltimos(4, _subE) + (_blnMono ? 0 : MediaUltimos(4, _subD));   // 400 ms
    double zs = MediaUltimos(MaxSub, _subE) + (_blnMono ? 0 : MediaUltimos(MaxSub, _subD)); // 3 s
    LufsM = Lufs(zm);
    LufsS = Lufs(zs);

    // Cada sub-bloco fecha um bloco de gate de 400 ms (75% de sobreposição).
    if (_subCount >= 4) { AcrescentarAoGate(zm); RecalcularIntegrado(); }
}

double AnalisadorAudio::MediaUltimos(int quantos, const double *anel) const
{
    int k = std::min(quantos, _subCount);
    if (k == 0) return 0;
    double s = 0;
    for (int i = 0; i < k; i++)
    {
        int idx = (_subPos - 1 - i + MaxSub) % MaxSub;
        s += anel[idx];
    }
    return s / k;
}

void AnalisadorAudio::AcrescentarAoGate(double z)
{
    double l = Lufs(z);
    if (l < HistMin) return;
    int i = std::min(HistBins - 1, (int) ((l - HistMin) / HistPasso));
    _histEnergia[i] += z;
    _histN[i]++;
}

void AnalisadorAudio::RecalcularIntegrado()
{
    // Gate absoluto (−70 LUFS): já aplicado na entrada do histograma.
    double soma = 0; int n = 0;
    for (int i = 0; i < HistBins; i++) { soma += _histEnergia[i]; n += _histN[i]; }
    if (n == 0) { LufsI = MenosInfinito; return; }

    // Gate relativo: média dos sobreviventes menos 10 LU. Entra o bin
    // cujo centro está no limiar ou acima.
    double rel = Lufs(soma / n) - 10.0;
    soma = 0; n = 0;
    for (int i = 0; i < HistBins; i++)
    {
        if (HistMin + (i + 0.5) * HistPasso < rel) continue;
        soma += _histEnergia[i];
        n += _histN[i];
    }
    LufsI = n == 0 ? MenosInfinito : Lufs(soma / n);
}

double AnalisadorAudio::Lufs(double z)
{
    return z > 1e-12 ? -0.691 + 10.0 * std::log10(z) : MenosInfinito;
}

double AnalisadorAudio::Db(double v)
{
    return v <= 1e-7 ? -140.0 : 20.0 * std::log10(v);
}

} // namespace ARCA::Modelo

// ARCA no xOS - Vistas/EspectroAnalise (de Vistas\EspectroAnalise.cs).
#pragma once

#include "Instrumentos.h"

#include <memory>
#include <vector>

namespace ARCA::Vistas {

/// <summary>
/// O espectro como INSTRUMENTO, não como enfeite — o "modo Análise" da
/// pesquisa. Sobre a mesma FFT do <see cref="Fft"/>, entrega o que um
/// profissional sente falta na hora: os dois canais (ou Mid/Side), régua
/// logarítmica de frequência COM rótulos, faixa dinâmica escolhível
/// (60/90/120 dB), cursor que lê Hz + dB + nota musical, congelar, e três
/// traços (linha, área, barras) sobre os mesmos dados.
///
/// **Por que cabe na máquina.** A FFT é minúscula para o E8400; o custo real
/// é encher pixel, e por isso a curva é desenhada numa superfície pequena
/// que a placa amplia — e uma linha enche muito menos que barra. A régua, os
/// rótulos e o cursor são camadas WPF por cima, nítidas e sem tocar na placa.
/// Como as abas mostram um visualizador por vez, este é o único desenhando.
/// </summary>
class EspectroAnalise : public Grid, public IInstrumento
{
public:
    EspectroAnalise();

    void Atualizar(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir) override;
    UIElementPtr Elemento() override { return shared_from_this(); }

private:
    // --- superfície da curva (CPU, ampliada pela placa) ---
    static constexpr int LargInterna = 1024;
    static constexpr int AltInterna = 360;

    std::shared_ptr<Image> _img = std::make_shared<Image>();
    std::shared_ptr<Canvas> _grade = std::make_shared<Canvas>();
    std::shared_ptr<Canvas> _sobre = std::make_shared<Canvas>();
    std::shared_ptr<Grid> _area = std::make_shared<Grid>();

    std::unique_ptr<Tela> _tela;

    // --- dados espectrais ---
    std::vector<double> _raiaA = std::vector<double>(Modelo::Fft::Tamanho / 2);
    std::vector<double> _raiaB = std::vector<double>(Modelo::Fft::Tamanho / 2);
    std::vector<float> _mono = std::vector<float>(Modelo::AnalisadorAudio::Janela);
    std::vector<float> _side = std::vector<float>(Modelo::AnalisadorAudio::Janela);

    std::vector<double> _colA, _colB;     // dB por coluna, já suavizado
    std::vector<double> _pico;            // retenção de pico da curva primária
    std::vector<int> _mapaIni, _mapaFim;  // coluna -> faixa de raias
    int _taxaDoMapa = 0;
    int _taxa = 44100;

    // --- opções ---
    int _modoCanal = 0;   // 0 = L/R ; 1 = Mono ; 2 = Mid/Side
    int _traco = 0;       // 0 = Linha ; 1 = Área ; 2 = Barras
    double _faixa = 90;   // dB visíveis
    bool _congelado = false;

    static constexpr double DbPiso = -140.0;

    // --- cursor ---
    std::shared_ptr<Line> _cursor = std::make_shared<Line>();
    std::shared_ptr<TextBlock> _leitura = std::make_shared<TextBlock>();
    bool _temCursor = false;

    // --- harmônicos ---
    std::shared_ptr<Canvas> _harm = std::make_shared<Canvas>();
    static constexpr int MaxHarm = 20;
    std::vector<std::shared_ptr<Line>> _hLin;
    std::vector<std::shared_ptr<TextBlock>> _hRot;
    std::shared_ptr<TextBlock> _f0Texto;
    bool _harmonicos = false;
    std::vector<double> _mag;
    double _f0 = 0;

    void ConstruirHarm();
    void AoHarmonicos(CheckBox &c);
    double DetectarF0();
    double LogProd(int i) const;
    void AtualizarHarmonicos();
    void EsconderHarm();
    void MontarMapa();
    void Reduzir(const std::vector<double> &raias, std::vector<double> &col);
    void Garantir();
    int Y(double db) const;
    void Desenhar();
    void DesenharCurva(const std::vector<double> &col, int traco, int cor, int corArea);
    void ConstruirGrade();
    void AoMover(MouseEventArgs &e);
    void AoSair();
    void PosicionarLeitura();
    UIElementPtr Grupo(const std::string &rotulo, const std::vector<std::string> &opcoes, int marcado,
                       const std::string &grupo, std::function<void(RadioButton &)> ao);
    void AoCanal(RadioButton &r);
    void AoFaixa(RadioButton &r);
    void AoTraco(RadioButton &r);
};

} // namespace ARCA::Vistas

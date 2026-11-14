// ARCA no xOS - Vistas/EspectrogramaArquivo (de Vistas\EspectrogramaArquivo.cs).
#pragma once

#include "Tela.h"
#include "Modelo/EspectrogramaCache.h"

#include <memory>
#include <vector>

namespace ARCA::Vistas {

/// <summary>
/// O espectrograma do arquivo — frequência (Y, log ou linear), tempo (X) e
/// intensidade (cor). Lê a matriz pronta do <see cref="EspectrogramaCache"/>
/// e colore; só redesenha ao mudar zoom, escala, cor ou tamanho — o playhead
/// corre por cima sem redesenhar. É a ferramenta forense da lista: revela
/// ruído persistente, cortes de banda, artefato de codec e a evolução do som.
/// </summary>
class EspectrogramaArquivo : public Grid
{
public:
    EspectrogramaArquivo();

    Evento<double> Buscou;

    void Definir(std::shared_ptr<Modelo::EspectrogramaCache> eg);
    /// <summary>A análise falhou: diz o porquê, em vez de "analisando…" para sempre.</summary>
    void MostrarFalha(const std::string &strMotivo);
    void PosicaoSegundos(double valor);

private:
    std::shared_ptr<Image> _img = std::make_shared<Image>();
    std::shared_ptr<Canvas> _sobre = std::make_shared<Canvas>();
    std::shared_ptr<Grid> _area = std::make_shared<Grid>();
    std::shared_ptr<Line> _playhead = std::make_shared<Line>();
    std::shared_ptr<TextBlock> _leitura = std::make_shared<TextBlock>();
    std::shared_ptr<TextBlock> _aviso = std::make_shared<TextBlock>();

    std::unique_ptr<Tela> _tela;
    std::shared_ptr<Modelo::EspectrogramaCache> _eg;

    double _ci = 0, _cf = 0;          // janela visível, em colunas
    bool _log = true;                 // escala de frequência
    int _mapa = 0;                    // 0 quente, 1 cinza, 2 fogo
    double _posSeg = 0;

    std::vector<int> _rb0, _rb1;      // por linha de tela: faixa de raias
    std::vector<uint8_t> _colRed;     // [x*Bins+bin]: máx no tempo da coluna x
    int _colRedLarg = 0;

    bool _arrastando = false, _moveu = false;
    Point _mouseIni;
    double _mouseUltimoX = 0;

    void RecriarTela();
    void MontarMapaLinhas(int h);
    double FreqDeFrac(double frac, double fMin, double fMax, double logMin, double logMax) const;
    void Redesenhar();
    void PosicionarPlayhead();
    void AoRoda(MouseWheelEventArgs &e);
    void Deslocar(double cols);
    void Ajustar();
    void AoApertar(MouseButtonEventArgs &e);
    void AoMover(MouseEventArgs &e);
    void AtualizarLeitura(Point p);
    void AoSoltar(MouseButtonEventArgs &e);
    void AoEscala(RadioButton &r);
    void AoCor(RadioButton &r);
    UIElementPtr Grupo(const std::string &rotulo, const std::vector<std::string> &opcoes, int marcado,
                       const std::string &grupo, std::function<void(RadioButton &)> ao);
};

// os textos de tempo, frequência e nota (o original repetia os mesmos em cada visualizador)
std::string TempoSegundos(double s);        // "m:ss,s"
std::string TextoFreq(double f);            // "440 Hz", "1,25 kHz"
std::string Nota(double f);                 // "Lá4"

} // namespace ARCA::Vistas

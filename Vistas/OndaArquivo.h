// ARCA no xOS - Vistas/OndaArquivo (de Vistas\OndaArquivo.cs).
#pragma once

#include "Tela.h"
#include "Modelo/OndaCache.h"

#include <memory>
#include <vector>

namespace ARCA::Vistas {

/// <summary>
/// A onda do arquivo inteiro, navegável — a categoria "visualizador de
/// arquivo/timeline". Desenha do <see cref="OndaCache"/>: min/máx por
/// coluna (nenhum transiente some), RMS por baixo, clipping em vermelho,
/// canais separados, escala linear ou dBFS.
///
/// **Por que cabe na GT210.** Nada aqui é por quadro. A onda só é
/// redesenhada quando o zoom, o pan ou o tamanho mudam — e desenhar é
/// traçar colunas numa superfície de software, sem a placa. O playhead, que
/// se move durante a reprodução, é uma linha leve por cima, movida sem
/// redesenhar a onda.
/// </summary>
class OndaArquivo : public Grid
{
public:
    OndaArquivo();

    /// <summary>Usuário clicou para buscar. Segundos.</summary>
    Evento<double> Buscou;

    /// <summary>Escala dBFS (true) ou linear (false).</summary>
    bool Db() const { return _db; }
    void Db(bool valor);

    void Definir(std::shared_ptr<Modelo::OndaCache> oc);
    /// <summary>A análise falhou: diz o porquê, em vez de "analisando…" para sempre.</summary>
    void MostrarFalha(const std::string &strMotivo);
    void Limpar();

    /// <summary>Posição da reprodução, em segundos. Move só o playhead.</summary>
    void PosicaoSegundos(double valor);

private:
    std::shared_ptr<Image> _img = std::make_shared<Image>();
    std::shared_ptr<Canvas> _sobre = std::make_shared<Canvas>();
    std::shared_ptr<Line> _playhead = std::make_shared<Line>();
    std::shared_ptr<TextBlock> _aviso = std::make_shared<TextBlock>();

    std::unique_ptr<Tela> _tela;
    std::shared_ptr<Modelo::OndaCache> _oc;

    int64_t _ini = 0, _fim = 0;          // janela visível, em amostras
    bool _db = false;                    // escala dBFS quando true

    std::vector<float> _mn, _mx, _rms;
    std::vector<bool> _clip;

    // Arrasto: distinguir clique (buscar) de pan.
    bool _arrastando = false;
    bool _moveu = false;
    Point _mouseIni;
    double _mouseUltimoX = 0;

    double _ganhoV = 1.0;      // zoom vertical (Ctrl+roda)

    // Seleção (Shift+arrastar), em amostras. -1 = sem seleção.
    bool _selecionando = false;
    int64_t _selAncora = 0, _selIni = -1, _selFim = -1;
    std::shared_ptr<Rectangle> _selRect = std::make_shared<Rectangle>();
    std::shared_ptr<TextBlock> _selTexto = std::make_shared<TextBlock>();
    std::vector<float> _s1mn = std::vector<float>(1), _s1mx = std::vector<float>(1), _s1rms = std::vector<float>(1);
    std::vector<bool> _s1cl = std::vector<bool>(1);

    // Marcadores (duplo-clique), em segundos.
    std::vector<double> _marcas;
    std::vector<std::shared_ptr<Line>> _marcaLinhas;

    std::shared_ptr<TextBlock> _hintV = std::make_shared<TextBlock>();

    double _posSeg = 0;

    void RecriarTela();
    void Redesenhar();
    double Mapear(float a) const;
    void PosicionarPlayhead();
    void AoRoda(MouseWheelEventArgs &e);
    void Deslocar(int64_t amostras);
    void Ajustar();
    void AoApertar(MouseButtonEventArgs &e);
    void AoMover(MouseEventArgs &e);
    void AoSoltar(MouseButtonEventArgs &e);
    void AoDireito(MouseButtonEventArgs &e);
    int64_t PosParaAmostra(double x) const;
    double AmostraParaX(int64_t a) const;
    void LimparSelecao();
    void AtualizarMarcasSelecao();
    void PosicionarSelecao();
    void AdicionarMarca(double seg);
    void PosicionarMarcas();
    void AtualizarHintV();
    static std::string Tempo(double s);
};

} // namespace ARCA::Vistas

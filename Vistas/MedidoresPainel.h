// ARCA no xOS - Vistas/MedidoresPainel (de Vistas\MedidoresPainel.cs: a classe Medidores).
#pragma once

#include "Instrumentos.h"

#include <limits>
#include <memory>

namespace ARCA::Vistas {

/// <summary>
/// A estação de medidores: nível, dinâmica e estéreo, tudo à vista de uma
/// vez. São NÚMEROS, não desenho por pixel — retângulos e texto, quase de
/// graça na placa —, então cabem todos juntos numa aba só, que é como um
/// operador de fato quer ler: simultâneos, não um por vez.
///
/// Cada medida sai da mesma passada única do <see cref="AnalisadorAudio"/>.
/// Crest é pico − RMS; DR é o crest ao longo do tempo; clipping é o pico
/// batendo no teto — o mesmo motor, leituras diferentes.
/// </summary>
class Medidores : public Grid, public IInstrumento
{
public:
    Medidores();

    /// <summary>Números do arquivo inteiro (do OndaCache), fixos por faixa.</summary>
    void DefinirArquivo(double integrado, double lra, double pico);

    void Atualizar(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir) override;
    UIElementPtr Elemento() override { return shared_from_this(); }

private:
    struct Barra
    {
        std::shared_ptr<Grid> Trilha;
        std::shared_ptr<Rectangle> Enche;
        std::shared_ptr<Rectangle> Marca;
        std::shared_ptr<TextBlock> Valor;
        bool Bipolar = false;
        double EscMin = 0, EscMax = 0;
        bool ComHold = false;
        bool AlertaTopo = false;
        Brush CorNormal;
        double Hold = 0;

        void Def(double v, const std::string &txt);
    };

    std::shared_ptr<Barra> _picoE, _picoD, _rmsE, _rmsD;
    std::shared_ptr<Barra> _cristaE, _cristaD, _dr;
    std::shared_ptr<Barra> _corr, _bal, _mid, _side;
    std::shared_ptr<Barra> _lufsM, _lufsS, _lufsI;
    std::shared_ptr<Barra> _arqI, _arqLra, _arqPico;
    std::shared_ptr<Rectangle> _luzE, _luzD;
    int _holdE = 0, _holdD = 0;
    double _drSuav = 0;
    double _fI = -std::numeric_limits<double>::infinity(), _fLra = 0, _fPico = -std::numeric_limits<double>::infinity();
    Modelo::AnalisadorAudio *_a = nullptr;

    static std::shared_ptr<StackPanel> Secao();
    UIElementPtr CabecalhoLoudness();
    void AoZerar();
    static std::shared_ptr<TextBlock> Cabecalho(const std::string &t);
    static std::shared_ptr<Barra> Linha(const std::shared_ptr<StackPanel> &host, const std::string &rotulo, const Brush &cor,
                                        double escMin, double escMax, bool comHold, bool alerta);
    static std::shared_ptr<Barra> LinhaBipolar(const std::shared_ptr<StackPanel> &host, const std::string &rotulo);
    std::shared_ptr<Grid> LinhaClipping();
    static std::shared_ptr<Grid> LinhaBase(const std::string &rotulo);
    static std::shared_ptr<TextBlock> Valor();
    static std::shared_ptr<TextBlock> Etiqueta(const std::string &t);
    static std::shared_ptr<Rectangle> Luz();
    static std::string FmtDb(double db);
    static std::string FmtSinal(double v);
    static std::string FmtLufs(double v);
};

} // namespace ARCA::Vistas

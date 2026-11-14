// ARCA no xOS - Vistas/InstrumentosShader (de Vistas\InstrumentosShader.cs).
#pragma once

#include "InstrumentoShader.h"

namespace ARCA::Vistas {

/// <summary>
/// Espectro desenhado pela placa.
///
/// A CPU escreve 512 pixels — um por coluna, com a altura da barra no canal
/// vermelho, a do pico no verde e a marca de década no azul. O shader lê
/// essa tira e decide a cor de cada pixel da tela.
/// </summary>
class EspectroShader : public InstrumentoShader
{
public:
    EspectroShader();

protected:
    int Colunas() const override { return Largura; }
    int Linhas() const override { return 1; }
    const char *Glsl() const override;
    void Preencher(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir) override;

private:
    static constexpr int Largura = 512;
    static constexpr double DbMin = -90.0;
    std::vector<double> _raias = std::vector<double>(Modelo::Fft::Tamanho / 2);
    std::vector<double> _picos = std::vector<double>(Largura);
    std::vector<int> _mapaIni, _mapaFim;
    std::vector<bool> _decada;
    int _taxaDoMapa = 0;
    void MontarMapa(int taxa);
};

/// <summary>
/// Forma de onda por canal, desenhada pela placa.
///
/// A tira tem duas linhas: a de cima é o canal esquerdo, a de baixo o
/// direito. Cada coluna guarda o máximo e o mínimo do trecho — desenhar só
/// uma amostra por pixel esconderia os picos.
/// </summary>
class OsciloscopioShader : public InstrumentoShader
{
protected:
    int Colunas() const override { return Largura; }
    int Linhas() const override { return 2; }
    const char *Glsl() const override;
    void Preencher(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir) override;

private:
    static constexpr int Largura = 512;
    void Linha(const std::vector<float> &amostras, int linha);
};

/// <summary>
/// Histograma de nível, desenhado pela placa.
///
/// É o instrumento que denuncia compressão: dinâmica espalha, masterização
/// esmagada empilha tudo perto do topo.
/// </summary>
class HistogramaShader : public InstrumentoShader
{
protected:
    int Colunas() const override { return Faixas; }
    int Linhas() const override { return 1; }
    const char *Glsl() const override;
    void Preencher(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir) override;

private:
    static constexpr int Faixas = 60;          // -60 dB a 0
    double _conta[Faixas] = {};
};

} // namespace ARCA::Vistas

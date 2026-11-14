// ARCA no xOS - Vistas/InstrumentoShader (de Vistas\InstrumentoShader.cs).
//
// LINUX: o original era um ShaderEffect do WPF (EfeitoDados) com um pixel shader HLSL compilado em
// tempo de execução pelo d3dcompiler (Interop\D3DCompilerNative.cs, ps_3_0). Aqui: um fragment
// shader GLSL ES 2 (o driver nouveau compila; não há D3DCompilerNative), desenhado pela GT210 numa
// textura do tamanho do instrumento (um FBO) e mostrada pelo NanoVG no lugar do elemento. A ideia é a
// mesma: a CPU escreve uma tira de poucos pixels (a textura de dados) e a placa pinta o resto.
#pragma once

#include "Instrumentos.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ARCA::Vistas {

/// <summary>
/// Instrumento desenhado pela placa. A CPU só preenche a tira de dados.
/// </summary>
class InstrumentoShader : public Border, public IInstrumento
{
public:
    ~InstrumentoShader() override;

    std::string Falha;                  // "" = null
    bool Pronto() const { return _programa != 0; }

    /// <summary>
    /// Compila o shader e prepara a textura. Chamado por quem cria, logo
    /// depois de construir — e não no construtor da base, que rodaria antes
    /// do construtor da classe derivada e leria campos ainda não inicializados.
    ///
    /// Devolve false se o shader não compilar ou o WPF o recusar; aí quem
    /// chama instala a versão em CPU no lugar.
    /// </summary>
    bool Montar();

    void Atualizar(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir) override;
    UIElementPtr Elemento() override { return shared_from_this(); }

    void OnRender(DrawingContext &dc) override;

protected:
    InstrumentoShader();
    virtual int Colunas() const = 0;
    virtual int Linhas() const = 0;
    // LINUX: o Hlsl do original, traduzido para GLSL ES 2 (o corpo do main; uv vem do vertex
    // shader com (0,0) no canto de cima à esquerda, como o TEXCOORD do WPF)
    virtual const char *Glsl() const = 0;

    /// <summary>Escreve um valor na tira. Cada canal carrega uma grandeza.</summary>
    void Definir(int coluna, int linha, uint8_t r, uint8_t g, uint8_t b);
    static uint8_t Byte(double v);
    virtual void Preencher(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir) = 0;

private:
    std::vector<uint8_t> _tira;         // RGBA, Colunas x Linhas
    unsigned _programa = 0, _texDados = 0, _vbo = 0;
    int _locDados = -1, _locQuadro = -1;
    unsigned _fbo = 0, _texAlvo = 0;
    int _larguraAlvo = 0, _alturaAlvo = 0;
    int _imagemNvg = 0;
    double _quadro = 0;
    bool _sujo = false;                 // a tira mudou desde o último desenho
};

} // namespace ARCA::Vistas

// ARCA no xOS - Vistas/Instrumentos (de Vistas\Instrumentos.cs; a interface IInstrumento vem de
// Vistas\InstrumentoShader.cs, onde o original a declarava).
#pragma once

#include "Tela.h"
#include "Modelo/AnalisadorAudio.h"
#include "Modelo/Fft.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace ARCA::Vistas {

// LINUX: o v.ToString("0.0", CultureInfo.CurrentCulture) dos instrumentos (pt-BR: vírgula decimal,
// sem separador de milhar). largura > 0 completa com zeros à esquerda ("00.0" = Fixo(v, 1, 4)).
inline std::string Fixo(double v, int casas, int largura = 0)
{
    char b[64];
    snprintf(b, sizeof b, "%0*.*f", largura, casas, v);
    for (char *p = b; *p; p++)
        if (*p == '.') *p = ',';
    std::string s = b;
    if (s == "-0" || s.compare(0, 3, "-0,") == 0)
    {
        // o .NET não escreve "-0,0": um número que arredonda para zero sai sem sinal
        bool zero = true;
        for (char c : s.substr(1))
            if (c != '0' && c != ',') zero = false;
        if (zero) s = s.substr(1);
    }
    return s;
}

/// <summary>O que a vista precisa saber de um instrumento, seja ele de CPU ou de placa.</summary>
// LINUX: no C# o IInstrumento era também o UIElement (Image ou Border); aqui a vista guarda o
// elemento à parte (Elemento()), porque o shared_ptr do C++ não converte entre as duas bases.
class IInstrumento
{
public:
    virtual ~IInstrumento() = default;
    virtual void Atualizar(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir) = 0;
    virtual UIElementPtr Elemento() = 0;
};

/// <summary>
/// Base dos instrumentos.
///
/// **Resolução interna fixa, independente do tamanho na tela.** A primeira
/// versão desenhava na resolução do controle, e na tela de 1920 os quatro
/// instrumentos somavam 1,2 milhão de pixels — limpos e reenviados a cada
/// quadro por um laço em C#. Num Core 2 Duo isso engasga.
///
/// Agora cada instrumento desenha numa superfície pequena e deixa a placa
/// ampliar. **Ampliar é a única parte disto que a GPU faz de fato**, e ela
/// faz de graça: o desenho é todo da CPU, porque WriteableBitmap é
/// rasterização por software. Sem um shader, a GT210 só carrega a imagem
/// pronta.
/// </summary>
class Instrumento : public Image, public IInstrumento
{
public:
    void Atualizar(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir) override;
    UIElementPtr Elemento() override { return shared_from_this(); }

protected:
    std::unique_ptr<Tela> T;
    static const int Fundo, Grade, GradeForte;

    Instrumento();
    virtual int LarguraInterna() const = 0;
    virtual int AlturaInterna() const = 0;
    virtual void Desenhar(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir) = 0;

private:
    void Garantir();
};

/// <summary>
/// Goniômetro — o vetorscópio do áudio.
///
/// Cada amostra vira um ponto: horizontal é a diferença entre os canais,
/// vertical é a soma. Som mono desenha uma linha vertical; estéreo amplo
/// abre em nuvem; oposição de fase deita a figura na horizontal — e aí
/// somar em mono cancelaria o som.
///
/// O rastro vem do esmaecimento, não de guardar histórico: é o mesmo
/// princípio do fósforo de um tubo.
/// </summary>
class Goniometro : public Instrumento
{
protected:
    // 256 e não 300: o esmaecimento percorre a superfície inteira a cada
    // quadro, e é o maior custo de CPU que sobrou no módulo. Reduzir o lado
    // corta um terço dos pixels sem prejuízo visível — a figura é ampliada
    // pela placa de qualquer forma.
    int LarguraInterna() const override { return 256; }
    int AlturaInterna() const override { return 256; }
    void Desenhar(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir) override;
};

/// <summary>Forma de onda por canal, um sobre o outro.</summary>
class Osciloscopio : public Instrumento
{
protected:
    int LarguraInterna() const override { return 512; }
    int AlturaInterna() const override { return 220; }
    void Desenhar(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir) override;

private:
    static const int CorEsq, CorDir;
    void Traco(const std::vector<float> &amostras, int topo, int altura, int cor);
};

/// <summary>Espectro em escala logarítmica de frequência, com retenção de pico.</summary>
class Espectro : public Instrumento
{
protected:
    int LarguraInterna() const override { return 512; }
    int AlturaInterna() const override { return 220; }
    void Desenhar(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir) override;

private:
    static const int CorBarra, CorPico;
    static constexpr double DbMin = -90.0;
    std::vector<double> _raias = std::vector<double>(Modelo::Fft::Tamanho / 2);
    std::vector<double> _picos;
    std::vector<int> _mapaIni, _mapaFim;   // coluna -> faixa de raias
    std::vector<int> _decadas;
    int _taxaDoMapa = 0;
    void MontarMapa(int taxa);
};

/// <summary>
/// Histograma de nível: quantas amostras caem em cada faixa de dB.
///
/// É o instrumento que denuncia compressão. Uma gravação com dinâmica
/// espalha; uma esmagada na masterização empilha tudo perto do topo.
/// </summary>
class Histograma : public Instrumento
{
protected:
    int LarguraInterna() const override { return 300; }
    int AlturaInterna() const override { return 200; }
    void Desenhar(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir) override;

private:
    static const int CorBarra;
    static constexpr int Faixas = 60;                  // -60 dB a 0
    double _conta[Faixas] = {};
};

} // namespace ARCA::Vistas

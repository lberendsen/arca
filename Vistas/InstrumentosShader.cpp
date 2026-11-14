// ARCA no xOS - Vistas/InstrumentosShader (de Vistas\InstrumentosShader.cs).
// LINUX: os pixel shaders HLSL (ps_3_0) traduzidos para GLSL ES 2, linha por linha: tex2D ->
// texture2D, lerp -> mix, saturate -> clamp(.., 0, 1), frac -> fract, float3/float4 -> vec3/vec4. O
// step(a, b) tem a mesma ordem de argumentos nas duas linguagens.
#include "InstrumentosShader.h"

#include <algorithm>
#include <cmath>

namespace ARCA::Vistas {

using Modelo::AnalisadorAudio;
using Modelo::Fft;

// ===============================================================
EspectroShader::EspectroShader()
{
    for (int i = 0; i < Largura; i++) _picos[i] = DbMin;
}

const char *EspectroShader::Glsl() const
{
    return
        "void main()\n"
        "{\n"
        "    vec4 d = texture2D(dados, vec2(uv.x, 0.5));\n"
        "    float y = 1.0 - uv.y;                 // 0 embaixo, 1 em cima\n"
        "\n"
        "    vec3 cor = vec3(0.090, 0.071, 0.043) + d.b * vec3(0.17, 0.13, 0.08);   // linha de decada\n"
        "\n"
        "    // Corpo da barra, com degrade: mais escuro embaixo, vivo no topo.\n"
        "    float dentro = step(y, d.r);\n"
        "    float t = clamp(y / max(d.r, 0.001), 0.0, 1.0);\n"
        "    vec3 corBarra = mix(vec3(0.42, 0.22, 0.03), vec3(1.00, 0.62, 0.17), t);\n"
        "    cor = mix(cor, corBarra, dentro);\n"
        "\n"
        "    // Marca de pico, que cai devagar.\n"
        "    float pico = 1.0 - smoothstep(0.0, 0.014, abs(y - d.g));\n"
        "    cor = mix(cor, vec3(1.00, 0.94, 0.80), pico * step(0.002, d.g));\n"
        "\n"
        "    gl_FragColor = vec4(cor, 1.0);\n"
        "}\n";
}

void EspectroShader::MontarMapa(int taxa)
{
    if (_taxaDoMapa == taxa && !_mapaIni.empty()) return;
    _taxaDoMapa = taxa;

    _mapaIni.assign(Largura, 0);
    _mapaFim.assign(Largura, 0);
    _decada.assign(Largura, false);

    double nyquist = taxa / 2.0;
    double logMin = std::log10(20.0);
    double logMax = std::log10(std::min(20000.0, nyquist));

    for (int x = 0; x < Largura; x++)
    {
        double f0 = std::pow(10, logMin + (logMax - logMin) * x / Largura);
        double f1 = std::pow(10, logMin + (logMax - logMin) * (x + 1.0) / Largura);
        int i0 = (int) (f0 * Fft::Tamanho / taxa);
        int i1 = (int) (f1 * Fft::Tamanho / taxa);
        if (i1 <= i0) i1 = i0 + 1;
        if (i0 < 0) i0 = 0;
        if (i1 > (int) _raias.size()) i1 = (int) _raias.size();
        _mapaIni[x] = i0;
        _mapaFim[x] = i1;
    }

    for (double f : {100.0, 1000.0, 10000.0})
    {
        int x = (int) ((std::log10(f) - logMin) / (logMax - logMin) * (Largura - 1));
        if (x >= 0 && x < Largura) _decada[x] = true;
    }
}

void EspectroShader::Preencher(AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir)
{
    if (a.Taxa() <= 0) return;
    MontarMapa(a.Taxa());
    Fft::EspectroEstereo(esq, &dir, _raias);   // os dois canais, não só o esquerdo

    for (int x = 0; x < Largura; x++)
    {
        int i0 = _mapaIni[x], i1 = _mapaFim[x];
        double db = DbMin;
        for (int i = i0; i < i1; i++) if (_raias[i] > db) db = _raias[i];

        if (db > _picos[x]) _picos[x] = db;
        else _picos[x] -= 0.35;
        if (_picos[x] < DbMin) _picos[x] = DbMin;

        Definir(x, 0,
                Byte((db - DbMin) / -DbMin),
                Byte((_picos[x] - DbMin) / -DbMin),
                _decada[x] ? (uint8_t) 255 : (uint8_t) 0);
    }
}

// ===============================================================
const char *OsciloscopioShader::Glsl() const
{
    return
        "void main()\n"
        "{\n"
        "    // Metade de cima e' o canal esquerdo, metade de baixo o direito.\n"
        "    float canal = step(0.5, uv.y);\n"
        "    vec4 d = texture2D(dados, vec2(uv.x, canal * 0.5 + 0.25));\n"
        "\n"
        "    float yl = fract(uv.y * 2.0);     // 0..1 dentro da metade\n"
        "    float v  = 1.0 - yl;              // 0 embaixo, 1 em cima\n"
        "\n"
        "    // Linha do zero.\n"
        "    vec3 cor = vec3(0.090, 0.071, 0.043) + (1.0 - smoothstep(0.0, 0.012, abs(v - 0.5))) * vec3(0.20, 0.16, 0.10);\n"
        "\n"
        "    // Entre o minimo e o maximo daquela coluna, com espessura minima: em\n"
        "    // silencio ou trecho baixo, minimo e maximo quase coincidem e a onda\n"
        "    // sumia. Assim ela vira um traco fino, como num osciloscopio.\n"
        "    float e = 0.006;\n"
        "    float dentro = step(d.g - e, v) * step(v, d.r + e);\n"
        "    vec3 corCanal = mix(vec3(1.00, 0.62, 0.17), vec3(0.94, 0.89, 0.68), canal);\n"
        "    cor = mix(cor, corCanal, dentro);\n"
        "\n"
        "    gl_FragColor = vec4(cor, 1.0);\n"
        "}\n";
}

void OsciloscopioShader::Preencher(AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir)
{
    Linha(esq, 0);
    Linha(dir, 1);
}

void OsciloscopioShader::Linha(const std::vector<float> &amostras, int linha)
{
    int n = (int) amostras.size();
    if (n == 0) return;
    for (int x = 0; x < Largura; x++)
    {
        int i0 = (int) ((int64_t) x * n / Largura);
        int i1 = (int) ((int64_t) (x + 1) * n / Largura);
        if (i1 <= i0) i1 = i0 + 1;
        if (i1 > n) i1 = n;

        float min = 1.0f, max = -1.0f;
        for (int i = i0; i < i1; i++)
        {
            float v = amostras[i];
            if (v < min) min = v;
            if (v > max) max = v;
        }
        // De -1..1 para 0..1, que é o que cabe num canal de 8 bits.
        Definir(x, linha, Byte((max + 1.0) / 2.0), Byte((min + 1.0) / 2.0), 0);
    }
}

// ===============================================================
const char *HistogramaShader::Glsl() const
{
    return
        "void main()\n"
        "{\n"
        "    vec4 d = texture2D(dados, vec2(uv.x, 0.5));\n"
        "    float y = 1.0 - uv.y;\n"
        "\n"
        "    vec3 cor = vec3(0.090, 0.071, 0.043) + d.b * vec3(0.26, 0.20, 0.12);   // marca dos -20 dB\n"
        "    float dentro = step(y, d.r);\n"
        "    float t = clamp(y / max(d.r, 0.001), 0.0, 1.0);\n"
        "    vec3 corBarra = mix(vec3(0.36, 0.22, 0.08), vec3(0.94, 0.78, 0.50), t);\n"
        "    cor = mix(cor, corBarra, dentro);\n"
        "\n"
        "    gl_FragColor = vec4(cor, 1.0);\n"
        "}\n";
}

void HistogramaShader::Preencher(AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir)
{
    for (int i = 0; i < Faixas; i++) _conta[i] *= 0.90;

    // Uma amostra em cada quatro: a distribuição não muda e poupa três
    // quartos dos logaritmos.
    for (size_t i = 0; i < esq.size(); i += 4)
    {
        double v = std::max(std::fabs(esq[i]), std::fabs(dir[i]));
        if (v <= 0.001) continue;
        int faixa = Faixas - 1 + (int) (20.0 * std::log10(v));
        if (faixa < 0) faixa = 0;
        else if (faixa >= Faixas) faixa = Faixas - 1;
        _conta[faixa] += 1.0;
    }

    double maior = 1.0;
    for (int i = 0; i < Faixas; i++) if (_conta[i] > maior) maior = _conta[i];

    for (int i = 0; i < Faixas; i++)
        Definir(i, 0, Byte(_conta[i] / maior), 0, i == Faixas - 20 ? (uint8_t) 255 : (uint8_t) 0);
}

} // namespace ARCA::Vistas

// ARCA no xOS - Modelo/EspectrogramaCache (de Modelo\EspectrogramaCache.cs).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ARCA::Modelo {

/// <summary>
/// O espectrograma do arquivo inteiro, pré-calculado e cacheado — a
/// categoria "preservação" da análise. Frequência × tempo × intensidade,
/// impossível de recalcular a cada quadro numa GT210; então é uma STFT
/// feita UMA vez sobre o arquivo, quantizada em 1 byte por raia por coluna,
/// guardada num sidecar. Desenhar depois é só ler a matriz e colorir —
/// nenche pixel, mas só quando o zoom muda, nunca por quadro.
///
/// **FFT própria.** A <see cref="Fft"/> estática usa buffers compartilhados
/// e roda na thread da interface (espectro ao vivo). A geração aqui roda em
/// thread de fundo, então traz a própria FFT — senão as duas se atropelariam.
///
/// **Tamanho limitado.** Um arquivo longo geraria colunas demais. Ao passar
/// do teto, as colunas existentes são mescladas duas a duas pelo máximo (o
/// máximo preserva tons e transientes) e o hop efetivo dobra. Cabe em
/// processo de 32 bits em qualquer duração, com a resolução temporal caindo
/// suavemente só nos arquivos muito longos.
/// </summary>
class EspectrogramaCache
{
public:
    // "ESG3": escala corrigida em 02/10/2026 (+6 dB, ganho da janela Hann).
    // Os "ESG2" estão na escala antiga, e os "ESG1" podem estar truncados
    // (ver OndaCache.Magica): os dois são refeitos.
    static constexpr int32_t Magica = 0x33475345;    // "ESG3"
    static constexpr int FftTam = 2048;
    static constexpr int HopBase = 1024;             // 50% de sobreposição
    static constexpr int Bins = FftTam / 2;          // 1024 raias úteis

    int Taxa = 0;
    int HopEfetivo = 0;
    int Colunas = 0;
    int64_t TotalAmostras = 0;

    /// <summary>Intensidade quantizada (0..255) da raia na coluna. 0 fora dos limites.</summary>
    uint8_t Valor(int coluna, int bin) const
    {
        if (coluna < 0 || coluna >= Colunas || bin < 0 || bin >= Bins) return 0;
        return _dados[(size_t) coluna * Bins + bin];
    }

    static double DbDeByte(uint8_t v);
    double SegundosDeColuna(double coluna) const;
    double ColunaDeSegundos(double seg) const;
    double FreqDeBin(double bin) const;

    /// <summary>
    /// STFT do arquivo inteiro. null se cancelar, ou se a decodificação
    /// falhar no meio — aí com o motivo em strFalha, para não salvar em
    /// cache um espectrograma truncado.
    /// </summary>
    static std::shared_ptr<EspectrogramaCache> Gerar(const std::string &arquivo, std::function<void(double)> progresso,
                                                     std::function<bool()> cancelou, std::string &strFalha);

    // LINUX: %LOCALAPPDATA%\ARCA\Espectros vira <ARCA_CACHE ou /var/cache/arca>/Espectros
    static std::string PastaCache();
    static std::string CaminhoSidecar(const std::string &arquivo);
    bool Salvar(const std::string &arquivoOrigem) const;
    static std::shared_ptr<EspectrogramaCache> Carregar(const std::string &arquivoOrigem);

private:
    static constexpr int MaxColunas = 24000;     // teto antes de mesclar
    static constexpr double DbMin = -100.0;      // piso da quantização

    std::vector<uint8_t> _dados;   // [coluna * Bins + bin], dB quantizado 0..255
};

} // namespace ARCA::Modelo

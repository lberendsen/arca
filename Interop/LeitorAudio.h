// ARCA no xOS - Interop/LeitorAudio (de Interop\LeitorAudio.cs).
// LINUX: o original decodificava pelo IWMSyncReader do wmvcore.dll (WMA, WMV, MP3, WAV: o que o
// Windows Media Format soubesse abrir). Aqui: FLAC pela libFLAC (com a conferência do MD5 do
// STREAMINFO ligada); o resto (MP3, WMA antigos, WAV, vídeos) pelo FFmpeg (libavformat,
// libavcodec, libswresample). A API e o PCM entregue são os mesmos: intercalado, little-endian,
// inteiro, no formato do arquivo.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ARCA::Interop {

/// Decodifica áudio para PCM.
///
/// É daqui que saem as amostras dos medidores e da forma de onda. A
/// alternativa era um plugin DSP, que exigiria um servidor COM registrado
/// na máquina — e a sonda mostrou que não há nenhum DSP registrado ali, nem
/// a categoria no registro, então nem se sabia se o WMP carregaria o nosso.
///
/// Há também um argumento de mérito: isto mede **o que está na gravação**,
/// não o que a cadeia de saída do player fez com ela.
///
/// Medido: 64x o tempo real numa máquina moderna, e basta 1x.
class LeitorAudio
{
public:
    LeitorAudio();
    ~LeitorAudio();                   // Dispose() -> Fechar()
    LeitorAudio(const LeitorAudio &) = delete;
    LeitorAudio &operator=(const LeitorAudio &) = delete;

    int Canais() const { return m_intCanais; }
    int Taxa() const { return m_intTaxa; }
    int Bits() const { return m_intBits; }
    const std::string &Falha() const { return m_strFalha; }      // "" = null no original
    bool Aberto() const;

    /// true só quando o arquivo acabou de verdade.
    /// Os ProximoBloco devolvem vazio (null no original) tanto no fim quanto em erro; quem
    /// guarda o resultado (onda, espectrograma) tem de conferir isto, ou
    /// um erro no meio da faixa vira um cache truncado que parece bom.
    bool Terminou() const { return m_blnTerminou; }

    /// Duração que o cabeçalho declara, em segundos; 0 se não diz.
    /// Arquivo cortado no fim termina "normalmente" para o decodificador
    /// — só comparando com isto se percebe que falta um pedaço.
    // LINUX: no FLAC, o total de amostras do STREAMINFO; no resto, a duração que o FFmpeg lê do
    // contêiner (no WMA, a do cabeçalho ASF, como antes).
    double DuracaoDeclarada() const { return m_dblDuracaoDeclarada; }

    /// Segundos que faltam entre o que se decodificou e o que o cabeçalho
    /// declara, ou 0 se não falta nada que importe (ou se não se sabe).
    double FaltaNoFim(int64_t lngQuadrosDecodificados) const;

    /// Folga entre a duração declarada e a decodificada que ainda conta como inteira.
    static constexpr double ToleranciaFimSegundos = 1.0;

    /// Tempo, em segundos, do último bloco entregue.
    double TempoDoUltimoBloco() const { return m_dblTempoDoUltimoBloco; }

    bool Abrir(const std::string &arquivo);

    /// Quantos fluxos que não eram o áudio foram desligados (vídeo, roteiro…). 0 num arquivo só de áudio.
    int FluxosDesligados() const { return m_intFluxosDesligados; }

    /// Reposiciona a leitura. Usado quando a reprodução salta.
    bool Posicionar(double segundos);

    /// Próximo bloco exatamente como o decodificador entrega: PCM
    /// intercalado, little-endian, sem conversão nenhuma. É o que as
    /// conferências bit a bit comparam com o CD. Vazio no fim ou em erro
    /// (ver Terminou).
    std::vector<uint8_t> ProximoBlocoBytes();

    /// Próximo bloco decodificado, já separado por canal e normalizado
    /// em -1..1. Devolve vazio no fim do arquivo ou em erro (ver Terminou).
    std::vector<std::vector<float>> ProximoBloco();

    /// Próximo bloco em amostras **inteiras**, exatamente como estão no
    /// arquivo. É o que a análise do arquivo inteiro (onda, loudness,
    /// espectrograma) consome.
    ///
    /// Devolve vazio no fim do arquivo ou em erro — e só Terminou()
    /// diz qual dos dois.
    std::vector<std::vector<int>> ProximoBlocoInteiro(int &quadros);

    void Fechar();

    struct Impl;                      // libFLAC ou FFmpeg (no .cpp)

private:
    std::unique_ptr<Impl> m;
    int m_intCanais = 0, m_intTaxa = 0, m_intBits = 0;
    std::string m_strFalha;
    bool m_blnTerminou = false;
    double m_dblDuracaoDeclarada = 0;
    double m_dblTempoDoUltimoBloco = 0;
    int m_intFluxosDesligados = 0;

    bool LerAmostra(std::vector<uint8_t> &arrBytes);
    std::vector<std::vector<float>> Separar(const uint8_t *p, int bytes) const;
};

} // namespace ARCA::Interop

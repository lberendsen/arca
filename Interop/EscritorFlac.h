// ARCA no xOS - Interop/EscritorFlac (de Interop\EscritorWma.cs).
// LINUX: o original codificava WMA 9.2 Lossless pelo IWMWriter do wmvcore.dll. No xOS o padrão
// é FLAC: libFLAC, nível 8, com a verificação da própria libFLAC ligada (cada quadro é
// decodificado de volta e comparado com o PCM que entrou, durante a codificação). A API é a mesma.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <FLAC/format.h>
#include <FLAC/stream_encoder.h>

namespace ARCA::Interop {

/// O object do KeyValuePair<string, object> do original: null, int (vai como número) ou texto.
using ValorAtributo = std::variant<std::monostate, int, std::string>;

/// Codifica PCM de CD (44,1 kHz, 16 bits, estéreo) em FLAC pela libFLAC.
///
/// O que garante que a troca não regride não é este código: é a
/// conferência bit a bit que a AQUISIÇÃO faz em toda faixa (o FLAC
/// decodificado tem de ter o mesmo SHA-256 do PCM lido do disco).
///
/// Uso: Abrir → Escrever (quantas vezes for) → Concluir.
class EscritorFlac
{
public:
    static constexpr int Taxa = 44100, Canais = 2, Bits = 16;

    EscritorFlac() = default;
    ~EscritorFlac();                  // Dispose() -> Fechar()
    EscritorFlac(const EscritorFlac &) = delete;
    EscritorFlac &operator=(const EscritorFlac &) = delete;

    const std::string &Falha() const { return m_strFalha; }      // "" = null no original

    /// A descrição do formato escolhido (vai para o registro).
    const std::string &Formato() const { return m_strFormato; }

    /// Prepara o arquivo: FLAC 44,1/16/2, nível 8, verificação ligada; entrada PCM no mesmo
    /// formato — sem conversão nenhuma no caminho — e as etiquetas, com os nomes ASF (traduzidos
    /// para Vorbis pelo mesmo mapa do EditorFlac). Textos vazios são omitidos; int vira número.
    bool Abrir(const std::string &strArquivo, const std::vector<std::pair<std::string, ValorAtributo>> &lstAtributos);

    /// Acrescenta PCM (intercalado, little-endian, quadros inteiros).
    bool Escrever(const uint8_t *arrPcm, int intInicio, int intBytes);
    bool Escrever(const std::vector<uint8_t> &arrPcm, int intInicio, int intBytes)
    { return Escrever(arrPcm.data(), intInicio, intBytes); }

    /// Fecha o arquivo (grava o STREAMINFO final, com o MD5 do PCM). false se algo falhou no caminho.
    bool Concluir();

    /// Quadros escritos até agora.
    int64_t Quadros() const { return m_lngQuadros; }

    void Fechar();

    // LINUX: o que vai em WM/EncodedBy (ENCODED-BY) no xOS, pela regra do PORTE.md (seção 4):
    // "ARCA (xOS) / libFLAC <versão>". No original, o ProcessadorExtracaoAudio escrevia
    // "ARCA / Windows Media Format / WMA 9 Lossless".
    static std::string CodificadoPor();

private:
    FLAC__StreamEncoder *m_escritor = nullptr;
    bool m_blnEscrevendo = false;
    int64_t m_lngQuadros = 0;
    std::string m_strFalha;
    std::string m_strFormato;
    std::vector<int32_t> m_arrAmostras;
    FLAC__StreamMetadata *m_arrMetadados[2] = { nullptr, nullptr };   // VORBIS_COMMENT e PADDING

    std::string EstadoDoEscritor() const;
};

} // namespace ARCA::Interop

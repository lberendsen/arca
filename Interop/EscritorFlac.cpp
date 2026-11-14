// ARCA no xOS - Interop/EscritorFlac (de Interop\EscritorWma.cs).
// LINUX: no original, perfil WMA Lossless procurado entre os codecs do Windows (IWMCodecInfo3),
// VBR qualidade 100, entrada PCM por WM_MEDIA_TYPE/WAVEFORMATEX e tags por IWMHeaderInfo. Aqui,
// FLAC__StreamEncoder: nível 8, verificação ligada, MD5 do PCM no STREAMINFO, etiquetas num bloco
// VORBIS_COMMENT e 8 KiB de padding (como o flac de linha de comando e o acervo convertido), para a
// AQUISIÇÃO gravar ISRC, compositor e verificação depois sem reescrever o arquivo.
#include "EscritorFlac.h"

#include "EditorFlac.h"

#include <FLAC/metadata.h>
#include <FLAC/stream_encoder.h>

namespace ARCA::Interop {

namespace {
constexpr int BytesPorQuadro = EscritorFlac::Canais * EscritorFlac::Bits / 8;
constexpr unsigned NivelCompressao = 8;
constexpr unsigned BytesDePadding = 8192;

} // namespace

EscritorFlac::~EscritorFlac() { Fechar(); }

std::string EscritorFlac::CodificadoPor()
{
    return std::string("ARCA (xOS) / libFLAC ") + FLAC__VERSION_STRING;
}

std::string EscritorFlac::EstadoDoEscritor() const
{
    if (!m_escritor) return "";
    FLAC__StreamEncoderState e = FLAC__stream_encoder_get_state(m_escritor);
    std::string s = FLAC__StreamEncoderStateString[e];
    if (e == FLAC__STREAM_ENCODER_VERIFY_MISMATCH_IN_AUDIO_DATA)
    {
        FLAC__uint64 pos;
        uint32_t quadro, canal, amostra;
        FLAC__int32 esperado, obtido;
        FLAC__stream_encoder_get_verify_decoder_error_stats(m_escritor, &pos, &quadro, &canal, &amostra, &esperado, &obtido);
        s += " (amostra " + std::to_string(pos) + ", canal " + std::to_string(canal) + ": esperava " +
             std::to_string(esperado) + ", a verificação decodificou " + std::to_string(obtido) + ")";
    }
    return s;
}

bool EscritorFlac::Abrir(const std::string &strArquivo,
                         const std::vector<std::pair<std::string, ValorAtributo>> &lstAtributos)
{
    Fechar();
    m_strFalha.clear();
    m_lngQuadros = 0;
    try
    {
        m_escritor = FLAC__stream_encoder_new();
        if (m_escritor == nullptr) { m_strFalha = "FLAC__stream_encoder_new devolveu nulo"; return false; }

        // Nível 8 é o "-8" do flac; a verificação decodifica cada quadro de volta durante a
        // codificação e para no primeiro número diferente.
        bool ok = FLAC__stream_encoder_set_verify(m_escritor, true)
               && FLAC__stream_encoder_set_compression_level(m_escritor, NivelCompressao)
               && FLAC__stream_encoder_set_channels(m_escritor, Canais)
               && FLAC__stream_encoder_set_bits_per_sample(m_escritor, Bits)
               && FLAC__stream_encoder_set_sample_rate(m_escritor, Taxa);   // o MD5 do PCM a libFLAC sempre calcula
        if (!ok) { m_strFalha = "a libFLAC recusou a configuração 44,1 kHz, 16 bits, estéreo, nível 8"; Fechar(); return false; }
        m_strFormato = std::string("FLAC (libFLAC ") + FLAC__VERSION_STRING + "), nível 8, 44,1 kHz, 16 bits, estéreo, verificação ligada";

        // As etiquetas, com os nomes ASF de quem chama. LINUX: no original, DefinirAtributo:
        // int ia como DWORD, texto como WMT_TYPE_STRING; no Vorbis tudo é texto, e o nome passa
        // pelo mapa do EditorFlac. Os técnicos do ASF (que o FLAC não guarda) ficam de fora.
        FLAC__StreamMetadata **arr = m_arrMetadados;
        arr[0] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
        arr[1] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PADDING);
        if (!arr[0] || !arr[1]) { m_strFalha = "sem memória para os metadados"; Fechar(); return false; }
        arr[1]->length = BytesDePadding;
        for (const auto &kv : lstAtributos)
        {
            std::string strTexto;
            if (std::holds_alternative<std::monostate>(kv.second)) continue;
            if (std::holds_alternative<int>(kv.second)) strTexto = std::to_string(std::get<int>(kv.second));
            else
            {
                strTexto = std::get<std::string>(kv.second);
                if (strTexto.empty()) continue;
            }
            std::string strNome = EditorFlac::NomeVorbis(kv.first);
            if (strNome.empty()) continue;
            FLAC__StreamMetadata_VorbisComment_Entry e;
            if (!FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(&e, strNome.c_str(), strTexto.c_str())
                || !FLAC__metadata_object_vorbiscomment_append_comment(arr[0], e, false))
            {
                m_strFalha = "etiqueta '" + kv.first + "' recusada pela libFLAC";
                Fechar();
                return false;
            }
        }
        if (!FLAC__stream_encoder_set_metadata(m_escritor, arr, 2))
        { m_strFalha = "a libFLAC recusou os metadados"; Fechar(); return false; }

        FLAC__StreamEncoderInitStatus st = FLAC__stream_encoder_init_file(m_escritor, strArquivo.c_str(), nullptr, nullptr);
        if (st != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
        {
            m_strFalha = std::string("FLAC__stream_encoder_init_file: ") + FLAC__StreamEncoderInitStatusString[st];
            if (st == FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR) m_strFalha += " — " + EstadoDoEscritor();
            Fechar();
            return false;
        }
        m_blnEscrevendo = true;
        return true;
    }
    catch (const std::exception &ex)
    {
        m_strFalha = std::string("exceção: ") + ex.what();
        Fechar();
        return false;
    }
}

/// Acrescenta PCM (intercalado, little-endian, quadros inteiros). O
/// tempo de cada amostra sai da contagem exata de quadros, não de soma
/// de durações, para não acumular arredondamento.
// LINUX: o FLAC não tem tempo por amostra (o IWMWriter pedia cnsSampleTime): a posição é a própria
// contagem de quadros.
bool EscritorFlac::Escrever(const uint8_t *arrPcm, int intInicio, int intBytes)
{
    if (!m_blnEscrevendo) { if (m_strFalha.empty()) m_strFalha = "o escritor não está aberto"; return false; }
    if (intBytes <= 0) return true;
    if (intBytes % BytesPorQuadro != 0) { m_strFalha = "bloco de PCM com quadro incompleto (" + std::to_string(intBytes) + " bytes)"; return false; }
    try
    {
        int intAmostras = intBytes / 2;
        m_arrAmostras.resize(intAmostras);
        const uint8_t *p = arrPcm + intInicio;
        for (int i = 0; i < intAmostras; i++)
            m_arrAmostras[i] = (int16_t) (p[2 * i] | (p[2 * i + 1] << 8));       // 16 bits, little-endian
        if (!FLAC__stream_encoder_process_interleaved(m_escritor, m_arrAmostras.data(), (uint32_t) (intBytes / BytesPorQuadro)))
        {
            m_strFalha = "libFLAC: " + EstadoDoEscritor();
            return false;
        }
        m_lngQuadros += intBytes / BytesPorQuadro;
        return true;
    }
    catch (const std::exception &ex) { m_strFalha = std::string("exceção: ") + ex.what(); return false; }
}

/// Fecha o arquivo. false se algo falhou no caminho.
bool EscritorFlac::Concluir()
{
    if (!m_blnEscrevendo) return false;
    bool ok = FLAC__stream_encoder_finish(m_escritor);
    m_blnEscrevendo = false;
    if (!ok && m_strFalha.empty()) m_strFalha = "ao concluir: libFLAC: " + EstadoDoEscritor();
    bool r = ok && m_strFalha.empty();
    Fechar();
    return r;
}

void EscritorFlac::Fechar()
{
    if (m_escritor != nullptr)
    {
        // Abandonado no meio (erro ou cancelamento): finish fecha o
        // arquivo; quem chamou é que decide apagá-lo.
        if (m_blnEscrevendo) FLAC__stream_encoder_finish(m_escritor);
        FLAC__stream_encoder_delete(m_escritor);
    }
    m_escritor = nullptr;
    m_blnEscrevendo = false;
    // os blocos de metadados vivem até o fim da codificação (a libFLAC não os copia)
    for (FLAC__StreamMetadata *&b : m_arrMetadados)
    {
        if (b) FLAC__metadata_object_delete(b);
        b = nullptr;
    }
}

} // namespace ARCA::Interop

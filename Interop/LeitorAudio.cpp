// ARCA no xOS - Interop/LeitorAudio (de Interop\LeitorAudio.cs).
// LINUX: o IWMSyncReader (GetNextSample, saídas, EnableDiscreteOutput) dá lugar a dois caminhos:
//   - FLAC: FLAC__StreamDecoder, quadro a quadro, com a conferência do MD5 do STREAMINFO ligada;
//   - o resto: FFmpeg (avformat lê o contêiner, avcodec decodifica, swresample só intercala e,
//     nos formatos com perda, que o FFmpeg entrega em ponto flutuante, passa a 16 bits).
// O que se entrega é o mesmo do original: PCM inteiro, intercalado, little-endian, no formato
// original do arquivo (16 ou 24 bits, todos os canais), e um erro no meio nunca vira fim.
#include "LeitorAudio.h"

#include "Modelo/Formato.h"

#include <FLAC/stream_decoder.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ARCA::Interop {

struct LeitorAudio::Impl
{
    int tipo = 0;                     // 0 nada, 1 libFLAC, 2 FFmpeg
    std::string arquivo;

    // ---- libFLAC ----
    FLAC__StreamDecoder *dec = nullptr;
    bool fimFlac = false;             // o decodificador chegou ao fim e foi concluído (finish)
    bool erro = false;
    std::string strErro;
    unsigned bpsArquivo = 0;          // bits por amostra do STREAMINFO
    FLAC__uint64 totalAmostras = 0;
    unsigned canaisArquivo = 0, taxaArquivo = 0;
    int bitsSaida = 0;

    // os bytes do último quadro decodificado, ainda não entregues, e o tempo do começo deles
    std::vector<uint8_t> pendente;
    double tempoPendente = 0;

    // ---- FFmpeg ----
    AVFormatContext *fmt = nullptr;
    AVCodecContext *cod = nullptr;
    SwrContext *swr = nullptr;
    AVPacket *pkt = nullptr;
    AVFrame *frm = nullptr;
    int idx = -1;
    bool eofLeitura = false;
    AVSampleFormat fmtSaida = AV_SAMPLE_FMT_NONE;
    bool empacota24 = false;          // S32 do FFmpeg (24 bits alinhados em cima) -> 3 bytes
    int64_t quadrosEntregues = 0;
    std::vector<uint8_t> conv;

    ~Impl() { Fecha(); }

    void Fecha()
    {
        if (dec) { FLAC__stream_decoder_delete(dec); dec = nullptr; }
        if (swr) swr_free(&swr);
        if (cod) avcodec_free_context(&cod);
        if (fmt) avformat_close_input(&fmt);
        if (pkt) av_packet_free(&pkt);
        if (frm) av_frame_free(&frm);
        tipo = 0;
        pendente.clear();
        fimFlac = erro = eofLeitura = empacota24 = false;
        idx = -1;
        fmtSaida = AV_SAMPLE_FMT_NONE;
        quadrosEntregues = 0;
        bpsArquivo = canaisArquivo = taxaArquivo = 0;
        totalAmostras = 0;
        bitsSaida = 0;
    }

    // ---- callbacks da libFLAC ----
    static FLAC__StreamDecoderWriteStatus Escreve(const FLAC__StreamDecoder *, const FLAC__Frame *q,
                                                  const FLAC__int32 *const buffer[], void *cliente)
    {
        Impl *s = (Impl *) cliente;
        unsigned n = q->header.blocksize, c = q->header.channels;
        int bytes = s->bitsSaida / 8;
        int desloca = s->bitsSaida - (int) s->bpsArquivo;
        FLAC__uint64 primeira = q->header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER
                                    ? q->header.number.sample_number
                                    : (FLAC__uint64) q->header.number.frame_number * n;
        if (s->pendente.empty())
            s->tempoPendente = s->taxaArquivo ? (double) primeira / s->taxaArquivo : 0;
        size_t base = s->pendente.size();
        s->pendente.resize(base + (size_t) n * c * bytes);
        uint8_t *p = s->pendente.data() + base;
        for (unsigned i = 0; i < n; i++)
            for (unsigned k = 0; k < c; k++)
            {
                int32_t v = (int32_t) ((uint32_t) buffer[k][i] << desloca);
                if (bytes == 1) *p++ = (uint8_t) (v + 128);       // PCM de 8 bits é sem sinal
                else
                    for (int b = 0; b < bytes; b++) *p++ = (uint8_t) ((uint32_t) v >> (8 * b));
            }
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    static void Metadados(const FLAC__StreamDecoder *, const FLAC__StreamMetadata *m, void *cliente)
    {
        Impl *s = (Impl *) cliente;
        if (m->type != FLAC__METADATA_TYPE_STREAMINFO) return;
        s->bpsArquivo = m->data.stream_info.bits_per_sample;
        s->canaisArquivo = m->data.stream_info.channels;
        s->taxaArquivo = m->data.stream_info.sample_rate;
        s->totalAmostras = m->data.stream_info.total_samples;
    }

    static void Erro(const FLAC__StreamDecoder *, FLAC__StreamDecoderErrorStatus st, void *cliente)
    {
        // LINUX: a libFLAC, sozinha, pularia o quadro ruim e seguiria (perda de sincronia, CRC
        // errado). Para o ARCA isso é erro de decodificação, como o hr < 0 do GetNextSample.
        Impl *s = (Impl *) cliente;
        if (!s->erro) s->strErro = FLAC__StreamDecoderErrorStatusString[st];
        s->erro = true;
    }

    bool IniciaFlac()
    {
        dec = FLAC__stream_decoder_new();
        if (!dec) return false;
        // O MD5 do STREAMINFO é conferido no fim: a prova de que o PCM entregue é o que foi
        // codificado (desligado pela própria libFLAC depois de um Posicionar).
        FLAC__stream_decoder_set_md5_checking(dec, true);
        if (FLAC__stream_decoder_init_file(dec, arquivo.c_str(), Escreve, Metadados, Erro, this)
            != FLAC__STREAM_DECODER_INIT_STATUS_OK)
            return false;
        fimFlac = false;
        erro = false;
        return FLAC__stream_decoder_process_until_end_of_metadata(dec) && !erro;
    }
};

namespace {

// O arquivo é FLAC nativo? "fLaC" no começo, ou depois de um ID3v2.
bool EhFlac(const std::string &arquivo)
{
    FILE *f = fopen(arquivo.c_str(), "rb");
    if (!f) return false;
    uint8_t c[10];
    bool r = false;
    if (fread(c, 1, 10, f) == 10)
    {
        if (memcmp(c, "fLaC", 4) == 0) r = true;
        else if (c[0] == 'I' && c[1] == 'D' && c[2] == '3')
        {
            long p = 10 + (((long) (c[6] & 0x7F) << 21) | ((c[7] & 0x7F) << 14) | ((c[8] & 0x7F) << 7) | (c[9] & 0x7F));
            uint8_t m[4];
            r = fseek(f, p, SEEK_SET) == 0 && fread(m, 1, 4, f) == 4 && memcmp(m, "fLaC", 4) == 0;
        }
    }
    fclose(f);
    return r;
}

std::string ErroAv(int e)
{
    char b[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(e, b, sizeof b);
    return b;
}

} // namespace

LeitorAudio::LeitorAudio() : m(new Impl()) { }

LeitorAudio::~LeitorAudio() { Fechar(); }

bool LeitorAudio::Aberto() const { return m->tipo != 0 && m_intCanais > 0; }

double LeitorAudio::FaltaNoFim(int64_t lngQuadrosDecodificados) const
{
    if (m_dblDuracaoDeclarada <= 0 || m_intTaxa <= 0) return 0;
    double dblFalta = m_dblDuracaoDeclarada - (double) lngQuadrosDecodificados / m_intTaxa;
    return dblFalta > ToleranciaFimSegundos ? dblFalta : 0;
}

bool LeitorAudio::Abrir(const std::string &arquivo)
{
    Fechar();
    m_strFalha.clear();
    m_blnTerminou = false;
    m->arquivo = arquivo;
    try
    {
        if (EhFlac(arquivo))
        {
            // ---- FLAC: libFLAC ----
            if (!m->IniciaFlac())
            {
                m_strFalha = m->dec ? std::string("libFLAC: ") +
                                      (m->erro ? m->strErro : FLAC__StreamDecoderStateString[FLAC__stream_decoder_get_state(m->dec)])
                                    : "FLAC__stream_decoder_new devolveu nulo";
                Fechar();
                return false;
            }
            m->tipo = 1;
            if (m->canaisArquivo == 0 || m->bpsArquivo == 0 || m->taxaArquivo == 0)
            { m_strFalha = "Nenhuma saída de áudio PCM."; Fechar(); return false; }
            // LINUX: FLAC de 4 a 32 bits; o que não é múltiplo de 8 (20 bits, por exemplo) sai no
            // contêiner de cima, alinhado em cima, como o WAVE_FORMAT_EXTENSIBLE entregaria.
            m->bitsSaida = (int) ((m->bpsArquivo + 7) / 8) * 8;
            m_intCanais = (int) m->canaisArquivo;
            m_intTaxa = (int) m->taxaArquivo;
            m_intBits = m->bitsSaida;
            m_dblDuracaoDeclarada = m->totalAmostras > 0 ? (double) m->totalAmostras / m->taxaArquivo : 0;
            return true;
        }

        // ---- o resto: FFmpeg ----
        av_log_set_level(AV_LOG_ERROR);
        int r = avformat_open_input(&m->fmt, arquivo.c_str(), nullptr, nullptr);
        if (r < 0) { m_strFalha = "FFmpeg: " + ErroAv(r); Fechar(); return false; }
        m->tipo = 2;
        r = avformat_find_stream_info(m->fmt, nullptr);
        if (r < 0) { m_strFalha = "FFmpeg: " + ErroAv(r); Fechar(); return false; }

        const AVCodec *codec = nullptr;
        m->idx = av_find_best_stream(m->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (m->idx < 0 || !codec) { m_strFalha = "Nenhuma saída de áudio PCM."; Fechar(); return false; }
        AVStream *st = m->fmt->streams[m->idx];

        m->cod = avcodec_alloc_context3(codec);
        if (!m->cod || avcodec_parameters_to_context(m->cod, st->codecpar) < 0)
        { m_strFalha = "FFmpeg: sem contexto para o decodificador"; Fechar(); return false; }
        m->cod->pkt_timebase = st->time_base;
        r = avcodec_open2(m->cod, codec, nullptr);
        if (r < 0) { m_strFalha = "FFmpeg: " + ErroAv(r); Fechar(); return false; }

        // O formato que se entrega. Só PCM inteiro: 16 bits, ou 24/32 quando o arquivo tem.
        // LINUX: o decodificador do Windows pedia "a saída original" (EnableDiscreteOutput,
        // item 13 da revisão); o equivalente aqui é entregar o formato inteiro do decodificador.
        // Os formatos com perda saem do FFmpeg em ponto flutuante; o do Windows os entregava em
        // 16 bits, e é para 16 bits que vão.
        AVSampleFormat in = m->cod->sample_fmt;
        switch (av_get_packed_sample_fmt(in))
        {
            case AV_SAMPLE_FMT_U8: m->fmtSaida = AV_SAMPLE_FMT_U8; m_intBits = 8; break;
            case AV_SAMPLE_FMT_S16: m->fmtSaida = AV_SAMPLE_FMT_S16; m_intBits = 16; break;
            case AV_SAMPLE_FMT_S32:
            case AV_SAMPLE_FMT_S64:
                m->fmtSaida = AV_SAMPLE_FMT_S32;
                if (m->cod->bits_per_raw_sample > 16 && m->cod->bits_per_raw_sample <= 24) { m_intBits = 24; m->empacota24 = true; }
                else m_intBits = 32;
                break;
            default: m->fmtSaida = AV_SAMPLE_FMT_S16; m_intBits = 16; break;
        }
        m_intCanais = m->cod->ch_layout.nb_channels;
        m_intTaxa = m->cod->sample_rate;
        if (m_intCanais <= 0 || m_intTaxa <= 0) { m_strFalha = "Nenhuma saída de áudio PCM."; Fechar(); return false; }

        AVChannelLayout lay;
        if (m->cod->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_default(&lay, m_intCanais);
        else av_channel_layout_copy(&lay, &m->cod->ch_layout);
        r = swr_alloc_set_opts2(&m->swr, &lay, m->fmtSaida, m_intTaxa, &lay, in, m_intTaxa, 0, nullptr);
        av_channel_layout_uninit(&lay);
        if (r < 0 || swr_init(m->swr) < 0) { m_strFalha = "FFmpeg: o swresample recusou o formato"; Fechar(); return false; }

        m->pkt = av_packet_alloc();
        m->frm = av_frame_alloc();
        if (!m->pkt || !m->frm) { m_strFalha = "FFmpeg: sem memória"; Fechar(); return false; }

        // DesligarOutrosFluxos: sem isto, num vídeo o decodificador trabalharia cada quadro de
        // vídeo só para o LerAmostra jogá-lo fora. O áudio entregue não muda — só deixa de
        // existir o trabalho descartado.
        for (unsigned i = 0; i < m->fmt->nb_streams; i++)
            if ((int) i != m->idx) { m->fmt->streams[i]->discard = AVDISCARD_ALL; m_intFluxosDesligados++; }

        if (m->fmt->duration != AV_NOPTS_VALUE && m->fmt->duration > 0)
            m_dblDuracaoDeclarada = m->fmt->duration / (double) AV_TIME_BASE;
        else if (st->duration != AV_NOPTS_VALUE && st->duration > 0)
            m_dblDuracaoDeclarada = st->duration * av_q2d(st->time_base);
        return true;
    }
    catch (const std::exception &ex)
    {
        m_strFalha = std::string("exceção: ") + ex.what();
        Fechar();
        return false;
    }
}

bool LeitorAudio::Posicionar(double segundos)
{
    if (m->tipo == 0) return false;
    if (segundos < 0) segundos = 0;
    if (m->tipo == 1)
    {
        // depois do fim o decodificador foi concluído: abre de novo
        if (m->fimFlac)
        {
            FLAC__stream_decoder_delete(m->dec);
            m->dec = nullptr;
            if (!m->IniciaFlac()) { m_strFalha = "libFLAC: não reabriu o arquivo para posicionar"; return false; }
        }
        m->pendente.clear();
        m->erro = false;
        FLAC__uint64 a = (FLAC__uint64) std::llround(segundos * m->taxaArquivo);
        if (m->totalAmostras > 0 && a >= m->totalAmostras) a = m->totalAmostras - 1;
        if (!FLAC__stream_decoder_seek_absolute(m->dec, a))
        {
            m_strFalha = std::string("libFLAC: ") + FLAC__StreamDecoderStateString[FLAC__stream_decoder_get_state(m->dec)];
            FLAC__stream_decoder_flush(m->dec);
            m->pendente.clear();
            return false;
        }
        m_dblTempoDoUltimoBloco = segundos;
        return true;
    }
    AVStream *st = m->fmt->streams[m->idx];
    int64_t ts = (int64_t) (segundos / av_q2d(st->time_base));
    if (st->start_time != AV_NOPTS_VALUE) ts += st->start_time;
    int r = av_seek_frame(m->fmt, m->idx, ts, AVSEEK_FLAG_BACKWARD);
    if (r < 0) { m_strFalha = "FFmpeg: " + ErroAv(r); return false; }
    avcodec_flush_buffers(m->cod);
    m->eofLeitura = false;
    m->quadrosEntregues = (int64_t) (segundos * m_intTaxa);
    m_dblTempoDoUltimoBloco = segundos;
    return true;
}

/// A próxima amostra que traz pelo menos um quadro. false no fim do
/// arquivo (Terminou) ou em erro (Falha).
/// Amostra vazia não é fim: é pulada.
bool LeitorAudio::LerAmostra(std::vector<uint8_t> &arrBytes)
{
    int passo = (m_intBits / 8) * m_intCanais;
    arrBytes.clear();
    if (m->tipo == 1)
    {
        while (true)
        {
            if (m->erro)
            {
                m_strFalha = "erro de decodificação perto de " + Modelo::Formato::Num(m_dblTempoDoUltimoBloco, 1) +
                             " s (" + m->strErro + ")";
                return false;
            }
            if (passo <= 0) { m_strFalha = "formato de áudio sem tamanho de amostra"; return false; }
            if (m->pendente.size() >= (size_t) passo)
            {
                m_dblTempoDoUltimoBloco = m->tempoPendente;
                arrBytes.swap(m->pendente);
                m->pendente.clear();
                return true;
            }
            if (m->fimFlac) { m_blnTerminou = true; return false; }
            FLAC__StreamDecoderState e = FLAC__stream_decoder_get_state(m->dec);
            if (e == FLAC__STREAM_DECODER_END_OF_STREAM)
            {
                // LINUX: o fim de verdade inclui a prova do STREAMINFO: o MD5 do que se
                // decodificou tem de ser o que o codificador gravou. Se não bater, é erro.
                bool blnMd5 = FLAC__stream_decoder_finish(m->dec);
                m->fimFlac = true;
                if (!blnMd5)
                {
                    m_strFalha = "o MD5 do áudio decodificado não confere com o do STREAMINFO";
                    return false;
                }
                m_blnTerminou = true;
                return false;
            }
            if (!FLAC__stream_decoder_process_single(m->dec))
            {
                m_strFalha = "erro de decodificação perto de " + Modelo::Formato::Num(m_dblTempoDoUltimoBloco, 1) +
                             " s (" + FLAC__StreamDecoderStateString[FLAC__stream_decoder_get_state(m->dec)] + ")";
                return false;
            }
        }
    }

    // FFmpeg
    while (true)
    {
        if (passo <= 0) { m_strFalha = "formato de áudio sem tamanho de amostra"; return false; }
        int r = avcodec_receive_frame(m->cod, m->frm);
        if (r == 0)
        {
            int n = m->frm->nb_samples;
            if (n > 0)
            {
                AVStream *st = m->fmt->streams[m->idx];
                int64_t pts = m->frm->best_effort_timestamp;
                m_dblTempoDoUltimoBloco = pts != AV_NOPTS_VALUE
                    ? (pts - (st->start_time != AV_NOPTS_VALUE ? st->start_time : 0)) * av_q2d(st->time_base)
                    : (double) m->quadrosEntregues / m_intTaxa;
                int bytesIn = av_get_bytes_per_sample(m->fmtSaida);
                m->conv.resize((size_t) n * m_intCanais * bytesIn);
                uint8_t *saida[1] = { m->conv.data() };
                int q = swr_convert(m->swr, saida, n, (const uint8_t **) m->frm->extended_data, n);
                av_frame_unref(m->frm);
                if (q < 0) { m_strFalha = "FFmpeg: swresample: " + ErroAv(q); return false; }
                m->quadrosEntregues += q;
                if (m->empacota24)
                {
                    // S32 com 24 bits significativos (alinhados em cima): 3 bytes por amostra
                    size_t total = (size_t) q * m_intCanais;
                    arrBytes.resize(total * 3);
                    for (size_t i = 0; i < total; i++)
                    {
                        uint32_t v = (uint32_t) m->conv[4 * i] | ((uint32_t) m->conv[4 * i + 1] << 8) |
                                     ((uint32_t) m->conv[4 * i + 2] << 16) | ((uint32_t) m->conv[4 * i + 3] << 24);
                        v >>= 8;
                        arrBytes[3 * i] = (uint8_t) v;
                        arrBytes[3 * i + 1] = (uint8_t) (v >> 8);
                        arrBytes[3 * i + 2] = (uint8_t) (v >> 16);
                    }
                }
                else arrBytes.assign(m->conv.begin(), m->conv.begin() + (size_t) q * m_intCanais * bytesIn);
                if ((int) arrBytes.size() >= passo) return true;
                arrBytes.clear();
            }
            else av_frame_unref(m->frm);
            continue;
        }
        if (r == AVERROR_EOF) { m_blnTerminou = true; return false; }
        if (r != AVERROR(EAGAIN))
        {
            m_strFalha = "erro de decodificação perto de " + Modelo::Formato::Num(m_dblTempoDoUltimoBloco, 1) + " s (" + ErroAv(r) + ")";
            return false;
        }
        if (m->eofLeitura) { m_blnTerminou = true; return false; }
        r = av_read_frame(m->fmt, m->pkt);
        if (r == AVERROR_EOF)
        {
            m->eofLeitura = true;
            avcodec_send_packet(m->cod, nullptr);       // esvazia o decodificador
            continue;
        }
        if (r < 0)
        {
            m_strFalha = "erro de leitura perto de " + Modelo::Formato::Num(m_dblTempoDoUltimoBloco, 1) + " s (" + ErroAv(r) + ")";
            return false;
        }
        if (m->pkt->stream_index != m->idx)
        {
            // Amostra de outro fluxo (vídeo, roteiro): não é áudio.
            av_packet_unref(m->pkt);
            continue;
        }
        r = avcodec_send_packet(m->cod, m->pkt);
        av_packet_unref(m->pkt);
        if (r < 0 && r != AVERROR(EAGAIN))
        {
            m_strFalha = "erro de decodificação perto de " + Modelo::Formato::Num(m_dblTempoDoUltimoBloco, 1) + " s (" + ErroAv(r) + ")";
            return false;
        }
    }
}

std::vector<uint8_t> LeitorAudio::ProximoBlocoBytes()
{
    if (m->tipo == 0) return {};
    try
    {
        std::vector<uint8_t> arr;
        if (!LerAmostra(arr)) return {};
        return arr;
    }
    catch (const std::exception &ex) { m_strFalha = ex.what(); return {}; }
}

std::vector<std::vector<float>> LeitorAudio::ProximoBloco()
{
    if (m->tipo == 0) return {};
    try
    {
        std::vector<uint8_t> arr;
        if (!LerAmostra(arr)) return {};
        return Separar(arr.data(), (int) arr.size());
    }
    catch (const std::exception &ex) { m_strFalha = ex.what(); return {}; }
}

std::vector<std::vector<int>> LeitorAudio::ProximoBlocoInteiro(int &quadros)
{
    quadros = 0;
    if (m->tipo == 0) return {};
    try
    {
        std::vector<uint8_t> arr;
        if (!LerAmostra(arr)) return {};
        const uint8_t *p = arr.data();
        int bytes = (int) arr.size();

        int bytesPorAmostra = m_intBits / 8;
        int passo = bytesPorAmostra * m_intCanais;
        quadros = bytes / passo;

        std::vector<std::vector<int>> canais(m_intCanais, std::vector<int>(quadros));

        for (int q = 0; q < quadros; q++)
            for (int c = 0; c < m_intCanais; c++)
            {
                int off = q * passo + c * bytesPorAmostra;
                int v;
                if (m_intBits == 16)
                    v = (int16_t) (p[off] | (p[off + 1] << 8));
                else if (m_intBits == 24)
                {
                    int b0 = p[off];
                    int b1 = p[off + 1];
                    int b2 = (int8_t) p[off + 2];
                    v = (int) ((uint32_t) b2 << 16) | (b1 << 8) | b0;
                }
                else if (m_intBits == 32)
                    v = (int32_t) ((uint32_t) p[off] | ((uint32_t) p[off + 1] << 8) | ((uint32_t) p[off + 2] << 16) | ((uint32_t) p[off + 3] << 24));
                else if (m_intBits == 8)
                    v = p[off] - 128;   // PCM de 8 bits e sem sinal
                else v = 0;
                canais[c][q] = v;
            }
        return canais;
    }
    catch (const std::exception &ex) { m_strFalha = ex.what(); return {}; }
}

std::vector<std::vector<float>> LeitorAudio::Separar(const uint8_t *p, int bytes) const
{
    int bytesPorAmostra = m_intBits / 8;
    int passo = bytesPorAmostra * m_intCanais;
    if (passo <= 0) return {};
    int quadros = bytes / passo;
    if (quadros <= 0) return {};

    std::vector<std::vector<float>> canais(m_intCanais, std::vector<float>(quadros));

    for (int q = 0; q < quadros; q++)
        for (int c = 0; c < m_intCanais; c++)
        {
            int off = q * passo + c * bytesPorAmostra;
            float v;
            if (m_intBits == 16)
                v = (int16_t) (p[off] | (p[off + 1] << 8)) / 32768.0f;
            else if (m_intBits == 24)
            {
                int b0 = p[off];
                int b1 = p[off + 1];
                int b2 = (int8_t) p[off + 2];
                v = ((int) ((uint32_t) b2 << 16) | (b1 << 8) | b0) / 8388608.0f;
            }
            else if (m_intBits == 32)
                v = (int32_t) ((uint32_t) p[off] | ((uint32_t) p[off + 1] << 8) | ((uint32_t) p[off + 2] << 16) | ((uint32_t) p[off + 3] << 24)) / 2147483648.0f;
            // ARCA-ORIGINAL: o Separar não trata PCM de 8 bits (o ProximoBlocoInteiro trata): um
            // arquivo de 8 bits chega aos medidores como silêncio.
            else v = 0.0f;
            canais[c][q] = v;
        }
    return canais;
}

void LeitorAudio::Fechar()
{
    if (m) m->Fecha();
    m_intCanais = 0; m_intTaxa = 0; m_intBits = 0;
    m_intFluxosDesligados = 0;
    m_dblTempoDoUltimoBloco = 0;
    // LINUX: o original não zerava a duração declarada no Fechar (o Abrir seguinte a recalcula);
    // aqui também é recalculada no Abrir.
    m_dblDuracaoDeclarada = 0;
}

} // namespace ARCA::Interop

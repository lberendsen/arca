// ARCA no xOS - Interop/PlayerWmp (de Interop\PlayerWmp.cs e Interop\HospedeiroWmp.cs).
//
// LINUX: o original era uma fachada de ligacao tardia sobre o controle COM do Windows Media Player
// (InvokeMember em "controls", "settings", "currentMedia"...). Aqui o reprodutor e proprio, com a
// mesma interface e os mesmos estados (o playState do WMP):
//   - FFmpeg (libavformat/libavcodec) abre e decodifica FLAC, WMA, MP3 e o audio dos videos;
//   - libswresample so intercala (e, nos formatos com perda, que o FFmpeg entrega em ponto
//     flutuante, passa a 32 bits): mesma taxa e mesmos bits do arquivo, sem reamostrar;
//   - ALSA toca, num fio proprio. As licoes da Discoteca (src/xshell/musica.cpp): a placa direto
//     (hw:CARD=Intel,DEV=0, bit a bit) e, ocupada ou recusando a taxa, o "default" (dmix, 48 kHz);
//     250 ms de folga. ARCA_ALSA troca o dispositivo (os testes usam "null").
//   - O volume (0 a 100) e o do proprio player, como no WMP: ganho nas amostras, aplicado antes da
//     placa; em 100 o som vai bit a bit. O volume da placa (Master) e do xOS e nao e mexido.
//   - Parado, o player solta a placa (a Discoteca e a TV a usam direto).
//   - Video: o audio toca; a imagem ainda nao e desenhada (pendencia: ver o fim deste arquivo).
//
// Tudo e chamado do fio da tela (a regra do original). Os comandos vao ao fio do reprodutor por
// uma fila protegida por mutex; o estado volta por campos atomicos/protegidos.
#include "PlayerWmp.h"

#include "Base/Arquivo.h"
#include "Base/Texto.h"
#include "Interop/EditorFlac.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libswresample/swresample.h>
}

#include <alsa/asoundlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace ARCA::Interop {

namespace {

double Relogio()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// durationString / currentPositionString do WMP: "03:45", "1:02:03"
std::string TempoWmp(double s)
{
    if (!(s > 0)) s = 0;
    long t = (long) s;
    char b[32];
    if (t >= 3600) snprintf(b, sizeof b, "%ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60);
    else snprintf(b, sizeof b, "%02ld:%02ld", t / 60, t % 60);
    return b;
}

// os dispositivos, na ordem de tentativa (GEMINI.md 10.1: bit a bit na placa; ocupada, o default)
std::vector<std::string> Dispositivos()
{
    const char *e = getenv("ARCA_ALSA");
    if (e && *e) return {e};
    return {"hw:CARD=Intel,DEV=0", "default"};
}

} // namespace

struct PlayerWmp::Impl
{
    // ---- o que o fio da tela pede ----
    std::mutex m;
    std::condition_variable cv;
    std::thread fio;
    bool sair = false;
    std::string url;                 // o URL atual (o que Url() devolve)
    int geracaoUrl = 0;              // muda a cada Url(valor)
    enum class Pedido { Nenhum, Tocar, Pausar, Parar };
    Pedido pedido = Pedido::Nenhum;
    bool temBusca = false;
    double busca = 0;
    bool repetir = false;

    // ---- o estado, lido pelo fio da tela ----
    std::atomic<int> estado{(int) EstadoPlayer::Indefinido};
    double pos = 0, carimbo = 0;     // posicao (s) e quando foi medida (Relogio), sob m
    double duracao = 0;
    std::map<std::string, std::string> tags;   // as etiquetas da midia (chave em minusculas)
    int largura = 0, altura = 0;
    bool temFluxoVideo = false;
    bool carregada = false;          // ha midia aberta (currentMedia != null)
    std::atomic<int> volume{50};
    std::atomic<bool> mudo{false};
    bool vivo = false;
    std::string modoInterface = "full";
    bool esticar = false, telaCheia = false;

    // ---- so do fio do reprodutor ----
    AVFormatContext *fmt = nullptr;
    AVCodecContext *cod = nullptr;
    SwrContext *swr = nullptr;
    AVPacket *pkt = nullptr;
    AVFrame *frm = nullptr;
    int idx = -1;
    int taxa = 0;
    bool bits32 = false;
    bool fimLeitura = false;
    double descartarAte = -1;        // busca precisa: joga fora o que vier antes disto (s)
    snd_pcm_t *pcm = nullptr;
    int taxaPcm = 0;
    bool bits32Pcm = false;
    bool podePausar = false;
    bool pcmNulo = false;            // o dispositivo "null" (testes): o ritmo e o do relogio
    double nuloInicio = 0;
    double base = 0;                 // a posicao do primeiro quadro escrito desde a ultima busca
    int64_t escritos = 0;            // quadros escritos desde base
    std::vector<uint8_t> saida;
    std::string urlAberta;           // copia do url do arquivo aberto (o url e do fio da tela)

    void Laco();
    void Fechar();
    bool Abrir(const std::string &arquivo);
    bool AbrePlaca();
    void FechaPlaca();
    void Posicionar(double seg);
    bool TocaUmPacote();             // false no fim do arquivo (ou erro)
    void Escreve(AVFrame *q);
    double PosicaoDaPlaca();
    void Mede()
    {
        // sob m
        pos = PosicaoDaPlaca();
        carimbo = Relogio();
    }
};

// =====================================================================================
// O fio do reprodutor
// =====================================================================================
void PlayerWmp::Impl::Fechar()
{
    if (frm) av_frame_free(&frm);
    if (pkt) av_packet_free(&pkt);
    if (swr) swr_free(&swr);
    if (cod) avcodec_free_context(&cod);
    if (fmt) avformat_close_input(&fmt);
    idx = -1;
    taxa = 0;
    fimLeitura = false;
}

bool PlayerWmp::Impl::Abrir(const std::string &arquivo)
{
    Fechar();
    const AVCodec *codec = nullptr;
    if (avformat_open_input(&fmt, arquivo.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0 ||
        (idx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0)) < 0)
    {
        Fechar();
        return false;
    }
    cod = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(cod, fmt->streams[idx]->codecpar);
    if (avcodec_open2(cod, codec, nullptr) < 0)
    {
        Fechar();
        return false;
    }
    taxa = cod->sample_rate;
    // Som do jeito que foi gravado: mais de 16 bits vai em 32; CD em 16 (como a Discoteca).
    bits32 = cod->bits_per_raw_sample > 16 || av_get_bytes_per_sample(av_get_packed_sample_fmt(cod->sample_fmt)) > 2;
    AVChannelLayout estereo = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&swr, &estereo, bits32 ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S16, taxa, &cod->ch_layout,
                            cod->sample_fmt, taxa, 0, nullptr) < 0 ||
        swr_init(swr) < 0)
    {
        Fechar();
        return false;
    }
    pkt = av_packet_alloc();
    frm = av_frame_alloc();
    for (unsigned i = 0; i < fmt->nb_streams; i++)
        if ((int) i != idx) fmt->streams[i]->discard = AVDISCARD_ALL;

    // o que o currentMedia do WMP sabe: duracao, etiquetas, imagem
    std::lock_guard<std::mutex> t(m);
    duracao = fmt->duration > 0 ? (double) fmt->duration / AV_TIME_BASE : 0;
    tags.clear();
    auto le = [&](AVDictionary *d) {
        const AVDictionaryEntry *e = nullptr;
        while ((e = av_dict_get(d, "", e, AV_DICT_IGNORE_SUFFIX)))
            if (!tags.count(Base::Minusculas(e->key))) tags[Base::Minusculas(e->key)] = Base::Utf8Valido(e->value);
    };
    le(fmt->metadata);
    le(fmt->streams[idx]->metadata);
    largura = altura = 0;
    temFluxoVideo = false;
    for (unsigned i = 0; i < fmt->nb_streams; i++)
    {
        AVStream *s = fmt->streams[i];
        // a capa embutida (FLAC, MP3) aparece como fluxo de video "anexado": nao e video
        if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !(s->disposition & AV_DISPOSITION_ATTACHED_PIC))
        {
            temFluxoVideo = true;
            largura = s->codecpar->width;
            altura = s->codecpar->height;
            break;
        }
    }
    return true;
}

bool PlayerWmp::Impl::AbrePlaca()
{
    if (pcm && (taxa != taxaPcm || bits32 != bits32Pcm)) FechaPlaca();
    if (pcm) return true;
    snd_pcm_format_t f = bits32 ? SND_PCM_FORMAT_S32_LE : SND_PCM_FORMAT_S16_LE;
    for (const std::string &d : Dispositivos())
    {
        if (snd_pcm_open(&pcm, d.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0)
        {
            pcm = nullptr;
            continue;
        }
        // a placa direto nao reamostra (bit a bit ou nada); o resto pode
        bool direto = d.compare(0, 3, "hw:") == 0;
        if (snd_pcm_set_params(pcm, f, SND_PCM_ACCESS_RW_INTERLEAVED, 2, (unsigned) taxa, direto ? 0 : 1, 250000) == 0)
        {
            taxaPcm = taxa;
            bits32Pcm = bits32;
            snd_pcm_hw_params_t *hw;
            snd_pcm_hw_params_alloca(&hw);
            podePausar = snd_pcm_hw_params_current(pcm, hw) == 0 && snd_pcm_hw_params_can_pause(hw);
            pcmNulo = snd_pcm_type(pcm) == SND_PCM_TYPE_NULL;
            nuloInicio = Relogio();
            if (FILE *log = fopen("/tmp/arca-saida.log", "w"))
            {
                // a cadeia de saida de verdade, como a Discoteca guarda em /tmp/musica-saida.log
                snd_output_t *o = nullptr;
                fprintf(log, "%s\n", urlAberta.c_str());
                if (snd_output_stdio_attach(&o, log, 0) == 0)
                {
                    snd_pcm_dump(pcm, o);
                    snd_output_close(o);
                }
                fclose(log);
            }
            return true;
        }
        snd_pcm_close(pcm);
        pcm = nullptr;
    }
    return false;
}

void PlayerWmp::Impl::FechaPlaca()
{
    if (!pcm) return;
    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);
    pcm = nullptr;
}

double PlayerWmp::Impl::PosicaoDaPlaca()
{
    if (taxa <= 0) return base;
    snd_pcm_sframes_t atraso = 0;
    if (pcmNulo)
    {
        // LINUX: o "null" do ALSA nao tem relogio (aceita tudo na hora); o atraso e o que ainda nao
        // "tocou" pelo relogio de parede
        double tocado = (Relogio() - nuloInicio) * taxa;
        atraso = (snd_pcm_sframes_t) std::max(0.0, escritos - tocado);
    }
    else if (!pcm || snd_pcm_delay(pcm, &atraso) < 0) atraso = 0;
    double p = base + (double) (escritos - std::max<snd_pcm_sframes_t>(0, atraso)) / taxa;
    return std::max(base, p);
}

void PlayerWmp::Impl::Posicionar(double seg)
{
    if (!fmt) return;
    if (seg < 0) seg = 0;
    int64_t ts = (int64_t) (seg * AV_TIME_BASE);
    av_seek_frame(fmt, -1, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(cod);
    swr_init(swr);
    fimLeitura = false;
    descartarAte = seg;
    if (pcm)
    {
        snd_pcm_drop(pcm);
        snd_pcm_prepare(pcm);
    }
    base = seg;
    escritos = 0;
    nuloInicio = Relogio();
}

void PlayerWmp::Impl::Escreve(AVFrame *q)
{
    // busca precisa: os quadros antes do ponto pedido nao tocam
    int pular = 0;
    if (descartarAte >= 0 && q->pts != AV_NOPTS_VALUE)
    {
        double t0 = q->pts * av_q2d(fmt->streams[idx]->time_base);
        double fim = t0 + (double) q->nb_samples / taxa;
        if (fim <= descartarAte) return;
        if (t0 < descartarAte) pular = (int) ((descartarAte - t0) * taxa);
        descartarAte = -1;
    }
    else descartarAte = -1;
    int bytesQuadro = bits32 ? 8 : 4;
    int max = swr_get_out_samples(swr, q->nb_samples);
    if ((int) saida.size() < max * bytesQuadro) saida.resize((size_t) max * bytesQuadro);
    uint8_t *p = saida.data();
    int n = swr_convert(swr, &p, max, (const uint8_t **) q->extended_data, q->nb_samples);
    if (n <= 0) return;
    if (pular > 0)
    {
        pular = std::min(pular, n);
        p += (size_t) pular * bytesQuadro;
        n -= pular;
        if (n <= 0) return;
    }

    // o volume do player: 100 = as amostras como estao (bit a bit)
    int vol = volume.load();
    bool calado = mudo.load();
    if (calado) std::memset(p, 0, (size_t) n * bytesQuadro);
    else if (vol < 100)
    {
        // LINUX: o WMP aplicava o volume dele nas amostras; a curva aqui e a de potencia (o
        // ganho em amplitude e (v/100)^2: 70 dá −6 dB, 50 dá −12 dB, como um controle de volume comum)
        double g = std::pow(std::max(0, vol) / 100.0, 2.0);
        if (bits32)
        {
            int32_t *s = (int32_t *) p;
            for (int i = 0; i < n * 2; i++) s[i] = (int32_t) std::lrint(s[i] * g);
        }
        else
        {
            int16_t *s = (int16_t *) p;
            for (int i = 0; i < n * 2; i++) s[i] = (int16_t) std::lrint(s[i] * g);
        }
    }

    while (n > 0 && pcm)
    {
        snd_pcm_sframes_t w;
        if (pcmNulo)
        {
            // LINUX: ritmo de tempo real no "null" (so testes): espera a folga de 250 ms
            double adiantado = (double) escritos / taxa - (Relogio() - nuloInicio);
            if (adiantado > 0.25) std::this_thread::sleep_for(std::chrono::duration<double>(adiantado - 0.25));
            w = n;
        }
        else w = snd_pcm_writei(pcm, p, (snd_pcm_uframes_t) n);
        if (w == -EAGAIN) continue;
        if (w < 0)
        {
            // subfluxo (a tela travou o fio?) ou suspensao: recupera e segue
            if (snd_pcm_recover(pcm, (int) w, 1) < 0) return;
            continue;
        }
        escritos += w;
        n -= (int) w;
        p += (size_t) w * bytesQuadro;
    }
}

bool PlayerWmp::Impl::TocaUmPacote()
{
    if (!fmt) return false;
    for (;;)
    {
        int r = avcodec_receive_frame(cod, frm);
        if (r == 0)
        {
            Escreve(frm);
            av_frame_unref(frm);
            return true;
        }
        if (r == AVERROR_EOF) return false;
        if (r != AVERROR(EAGAIN)) return false;
        if (fimLeitura) return false;
        int lido = av_read_frame(fmt, pkt);
        if (lido < 0)
        {
            fimLeitura = true;
            avcodec_send_packet(cod, nullptr);   // esvazia o decodificador
            continue;
        }
        if (pkt->stream_index == idx) avcodec_send_packet(cod, pkt);
        av_packet_unref(pkt);
    }
}

void PlayerWmp::Impl::Laco()
{
    int geracaoAberta = 0;
    std::unique_lock<std::mutex> lk(m);
    for (;;)
    {
        cv.wait(lk, [&] {
            return sair || pedido != Pedido::Nenhum || temBusca || geracaoUrl != geracaoAberta ||
                   estado.load() == (int) EstadoPlayer::Tocando;
        });
        if (sair) break;

        // ---- outro URL: solta o anterior e abre o novo (o "currentMedia") ----
        if (geracaoUrl != geracaoAberta)
        {
            geracaoAberta = geracaoUrl;
            std::string novo = url;
            urlAberta = novo;
            lk.unlock();
            FechaPlaca();
            Fechar();
            bool ok = !novo.empty() && Abrir(novo);
            lk.lock();
            if (geracaoUrl != geracaoAberta) continue;    // trocou de novo enquanto abria
            carregada = ok;
            if (!ok)
            {
                duracao = 0;
                tags.clear();
                largura = altura = 0;
                temFluxoVideo = false;
            }
            base = 0;
            escritos = 0;
            pos = 0;
            carimbo = Relogio();
            // WMP: URL vazio = Indefinido; arquivo que nao abre tambem nao toca
            estado = (int) (ok ? EstadoPlayer::Parado : EstadoPlayer::Indefinido);
        }

        // ---- os pedidos ----
        Pedido p = pedido;
        pedido = Pedido::Nenhum;
        EstadoPlayer e = (EstadoPlayer) estado.load();
        if (temBusca)
        {
            temBusca = false;
            double alvo = busca;
            if (carregada)
            {
                if (duracao > 0) alvo = std::min(alvo, duracao);
                lk.unlock();
                Posicionar(alvo);
                lk.lock();
                pos = alvo;
                carimbo = Relogio();
                // pausado com a placa em pausa: Posicionar soltou a pausa (drop): fica so preparada
            }
        }
        if (p == Pedido::Parar)
        {
            lk.unlock();
            FechaPlaca();
            if (carregada) Posicionar(0);
            lk.lock();
            pos = 0;
            carimbo = Relogio();
            if (carregada) estado = (int) EstadoPlayer::Parado;
            continue;
        }
        if (p == Pedido::Pausar && e == EstadoPlayer::Tocando)
        {
            pos = PosicaoDaPlaca();
            carimbo = Relogio();
            double ondeParou = pos;
            lk.unlock();
            bool emPausa = pcm && podePausar && !pcmNulo && snd_pcm_pause(pcm, 1) == 0;
            // sem pausa na placa (o dmix nao tem): solta o que esta na folga e volta dali
            if (!emPausa) Posicionar(ondeParou);
            lk.lock();
            estado = (int) EstadoPlayer::Pausado;
            continue;
        }
        if (p == Pedido::Tocar && carregada && e != EstadoPlayer::Tocando)
        {
            lk.unlock();
            bool ok = true;
            if (e == EstadoPlayer::Pausado && pcm && snd_pcm_state(pcm) == SND_PCM_STATE_PAUSED)
                ok = snd_pcm_pause(pcm, 0) == 0 || snd_pcm_prepare(pcm) == 0;
            else
            {
                if (e == EstadoPlayer::MidiaTerminou) Posicionar(0);
                ok = AbrePlaca();
                if (ok) snd_pcm_prepare(pcm);
                nuloInicio = Relogio() - (double) escritos / std::max(1, taxa);
            }
            lk.lock();
            if (ok) estado = (int) EstadoPlayer::Tocando;
            continue;
        }

        // ---- tocando: um pacote por volta, sem o mutex ----
        if (estado.load() == (int) EstadoPlayer::Tocando)
        {
            lk.unlock();
            bool mais = TocaUmPacote();
            lk.lock();
            Mede();
            if (mais) continue;

            // Fim do arquivo: espera a placa tocar o que tem na folga, atento a pedidos.
            while (pedido == Pedido::Nenhum && !temBusca && geracaoUrl == geracaoAberta && !sair)
            {
                Mede();
                lk.unlock();
                snd_pcm_sframes_t atraso = 0;
                bool resta;
                if (pcmNulo) resta = (double) escritos / taxa > Relogio() - nuloInicio;
                else resta = pcm && snd_pcm_delay(pcm, &atraso) == 0 && atraso > 0 &&
                             snd_pcm_state(pcm) == SND_PCM_STATE_RUNNING;
                if (resta) std::this_thread::sleep_for(std::chrono::milliseconds(20));
                lk.lock();
                if (!resta) break;
            }
            if (pedido != Pedido::Nenhum || temBusca || geracaoUrl != geracaoAberta || sair) continue;
            if (repetir)
            {
                // setMode("loop", true)
                lk.unlock();
                Posicionar(0);
                lk.lock();
                continue;
            }
            // WMP: MediaEnded (8) e, logo depois, Stopped (1), com a posicao no começo
            pos = duracao;
            carimbo = Relogio();
            estado = (int) EstadoPlayer::MidiaTerminou;
            lk.unlock();
            FechaPlaca();
            Posicionar(0);
            lk.lock();
            cv.wait_for(lk, std::chrono::milliseconds(400), [&] {
                return sair || pedido != Pedido::Nenhum || temBusca || geracaoUrl != geracaoAberta;
            });
            if (estado.load() == (int) EstadoPlayer::MidiaTerminou)
            {
                estado = (int) EstadoPlayer::Parado;
                pos = 0;
                carimbo = Relogio();
            }
        }
    }
    lk.unlock();
    FechaPlaca();
    Fechar();
}

// =====================================================================================
// A fachada (fio da tela)
// =====================================================================================
PlayerWmp::PlayerWmp() : _impl(std::make_unique<Impl>())
{
    // LINUX: HospedeiroWmp.Disponivel() conferia se o controle COM existia; aqui, se alguma saida
    // de audio abre (abre e fecha na hora, sem tocar nada)
    for (const std::string &d : Dispositivos())
    {
        snd_pcm_t *p = nullptr;
        int r = snd_pcm_open(&p, d.c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
        if (r == 0) snd_pcm_close(p);
        // ocupada (a Discoteca, a TV ou o radio tocando direto na placa) tambem conta: ela existe
        if (r == 0 || r == -EBUSY)
        {
            _impl->vivo = true;
            break;
        }
    }
    _impl->fio = std::thread([this] { _impl->Laco(); });
}

PlayerWmp::~PlayerWmp()
{
    {
        std::lock_guard<std::mutex> t(_impl->m);
        _impl->sair = true;
    }
    _impl->cv.notify_all();
    if (_impl->fio.joinable()) _impl->fio.join();
}

bool PlayerWmp::Vivo() const { return _impl->vivo; }

std::string PlayerWmp::Url() const
{
    std::lock_guard<std::mutex> t(_impl->m);
    return _impl->url;
}

void PlayerWmp::Url(const std::string &valor)
{
    {
        std::lock_guard<std::mutex> t(_impl->m);
        _impl->url = valor;
        _impl->geracaoUrl++;
        _impl->pedido = Impl::Pedido::Nenhum;
        _impl->temBusca = false;
        // WMP: trocando de midia o estado passa por Transicao ate a nova abrir
        _impl->estado = (int) (valor.empty() ? EstadoPlayer::Indefinido : EstadoPlayer::EmTransicao);
        _impl->pos = 0;
        _impl->carimbo = Relogio();
    }
    _impl->cv.notify_all();
}

EstadoPlayer PlayerWmp::Estado() const { return (EstadoPlayer) _impl->estado.load(); }

void PlayerWmp::Tocar()
{
    {
        std::lock_guard<std::mutex> t(_impl->m);
        _impl->pedido = Impl::Pedido::Tocar;
    }
    _impl->cv.notify_all();
}

void PlayerWmp::Pausar()
{
    {
        std::lock_guard<std::mutex> t(_impl->m);
        _impl->pedido = Impl::Pedido::Pausar;
    }
    _impl->cv.notify_all();
}

/// <summary>
/// Parar de verdade, não pausar.
///
/// O stop do WMP interrompe, mas costuma deixar o currentPosition onde
/// estava — e aí o play seguinte retoma dali, que é comportamento de
/// pausa. Zerar a posição em seguida faz o botão significar o que diz.
/// </summary>
void PlayerWmp::Parar()
{
    {
        std::lock_guard<std::mutex> t(_impl->m);
        _impl->pedido = Impl::Pedido::Parar;
        _impl->temBusca = false;
        _impl->pos = 0;
        _impl->carimbo = Relogio();
    }
    _impl->cv.notify_all();
}

double PlayerWmp::Posicao() const
{
    std::lock_guard<std::mutex> t(_impl->m);
    double p = _impl->pos;
    // entre duas medidas do fio, a posicao anda com o relogio (no maximo meio segundo)
    if (_impl->estado.load() == (int) EstadoPlayer::Tocando && _impl->pedido == Impl::Pedido::Nenhum && !_impl->temBusca)
        p += std::min(0.5, std::max(0.0, Relogio() - _impl->carimbo));
    if (_impl->duracao > 0) p = std::min(p, _impl->duracao);
    return p;
}

void PlayerWmp::Posicao(double segundos)
{
    {
        std::lock_guard<std::mutex> t(_impl->m);
        if (!_impl->carregada && _impl->estado.load() != (int) EstadoPlayer::EmTransicao) return;
        _impl->busca = std::max(0.0, segundos);
        _impl->temBusca = true;
        _impl->pos = _impl->busca;
        _impl->carimbo = Relogio();
    }
    _impl->cv.notify_all();
}

std::string PlayerWmp::PosicaoTexto() const
{
    // currentPositionString: vazio sem midia ("0:00" no original, pelo ??)
    if (!TemMidia()) return "0:00";
    return TempoWmp(Posicao());
}

double PlayerWmp::Duracao() const
{
    std::lock_guard<std::mutex> t(_impl->m);
    return _impl->carregada ? _impl->duracao : 0;
}

std::string PlayerWmp::DuracaoTexto() const
{
    if (!TemMidia()) return "0:00";
    return TempoWmp(Duracao());
}

int PlayerWmp::Volume() const { return _impl->volume.load(); }
void PlayerWmp::Volume(int valor) { _impl->volume = std::max(0, std::min(100, valor)); }
bool PlayerWmp::Mudo() const { return _impl->mudo.load(); }
void PlayerWmp::Mudo(bool valor) { _impl->mudo = valor; }

void PlayerWmp::DefinirModoInterface(const std::string &modo)
{
    std::lock_guard<std::mutex> t(_impl->m);
    _impl->modoInterface = modo;
}

void PlayerWmp::DefinirRepeticao(bool repetir)
{
    std::lock_guard<std::mutex> t(_impl->m);
    _impl->repetir = repetir;
}

// ---- mídia atual -----------------------------------------------
bool PlayerWmp::TemMidia() const
{
    std::lock_guard<std::mutex> t(_impl->m);
    return _impl->carregada;
}

std::string PlayerWmp::TituloAtual() const
{
    std::string t = InfoAtual("Title");
    if (!t.empty()) return t;
    // currentMedia.name: o nome do arquivo sem a extensão
    if (!TemMidia()) return "";
    return Base::NomeSemExtensao(Url());
}

std::string PlayerWmp::ArtistaAtual() const
{
    std::string a = InfoAtual("WM/AlbumArtist");
    return !a.empty() ? a : InfoAtual("Author");
}

std::string PlayerWmp::AlbumAtual() const { return InfoAtual("WM/AlbumTitle"); }

int PlayerWmp::LarguraVideo() const
{
    std::lock_guard<std::mutex> t(_impl->m);
    return _impl->carregada ? _impl->largura : 0;
}

int PlayerWmp::AlturaVideo() const
{
    std::lock_guard<std::mutex> t(_impl->m);
    return _impl->carregada ? _impl->altura : 0;
}

/// <summary>
/// A mídia atual é vídeo, pelo que o WMP sabe dela (MediaType "video")?
/// Diferente do TemVideo, vale desde o início: a altura da imagem só
/// aparece depois que o primeiro quadro é decodificado.
/// </summary>
bool PlayerWmp::MidiaEhVideo() const { return Base::IgualSemCaixa(InfoAtual("MediaType"), "video"); }

void PlayerWmp::EsticarVideo(bool esticar)
{
    std::lock_guard<std::mutex> t(_impl->m);
    _impl->esticar = esticar;
}

bool PlayerWmp::TelaCheia() const
{
    std::lock_guard<std::mutex> t(_impl->m);
    return _impl->telaCheia;
}

void PlayerWmp::TelaCheia(bool valor)
{
    // LINUX: a imagem do video ainda nao e desenhada (pendencia); sem ela nao ha tela cheia, e o
    // pedido nao "pega" (quem pede le de volta e avisa, como no original quando o WMP recusava)
    std::lock_guard<std::mutex> t(_impl->m);
    _impl->telaCheia = false;
    (void) valor;
}

std::string PlayerWmp::InfoAtual(const std::string &atributo) const
{
    std::lock_guard<std::mutex> t(_impl->m);
    if (!_impl->carregada) return "";
    if (atributo == "MediaType") return _impl->temFluxoVideo ? "video" : "audio";
    // LINUX: os nomes ASF que o ARCA pede, pelos nomes que o FFmpeg da (ele traduz os do ASF e os
    // Vorbis comment do FLAC para os mesmos: title, artist, album, album_artist...)
    static const std::map<std::string, std::string> ffmpeg = {
        {"Title", "title"}, {"Author", "artist"}, {"WM/AlbumArtist", "album_artist"},
        {"WM/AlbumTitle", "album"}, {"WM/Genre", "genre"}, {"WM/Year", "date"},
        {"WM/TrackNumber", "track"}, {"WM/Composer", "composer"}, {"WM/PartOfSet", "disc"},
    };
    std::vector<std::string> chaves;
    auto f = ffmpeg.find(atributo);
    if (f != ffmpeg.end()) chaves.push_back(f->second);
    std::string vorbis = EditorFlac::NomeVorbis(atributo);
    if (!vorbis.empty()) chaves.push_back(Base::Minusculas(vorbis));
    chaves.push_back(Base::Minusculas(atributo));
    for (const std::string &c : chaves)
    {
        auto i = _impl->tags.find(c);
        if (i != _impl->tags.end() && !i->second.empty()) return i->second;
    }
    return "";
}

// PENDENCIA (video): o original mostrava o video na VagaVideo do rodape (110 px) e no palco do EM
// EXECUCAO, e em tela cheia com a barra do WMP. Aqui o fluxo de video e reconhecido (MidiaEhVideo,
// LarguraVideo/AlturaVideo, o palco aparece) e o audio toca, mas a imagem nao e decodificada nem
// desenhada. O acervo tem 3 videos (.mpg, MPEG) em DADOS\Musicas. Para desenhar: decodificar o
// fluxo de video neste fio, sincronizar pelo relogio da placa (PosicaoDaPlaca), converter para RGBA
// com libswscale (ou YUV direto num shader da GT210) e entregar o quadro a quem desenha a
// MolduraVideo (ver Vistas/MainWindowTocador.cpp, PosicionarVideo).

} // namespace ARCA::Interop

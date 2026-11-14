// ARCA no xOS - Interop/ScannerSane (de Interop\ScannerWia.cs). Ver ScannerSane.h.
#include "ScannerSane.h"

#include "Base/Excecoes.h"
#include "Base/Texto.h"

#include <sane/sane.h>
#include <sane/saneopts.h>
#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <vector>

namespace ARCA::Interop {

using Base::InvalidOperationException;
using Base::OperationCanceledException;

std::function<std::string(double &)> ScannerSane::DialogoComum;

namespace {

std::mutex trava;          // um uso do scanner de cada vez

struct Sessao
{
    bool iniciado = false;
    Sessao()
    {
        SANE_Int versao = 0;
        if (sane_init(&versao, nullptr) != SANE_STATUS_GOOD)
            throw InvalidOperationException("O componente de digitalização (SANE) não está disponível nesta máquina.");
        iniciado = true;
    }
    ~Sessao()
    {
        if (iniciado) sane_exit();
    }
};

std::string Mensagem(SANE_Status s) { return sane_strstatus(s); }

// o primeiro scanner (o WIA tambem pega o unico que ha, com AlwaysSelectDevice = false)
std::string Primeiro(std::string *nome)
{
    const SANE_Device **lista = nullptr;
    if (sane_get_devices(&lista, SANE_TRUE) != SANE_STATUS_GOOD || !lista || !lista[0]) return "";
    if (nome) *nome = std::string(lista[0]->vendor ? lista[0]->vendor : "") + " " + (lista[0]->model ? lista[0]->model : "");
    return lista[0]->name;
}

int Opcao(SANE_Handle h, const char *nome)
{
    SANE_Int n = 0;
    if (sane_control_option(h, 0, SANE_ACTION_GET_VALUE, &n, nullptr) != SANE_STATUS_GOOD) return -1;
    for (int i = 1; i < n; i++)
    {
        const SANE_Option_Descriptor *d = sane_get_option_descriptor(h, i);
        if (d && d->name && strcmp(d->name, nome) == 0) return i;
    }
    return -1;
}

void PoeTexto(SANE_Handle h, const char *nome, const char *valor)
{
    int i = Opcao(h, nome);
    if (i < 0) return;
    std::vector<char> v(valor, valor + strlen(valor) + 1);
    sane_control_option(h, i, SANE_ACTION_SET_VALUE, v.data(), nullptr);
}

void PoeNumero(SANE_Handle h, const char *nome, double valor)
{
    int i = Opcao(h, nome);
    if (i < 0) return;
    const SANE_Option_Descriptor *d = sane_get_option_descriptor(h, i);
    SANE_Word w = d->type == SANE_TYPE_FIXED ? SANE_FIX(valor) : (SANE_Word) valor;
    sane_control_option(h, i, SANE_ACTION_SET_VALUE, &w, nullptr);
}

double LeNumero(SANE_Handle h, const char *nome, double padrao)
{
    int i = Opcao(h, nome);
    if (i < 0) return padrao;
    const SANE_Option_Descriptor *d = sane_get_option_descriptor(h, i);
    SANE_Word w = 0;
    if (sane_control_option(h, i, SANE_ACTION_GET_VALUE, &w, nullptr) != SANE_STATUS_GOOD) return padrao;
    return d->type == SANE_TYPE_FIXED ? SANE_UNFIX(w) : (double) w;
}

// o maximo da faixa de uma opcao (a mesa: br-x e br-y)
double Maximo(SANE_Handle h, const char *nome, double padrao)
{
    int i = Opcao(h, nome);
    if (i < 0) return padrao;
    const SANE_Option_Descriptor *d = sane_get_option_descriptor(h, i);
    if (d->constraint_type != SANE_CONSTRAINT_RANGE || !d->constraint.range) return padrao;
    return d->type == SANE_TYPE_FIXED ? SANE_UNFIX(d->constraint.range->max) : (double) d->constraint.range->max;
}

// ---- PNG sem perda, com o dpi carimbado (pHYs) ----
void Pedaco(FILE *f, const char *tipo, const std::vector<uint8_t> &dados)
{
    uint8_t t[4] = {(uint8_t) (dados.size() >> 24), (uint8_t) (dados.size() >> 16), (uint8_t) (dados.size() >> 8),
                    (uint8_t) dados.size()};
    fwrite(t, 1, 4, f);
    fwrite(tipo, 1, 4, f);
    if (!dados.empty()) fwrite(dados.data(), 1, dados.size(), f);
    uLong crc = crc32(0, (const Bytef *) tipo, 4);
    if (!dados.empty()) crc = crc32(crc, dados.data(), (uInt) dados.size());
    uint8_t c[4] = {(uint8_t) (crc >> 24), (uint8_t) (crc >> 16), (uint8_t) (crc >> 8), (uint8_t) crc};
    fwrite(c, 1, 4, f);
}

void U32(std::vector<uint8_t> &v, uint32_t x)
{
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}

void GravaPng(const std::string &caminho, const std::vector<uint8_t> &rgb, int w, int h, int canais, int dpi)
{
    // filtro 0 em cada linha
    std::vector<uint8_t> cru;
    cru.reserve((size_t) h * (w * canais + 1));
    for (int y = 0; y < h; y++)
    {
        cru.push_back(0);
        cru.insert(cru.end(), rgb.begin() + (size_t) y * w * canais, rgb.begin() + (size_t) (y + 1) * w * canais);
    }
    uLongf n = compressBound(cru.size());
    std::vector<uint8_t> z(n);
    if (compress2(z.data(), &n, cru.data(), cru.size(), 6) != Z_OK)
        throw InvalidOperationException("O scanner falhou: não foi possível compactar a imagem.");
    z.resize(n);

    FILE *f = fopen(caminho.c_str(), "wb");
    if (!f) throw InvalidOperationException("O scanner falhou: não foi possível gravar " + caminho);
    static const uint8_t assinatura[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(assinatura, 1, 8, f);
    std::vector<uint8_t> ihdr;
    U32(ihdr, w);
    U32(ihdr, h);
    ihdr.push_back(8);                          // bits por canal
    ihdr.push_back(canais == 3 ? 2 : 0);        // RGB ou cinza
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    Pedaco(f, "IHDR", ihdr);
    if (dpi > 0)
    {
        std::vector<uint8_t> phys;
        uint32_t ppm = (uint32_t) (dpi / 0.0254 + 0.5);
        U32(phys, ppm);
        U32(phys, ppm);
        phys.push_back(1);                      // metros
        Pedaco(f, "pHYs", phys);
    }
    Pedaco(f, "IDAT", z);
    Pedaco(f, "IEND", {});
    bool ok = fflush(f) == 0;
    fclose(f);
    if (!ok) throw InvalidOperationException("O scanner falhou: não foi possível gravar " + caminho);
}

std::string ArquivoTemporario()
{
    std::random_device r;
    char b[64];
    snprintf(b, sizeof b, "/tmp/ARCA_scan_%08x%08x.png", r(), r());
    return b;
}

} // namespace

std::string ScannerSane::Dispositivo(std::string *strNome)
{
    std::lock_guard<std::mutex> g(trava);
    Sessao s;
    return Primeiro(strNome);
}

void ScannerSane::Mesa(double &dblLarguraMm, double &dblAlturaMm)
{
    std::lock_guard<std::mutex> g(trava);
    Sessao s;
    dblLarguraMm = 216.069;
    dblAlturaMm = 297.011;
    std::string d = Primeiro(nullptr);
    if (d.empty()) return;
    SANE_Handle h = nullptr;
    if (sane_open(d.c_str(), &h) != SANE_STATUS_GOOD) return;
    dblLarguraMm = Maximo(h, SANE_NAME_SCAN_BR_X, dblLarguraMm);
    dblAlturaMm = Maximo(h, SANE_NAME_SCAN_BR_Y, dblAlturaMm);
    sane_close(h);
}

std::string ScannerSane::DigitalizarArea(double x, double y, double largura, double altura, int intDpi,
                                         const std::function<void(double)> &progresso,
                                         const std::function<bool()> &cancelou)
{
    std::lock_guard<std::mutex> g(trava);
    Sessao s;
    std::string d = Primeiro(nullptr);
    if (d.empty())
        throw InvalidOperationException("Nenhum scanner encontrado. Confira se está ligado e conectado, e tente de novo.");
    SANE_Handle h = nullptr;
    SANE_Status st = sane_open(d.c_str(), &h);
    if (st != SANE_STATUS_GOOD)
        throw InvalidOperationException("O scanner falhou: " + Mensagem(st));

    struct Fecha
    {
        SANE_Handle h;
        ~Fecha() { sane_close(h); }
    } fecha{h};

    PoeTexto(h, SANE_NAME_SCAN_SOURCE, "Flatbed");
    PoeTexto(h, SANE_NAME_SCAN_MODE, SANE_VALUE_SCAN_MODE_COLOR);   // WiaImageIntent.ColorIntent
    PoeNumero(h, SANE_NAME_SCAN_RESOLUTION, intDpi);
    double mesaL = Maximo(h, SANE_NAME_SCAN_BR_X, 216.069), mesaA = Maximo(h, SANE_NAME_SCAN_BR_Y, 297.011);
    if (largura <= 0 || altura <= 0) { x = 0; y = 0; largura = mesaL; altura = mesaA; }
    PoeNumero(h, SANE_NAME_SCAN_TL_X, x);
    PoeNumero(h, SANE_NAME_SCAN_TL_Y, y);
    PoeNumero(h, SANE_NAME_SCAN_BR_X, std::min(mesaL, x + largura));
    PoeNumero(h, SANE_NAME_SCAN_BR_Y, std::min(mesaA, y + altura));
    double dpiReal = LeNumero(h, SANE_NAME_SCAN_RESOLUTION, intDpi);

    st = sane_start(h);
    if (st == SANE_STATUS_CANCELLED) throw OperationCanceledException();
    if (st != SANE_STATUS_GOOD) throw InvalidOperationException("O scanner falhou: " + Mensagem(st));

    SANE_Parameters p;
    if (sane_get_parameters(h, &p) != SANE_STATUS_GOOD || p.depth != 8 ||
        !(p.format == SANE_FRAME_RGB || p.format == SANE_FRAME_GRAY))
    {
        sane_cancel(h);
        throw InvalidOperationException("O scanner falhou: formato de imagem que o ARCA não lê.");
    }
    int canais = p.format == SANE_FRAME_RGB ? 3 : 1;
    std::vector<uint8_t> dados;
    int64_t esperado = p.lines > 0 ? (int64_t) p.lines * p.bytes_per_line : -1;
    if (esperado > 0) dados.reserve(esperado);
    std::vector<uint8_t> buf(1 << 16);
    for (;;)
    {
        if (cancelou && cancelou())
        {
            sane_cancel(h);
            throw OperationCanceledException();
        }
        SANE_Int n = 0;
        st = sane_read(h, buf.data(), (SANE_Int) buf.size(), &n);
        if (st == SANE_STATUS_EOF) break;
        if (st != SANE_STATUS_GOOD)
        {
            sane_cancel(h);
            if (st == SANE_STATUS_CANCELLED) throw OperationCanceledException();
            throw InvalidOperationException("O scanner falhou: " + Mensagem(st));
        }
        dados.insert(dados.end(), buf.begin(), buf.begin() + n);
        if (progresso && esperado > 0) progresso(std::min(1.0, (double) dados.size() / esperado));
    }
    sane_cancel(h);

    int w = p.pixels_per_line, linhas = (int) (dados.size() / p.bytes_per_line);
    if (w <= 0 || linhas <= 0) throw InvalidOperationException("O scanner falhou: a imagem veio vazia.");
    // tira o enchimento do fim de cada linha, se houver
    std::vector<uint8_t> px((size_t) w * linhas * canais);
    for (int yl = 0; yl < linhas; yl++)
        memcpy(&px[(size_t) yl * w * canais], &dados[(size_t) yl * p.bytes_per_line], (size_t) w * canais);

    std::string strArquivo = ArquivoTemporario();
    GravaPng(strArquivo, px, w, linhas, canais, (int) (dpiReal + 0.5));
    if (progresso) progresso(1.0);
    return strArquivo;
}

std::string ScannerSane::Previa(const std::function<void(double)> &progresso, const std::function<bool()> &cancelou)
{
    return DigitalizarArea(0, 0, 0, 0, ResolucaoPrevia, progresso, cancelou);
}

std::string ScannerSane::Digitalizar(double &dblDpi)
{
    dblDpi = 0;
    if (DialogoComum) return DialogoComum(dblDpi);
    // LINUX: sem dialogo registrado (linha de comando, testes): a mesa inteira na resolucao padrao
    std::string r = DigitalizarArea(0, 0, 0, 0, ResolucaoPadrao, nullptr, nullptr);
    dblDpi = ResolucaoPadrao;
    return r;
}

} // namespace ARCA::Interop

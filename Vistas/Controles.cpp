// ARCA no xOS - Vistas/Controles: o layout do WPF (Measure/Arrange), o desenho dos gabaritos do
// Tema.xaml e os controles basicos. As listas (ListBox, ComboBox, DataGrid) ficam em
// ControlesListas.cpp. Nao vem de um .cs do ARCA: e o WPF que o ARCA usava.
#include "Controles.h"
#include "Janela.h"

#ifndef NANOVG_GLES2
#define NANOVG_GLES2
#endif
#include "nanovg/nanovg.h"
#include "nanovg/stb_image.h"

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <stdexcept>

#include <jpeglib.h>
#include <zlib.h>

namespace ARCA::Vistas {

// =====================================================================================
// apoio
// =====================================================================================
double Agora()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

size_t ByteDoCaractere(const std::string &s, int caractere)
{
    size_t i = 0;
    int c = 0;
    while (i < s.size() && c < caractere)
    {
        unsigned char b = (unsigned char) s[i];
        i += b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
        c++;
    }
    return std::min(i, s.size());
}

int CaracteresEm(const std::string &s)
{
    int n = 0;
    for (unsigned char b : s)
        if ((b & 0xC0) != 0x80) n++;
    return n;
}

namespace {

NVGcolor CorNvg(Color c, double opacidade = 1)
{
    return nvgRGBA(c.R, c.G, c.B, (unsigned char) std::clamp(c.A * opacidade, 0.0, 255.0));
}

double Arred(double v) { return std::floor(v + 0.5); }

bool Finito(double v) { return std::isfinite(v); }

} // namespace

// =====================================================================================
// BitmapSource
// =====================================================================================
namespace {

struct ErroJpeg
{
    jpeg_error_mgr pub;
    jmp_buf salto;
    char msg[JMSG_LENGTH_MAX];
};

void SaiJpeg(j_common_ptr c)
{
    ErroJpeg *e = (ErroJpeg *) c->err;
    (*c->err->format_message)(c, e->msg);
    longjmp(e->salto, 1);
}

// media de area: cada pixel de destino e a media dos pixels de origem que ele cobre
void Reduz(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh)
{
    double ex = (double) sw / dw, ey = (double) sh / dh;
    if (ex <= 1 && ey <= 1)
    {
        // ampliar: bilinear
        for (int y = 0; y < dh; y++)
        {
            double fy = std::max(0.0, (y + 0.5) * ey - 0.5);
            int y0 = std::min((int) fy, sh - 1), y1 = std::min(y0 + 1, sh - 1);
            double ty = fy - y0;
            for (int x = 0; x < dw; x++)
            {
                double fx = std::max(0.0, (x + 0.5) * ex - 0.5);
                int x0 = std::min((int) fx, sw - 1), x1 = std::min(x0 + 1, sw - 1);
                double tx = fx - x0;
                for (int k = 0; k < 4; k++)
                {
                    double a = src[(y0 * sw + x0) * 4 + k] * (1 - tx) + src[(y0 * sw + x1) * 4 + k] * tx;
                    double b = src[(y1 * sw + x0) * 4 + k] * (1 - tx) + src[(y1 * sw + x1) * 4 + k] * tx;
                    dst[(y * dw + x) * 4 + k] = (uint8_t) std::clamp(a * (1 - ty) + b * ty + 0.5, 0.0, 255.0);
                }
            }
        }
        return;
    }
    std::vector<double> soma((size_t) dw * 4);
    for (int y = 0; y < dh; y++)
    {
        double y0 = y * ey, y1 = (y + 1) * ey;
        std::fill(soma.begin(), soma.end(), 0.0);
        double pesoY = 0;
        for (int sy = (int) y0; sy < std::min(sh, (int) std::ceil(y1)); sy++)
        {
            double wy = std::min(y1, sy + 1.0) - std::max(y0, (double) sy);
            if (wy <= 0) continue;
            pesoY += wy;
            const uint8_t *linha = src + (size_t) sy * sw * 4;
            for (int x = 0; x < dw; x++)
            {
                double x0 = x * ex, x1 = (x + 1) * ex;
                double acc[4] = {0, 0, 0, 0}, pesoX = 0;
                for (int sx = (int) x0; sx < std::min(sw, (int) std::ceil(x1)); sx++)
                {
                    double wx = std::min(x1, sx + 1.0) - std::max(x0, (double) sx);
                    if (wx <= 0) continue;
                    pesoX += wx;
                    for (int k = 0; k < 4; k++) acc[k] += linha[sx * 4 + k] * wx;
                }
                if (pesoX > 0)
                    for (int k = 0; k < 4; k++) soma[x * 4 + k] += acc[k] / pesoX * wy;
            }
        }
        for (int x = 0; x < dw; x++)
            for (int k = 0; k < 4; k++)
                dst[((size_t) y * dw + x) * 4 + k] = (uint8_t) std::clamp(soma[x * 4 + k] / std::max(pesoY, 1e-9) + 0.5, 0.0, 255.0);
    }
}

void AlvoDecodificacao(int w, int h, int dw, int dh, int &ow, int &oh)
{
    ow = w;
    oh = h;
    if (dw > 0 && dh > 0) ow = dw, oh = dh;
    else if (dw > 0) ow = dw, oh = std::max(1, (int) std::lround((double) h * dw / w));
    else if (dh > 0) oh = dh, ow = std::max(1, (int) std::lround((double) w * dh / h));
}

std::shared_ptr<BitmapSource> DecodificaJpeg(const uint8_t *dados, size_t n, int dw, int dh)
{
    auto img = std::make_shared<BitmapSource>();
    std::vector<uint8_t> linha;
    jpeg_decompress_struct c;
    ErroJpeg erro;
    std::memset(&c, 0, sizeof c);
    c.err = jpeg_std_error(&erro.pub);
    erro.pub.error_exit = SaiJpeg;
    erro.msg[0] = 0;
    if (setjmp(erro.salto))
    {
        jpeg_destroy_decompress(&c);
        throw std::runtime_error(std::string("JPEG inválido: ") + erro.msg);
    }
    jpeg_create_decompress(&c);
    jpeg_mem_src(&c, dados, (unsigned long) n);
    jpeg_read_header(&c, TRUE);
    int alvoW, alvoH;
    AlvoDecodificacao((int) c.image_width, (int) c.image_height, dw, dh, alvoW, alvoH);
    // a reducao do proprio decodificador (1/2, 1/4, 1/8): bem mais rapida para miniaturas de
    // digitalizacoes grandes; o resto da reducao e feito depois, pela media de area
    unsigned den = 1;
    while (den < 8 && (int) (c.image_width / (den * 2)) >= alvoW && (int) (c.image_height / (den * 2)) >= alvoH) den *= 2;
    c.scale_num = 1;
    c.scale_denom = den;
    bool cmyk = c.jpeg_color_space == JCS_CMYK || c.jpeg_color_space == JCS_YCCK;
    c.out_color_space = cmyk ? JCS_CMYK : JCS_RGB;
    jpeg_start_decompress(&c);
    int w = (int) c.output_width, h = (int) c.output_height, comp = c.output_components;
    img->PixelWidth = w;
    img->PixelHeight = h;
    img->Pixels.resize((size_t) w * h * 4);
    linha.resize((size_t) w * comp);
    while (c.output_scanline < c.output_height)
    {
        JSAMPROW p = linha.data();
        int y = (int) c.output_scanline;
        jpeg_read_scanlines(&c, &p, 1);
        uint8_t *d = &img->Pixels[(size_t) y * w * 4];
        for (int x = 0; x < w; x++)
        {
            if (cmyk)
            {
                // o Adobe grava CMYK invertido
                int C = linha[x * 4], M = linha[x * 4 + 1], Y = linha[x * 4 + 2], K = linha[x * 4 + 3];
                d[x * 4] = (uint8_t) (C * K / 255);
                d[x * 4 + 1] = (uint8_t) (M * K / 255);
                d[x * 4 + 2] = (uint8_t) (Y * K / 255);
            }
            else if (comp == 1)
                d[x * 4] = d[x * 4 + 1] = d[x * 4 + 2] = linha[x];
            else
            {
                d[x * 4] = linha[x * 3];
                d[x * 4 + 1] = linha[x * 3 + 1];
                d[x * 4 + 2] = linha[x * 3 + 2];
            }
            d[x * 4 + 3] = 255;
        }
    }
    if (c.density_unit == 1 && c.X_density > 0) img->DpiX = c.X_density, img->DpiY = c.Y_density > 0 ? c.Y_density : c.X_density;
    else if (c.density_unit == 2 && c.X_density > 0) img->DpiX = c.X_density * 2.54, img->DpiY = (c.Y_density > 0 ? c.Y_density : c.X_density) * 2.54;
    jpeg_finish_decompress(&c);
    jpeg_destroy_decompress(&c);
    if (alvoW != w || alvoH != h)
    {
        if (dw > 0 || dh > 0)
            return img->Redimensionada(alvoW, alvoH);
    }
    return img;
}

} // namespace

BitmapSource::~BitmapSource()
{
    if (_textura) Application::ApagaTexturaDepois(_textura);
}

std::shared_ptr<BitmapSource> BitmapSource::DeBytes(const uint8_t *dados, size_t n, int dw, int dh)
{
    if (!dados || n < 4) throw std::runtime_error("Imagem vazia.");
    if (dados[0] == 0xFF && dados[1] == 0xD8) return DecodificaJpeg(dados, n, dw, dh);
    int w = 0, h = 0, comp = 0;
    unsigned char *p = stbi_load_from_memory(dados, (int) n, &w, &h, &comp, 4);
    if (!p) throw std::runtime_error(std::string("Formato de imagem não reconhecido (") + stbi_failure_reason() + ").");
    auto img = std::make_shared<BitmapSource>();
    img->PixelWidth = w;
    img->PixelHeight = h;
    img->Pixels.assign(p, p + (size_t) w * h * 4);
    stbi_image_free(p);
    // o dpi do PNG (pHYs em pixels por metro), que o WPF le e o stb_image nao
    if (n > 8 && dados[0] == 0x89 && dados[1] == 'P' && dados[2] == 'N' && dados[3] == 'G')
    {
        auto u32 = [](const uint8_t *q) { return (uint32_t) q[0] << 24 | (uint32_t) q[1] << 16 | (uint32_t) q[2] << 8 | q[3]; };
        for (size_t i = 8; i + 8 <= n;)
        {
            uint32_t tam = u32(dados + i);
            const uint8_t *tipo = dados + i + 4;
            if (std::memcmp(tipo, "IDAT", 4) == 0 || std::memcmp(tipo, "IEND", 4) == 0) break;
            if (std::memcmp(tipo, "pHYs", 4) == 0 && tam >= 9 && i + 8 + 9 <= n)
            {
                uint32_t px = u32(dados + i + 8), py = u32(dados + i + 12);
                if (dados[i + 16] == 1 && px > 0)
                    img->DpiX = px * 0.0254, img->DpiY = (py > 0 ? py : px) * 0.0254;
                break;
            }
            if (tam > n) break;
            i += 12 + (size_t) tam;
        }
    }
    int aw, ah;
    AlvoDecodificacao(w, h, dw, dh, aw, ah);
    if (aw != w || ah != h) return img->Redimensionada(aw, ah);
    return img;
}

std::shared_ptr<BitmapSource> BitmapSource::DeBytes(const std::vector<uint8_t> &dados, int dw, int dh)
{
    return DeBytes(dados.data(), dados.size(), dw, dh);
}

std::shared_ptr<BitmapSource> BitmapSource::DeArquivo(const std::string &caminho, int dw, int dh)
{
    std::ifstream f(caminho, std::ios::binary);
    if (!f) throw std::runtime_error("Não foi possível abrir o arquivo '" + caminho + "'.");
    std::vector<uint8_t> v((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return DeBytes(v, dw, dh);
}

std::shared_ptr<BitmapSource> BitmapSource::CreateBgra32(int w, int h, double dpiX, double dpiY, const uint8_t *bgra, int passo)
{
    auto img = std::make_shared<BitmapSource>();
    img->PixelWidth = w;
    img->PixelHeight = h;
    img->DpiX = dpiX;
    img->DpiY = dpiY;
    img->Pixels.resize((size_t) w * h * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            const uint8_t *s = bgra + (size_t) y * passo + x * 4;
            uint8_t *d = &img->Pixels[((size_t) y * w + x) * 4];
            d[0] = s[2], d[1] = s[1], d[2] = s[0], d[3] = s[3];
        }
    return img;
}

std::shared_ptr<BitmapSource> BitmapSource::CreateRgba(int w, int h, std::vector<uint8_t> rgba)
{
    auto img = std::make_shared<BitmapSource>();
    img->PixelWidth = w;
    img->PixelHeight = h;
    img->Pixels = std::move(rgba);
    img->Pixels.resize((size_t) w * h * 4);
    return img;
}

std::shared_ptr<BitmapSource> BitmapSource::Girada(int graus) const
{
    graus = ((graus % 360) + 360) % 360;
    auto img = std::make_shared<BitmapSource>();
    bool troca = graus == 90 || graus == 270;
    int w = PixelWidth, h = PixelHeight;
    img->PixelWidth = troca ? h : w;
    img->PixelHeight = troca ? w : h;
    img->DpiX = troca ? DpiY : DpiX;
    img->DpiY = troca ? DpiX : DpiY;
    img->Pixels.resize(Pixels.size());
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            int nx = x, ny = y;
            if (graus == 90) nx = h - 1 - y, ny = x;
            else if (graus == 180) nx = w - 1 - x, ny = h - 1 - y;
            else if (graus == 270) nx = y, ny = w - 1 - x;
            std::memcpy(&img->Pixels[((size_t) ny * img->PixelWidth + nx) * 4], &Pixels[((size_t) y * w + x) * 4], 4);
        }
    return img;
}

std::shared_ptr<BitmapSource> BitmapSource::Redimensionada(int w, int h) const
{
    auto img = std::make_shared<BitmapSource>();
    w = std::max(1, w);
    h = std::max(1, h);
    img->PixelWidth = w;
    img->PixelHeight = h;
    img->DpiX = DpiX * w / std::max(1, PixelWidth);
    img->DpiY = DpiY * h / std::max(1, PixelHeight);
    img->Pixels.resize((size_t) w * h * 4);
    if (PixelWidth > 0 && PixelHeight > 0) Reduz(Pixels.data(), PixelWidth, PixelHeight, img->Pixels.data(), w, h);
    return img;
}

std::shared_ptr<BitmapSource> BitmapSource::Escalada(double e) const
{
    return Redimensionada((int) std::lround(PixelWidth * e), (int) std::lround(PixelHeight * e));
}

std::shared_ptr<BitmapSource> BitmapSource::Cortada(Int32Rect r) const
{
    r.X = std::clamp(r.X, 0, PixelWidth);
    r.Y = std::clamp(r.Y, 0, PixelHeight);
    r.Width = std::clamp(r.Width, 0, PixelWidth - r.X);
    r.Height = std::clamp(r.Height, 0, PixelHeight - r.Y);
    auto img = std::make_shared<BitmapSource>();
    img->PixelWidth = r.Width;
    img->PixelHeight = r.Height;
    img->DpiX = DpiX;
    img->DpiY = DpiY;
    img->Pixels.resize((size_t) r.Width * r.Height * 4);
    for (int y = 0; y < r.Height; y++)
        std::memcpy(&img->Pixels[(size_t) y * r.Width * 4], &Pixels[((size_t) (y + r.Y) * PixelWidth + r.X) * 4], (size_t) r.Width * 4);
    return img;
}

std::vector<uint8_t> BitmapSource::CodificaJpeg(int qualidade) const
{
    std::vector<uint8_t> linha((size_t) PixelWidth * 3);
    unsigned char *saida = nullptr;
    unsigned long tamanho = 0;
    jpeg_compress_struct c;
    ErroJpeg erro;
    std::memset(&c, 0, sizeof c);
    c.err = jpeg_std_error(&erro.pub);
    erro.pub.error_exit = SaiJpeg;
    if (setjmp(erro.salto))
    {
        jpeg_destroy_compress(&c);
        free(saida);
        throw std::runtime_error(std::string("Falha ao codificar JPEG: ") + erro.msg);
    }
    jpeg_create_compress(&c);
    jpeg_mem_dest(&c, &saida, &tamanho);
    c.image_width = (JDIMENSION) PixelWidth;
    c.image_height = (JDIMENSION) PixelHeight;
    c.input_components = 3;
    c.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c);
    jpeg_set_quality(&c, std::clamp(qualidade, 1, 100), TRUE);
    c.density_unit = 1;
    c.X_density = (UINT16) std::lround(DpiX);
    c.Y_density = (UINT16) std::lround(DpiY);
    jpeg_start_compress(&c, TRUE);
    while (c.next_scanline < c.image_height)
    {
        const uint8_t *s = &Pixels[(size_t) c.next_scanline * PixelWidth * 4];
        for (int x = 0; x < PixelWidth; x++)
        {
            // o alfa sobre branco, como o JpegBitmapEncoder faz com Bgra32
            int a = s[x * 4 + 3];
            for (int k = 0; k < 3; k++) linha[x * 3 + k] = (uint8_t) ((s[x * 4 + k] * a + 255 * (255 - a)) / 255);
        }
        JSAMPROW p = linha.data();
        jpeg_write_scanlines(&c, &p, 1);
    }
    jpeg_finish_compress(&c);
    jpeg_destroy_compress(&c);
    std::vector<uint8_t> r(saida, saida + tamanho);
    free(saida);
    return r;
}

// ---- os codificadores sem perda (PngBitmapEncoder, BmpBitmapEncoder, TiffBitmapEncoder) ----
namespace {

bool TemAlfa(const BitmapSource &b)
{
    for (size_t i = 3; i < b.Pixels.size(); i += 4)
        if (b.Pixels[i] != 255) return true;
    return false;
}

void Be32(std::vector<uint8_t> &v, uint32_t x)
{
    v.push_back((uint8_t) (x >> 24)); v.push_back((uint8_t) (x >> 16)); v.push_back((uint8_t) (x >> 8)); v.push_back((uint8_t) x);
}
void Le16(std::vector<uint8_t> &v, uint32_t x) { v.push_back((uint8_t) x); v.push_back((uint8_t) (x >> 8)); }
void Le32(std::vector<uint8_t> &v, uint32_t x) { Le16(v, x & 0xFFFF); Le16(v, x >> 16); }

void PedacoPng(std::vector<uint8_t> &saida, const char *tipo, const std::vector<uint8_t> &dados)
{
    Be32(saida, (uint32_t) dados.size());
    size_t inicio = saida.size();
    saida.insert(saida.end(), tipo, tipo + 4);
    saida.insert(saida.end(), dados.begin(), dados.end());
    uLong crc = crc32(0, saida.data() + inicio, (uInt) (saida.size() - inicio));
    Be32(saida, (uint32_t) crc);
}

} // namespace

std::vector<uint8_t> BitmapSource::CodificaPng() const
{
    if (PixelWidth <= 0 || PixelHeight <= 0) throw std::runtime_error("Imagem vazia.");
    int canais = TemAlfa(*this) ? 4 : 3;
    std::vector<uint8_t> cru;
    cru.reserve((size_t) PixelHeight * (PixelWidth * canais + 1));
    for (int y = 0; y < PixelHeight; y++)
    {
        cru.push_back(0);   // filtro 0
        const uint8_t *s = &Pixels[(size_t) y * PixelWidth * 4];
        for (int x = 0; x < PixelWidth; x++) cru.insert(cru.end(), s + x * 4, s + x * 4 + canais);
    }
    uLongf n = compressBound((uLong) cru.size());
    std::vector<uint8_t> z(n);
    if (compress2(z.data(), &n, cru.data(), (uLong) cru.size(), 6) != Z_OK) throw std::runtime_error("Falha ao codificar PNG.");
    z.resize(n);
    std::vector<uint8_t> saida = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    Be32(ihdr, (uint32_t) PixelWidth);
    Be32(ihdr, (uint32_t) PixelHeight);
    ihdr.push_back(8);
    ihdr.push_back(canais == 4 ? 6 : 2);
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    PedacoPng(saida, "IHDR", ihdr);
    std::vector<uint8_t> phys;
    Be32(phys, (uint32_t) std::lround(DpiX / 0.0254));
    Be32(phys, (uint32_t) std::lround(DpiY / 0.0254));
    phys.push_back(1);
    PedacoPng(saida, "pHYs", phys);
    PedacoPng(saida, "IDAT", z);
    PedacoPng(saida, "IEND", {});
    return saida;
}

std::vector<uint8_t> BitmapSource::CodificaBmp() const
{
    if (PixelWidth <= 0 || PixelHeight <= 0) throw std::runtime_error("Imagem vazia.");
    uint32_t passo = ((uint32_t) PixelWidth * 3 + 3) & ~3u;
    uint32_t tamDados = passo * (uint32_t) PixelHeight;
    std::vector<uint8_t> s;
    s.reserve(54 + tamDados);
    s.push_back('B'); s.push_back('M');
    Le32(s, 54 + tamDados); Le32(s, 0); Le32(s, 54);
    Le32(s, 40); Le32(s, (uint32_t) PixelWidth); Le32(s, (uint32_t) PixelHeight);
    Le16(s, 1); Le16(s, 24); Le32(s, 0); Le32(s, tamDados);
    Le32(s, (uint32_t) std::lround(DpiX / 0.0254)); Le32(s, (uint32_t) std::lround(DpiY / 0.0254));
    Le32(s, 0); Le32(s, 0);
    for (int y = PixelHeight - 1; y >= 0; y--)
    {
        const uint8_t *p = &Pixels[(size_t) y * PixelWidth * 4];
        size_t ini = s.size();
        for (int x = 0; x < PixelWidth; x++)
        {
            int a = p[x * 4 + 3];   // o alfa sobre branco, como no JPEG
            for (int k = 2; k >= 0; k--) s.push_back((uint8_t) ((p[x * 4 + k] * a + 255 * (255 - a)) / 255));
        }
        while (s.size() - ini < passo) s.push_back(0);
    }
    return s;
}

std::vector<uint8_t> BitmapSource::CodificaTiffLzw() const
{
    if (PixelWidth <= 0 || PixelHeight <= 0) throw std::runtime_error("Imagem vazia.");
    int canais = TemAlfa(*this) ? 4 : 3;
    // ---- LZW do TIFF (codigos de 9 a 12 bits, MSB primeiro, "early change" como a libtiff) ----
    std::vector<uint8_t> lzw;
    uint32_t acum = 0;
    int nAcum = 0, nbits = 9;
    auto poe = [&](int codigo) {
        acum = (acum << nbits) | (uint32_t) codigo;
        nAcum += nbits;
        while (nAcum >= 8)
        {
            lzw.push_back((uint8_t) (acum >> (nAcum - 8)));
            nAcum -= 8;
        }
        acum &= (1u << nAcum) - 1;
    };
    const int LIMPA = 256, FIM = 257, TAM = 9001;
    std::vector<int32_t> chave(TAM), codigo(TAM);
    std::vector<uint32_t> geracao(TAM, 0);
    uint32_t ger = 1;
    int proximo = 258;
    poe(LIMPA);
    int prefixo = -1;
    for (int y = 0; y < PixelHeight; y++)
    {
        const uint8_t *p = &Pixels[(size_t) y * PixelWidth * 4];
        for (int x = 0; x < PixelWidth; x++)
            for (int k = 0; k < canais; k++)
            {
                int c = p[x * 4 + k];
                if (prefixo < 0) { prefixo = c; continue; }
                int32_t k2 = (prefixo << 8) | c;
                int h = (int) (((uint32_t) k2 * 2654435761u) % TAM);
                bool achou = false;
                while (geracao[h] == ger)
                {
                    if (chave[h] == k2) { achou = true; break; }
                    h = (h + 1) % TAM;
                }
                if (achou) { prefixo = codigo[h]; continue; }
                poe(prefixo);
                geracao[h] = ger;
                chave[h] = k2;
                codigo[h] = proximo++;
                if (proximo > (1 << nbits) - 1 && nbits < 12) nbits++;
                if (proximo >= 4094)
                {
                    poe(LIMPA);
                    nbits = 9;
                    proximo = 258;
                    ger++;
                }
                prefixo = c;
            }
    }
    if (prefixo >= 0) poe(prefixo);
    poe(FIM);
    if (nAcum > 0) lzw.push_back((uint8_t) (acum << (8 - nAcum)));

    // ---- o arquivo: cabecalho, dados, IFD ----
    std::vector<uint8_t> s = {'I', 'I', 42, 0};
    uint32_t inicioDados = 8;
    Le32(s, 0);                       // offset do IFD, preenchido abaixo
    s.insert(s.end(), lzw.begin(), lzw.end());
    if (s.size() & 1) s.push_back(0);
    uint32_t offBits = (uint32_t) s.size();
    for (int k = 0; k < canais; k++) Le16(s, 8);
    uint32_t offResX = (uint32_t) s.size();
    Le32(s, (uint32_t) std::lround(DpiX * 100)); Le32(s, 100);
    uint32_t offResY = (uint32_t) s.size();
    Le32(s, (uint32_t) std::lround(DpiY * 100)); Le32(s, 100);
    uint32_t offIfd = (uint32_t) s.size();
    s[4] = (uint8_t) offIfd; s[5] = (uint8_t) (offIfd >> 8); s[6] = (uint8_t) (offIfd >> 16); s[7] = (uint8_t) (offIfd >> 24);
    struct Entrada { uint16_t tag, tipo; uint32_t n, valor; };
    std::vector<Entrada> e = {
        {256, 4, 1, (uint32_t) PixelWidth}, {257, 4, 1, (uint32_t) PixelHeight}, {258, 3, (uint32_t) canais, offBits},
        {259, 3, 1, 5}, {262, 3, 1, 2}, {273, 4, 1, inicioDados}, {277, 3, 1, (uint32_t) canais},
        {278, 4, 1, (uint32_t) PixelHeight}, {279, 4, 1, (uint32_t) lzw.size()}, {282, 5, 1, offResX},
        {283, 5, 1, offResY}, {284, 3, 1, 1}, {296, 3, 1, 2}};
    if (canais == 4) e.push_back({338, 3, 1, 2});   // alfa nao associado
    Le16(s, (uint32_t) e.size());
    for (auto &x : e)
    {
        Le16(s, x.tag);
        Le16(s, x.tipo);
        Le32(s, x.n);
        if (x.tipo == 3 && x.n == 1) { Le16(s, x.valor); Le16(s, 0); }
        else Le32(s, x.valor);
    }
    Le32(s, 0);
    return s;
}

int BitmapSource::Textura(NVGcontext *vg)
{
    if (PixelWidth <= 0 || PixelHeight <= 0 || Pixels.size() < (size_t) PixelWidth * PixelHeight * 4) return 0;
    if (!_textura)
    {
        _textura = nvgCreateImageRGBA(vg, PixelWidth, PixelHeight, 0, Pixels.data());
        _versaoTextura = _versao;
    }
    else if (_versaoTextura != _versao)
    {
        nvgUpdateImage(vg, _textura, Pixels.data());
        _versaoTextura = _versao;
    }
    return _textura;
}

// =====================================================================================
// Texto
// =====================================================================================
namespace {

bool EhMono(const std::string &f)
{
    return f == Tema::FonteMono || f.find("Consolas") != std::string::npos || f.find("Courier") != std::string::npos ||
           f.find("Mono") != std::string::npos || f.find("Lucida Console") != std::string::npos;
}

int Face(NVGcontext *vg, const FonteTexto &f)
{
    // "sans", "sans-bold", "sans-semibold", "sans-italic", "sans-bolditalic", "mono", "mono-bold"
    static int ids[2][3][2];
    static bool feito = false;
    if (!feito)
    {
        const char *nomes[2][3][2] = {
            {{"sans", "sans-italic"}, {"sans-semibold", "sans-semibolditalic"}, {"sans-bold", "sans-bolditalic"}},
            {{"mono", "mono"}, {"mono-bold", "mono-bold"}, {"mono-bold", "mono-bold"}}};
        for (int a = 0; a < 2; a++)
            for (int b = 0; b < 3; b++)
                for (int c = 0; c < 2; c++)
                {
                    int id = nvgFindFont(vg, nomes[a][b][c]);
                    if (id < 0 && b == 1) id = nvgFindFont(vg, a ? "mono-bold" : (c ? "sans-bolditalic" : "sans-bold"));
                    if (id < 0 && c == 1) id = nvgFindFont(vg, a ? "mono" : "sans-italic");
                    if (id < 0) id = nvgFindFont(vg, a ? "mono" : "sans");
                    if (id < 0) id = nvgFindFont(vg, "sans");
                    ids[a][b][c] = id;
                }
        feito = ids[0][0][0] >= 0;
    }
    int a = EhMono(f.Familia) ? 1 : 0;
    int b = f.Peso == FontWeight::Normal ? 0 : f.Peso == FontWeight::SemiBold ? 1 : 2;
    int c = f.Estilo == FontStyle::Italic ? 1 : 0;
    return ids[a][b][c];
}

void Prepara(NVGcontext *vg, const FonteTexto &f)
{
    nvgFontFaceId(vg, Face(vg, f));
    nvgFontSize(vg, (float) f.Tamanho);
    nvgTextLetterSpacing(vg, 0);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
}

} // namespace

namespace Texto {

double AlturaLinha(const FonteTexto &f)
{
    // as metricas das fontes do Windows, para o layout ter as mesmas alturas do original:
    // Segoe UI 2724/2048 por em, Consolas 2400/2048
    return std::ceil(f.Tamanho * (EhMono(f.Familia) ? 1.1719 : 1.3301) - 0.01);
}

double Ascendente(const FonteTexto &f) { return f.Tamanho * (EhMono(f.Familia) ? 0.9277 : 1.0791); }

double Largura(const FonteTexto &f, const std::string &s)
{
    NVGcontext *vg = Application::Vg();
    if (!vg || s.empty()) return 0;
    Prepara(vg, f);
    return nvgTextBounds(vg, 0, 0, s.data(), s.data() + s.size(), nullptr);
}

std::vector<LinhaDeTexto> Quebra(const FonteTexto &f, const std::string &s, double largura, bool quebrar)
{
    std::vector<LinhaDeTexto> r;
    NVGcontext *vg = Application::Vg();
    if (!vg) return r;
    Prepara(vg, f);
    double max = quebrar && Finito(largura) ? std::max(1.0, largura) : 1e9;
    NVGtextRow linhas[64];
    // os paragrafos (\r\n, \n ou \r) um a um: assim a linha vazia entre dois paragrafos conta
    size_t p = 0;
    while (true)
    {
        size_t q = s.find_first_of("\r\n", p);
        size_t fimPar = q == std::string::npos ? s.size() : q;
        if (fimPar == p) r.push_back({p, p, 0});
        const char *ini = s.data() + p, *fim = s.data() + fimPar;
        while (ini < fim)
        {
            int n = nvgTextBreakLines(vg, ini, fim, (float) max, linhas, 64);
            if (n <= 0) break;
            for (int i = 0; i < n; i++)
            {
                LinhaDeTexto l;
                l.Inicio = (size_t) (linhas[i].start - s.data());
                l.Fim = (size_t) (linhas[i].end - s.data());
                // a largura do avanco (a do NanoVG e a da tinta)
                l.Largura = nvgTextBounds(vg, 0, 0, linhas[i].start, linhas[i].end, nullptr);
                r.push_back(l);
            }
            ini = linhas[n - 1].next;
        }
        if (q == std::string::npos) break;
        p = q + ((s[q] == '\r' && q + 1 < s.size() && s[q + 1] == '\n') ? 2 : 1);
        if (p >= s.size())
        {
            // texto terminado em quebra de linha: o WPF conta a linha vazia do fim
            r.push_back({s.size(), s.size(), 0});
            break;
        }
    }
    if (r.empty()) r.push_back({0, 0, 0});
    return r;
}

std::string ComReticencias(const FonteTexto &f, const std::string &s, double largura)
{
    if (!Finito(largura) || Largura(f, s) <= largura + 0.5) return s;
    const std::string pontos = "\xE2\x80\xA6";
    double lp = Largura(f, pontos);
    std::vector<double> xs;
    std::vector<size_t> bytes;
    Posicoes(f, s, xs, bytes);
    size_t corte = 0;
    for (size_t i = 0; i < xs.size(); i++)
        if (xs[i] + lp <= largura) corte = bytes[i];
    std::string r = s.substr(0, corte);
    while (!r.empty() && r.back() == ' ') r.pop_back();
    return r + pontos;
}

void Posicoes(const FonteTexto &f, const std::string &s, std::vector<double> &x, std::vector<size_t> &bytes)
{
    x.clear();
    bytes.clear();
    NVGcontext *vg = Application::Vg();
    x.push_back(0);
    bytes.push_back(0);
    if (!vg || s.empty()) return;
    Prepara(vg, f);
    std::vector<NVGglyphPosition> g(s.size() + 1);
    int n = nvgTextGlyphPositions(vg, 0, 0, s.data(), s.data() + s.size(), g.data(), (int) g.size());
    x.clear();
    bytes.clear();
    for (int i = 0; i < n; i++)
    {
        x.push_back(g[i].x);
        bytes.push_back((size_t) (g[i].str - s.data()));
    }
    x.push_back(nvgTextBounds(vg, 0, 0, s.data(), s.data() + s.size(), nullptr));
    bytes.push_back(s.size());
    if (x.size() == 1) x.insert(x.begin(), 0), bytes.insert(bytes.begin(), 0);
}

} // namespace Texto

// =====================================================================================
// DrawingContext
// =====================================================================================
void DrawingContext::PreencheCaminho(const Brush &b, const Rect &lim, const std::function<void()> &caminho)
{
    if (b.Nulo()) return;
    double op = b.Opacity * (_opacidades.empty() ? 1 : _opacidades.back());
    const auto &p = b.Paradas();
    if (p.empty() || p.size() == 1)
    {
        nvgBeginPath(vg);
        caminho();
        nvgFillColor(vg, CorNvg(p.empty() ? b.Cor() : p[0].Color, op));
        nvgFill(vg);
        return;
    }
    // o degrade do WPF e relativo a caixa (MappingMode RelativeToBoundingBox)
    Point s = b.StartPoint(), e = b.EndPoint();
    double sx = lim.X + s.X * lim.Width, sy = lim.Y + s.Y * lim.Height;
    double ex = lim.X + e.X * lim.Width, ey = lim.Y + e.Y * lim.Height;
    if (p.size() == 2)
    {
        double ax = sx + (ex - sx) * p[0].Offset, ay = sy + (ey - sy) * p[0].Offset;
        double bx = sx + (ex - sx) * p[1].Offset, by = sy + (ey - sy) * p[1].Offset;
        nvgBeginPath(vg);
        caminho();
        nvgFillPaint(vg, nvgLinearGradient(vg, (float) ax, (float) ay, (float) bx, (float) by, CorNvg(p[0].Color, op), CorNvg(p[1].Color, op)));
        nvgFill(vg);
        return;
    }
    // varias paradas: o NanoVG so faz duas, entao o caminho e pintado em faixas, cada uma recortada
    // ao trecho entre duas paradas (degrades na vertical ou na horizontal, que e o que o Tema tem)
    bool vertical = std::fabs(ex - sx) < std::fabs(ey - sy);
    for (size_t i = 0; i + 1 < p.size(); i++)
    {
        double t0 = i == 0 ? -1.0 : p[i].Offset, t1 = i + 2 == p.size() ? 2.0 : p[i + 1].Offset;
        double a = vertical ? sy + (ey - sy) * t0 : sx + (ex - sx) * t0;
        double c = vertical ? sy + (ey - sy) * t1 : sx + (ex - sx) * t1;
        if (a > c) std::swap(a, c);
        nvgSave(vg);
        if (vertical) nvgIntersectScissor(vg, (float) (lim.X - 1), (float) a, (float) (lim.Width + 2), (float) (c - a + 0.5));
        else nvgIntersectScissor(vg, (float) a, (float) (lim.Y - 1), (float) (c - a + 0.5), (float) (lim.Height + 2));
        double gx0 = sx + (ex - sx) * p[i].Offset, gy0 = sy + (ey - sy) * p[i].Offset;
        double gx1 = sx + (ex - sx) * p[i + 1].Offset, gy1 = sy + (ey - sy) * p[i + 1].Offset;
        nvgBeginPath(vg);
        caminho();
        nvgFillPaint(vg, nvgLinearGradient(vg, (float) gx0, (float) gy0, (float) gx1, (float) gy1, CorNvg(p[i].Color, op), CorNvg(p[i + 1].Color, op)));
        nvgFill(vg);
        nvgRestore(vg);
    }
}

void DrawingContext::Retangulo(const Rect &r, const Brush &fundo, const Brush &borda, Thickness t, ARCA::Vistas::CornerRadius k)
{
    if (r.Width <= 0 || r.Height <= 0) return;
    bool redondo = !k.Zero();
    // retangulos retos sem antialias: a borda de 1 px fica em 1 px, como no WPF com UseLayoutRounding
    nvgShapeAntiAlias(vg, redondo ? 1 : 0);
    Rect in(r.X + t.Left, r.Y + t.Top, std::max(0.0, r.Width - t.Horizontal()), std::max(0.0, r.Height - t.Vertical()));
    auto ri = [&](double raio, double a, double b) { return std::max(0.0, raio - std::max(a, b)); };
    if (!fundo.Nulo())
    {
        // o fundo fica por dentro da borda (o Border do WPF)
        PreencheCaminho(fundo, r, [&] {
            if (redondo)
                nvgRoundedRectVarying(vg, (float) in.X, (float) in.Y, (float) in.Width, (float) in.Height,
                                      (float) ri(k.TopLeft, t.Left, t.Top), (float) ri(k.TopRight, t.Right, t.Top),
                                      (float) ri(k.BottomRight, t.Right, t.Bottom), (float) ri(k.BottomLeft, t.Left, t.Bottom));
            else
                nvgRect(vg, (float) in.X, (float) in.Y, (float) in.Width, (float) in.Height);
        });
    }
    if (!borda.Nulo() && (t.Left > 0 || t.Top > 0 || t.Right > 0 || t.Bottom > 0))
    {
        PreencheCaminho(borda, r, [&] {
            if (redondo)
                nvgRoundedRectVarying(vg, (float) r.X, (float) r.Y, (float) r.Width, (float) r.Height,
                                      (float) k.TopLeft, (float) k.TopRight, (float) k.BottomRight, (float) k.BottomLeft);
            else
                nvgRect(vg, (float) r.X, (float) r.Y, (float) r.Width, (float) r.Height);
            if (in.Width > 0 && in.Height > 0)
            {
                if (redondo)
                    nvgRoundedRectVarying(vg, (float) in.X, (float) in.Y, (float) in.Width, (float) in.Height,
                                          (float) ri(k.TopLeft, t.Left, t.Top), (float) ri(k.TopRight, t.Right, t.Top),
                                          (float) ri(k.BottomRight, t.Right, t.Bottom), (float) ri(k.BottomLeft, t.Left, t.Bottom));
                else
                    nvgRect(vg, (float) in.X, (float) in.Y, (float) in.Width, (float) in.Height);
                nvgPathWinding(vg, NVG_HOLE);
            }
        });
    }
    nvgShapeAntiAlias(vg, 1);
}

void DrawingContext::Elipse(const Rect &r, const Brush &fundo, const Brush &borda, double espessura)
{
    if (r.Width <= 0 || r.Height <= 0) return;
    float cx = (float) (r.X + r.Width / 2), cy = (float) (r.Y + r.Height / 2);
    PreencheCaminho(fundo, r, [&] { nvgEllipse(vg, cx, cy, (float) (r.Width / 2), (float) (r.Height / 2)); });
    if (!borda.Nulo() && espessura > 0)
    {
        double op = borda.Opacity * (_opacidades.empty() ? 1 : _opacidades.back());
        nvgBeginPath(vg);
        nvgEllipse(vg, cx, cy, (float) ((r.Width - espessura) / 2), (float) ((r.Height - espessura) / 2));
        nvgStrokeColor(vg, CorNvg(borda.Cor(), op));
        nvgStrokeWidth(vg, (float) espessura);
        nvgStroke(vg);
    }
}

void DrawingContext::Linha(Point a, Point b, const Brush &cor, double espessura)
{
    if (cor.Nulo()) return;
    double op = cor.Opacity * (_opacidades.empty() ? 1 : _opacidades.back());
    nvgBeginPath(vg);
    nvgMoveTo(vg, (float) a.X, (float) a.Y);
    nvgLineTo(vg, (float) b.X, (float) b.Y);
    nvgStrokeColor(vg, CorNvg(cor.Cor(), op));
    nvgStrokeWidth(vg, (float) espessura);
    nvgStroke(vg);
}

void DrawingContext::Texto(const std::string &s, const FonteTexto &f, double x, double y, const Brush &cor)
{
    if (s.empty() || cor.Nulo()) return;
    double op = cor.Opacity * (_opacidades.empty() ? 1 : _opacidades.back());
    Prepara(vg, f);
    nvgFillColor(vg, CorNvg(cor.Cor(), op));
    // TextFormattingMode="Display": a linha de base no pixel inteiro
    nvgText(vg, (float) Arred(x), (float) Arred(y + Vistas::Texto::Ascendente(f)), s.data(), s.data() + s.size());
}

void DrawingContext::Imagem(BitmapSource &img, const Rect &d, double opacidade, const Rect *origem)
{
    int t = img.Textura(vg);
    if (!t || d.Width <= 0 || d.Height <= 0) return;
    double op = opacidade * (_opacidades.empty() ? 1 : _opacidades.back());
    Rect o = origem ? *origem : Rect(0, 0, img.PixelWidth, img.PixelHeight);
    double ex = d.Width / std::max(1e-9, o.Width), ey = d.Height / std::max(1e-9, o.Height);
    NVGpaint p = nvgImagePattern(vg, (float) (d.X - o.X * ex), (float) (d.Y - o.Y * ey), (float) (img.PixelWidth * ex),
                                 (float) (img.PixelHeight * ey), 0, t, (float) op);
    nvgShapeAntiAlias(vg, 0);
    nvgBeginPath(vg);
    nvgRect(vg, (float) d.X, (float) d.Y, (float) d.Width, (float) d.Height);
    nvgFillPaint(vg, p);
    nvgFill(vg);
    nvgShapeAntiAlias(vg, 1);
}

void DrawingContext::Recorta(const Rect &r)
{
    Rect n = _recortes.empty() ? r : _recortes.back().Intersecao(r);
    _recortes.push_back(n);
    nvgScissor(vg, (float) n.X, (float) n.Y, (float) n.Width, (float) n.Height);
}

void DrawingContext::Restaura()
{
    if (!_recortes.empty()) _recortes.pop_back();
    if (_recortes.empty()) nvgResetScissor(vg);
    else
    {
        const Rect &n = _recortes.back();
        nvgScissor(vg, (float) n.X, (float) n.Y, (float) n.Width, (float) n.Height);
    }
}

Rect DrawingContext::RecorteAtual() const { return _recortes.empty() ? Rect(-1e6, -1e6, 2e6, 2e6) : _recortes.back(); }

void DrawingContext::EmpilhaOpacidade(double o) { _opacidades.push_back((_opacidades.empty() ? 1 : _opacidades.back()) * o); }
void DrawingContext::DesempilhaOpacidade()
{
    if (!_opacidades.empty()) _opacidades.pop_back();
}

// =====================================================================================
// Argumentos
// =====================================================================================
Point MouseEventArgs::GetPosition(const UIElement *e) const
{
    if (!e) return Posicao;
    return Point(Posicao.X - e->Limites.X, Posicao.Y - e->Limites.Y);
}

// =====================================================================================
// UIElement
// =====================================================================================
UIElement::UIElement() = default;

UIElement::~UIElement()
{
    for (Window *j : Application::Janelas()) j->Esquece(this);
}

void UIElement::Adota(UIElement *pai, UIElement *filho)
{
    if (!filho) return;
    if (filho->Parent && filho->Parent != pai)
        fprintf(stderr, "xos-arca: o elemento %s ja tinha pai (%s)\n", filho->Tipo(), filho->Parent->Tipo());
    filho->Parent = pai;
    if (Window *j = pai ? pai->Janela() : nullptr) j->Redesenha();
}

void UIElement::Solta(UIElement *filho)
{
    if (!filho) return;
    Window *j = filho->Janela();
    filho->Parent = nullptr;
    filho->_carregado = false;
    if (j)
    {
        j->Esquece(filho);
        j->Redesenha();
    }
}

Window *UIElement::Janela() const
{
    const UIElement *e = this;
    while (e->Parent) e = e->Parent;
    return dynamic_cast<Window *>(const_cast<UIElement *>(e));
}

bool UIElement::EhAncestralDe(const UIElement *e) const
{
    for (; e; e = e->Parent)
        if (e == this) return true;
    return false;
}

bool UIElement::IsMouseOver() const
{
    Window *j = Janela();
    return j && j->EstaSobre(this);
}
bool UIElement::IsMouseDirectlyOver() const
{
    Window *j = Janela();
    return j && j->EstaDiretamenteSobre(this);
}
bool UIElement::IsKeyboardFocused() const
{
    Window *j = Janela();
    return j && j->Foco() == this;
}
bool UIElement::IsKeyboardFocusWithin() const
{
    Window *j = Janela();
    return j && j->Foco() && EhAncestralDe(j->Foco());
}
bool UIElement::IsMouseCaptured() const
{
    Window *j = Janela();
    return j && j->Captura() == this;
}
bool UIElement::IsMouseCaptureWithin() const
{
    Window *j = Janela();
    return j && j->Captura() && EhAncestralDe(j->Captura());
}

bool UIElement::IsVisible() const
{
    for (const UIElement *e = this; e; e = e->Parent)
    {
        if (e->Visibility != Vistas::Visibility::Visible) return false;
        if (!e->Parent)
        {
            const Window *j = dynamic_cast<const Window *>(e);
            return j && j->EstaAberta();
        }
    }
    return false;
}

bool UIElement::IsEnabledEfetivo() const
{
    for (const UIElement *e = this; e; e = e->Parent)
        if (!e->IsEnabled) return false;
    return true;
}

Point UIElement::TranslatePoint(Point p, const UIElement *rel) const
{
    Point a(p.X + Limites.X, p.Y + Limites.Y);
    if (!rel) return a;
    return Point(a.X - rel->Limites.X, a.Y - rel->Limites.Y);
}

bool UIElement::Focus()
{
    Window *j = Janela();
    if (!j || !Focusable || !IsEnabledEfetivo() || !IsVisible()) return false;
    j->DefineFoco(this);
    return true;
}

bool UIElement::CaptureMouse()
{
    Window *j = Janela();
    if (!j) return false;
    j->DefineCaptura(this);
    return true;
}

void UIElement::ReleaseMouseCapture()
{
    Window *j = Janela();
    if (j && j->Captura() == this) j->DefineCaptura(nullptr);
}

void UIElement::InvalidateVisual()
{
    if (Window *j = Janela()) j->Redesenha();
}

void UIElement::UpdateLayout()
{
    if (Window *j = Janela())
    {
        Size t = j->TamanhoCliente();
        j->MeasureOverride(t);
        j->ArrangeOverride(t);
    }
}

void UIElement::BringIntoView()
{
    for (UIElement *e = Parent; e; e = e->Parent)
        if (auto *s = dynamic_cast<ScrollViewer *>(e))
        {
            s->MostraRetangulo(Limites);
            return;
        }
}

const Vistas::Style *UIElement::EstiloEfetivo() const
{
    return Style ? Style : Tema::EstiloPadrao(Tipo());
}

Brush UIElement::ForegroundEfetivo() const
{
    for (const UIElement *e = this; e; e = e->Parent)
    {
        if (!e->Foreground.Nulo()) return e->Foreground;
        if (auto g = e->ForegroundDoGabarito()) return *g;
        const Vistas::Style *s = e->EstiloEfetivo();
        if (s && s->Foreground) return *s->Foreground;
    }
    return Brush(Tema::SistemaTextoControle);
}

double UIElement::FontSizeEfetivo() const
{
    for (const UIElement *e = this; e; e = e->Parent)
    {
        if (!std::isnan(e->FontSize)) return e->FontSize;
        const Vistas::Style *s = e->EstiloEfetivo();
        if (s && s->FontSize) return *s->FontSize;
    }
    return 12;   // SystemFonts.MessageFontSize (Segoe UI 9 pt a 96 DPI)
}

Vistas::FontWeight UIElement::FontWeightEfetivo() const
{
    for (const UIElement *e = this; e; e = e->Parent)
    {
        if (e->FontWeight) return *e->FontWeight;
        if (auto g = e->FontWeightDoGabarito()) return *g;
        const Vistas::Style *s = e->EstiloEfetivo();
        if (s && s->FontWeight) return *s->FontWeight;
    }
    return Vistas::FontWeight::Normal;
}

Vistas::FontStyle UIElement::FontStyleEfetivo() const
{
    for (const UIElement *e = this; e; e = e->Parent)
    {
        if (e->FontStyle) return *e->FontStyle;
        const Vistas::Style *s = e->EstiloEfetivo();
        if (s && s->FontStyle) return *s->FontStyle;
    }
    return Vistas::FontStyle::Normal;
}

std::string UIElement::FontFamilyEfetiva() const
{
    for (const UIElement *e = this; e; e = e->Parent)
    {
        if (!e->FontFamily.empty()) return e->FontFamily;
        const Vistas::Style *s = e->EstiloEfetivo();
        if (s && s->FontFamily) return *s->FontFamily;
    }
    return Tema::Fonte;
}

FonteTexto UIElement::Fonte() const
{
    FonteTexto f;
    f.Familia = FontFamilyEfetiva();
    f.Tamanho = FontSizeEfetivo();
    f.Peso = FontWeightEfetivo();
    f.Estilo = FontStyleEfetivo();
    return f;
}

Thickness UIElement::MargemEfetiva() const
{
    if (Margin) return *Margin;
    const Vistas::Style *s = EstiloEfetivo();
    if (s && s->Margin) return *s->Margin;
    return Thickness();
}

namespace {
// os limites de tamanho do FrameworkElement (MinMax)
struct MinMax
{
    double minW, maxW, minH, maxH;
    explicit MinMax(const UIElement &e)
    {
        maxH = e.MaxHeight;
        minH = e.MinHeight;
        double l = e.Height;
        double h = std::isnan(l) ? Infinito : l;
        maxH = std::max(std::min(h, maxH), minH);
        h = std::isnan(l) ? 0 : l;
        minH = std::max(std::min(maxH, h), minH);
        maxW = e.MaxWidth;
        minW = e.MinWidth;
        l = e.Width;
        double w = std::isnan(l) ? Infinito : l;
        maxW = std::max(std::min(w, maxW), minW);
        w = std::isnan(l) ? 0 : l;
        minW = std::max(std::min(maxW, w), minW);
    }
};
} // namespace

void UIElement::Measure(Size d)
{
    if (Visibility == Vistas::Visibility::Collapsed)
    {
        DesiredSize = Size();
        return;
    }
    Thickness m = MargemEfetiva();
    MinMax mm(*this);
    double w = std::max(0.0, d.Width - m.Horizontal()), h = std::max(0.0, d.Height - m.Vertical());
    w = std::max(mm.minW, std::min(w, mm.maxW));
    h = std::max(mm.minH, std::min(h, mm.maxH));
    Size s = MeasureOverride(Size(w, h));
    s.Width = std::max(s.Width, mm.minW);
    s.Height = std::max(s.Height, mm.minH);
    if (s.Width > mm.maxW) s.Width = mm.maxW;
    if (s.Height > mm.maxH) s.Height = mm.maxH;
    _ultimoTamanho = s;     // o desejado sem corte (unclippedDesiredSize)
    double dw = std::max(0.0, s.Width + m.Horizontal()), dh = std::max(0.0, s.Height + m.Vertical());
    if (dw > d.Width) dw = d.Width;
    if (dh > d.Height) dh = d.Height;
    DesiredSize = Size(Arred(dw), Arred(dh));
    _tamanhoValido = true;
}

void UIElement::Arrange(Rect f)
{
    if (Visibility == Vistas::Visibility::Collapsed)
    {
        Limites = Rect(f.X, f.Y, 0, 0);
        return;
    }
    Thickness m = MargemEfetiva();
    MinMax mm(*this);
    double aw = std::max(0.0, f.Width - m.Horizontal()), ah = std::max(0.0, f.Height - m.Vertical());
    double w = aw, h = ah;
    Size cheio = _tamanhoValido ? _ultimoTamanho : Size(0, 0);
    if (HorizontalAlignment != Vistas::HorizontalAlignment::Stretch) w = cheio.Width;
    if (VerticalAlignment != Vistas::VerticalAlignment::Stretch) h = cheio.Height;
    if (w < cheio.Width) w = cheio.Width;
    if (h < cheio.Height) h = cheio.Height;
    w = std::max(mm.minW, std::min(w, mm.maxW));
    h = std::max(mm.minH, std::min(h, mm.maxH));
    double x0 = f.X + m.Left, y0 = f.Y + m.Top;
    // posicao conforme o alinhamento (esticado e menor que a vaga: centraliza, como o WPF)
    double ox = 0, oy = 0;
    switch (HorizontalAlignment)
    {
    case Vistas::HorizontalAlignment::Left: break;
    case Vistas::HorizontalAlignment::Right: ox = aw - w; break;
    default: ox = (aw - w) / 2; break;
    }
    switch (VerticalAlignment)
    {
    case Vistas::VerticalAlignment::Top: break;
    case Vistas::VerticalAlignment::Bottom: oy = ah - h; break;
    default: oy = (ah - h) / 2; break;
    }
    if (w > aw && HorizontalAlignment != Vistas::HorizontalAlignment::Right) ox = 0;
    if (h > ah && VerticalAlignment != Vistas::VerticalAlignment::Bottom) oy = 0;
    // UseLayoutRounding: cantos no pixel inteiro
    double l = Arred(x0 + ox), t = Arred(y0 + oy);
    double r = Arred(x0 + ox + w), b = Arred(y0 + oy + h);
    Limites = Rect(l, t, std::max(0.0, r - l), std::max(0.0, b - t));
    Size antes(ActualWidth, ActualHeight);
    Size real = ArrangeOverride(Size(Limites.Width, Limites.Height));
    (void) real;
    ActualWidth = Limites.Width;
    ActualHeight = Limites.Height;
    if (antes.Width != ActualWidth || antes.Height != ActualHeight)
    {
        if (!SizeChanged.Vazio())
        {
            SizeChangedEventArgs a;
            a.PreviousSize = antes;
            a.NewSize = Size(ActualWidth, ActualHeight);
            a.WidthChanged = antes.Width != ActualWidth;
            a.HeightChanged = antes.Height != ActualHeight;
            SizeChanged(a);
            if (Window *j = Janela()) j->Redesenha();
        }
    }
}

Size UIElement::MeasureOverride(Size d)
{
    std::vector<UIElement *> f;
    Filhos(f);
    Size r;
    for (UIElement *c : f)
    {
        c->Measure(d);
        r.Width = std::max(r.Width, c->DesiredSize.Width);
        r.Height = std::max(r.Height, c->DesiredSize.Height);
    }
    return r;
}

Size UIElement::ArrangeOverride(Size fs)
{
    std::vector<UIElement *> f;
    Filhos(f);
    for (UIElement *c : f) c->Arrange(Rect(Limites.X, Limites.Y, fs.Width, fs.Height));
    return fs;
}

void UIElement::Render(DrawingContext &dc)
{
    if (Visibility != Vistas::Visibility::Visible || Opacity <= 0) return;
    Rect corte = dc.RecorteAtual();
    // fora do recorte: nada a desenhar (as listas compridas agradecem)
    if (Limites.Right() < corte.X || Limites.Bottom() < corte.Y || Limites.X > corte.Right() || Limites.Y > corte.Bottom())
        if (!dynamic_cast<Canvas *>(this)) return;
    double op = Opacity * OpacidadeDoGabarito();
    bool opacidade = op < 1;
    if (opacidade) dc.EmpilhaOpacidade(op);
    nvgGlobalAlpha(dc.vg, 1);
    bool recorta = ClipToBounds;
    if (recorta) dc.Recorta(Limites);
    OnRender(dc);
    std::vector<UIElement *> f;
    Filhos(f);
    for (UIElement *c : f) c->Render(dc);
    OnRenderDepois(dc);
    if (recorta) dc.Restaura();
    if (opacidade) dc.DesempilhaOpacidade();
}

void UIElement::OnRender(DrawingContext &dc) { (void) dc; }

UIElement *UIElement::Acerta(Point p)
{
    if (Visibility != Vistas::Visibility::Visible || !IsHitTestVisible || !IsEnabled) return nullptr;
    bool dentro = Limites.Contains(p);
    bool recorta = ClipToBounds || dynamic_cast<ScrollViewer *>(this) || dynamic_cast<ListBox *>(this) ||
                   dynamic_cast<DataGrid *>(this);
    if (recorta && !dentro) return nullptr;
    std::vector<UIElement *> f;
    Filhos(f);
    for (auto it = f.rbegin(); it != f.rend(); ++it)
        if (UIElement *r = (*it)->Acerta(p)) return r;
    if (dentro && AcertaProprio(p)) return this;
    return nullptr;
}

bool UIElement::AcertaProprio(Point p) const
{
    (void) p;
    return false;
}

Vistas::Cursor UIElement::CursorEm(Point p) const
{
    (void) p;
    return Cursor;
}

// =====================================================================================
// UIElementCollection
// =====================================================================================
void UIElementCollection::Add(UIElementPtr e)
{
    if (!e) return;
    UIElement::Adota(_dono, e.get());
    _itens.push_back(std::move(e));
}

void UIElementCollection::Insert(size_t i, UIElementPtr e)
{
    if (!e) return;
    UIElement::Adota(_dono, e.get());
    _itens.insert(_itens.begin() + (std::ptrdiff_t) std::min(i, _itens.size()), std::move(e));
}

bool UIElementCollection::Remove(const UIElement *e)
{
    for (size_t i = 0; i < _itens.size(); i++)
        if (_itens[i].get() == e)
        {
            RemoveAt(i);
            return true;
        }
    return false;
}

bool UIElementCollection::Remove(const UIElementPtr &e) { return Remove(e.get()); }

void UIElementCollection::RemoveAt(size_t i)
{
    if (i >= _itens.size()) return;
    UIElementPtr e = _itens[i];
    _itens.erase(_itens.begin() + (std::ptrdiff_t) i);
    UIElement::Solta(e.get());
}

void UIElementCollection::Clear()
{
    auto v = std::move(_itens);
    _itens.clear();
    for (auto &e : v) UIElement::Solta(e.get());
}

bool UIElementCollection::Contains(const UIElement *e) const { return IndexOf(e) >= 0; }

int UIElementCollection::IndexOf(const UIElement *e) const
{
    for (size_t i = 0; i < _itens.size(); i++)
        if (_itens[i].get() == e) return (int) i;
    return -1;
}

// =====================================================================================
// Paineis
// =====================================================================================
Panel::~Panel()
{
    for (auto &c : Children)
        if (c->Parent == this) c->Parent = nullptr;
}

void Panel::Filhos(std::vector<UIElement *> &s) const
{
    for (auto &c : Children) s.push_back(c.get());
}

void Panel::OnRender(DrawingContext &dc)
{
    if (!Background.Nulo()) dc.Retangulo(Limites, Background);
}

bool Panel::AcertaProprio(Point p) const
{
    (void) p;
    return !Background.Nulo();
}

Size StackPanel::MeasureOverride(Size d)
{
    bool h = Orientation == Vistas::Orientation::Horizontal;
    Size r;
    for (auto &c : Children)
    {
        c->Measure(h ? Size(Infinito, d.Height) : Size(d.Width, Infinito));
        if (h)
        {
            r.Width += c->DesiredSize.Width;
            r.Height = std::max(r.Height, c->DesiredSize.Height);
        }
        else
        {
            r.Height += c->DesiredSize.Height;
            r.Width = std::max(r.Width, c->DesiredSize.Width);
        }
    }
    return r;
}

Size StackPanel::ArrangeOverride(Size f)
{
    bool h = Orientation == Vistas::Orientation::Horizontal;
    double pos = 0;
    for (auto &c : Children)
    {
        if (h)
        {
            double w = c->DesiredSize.Width;
            c->Arrange(Rect(Limites.X + pos, Limites.Y, w, std::max(f.Height, c->DesiredSize.Height)));
            pos += w;
        }
        else
        {
            double hh = c->DesiredSize.Height;
            c->Arrange(Rect(Limites.X, Limites.Y + pos, std::max(f.Width, c->DesiredSize.Width), hh));
            pos += hh;
        }
    }
    return f;
}

Size DockPanel::MeasureOverride(Size d)
{
    double usadoW = 0, usadoH = 0, maxW = 0, maxH = 0;
    size_t n = Children.Count();
    for (size_t i = 0; i < n; i++)
    {
        auto &c = Children[i];
        Size resto(std::max(0.0, d.Width - usadoW), std::max(0.0, d.Height - usadoH));
        c->Measure(resto);
        Size s = c->DesiredSize;
        bool ultimo = LastChildFill && i + 1 == n;
        Dock k = ultimo ? Dock::Left : c->DockPanel_Dock;
        if (ultimo)
        {
            maxW = std::max(maxW, usadoW + s.Width);
            maxH = std::max(maxH, usadoH + s.Height);
        }
        else if (k == Dock::Left || k == Dock::Right)
        {
            maxH = std::max(maxH, usadoH + s.Height);
            usadoW += s.Width;
        }
        else
        {
            maxW = std::max(maxW, usadoW + s.Width);
            usadoH += s.Height;
        }
    }
    return Size(std::max(maxW, usadoW), std::max(maxH, usadoH));
}

Size DockPanel::ArrangeOverride(Size f)
{
    double l = 0, t = 0, r = 0, b = 0;
    size_t n = Children.Count();
    for (size_t i = 0; i < n; i++)
    {
        auto &c = Children[i];
        Size s = c->DesiredSize;
        Rect area(Limites.X + l, Limites.Y + t, std::max(0.0, f.Width - l - r), std::max(0.0, f.Height - t - b));
        if (!(LastChildFill && i + 1 == n))
        {
            switch (c->DockPanel_Dock)
            {
            case Dock::Left: l += s.Width; area.Width = s.Width; break;
            case Dock::Right: r += s.Width; area.X = std::max(Limites.X, Limites.X + f.Width - r); area.Width = s.Width; break;
            case Dock::Top: t += s.Height; area.Height = s.Height; break;
            case Dock::Bottom: b += s.Height; area.Y = std::max(Limites.Y, Limites.Y + f.Height - b); area.Height = s.Height; break;
            }
        }
        c->Arrange(area);
    }
    return f;
}

Size WrapPanel::MeasureOverride(Size d)
{
    bool h = Orientation == Vistas::Orientation::Horizontal;
    double limite = h ? d.Width : d.Height;
    double linhaU = 0, linhaV = 0, totalU = 0, totalV = 0;
    for (auto &c : Children)
    {
        Size disp(std::isnan(ItemWidth) ? d.Width : ItemWidth, std::isnan(ItemHeight) ? d.Height : ItemHeight);
        c->Measure(disp);
        double u = h ? (std::isnan(ItemWidth) ? c->DesiredSize.Width : ItemWidth) : (std::isnan(ItemHeight) ? c->DesiredSize.Height : ItemHeight);
        double v = h ? (std::isnan(ItemHeight) ? c->DesiredSize.Height : ItemHeight) : (std::isnan(ItemWidth) ? c->DesiredSize.Width : ItemWidth);
        if (linhaU + u > limite + 0.01 && linhaU > 0)
        {
            totalU = std::max(totalU, linhaU);
            totalV += linhaV;
            linhaU = 0;
            linhaV = 0;
        }
        linhaU += u;
        linhaV = std::max(linhaV, v);
    }
    totalU = std::max(totalU, linhaU);
    totalV += linhaV;
    return h ? Size(totalU, totalV) : Size(totalV, totalU);
}

Size WrapPanel::ArrangeOverride(Size f)
{
    bool h = Orientation == Vistas::Orientation::Horizontal;
    double limite = h ? f.Width : f.Height;
    std::vector<UIElement *> linha;
    double linhaU = 0, linhaV = 0, posV = 0;
    auto tam = [&](UIElement *c, double &u, double &v) {
        u = h ? (std::isnan(ItemWidth) ? c->DesiredSize.Width : ItemWidth) : (std::isnan(ItemHeight) ? c->DesiredSize.Height : ItemHeight);
        v = h ? (std::isnan(ItemHeight) ? c->DesiredSize.Height : ItemHeight) : (std::isnan(ItemWidth) ? c->DesiredSize.Width : ItemWidth);
    };
    auto fecha = [&] {
        double pu = 0;
        for (UIElement *c : linha)
        {
            double u, v;
            tam(c, u, v);
            if (h) c->Arrange(Rect(Limites.X + pu, Limites.Y + posV, u, linhaV));
            else c->Arrange(Rect(Limites.X + posV, Limites.Y + pu, linhaV, u));
            pu += u;
        }
        posV += linhaV;
        linha.clear();
        linhaU = linhaV = 0;
    };
    for (auto &c : Children)
    {
        double u, v;
        tam(c.get(), u, v);
        if (linhaU + u > limite + 0.01 && linhaU > 0) fecha();
        linha.push_back(c.get());
        linhaU += u;
        linhaV = std::max(linhaV, v);
    }
    fecha();
    return f;
}

Size Canvas::MeasureOverride(Size d)
{
    (void) d;
    for (auto &c : Children) c->Measure(Size(Infinito, Infinito));
    return Size();
}

Size Canvas::ArrangeOverride(Size f)
{
    for (auto &c : Children)
    {
        Size s = c->DesiredSize;
        double x = 0, y = 0;
        if (!std::isnan(c->Canvas_Left)) x = c->Canvas_Left;
        else if (!std::isnan(c->Canvas_Right)) x = f.Width - s.Width - c->Canvas_Right;
        if (!std::isnan(c->Canvas_Top)) y = c->Canvas_Top;
        else if (!std::isnan(c->Canvas_Bottom)) y = f.Height - s.Height - c->Canvas_Bottom;
        c->Arrange(Rect(Limites.X + x, Limites.Y + y, s.Width, s.Height));
    }
    return f;
}

// ---- Grid ----
// O algoritmo do Grid do WPF, simplificado ao que o ARCA usa: definicoes em pixel, Auto e
// estrela; filhos que cruzam varias celulas entram no calculo das Auto pela ultima delas.
void Grid::Resolve(std::vector<double> &tam, bool colunas, double disp, bool medindo)
{
    size_t n = colunas ? std::max<size_t>(1, ColumnDefinitions.size()) : std::max<size_t>(1, RowDefinitions.size());
    auto def = [&](size_t i, GridLength &g, double &mn, double &mx) {
        if (colunas && !ColumnDefinitions.empty()) g = ColumnDefinitions[i].Width, mn = ColumnDefinitions[i].MinWidth, mx = ColumnDefinitions[i].MaxWidth;
        else if (!colunas && !RowDefinitions.empty()) g = RowDefinitions[i].Height, mn = RowDefinitions[i].MinHeight, mx = RowDefinitions[i].MaxHeight;
        else g = GridLength::Star(1), mn = 0, mx = Infinito;
    };
    tam.assign(n, 0);
    std::vector<char> estrela(n, 0), automatico(n, 0);
    std::vector<double> pesos(n, 0), minimos(n, 0), maximos(n, Infinito);
    bool infinito = !Finito(disp);
    for (size_t i = 0; i < n; i++)
    {
        GridLength g;
        double mn, mx;
        def(i, g, mn, mx);
        minimos[i] = mn;
        maximos[i] = mx;
        if (g.IsAbsolute()) tam[i] = std::clamp(g.Value, mn, std::max(mn, mx));
        else if (g.IsAuto()) automatico[i] = 1, tam[i] = mn;
        else
        {
            // estrela em espaco infinito vira Auto (o WPF faz igual)
            if (infinito) automatico[i] = 1, tam[i] = mn;
            else estrela[i] = 1, pesos[i] = g.Value, tam[i] = mn;
        }
    }
    // o conteudo das Auto (e das estrelas em espaco infinito)
    for (auto &c : Children)
    {
        if (c->Visibility == Vistas::Visibility::Collapsed) continue;
        int a = colunas ? c->Grid_Column : c->Grid_Row;
        int s = colunas ? c->Grid_ColumnSpan : c->Grid_RowSpan;
        a = std::clamp(a, 0, (int) n - 1);
        int b = std::clamp(a + std::max(1, s) - 1, a, (int) n - 1);
        bool temEstrela = false, temAuto = false;
        double fixo = 0;
        int ultimaAuto = -1;
        for (int i = a; i <= b; i++)
        {
            if (estrela[i]) temEstrela = true;
            if (automatico[i]) temAuto = true, ultimaAuto = i;
            else fixo += tam[i];
        }
        if (temEstrela || !temAuto) continue;
        double desejo = colunas ? c->DesiredSize.Width : c->DesiredSize.Height;
        double atual = 0;
        for (int i = a; i <= b; i++)
            if (automatico[i]) atual += tam[i];
        double falta = desejo - fixo - atual;
        if (falta > 0) tam[ultimaAuto] = std::min(maximos[ultimaAuto], tam[ultimaAuto] + falta);
    }
    // as estrelas dividem o que sobrou, respeitando minimo e maximo
    if (!infinito)
    {
        double usado = 0;
        for (size_t i = 0; i < n; i++)
            if (!estrela[i]) usado += tam[i];
        double resto = std::max(0.0, disp - usado);
        std::vector<char> presa(n, 0);
        for (int volta = 0; volta < 8; volta++)
        {
            double soma = 0, livre = resto;
            for (size_t i = 0; i < n; i++)
                if (estrela[i])
                {
                    if (presa[i]) livre -= tam[i];
                    else soma += pesos[i];
                }
            bool mudou = false;
            for (size_t i = 0; i < n; i++)
            {
                if (!estrela[i] || presa[i]) continue;
                double v = soma > 0 ? std::max(0.0, livre) * pesos[i] / soma : 0;
                if (v < minimos[i]) v = minimos[i], presa[i] = 1, mudou = true;
                else if (v > maximos[i]) v = maximos[i], presa[i] = 1, mudou = true;
                tam[i] = v;
            }
            if (!mudou) break;
        }
        if (medindo)
        {
            // o desejado das estrelas: o que o conteudo pede (o Grid centralizado nao estica)
            std::vector<double> pede(n, 0);
            for (auto &c : Children)
            {
                if (c->Visibility == Vistas::Visibility::Collapsed) continue;
                int a = std::clamp(colunas ? c->Grid_Column : c->Grid_Row, 0, (int) n - 1);
                int s = std::max(1, colunas ? c->Grid_ColumnSpan : c->Grid_RowSpan);
                if (s == 1 && estrela[a]) pede[a] = std::max(pede[a], colunas ? c->DesiredSize.Width : c->DesiredSize.Height);
            }
            _desejoEstrela = 0;
            for (size_t i = 0; i < n; i++)
                if (estrela[i]) _desejoEstrela += std::max(pede[i], minimos[i]);
        }
    }
    else if (medindo)
        _desejoEstrela = 0;
}

Size Grid::MeasureOverride(Size d)
{
    // 1) colunas: os filhos medidos com a largura livre; 2) linhas, com a largura das colunas
    for (auto &c : Children) c->Measure(Size(Infinito, Infinito));
    Resolve(_col, true, d.Width, true);
    double desejoEstrelaCol = _desejoEstrela;
    auto largura = [&](const UIElementPtr &c) {
        int a = std::clamp(c->Grid_Column, 0, (int) _col.size() - 1);
        int b = std::min((int) _col.size() - 1, a + std::max(1, c->Grid_ColumnSpan) - 1);
        double w = 0;
        for (int i = a; i <= b; i++) w += _col[i];
        return w;
    };
    for (auto &c : Children) c->Measure(Size(largura(c), Infinito));
    Resolve(_lin, false, d.Height, true);
    double desejoEstrelaLin = _desejoEstrela;
    // medida final, com as celulas de verdade (o ScrollViewer precisa saber a altura)
    for (auto &c : Children)
    {
        int a = std::clamp(c->Grid_Row, 0, (int) _lin.size() - 1);
        int b = std::min((int) _lin.size() - 1, a + std::max(1, c->Grid_RowSpan) - 1);
        double h = 0;
        bool infinita = false;
        for (int i = a; i <= b; i++)
        {
            h += _lin[i];
            GridLength g = RowDefinitions.empty() ? GridLength::Star(1) : RowDefinitions[i].Height;
            if (g.IsAuto() || (g.IsStar() && !Finito(d.Height))) infinita = true;
        }
        c->Measure(Size(largura(c), infinita ? Infinito : h));
    }
    // o desejado das estrelas com as celulas de verdade (o WPF soma o que o conteudo pediu medido na
    // celula: um DataGrid numa linha estrela pede no maximo a altura que ganhou)
    auto desejo = [&](bool colunas, size_t n) {
        std::vector<double> pede(n, 0);
        for (auto &c : Children)
        {
            if (c->Visibility == Vistas::Visibility::Collapsed) continue;
            int a = std::clamp(colunas ? c->Grid_Column : c->Grid_Row, 0, (int) n - 1);
            int s = std::max(1, colunas ? c->Grid_ColumnSpan : c->Grid_RowSpan);
            if (s == 1) pede[a] = std::max(pede[a], colunas ? c->DesiredSize.Width : c->DesiredSize.Height);
        }
        double soma = 0;
        for (size_t i = 0; i < n; i++)
        {
            GridLength g = colunas ? (ColumnDefinitions.empty() ? GridLength::Star(1) : ColumnDefinitions[i].Width)
                                   : (RowDefinitions.empty() ? GridLength::Star(1) : RowDefinitions[i].Height);
            if (g.IsStar()) soma += pede[i];
        }
        return soma;
    };
    if (Finito(d.Width)) desejoEstrelaCol = desejo(true, _col.size());
    if (Finito(d.Height)) desejoEstrelaLin = desejo(false, _lin.size());
    double w = 0, h = 0;
    for (size_t i = 0; i < _col.size(); i++)
    {
        GridLength g = ColumnDefinitions.empty() ? GridLength::Star(1) : ColumnDefinitions[i].Width;
        if (!(g.IsStar() && Finito(d.Width))) w += _col[i];
    }
    for (size_t i = 0; i < _lin.size(); i++)
    {
        GridLength g = RowDefinitions.empty() ? GridLength::Star(1) : RowDefinitions[i].Height;
        if (!(g.IsStar() && Finito(d.Height))) h += _lin[i];
    }
    return Size(w + desejoEstrelaCol, h + desejoEstrelaLin);
}

Size Grid::ArrangeOverride(Size f)
{
    Resolve(_col, true, f.Width, false);
    Resolve(_lin, false, f.Height, false);
    std::vector<double> xs(_col.size() + 1, 0), ys(_lin.size() + 1, 0);
    for (size_t i = 0; i < _col.size(); i++) xs[i + 1] = xs[i] + _col[i];
    for (size_t i = 0; i < _lin.size(); i++) ys[i + 1] = ys[i] + _lin[i];
    for (size_t i = 0; i < ColumnDefinitions.size(); i++) ColumnDefinitions[i].ActualWidth = _col[i], ColumnDefinitions[i].Offset = xs[i];
    for (size_t i = 0; i < RowDefinitions.size(); i++) RowDefinitions[i].ActualHeight = _lin[i], RowDefinitions[i].Offset = ys[i];
    for (auto &c : Children)
    {
        int ca = std::clamp(c->Grid_Column, 0, (int) _col.size() - 1);
        int cb = std::min((int) _col.size() - 1, ca + std::max(1, c->Grid_ColumnSpan) - 1);
        int la = std::clamp(c->Grid_Row, 0, (int) _lin.size() - 1);
        int lb = std::min((int) _lin.size() - 1, la + std::max(1, c->Grid_RowSpan) - 1);
        c->Arrange(Rect(Limites.X + xs[ca], Limites.Y + ys[la], xs[cb + 1] - xs[ca], ys[lb + 1] - ys[la]));
    }
    return f;
}

// =====================================================================================
// Border
// =====================================================================================
Border::~Border()
{
    if (_filho && _filho->Parent == this) _filho->Parent = nullptr;
}

void Border::SetChild(UIElementPtr c)
{
    if (_filho) Solta(_filho.get());
    _filho = std::move(c);
    if (_filho) Adota(this, _filho.get());
}

void Border::Filhos(std::vector<UIElement *> &s) const
{
    if (_filho) s.push_back(_filho.get());
}

Size Border::MeasureOverride(Size d)
{
    double fw = BorderThickness.Horizontal() + Padding.Horizontal(), fh = BorderThickness.Vertical() + Padding.Vertical();
    if (!_filho) return Size(fw, fh);
    _filho->Measure(Size(std::max(0.0, d.Width - fw), std::max(0.0, d.Height - fh)));
    return Size(_filho->DesiredSize.Width + fw, _filho->DesiredSize.Height + fh);
}

Size Border::ArrangeOverride(Size f)
{
    if (_filho)
    {
        double l = BorderThickness.Left + Padding.Left, t = BorderThickness.Top + Padding.Top;
        _filho->Arrange(Rect(Limites.X + l, Limites.Y + t, std::max(0.0, f.Width - l - BorderThickness.Right - Padding.Right),
                             std::max(0.0, f.Height - t - BorderThickness.Bottom - Padding.Bottom)));
    }
    return f;
}

void Border::OnRender(DrawingContext &dc) { dc.Retangulo(Limites, Background, BorderBrush, BorderThickness, CornerRadius); }

bool Border::AcertaProprio(Point p) const
{
    (void) p;
    return !Background.Nulo() || (!BorderBrush.Nulo() && (BorderThickness.Left > 0 || BorderThickness.Top > 0));
}

// =====================================================================================
// TextBlock
// =====================================================================================
Vistas::TextWrapping TextBlock::QuebraEfetiva() const
{
    if (TextWrapping) return *TextWrapping;
    const Vistas::Style *s = EstiloEfetivo();
    if (s && s->TextWrapping) return *s->TextWrapping;
    return Vistas::TextWrapping::NoWrap;
}

void TextBlock::Quebrar(double largura)
{
    FonteTexto f = Fonte();
    bool q = QuebraEfetiva() != Vistas::TextWrapping::NoWrap;
    if (_textoMedido == Text && _larguraMedida == largura && _quebraMedida == q && _fonteMedida.Familia == f.Familia &&
        _fonteMedida.Tamanho == f.Tamanho && _fonteMedida.Peso == f.Peso && _fonteMedida.Estilo == f.Estilo)
        return;
    _linhas = Vistas::Texto::Quebra(f, Text, largura, q);
    _textoMedido = Text;
    _larguraMedida = largura;
    _quebraMedida = q;
    _fonteMedida = f;
}

Size TextBlock::MeasureOverride(Size d)
{
    double disp = std::max(0.0, d.Width - Padding.Horizontal());
    Quebrar(disp);
    FonteTexto f = Fonte();
    double lh = std::isnan(LineHeight) ? Vistas::Texto::AlturaLinha(f) : LineHeight;
    double w = 0;
    for (auto &l : _linhas) w = std::max(w, l.Largura);
    // com reticencias o texto nunca pede mais que a vaga
    if (TextTrimming != Vistas::TextTrimming::None) w = std::min(w, disp);
    return Size(std::ceil(w) + Padding.Horizontal(), lh * _linhas.size() + Padding.Vertical());
}

Size TextBlock::ArrangeOverride(Size f)
{
    Quebrar(std::max(0.0, f.Width - Padding.Horizontal()));
    return f;
}

void TextBlock::OnRender(DrawingContext &dc)
{
    if (!Background.Nulo()) dc.Retangulo(Limites, Background);
    if (Text.empty()) return;
    FonteTexto f = Fonte();
    Brush cor = ForegroundEfetivo();
    double lh = std::isnan(LineHeight) ? Vistas::Texto::AlturaLinha(f) : LineHeight;
    double larg = Limites.Width - Padding.Horizontal();
    double y = Limites.Y + Padding.Top;
    // LineHeight maior que a fonte: o WPF poe a sobra em cima (a linha de base desce)
    double desc = std::isnan(LineHeight) ? 0 : std::max(0.0, LineHeight - Vistas::Texto::AlturaLinha(f));
    for (size_t i = 0; i < _linhas.size(); i++, y += lh)
    {
        std::string s = Text.substr(_linhas[i].Inicio, _linhas[i].Fim - _linhas[i].Inicio);
        double w = _linhas[i].Largura;
        if (TextTrimming != Vistas::TextTrimming::None && w > larg + 0.5)
        {
            s = Vistas::Texto::ComReticencias(f, s, larg);
            w = Vistas::Texto::Largura(f, s);
        }
        double x = Limites.X + Padding.Left;
        if (TextAlignment == Vistas::TextAlignment::Center) x += (larg - w) / 2;
        else if (TextAlignment == Vistas::TextAlignment::Right) x += larg - w;
        dc.Texto(s, f, x, y + desc, cor);
    }
}

bool TextBlock::AcertaProprio(Point p) const
{
    (void) p;
    return true;
}

// =====================================================================================
// Image e formas
// =====================================================================================
Size Image::MeasureOverride(Size d)
{
    if (!Source) return Size();
    double iw = Source->Width(), ih = Source->Height();
    if (iw <= 0 || ih <= 0) return Size();
    if (Stretch == Vistas::Stretch::None) return Size(iw, ih);
    bool fw = Finito(d.Width), fh = Finito(d.Height);
    if (!fw && !fh) return Size(iw, ih);
    double sx = fw ? d.Width / iw : Infinito, sy = fh ? d.Height / ih : Infinito;
    switch (Stretch)
    {
    case Vistas::Stretch::Uniform:
    {
        double s = std::min(sx, sy);
        return Size(iw * s, ih * s);
    }
    case Vistas::Stretch::UniformToFill:
    {
        double s = !fw ? sy : !fh ? sx : std::max(sx, sy);
        return Size(fw ? std::min(d.Width, iw * s) : iw * s, fh ? std::min(d.Height, ih * s) : ih * s);
    }
    default:
        return Size(fw ? d.Width : iw * sy, fh ? d.Height : ih * sx);
    }
}

Size Image::ArrangeOverride(Size f) { return f; }

Rect Image::AreaDaImagem() const
{
    if (!Source) return Rect();
    double iw = Source->Width(), ih = Source->Height();
    Rect r = Limites;
    if (iw <= 0 || ih <= 0) return Rect();
    switch (Stretch)
    {
    case Vistas::Stretch::None: return Rect(r.X, r.Y, iw, ih);
    case Vistas::Stretch::Fill: return r;
    case Vistas::Stretch::Uniform:
    {
        double s = std::min(r.Width / iw, r.Height / ih);
        return Rect(r.X + (r.Width - iw * s) / 2, r.Y + (r.Height - ih * s) / 2, iw * s, ih * s);
    }
    case Vistas::Stretch::UniformToFill:
    {
        double s = std::max(r.Width / iw, r.Height / ih);
        return Rect(r.X + (r.Width - iw * s) / 2, r.Y + (r.Height - ih * s) / 2, iw * s, ih * s);
    }
    }
    return r;
}

void Image::OnRender(DrawingContext &dc)
{
    if (!Source) return;
    Rect a = AreaDaImagem();
    bool recorta = Stretch == Vistas::Stretch::UniformToFill || Stretch == Vistas::Stretch::None;
    if (recorta) dc.Recorta(Limites);
    dc.Imagem(*Source, a);
    if (recorta) dc.Restaura();
}

bool Image::AcertaProprio(Point p) const { return Source && AreaDaImagem().Contains(p); }

bool Shape::AcertaProprio(Point p) const
{
    (void) p;
    return !Fill.Nulo() || !Stroke.Nulo();
}

void Rectangle::OnRender(DrawingContext &dc)
{
    double r = std::max(RadiusX, RadiusY);
    if (!Fill.Nulo()) dc.Retangulo(Limites, Fill, Brush(), Thickness(), Vistas::CornerRadius(r));
    if (!Stroke.Nulo() && StrokeThickness > 0)
        dc.Retangulo(Limites, Brush(), Stroke, Thickness(StrokeThickness), Vistas::CornerRadius(r));
}

void Ellipse::OnRender(DrawingContext &dc) { dc.Elipse(Limites, Fill, Stroke, StrokeThickness); }

Size Line::MeasureOverride(Size d)
{
    (void) d;
    return Size(std::max(0.0, std::max(X1, X2)), std::max(0.0, std::max(Y1, Y2)));
}

void Line::OnRender(DrawingContext &dc)
{
    if (Stroke.Nulo() || StrokeThickness <= 0) return;
    dc.Linha(Point(Limites.X + X1, Limites.Y + Y1), Point(Limites.X + X2, Limites.Y + Y2), Stroke, StrokeThickness);
}

namespace {
struct ComandoCaminho
{
    char c;
    std::vector<double> v;
};

std::vector<ComandoCaminho> LeCaminho(const std::string &d, Rect &lim)
{
    std::vector<ComandoCaminho> r;
    size_t i = 0;
    auto pula = [&] {
        while (i < d.size() && (d[i] == ' ' || d[i] == ',' || d[i] == '\t' || d[i] == '\n')) i++;
    };
    char atual = 0;
    double x = 0, y = 0, minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    auto marca = [&](double a, double b) {
        minx = std::min(minx, a), miny = std::min(miny, b), maxx = std::max(maxx, a), maxy = std::max(maxy, b);
    };
    while (true)
    {
        pula();
        if (i >= d.size()) break;
        if (isalpha((unsigned char) d[i]) && d[i] != 'e' && d[i] != 'E')
        {
            atual = d[i++];
            if (atual == 'Z' || atual == 'z' || atual == 'F') { r.push_back({'Z', {}}); continue; }
            continue;
        }
        int n = (atual == 'M' || atual == 'm' || atual == 'L' || atual == 'l') ? 2 : (atual == 'H' || atual == 'h' || atual == 'V' || atual == 'v') ? 1
              : (atual == 'C' || atual == 'c') ? 6 : (atual == 'Q' || atual == 'q') ? 4 : 0;
        if (!n) { i++; continue; }
        ComandoCaminho c{atual, {}};
        for (int k = 0; k < n; k++)
        {
            pula();
            char *fim = nullptr;
            double v = strtod(d.c_str() + i, &fim);
            if (fim == d.c_str() + i) break;
            i = (size_t) (fim - d.c_str());
            c.v.push_back(v);
        }
        if ((int) c.v.size() < n) break;
        bool rel = islower((unsigned char) atual);
        // tudo em absoluto
        switch (toupper(atual))
        {
        case 'M': case 'L':
            if (rel) c.v[0] += x, c.v[1] += y;
            x = c.v[0], y = c.v[1];
            c.c = toupper(atual);
            break;
        case 'H':
            x = rel ? x + c.v[0] : c.v[0];
            c = {'L', {x, y}};
            break;
        case 'V':
            y = rel ? y + c.v[0] : c.v[0];
            c = {'L', {x, y}};
            break;
        case 'C':
            if (rel) for (int k = 0; k < 6; k += 2) c.v[k] += x, c.v[k + 1] += y;
            x = c.v[4], y = c.v[5];
            c.c = 'C';
            break;
        case 'Q':
            if (rel) for (int k = 0; k < 4; k += 2) c.v[k] += x, c.v[k + 1] += y;
            x = c.v[2], y = c.v[3];
            c.c = 'Q';
            break;
        }
        for (size_t k = 0; k + 1 < c.v.size(); k += 2) marca(c.v[k], c.v[k + 1]);
        r.push_back(c);
        if (atual == 'M') atual = 'L';
        else if (atual == 'm') atual = 'l';
    }
    lim = minx <= maxx ? Rect(minx, miny, maxx - minx, maxy - miny) : Rect();
    return r;
}
} // namespace

Size Path::MeasureOverride(Size d)
{
    (void) d;
    Rect lim;
    LeCaminho(Data, lim);
    return Size(std::max(0.0, lim.Right()) + StrokeThickness / 2, std::max(0.0, lim.Bottom()) + StrokeThickness / 2);
}

void Path::OnRender(DrawingContext &dc)
{
    Rect lim;
    auto cmds = LeCaminho(Data, lim);
    if (cmds.empty()) return;
    double ox = Limites.X, oy = Limites.Y;
    auto caminho = [&] {
        for (auto &c : cmds)
        {
            switch (c.c)
            {
            case 'M': nvgMoveTo(dc.vg, (float) (ox + c.v[0]), (float) (oy + c.v[1])); break;
            case 'L': nvgLineTo(dc.vg, (float) (ox + c.v[0]), (float) (oy + c.v[1])); break;
            case 'C': nvgBezierTo(dc.vg, (float) (ox + c.v[0]), (float) (oy + c.v[1]), (float) (ox + c.v[2]), (float) (oy + c.v[3]), (float) (ox + c.v[4]), (float) (oy + c.v[5])); break;
            case 'Q': nvgQuadTo(dc.vg, (float) (ox + c.v[0]), (float) (oy + c.v[1]), (float) (ox + c.v[2]), (float) (oy + c.v[3])); break;
            case 'Z': nvgClosePath(dc.vg); break;
            }
        }
    };
    Rect l(ox + lim.X, oy + lim.Y, lim.Width, lim.Height);
    if (!Fill.Nulo()) dc.PreencheCaminho(Fill, l, caminho);
    if (!Stroke.Nulo() && StrokeThickness > 0)
    {
        nvgBeginPath(dc.vg);
        caminho();
        nvgStrokeColor(dc.vg, CorNvg(Stroke.Cor(), Stroke.Opacity));
        nvgStrokeWidth(dc.vg, (float) StrokeThickness);
        nvgLineJoin(dc.vg, NVG_MITER);
        nvgStroke(dc.vg);
    }
}

// =====================================================================================
// Control, Conteudo, ContentControl
// =====================================================================================
Brush Control::BackgroundEfetivo() const
{
    if (!Background.Nulo()) return Background;
    const Vistas::Style *s = EstiloEfetivo();
    return s && s->Background ? *s->Background : Brush();
}

Brush Control::BorderBrushEfetivo() const
{
    if (!BorderBrush.Nulo()) return BorderBrush;
    const Vistas::Style *s = EstiloEfetivo();
    return s && s->BorderBrush ? *s->BorderBrush : Brush();
}

Thickness Control::BorderThicknessEfetiva() const
{
    if (BorderThickness) return *BorderThickness;
    const Vistas::Style *s = EstiloEfetivo();
    return s && s->BorderThickness ? *s->BorderThickness : Thickness();
}

Thickness Control::PaddingEfetivo() const
{
    if (Padding) return *Padding;
    const Vistas::Style *s = EstiloEfetivo();
    return s && s->Padding ? *s->Padding : Thickness();
}

Vistas::Gabarito Control::GabaritoEfetivo() const
{
    if (Template) return *Template;
    const Vistas::Style *s = EstiloEfetivo();
    return s && s->Template ? *s->Template : Vistas::Gabarito::Nenhum;
}

bool Control::AcertaProprio(Point p) const
{
    (void) p;
    return true;
}

Conteudo &Conteudo::operator=(const std::string &texto)
{
    if (_ehTexto && _el)
    {
        static_cast<TextBlock *>(_el.get())->Text = texto;
        if (Window *j = _dono->Janela()) j->Redesenha();
        return *this;
    }
    auto t = std::make_shared<TextBlock>(texto);
    *this = std::static_pointer_cast<UIElement>(t);
    _ehTexto = true;
    return *this;
}

Conteudo &Conteudo::operator=(UIElementPtr e)
{
    if (_el) UIElement::Solta(_el.get());
    _el = std::move(e);
    _ehTexto = false;
    if (_el) UIElement::Adota(_dono, _el.get());
    return *this;
}

std::string Conteudo::ToString() const
{
    if (auto *t = dynamic_cast<TextBlock *>(_el.get())) return t->Text;
    return "";
}

ContentControl::~ContentControl()
{
    if (auto e = Content.Elemento())
        if (e->Parent == this) e->Parent = nullptr;
}

void ContentControl::Filhos(std::vector<UIElement *> &s) const
{
    if (auto e = Content.Elemento()) s.push_back(e.get());
}

Thickness ContentControl::MolduraDoConteudo() const
{
    Thickness b = BorderThicknessEfetiva(), p = PaddingEfetivo();
    return Thickness(b.Left + p.Left, b.Top + p.Top, b.Right + p.Right, b.Bottom + p.Bottom);
}

Size ContentControl::MeasureOverride(Size d)
{
    Thickness m = MolduraDoConteudo();
    auto e = Content.Elemento();
    if (!e) return Size(m.Horizontal(), m.Vertical());
    e->Measure(Size(std::max(0.0, d.Width - m.Horizontal()), std::max(0.0, d.Height - m.Vertical())));
    return Size(e->DesiredSize.Width + m.Horizontal(), e->DesiredSize.Height + m.Vertical());
}

namespace {
// o ContentPresenter com HorizontalAlignment/VerticalAlignment do gabarito
void ArrumaConteudo(UIElement *e, const Rect &in, Vistas::HorizontalAlignment h, Vistas::VerticalAlignment v)
{
    if (!e) return;
    double w = in.Width, hh = in.Height, x = in.X, y = in.Y;
    if (h != Vistas::HorizontalAlignment::Stretch)
    {
        w = std::min(e->DesiredSize.Width, in.Width);
        if (h == Vistas::HorizontalAlignment::Center) x += (in.Width - w) / 2;
        else if (h == Vistas::HorizontalAlignment::Right) x += in.Width - w;
    }
    if (v != Vistas::VerticalAlignment::Stretch)
    {
        hh = std::min(e->DesiredSize.Height, in.Height);
        if (v == Vistas::VerticalAlignment::Center) y += (in.Height - hh) / 2;
        else if (v == Vistas::VerticalAlignment::Bottom) y += in.Height - hh;
    }
    e->Arrange(Rect(x, y, w, hh));
}
} // namespace

Size ContentControl::ArrangeOverride(Size f)
{
    Thickness m = MolduraDoConteudo();
    Rect in(Limites.X + m.Left, Limites.Y + m.Top, std::max(0.0, f.Width - m.Horizontal()), std::max(0.0, f.Height - m.Vertical()));
    ArrumaConteudo(Content.Elemento().get(), in, HorizontalContentAlignment, VerticalContentAlignment);
    return f;
}

void ContentControl::OnRender(DrawingContext &dc)
{
    dc.Retangulo(Limites, BackgroundEfetivo(), BorderBrushEfetivo(), BorderThicknessEfetiva());
}

bool UserControl::AcertaProprio(Point p) const
{
    (void) p;
    return !BackgroundEfetivo().Nulo();
}

// =====================================================================================
// Gabaritos
// =====================================================================================
void DesenhaGabarito(DrawingContext &dc, const Rect &r, const GabaritoBorda &g, bool sobre, bool apertado, bool marcado,
                     bool ligado, const Brush &fundoLocal, const Brush &bordaLocal)
{
    (void) ligado;
    Brush fundo = !fundoLocal.Nulo() ? fundoLocal : g.Fundo;
    Brush borda = !bordaLocal.Nulo() ? bordaLocal : g.Borda;
    if (sobre)
    {
        if (!g.FundoSobre.Nulo()) fundo = g.FundoSobre;
        if (!g.BordaSobre.Nulo()) borda = g.BordaSobre;
    }
    if (apertado && !g.FundoApertado.Nulo()) fundo = g.FundoApertado;
    if (marcado)
    {
        if (!g.FundoMarcado.Nulo()) fundo = g.FundoMarcado;
        if (!g.BordaMarcada.Nulo()) borda = g.BordaMarcada;
    }
    dc.Retangulo(r, fundo, borda, g.Espessura, g.Cantos);
}

namespace {
Thickness Soma(Thickness a, Thickness b) { return Thickness(a.Left + b.Left, a.Top + b.Top, a.Right + b.Right, a.Bottom + b.Bottom); }

// a moldura do conteudo em cada gabarito
Thickness MolduraDe(const Control &c, Vistas::Gabarito g)
{
    const GabaritoBorda &m = Tema::Modelo(g);
    switch (g)
    {
    case Vistas::Gabarito::Nenhum: return Soma(c.BorderThicknessEfetiva(), c.PaddingEfetivo());
    case Vistas::Gabarito::OrbePlay: return c.PaddingEfetivo();
    case Vistas::Gabarito::CheckBox: return Thickness(Tema::CaixaMarcarLado + 6, 0, 0, 0);
    case Vistas::Gabarito::RadioButton: return Thickness(Tema::CaixaMarcarLado + 4, 0, 0, 0);
    default:
        return Soma(m.Espessura, m.RecuoDoControle ? c.PaddingEfetivo() : m.Recuo);
    }
}

// o botao de opcao do Aero (o Tema nao o redesenha): a bolinha clara com o ponto
void BolinhaAero(DrawingContext &dc, Rect r, bool marcado, bool sobre, bool apertado, bool ligado)
{
    double lado = Tema::CaixaMarcarLado;
    Rect b(r.X, Arred(r.Y + (r.Height - lado) / 2), lado, lado);
    Brush borda = Brush(Color::De(!ligado ? "#FFAEB2B5" : sobre ? "#FF3C7FB1" : "#FF8E8F8F"));
    Brush fundo = Brush::Linear(Point(0, 0), Point(1, 1),
                                {{Color::De(apertado ? "#FFC2E4F6" : sobre ? "#FFE1F2FB" : "#FFF4F4F4"), 0},
                                 {Color::De(apertado ? "#FF9AD3F2" : sobre ? "#FFC5E9FB" : "#FFDCDCDC"), 1}});
    dc.Elipse(b, fundo, borda, 1);
    if (marcado)
    {
        Rect p(b.X + 3.5, b.Y + 3.5, lado - 7, lado - 7);
        dc.Elipse(p, Brush::Linear(Point(0, 0), Point(0, 1), {{Color::De(ligado ? "#FF3F8DD1" : "#FFAEB2B5"), 0},
                                                              {Color::De(ligado ? "#FF0D3A6B" : "#FF8A8A8A"), 1}}));
    }
}
} // namespace

// =====================================================================================
// ButtonBase, Button
// =====================================================================================
void ButtonBase::OnClick()
{
    RoutedEventArgs a;
    a.Source = this;
    a.OriginalSource = this;
    Click(a);
}

void ButtonBase::PerformClick()
{
    if (IsEnabledEfetivo()) OnClick();
}

void ButtonBase::OnMouseDown(MouseButtonEventArgs &e)
{
    if (e.ChangedButton != MouseButton::Left) return;
    e.Handled = true;
    Focus();
    if (ClickMode == Vistas::ClickMode::Press)
    {
        OnClick();
        return;
    }
    _apertado = true;
    CaptureMouse();
}

void ButtonBase::OnMouseUp(MouseButtonEventArgs &e)
{
    if (e.ChangedButton != MouseButton::Left || !_apertado) return;
    e.Handled = true;
    bool dentro = Limites.Contains(e.Posicao);
    _apertado = false;
    ReleaseMouseCapture();
    if (dentro && IsEnabledEfetivo()) OnClick();
}

void ButtonBase::OnLostMouseCapture() { _apertado = false; }

void ButtonBase::OnKeyDown(KeyEventArgs &e)
{
    if (e.Key == Key::Space && !e.IsRepeat)
    {
        _apertadoTeclado = true;
        e.Handled = true;
    }
    else if (e.Key == Key::Enter)
    {
        // Enter no botao com foco clica (KeyboardNavigation.AcceptsReturn do Button)
        e.Handled = true;
        OnClick();
    }
}

void ButtonBase::OnKeyUp(KeyEventArgs &e)
{
    if (e.Key == Key::Space && _apertadoTeclado)
    {
        _apertadoTeclado = false;
        e.Handled = true;
        OnClick();
    }
}

Vistas::Cursor ButtonBase::CursorEm(Point p) const { return UIElement::CursorEm(p); }

Button::Button()
{
    HorizontalContentAlignment = Vistas::HorizontalAlignment::Center;
    VerticalContentAlignment = Vistas::VerticalAlignment::Center;
}

void Button::OnClick()
{
    // o Click primeiro; depois, se e o botao de cancelar de uma janela modal, ela fecha com
    // DialogResult = false (o Window.DialogCancelCommand do WPF)
    ButtonBase::OnClick();
    if (IsCancel)
        if (Window *j = Janela())
            if (j->EhModal() && j->EstaAberta()) j->SetDialogResult(false);
}

Thickness Button::MolduraDoConteudo() const { return MolduraDe(*this, GabaritoEfetivo()); }

Size Button::MeasureOverride(Size d) { return ContentControl::MeasureOverride(d); }

Size Button::ArrangeOverride(Size f)
{
    Vistas::Gabarito g = GabaritoEfetivo();
    Thickness m = MolduraDoConteudo();
    Rect in(Limites.X + m.Left, Limites.Y + m.Top, std::max(0.0, f.Width - m.Horizontal()), std::max(0.0, f.Height - m.Vertical()));
    if (g == Vistas::Gabarito::Nenhum)
        ArrumaConteudo(Content.Elemento().get(), in, HorizontalContentAlignment, VerticalContentAlignment);
    else
    {
        const GabaritoBorda &gb = Tema::Modelo(g);
        ArrumaConteudo(Content.Elemento().get(), in, gb.Conteudo, gb.ConteudoV);
    }
    return f;
}

std::optional<Brush> Button::ForegroundDoGabarito() const
{
    if (!IsEnabledEfetivo())
    {
        const GabaritoBorda &g = Tema::Modelo(GabaritoEfetivo());
        if (!g.TextoDesligado.Nulo() && Foreground.Nulo()) return g.TextoDesligado;
    }
    return std::nullopt;
}

double Button::OpacidadeDoGabarito() const
{
    if (IsEnabledEfetivo()) return 1;
    return Tema::Modelo(GabaritoEfetivo()).OpacidadeDesligado;
}

void Button::OnRender(DrawingContext &dc)
{
    Vistas::Gabarito g = GabaritoEfetivo();
    bool sobre = IsMouseOver(), apertado = IsPressed() || _apertadoTeclado, ligado = IsEnabledEfetivo();
    if (g == Vistas::Gabarito::Nenhum)
    {
        ContentControl::OnRender(dc);
        return;
    }
    if (g == Vistas::Gabarito::OrbePlay)
    {
        // <Ellipse Fill="{StaticResource OrbePlay}" Stroke="#FF6B4310" StrokeThickness="1" />
        // <Ellipse Margin="4,3,4,22" Fill="#55FFFFFF" />  (o brilho de cima)
        const GabaritoBorda &m = Tema::Modelo(g);
        dc.Elipse(Limites, m.Fundo, m.Borda, 1);
        Rect brilho(Limites.X + 4, Limites.Y + 3, std::max(0.0, Limites.Width - 8), std::max(0.0, Limites.Height - 25));
        dc.Elipse(brilho, Brush(Color::De("#55FFFFFF")));
        return;
    }
    DesenhaGabarito(dc, Limites, Tema::Modelo(g), sobre, apertado, false, ligado, Background, BorderBrush);
}

void Button::OnRenderDepois(DrawingContext &dc) { (void) dc; }

// =====================================================================================
// ToggleButton, RadioButton, CheckBox
// =====================================================================================
ToggleButton::ToggleButton()
    : IsChecked(false, [this](const bool &, const bool &novo) { AoMarcar(novo); })
{
    VerticalContentAlignment = Vistas::VerticalAlignment::Center;
}

void ToggleButton::AoMarcar(bool marcado)
{
    RoutedEventArgs a;
    a.Source = a.OriginalSource = this;
    if (marcado) Checked(a);
    else Unchecked(a);
    InvalidateVisual();
}

void ToggleButton::OnClick()
{
    IsChecked = !IsChecked();
    ButtonBase::OnClick();
}

Thickness ToggleButton::MolduraDoConteudo() const
{
    Vistas::Gabarito g = GabaritoEfetivo();
    if (g == Vistas::Gabarito::Nenhum) g = Vistas::Gabarito::ToggleButton;
    return MolduraDe(*this, g);
}

Size ToggleButton::MeasureOverride(Size d)
{
    Size s = ContentControl::MeasureOverride(d);
    Vistas::Gabarito g = GabaritoEfetivo();
    if (g == Vistas::Gabarito::CheckBox || g == Vistas::Gabarito::RadioButton) s.Height = std::max(s.Height, Tema::CaixaMarcarLado);
    return s;
}

Size ToggleButton::ArrangeOverride(Size f)
{
    Vistas::Gabarito g = GabaritoEfetivo();
    if (g == Vistas::Gabarito::Nenhum) g = Vistas::Gabarito::ToggleButton;
    Thickness m = MolduraDoConteudo();
    Rect in(Limites.X + m.Left, Limites.Y + m.Top, std::max(0.0, f.Width - m.Horizontal()), std::max(0.0, f.Height - m.Vertical()));
    if (g == Vistas::Gabarito::CheckBox || g == Vistas::Gabarito::RadioButton)
        ArrumaConteudo(Content.Elemento().get(), in, Vistas::HorizontalAlignment::Left, Vistas::VerticalAlignment::Center);
    else
    {
        const GabaritoBorda &gb = Tema::Modelo(g);
        ArrumaConteudo(Content.Elemento().get(), in, gb.Conteudo, gb.ConteudoV);
    }
    return f;
}

std::optional<Brush> ToggleButton::ForegroundDoGabarito() const
{
    Vistas::Gabarito g = GabaritoEfetivo();
    if (g == Vistas::Gabarito::Nenhum) g = Vistas::Gabarito::ToggleButton;
    const GabaritoBorda &m = Tema::Modelo(g);
    if (!IsEnabledEfetivo() && !m.TextoDesligado.Nulo()) return m.TextoDesligado;
    // a CheckBox usa TextoMarcado so para a marca, nao para o texto
    if (IsChecked() && !m.TextoMarcado.Nulo() && g != Vistas::Gabarito::CheckBox) return m.TextoMarcado;
    return std::nullopt;
}

std::optional<Vistas::FontWeight> ToggleButton::FontWeightDoGabarito() const
{
    Vistas::Gabarito g = GabaritoEfetivo();
    if (IsChecked() && g != Vistas::Gabarito::Nenhum) return Tema::Modelo(g).PesoMarcado;
    return std::nullopt;
}

double ToggleButton::OpacidadeDoGabarito() const
{
    if (IsEnabledEfetivo()) return 1;
    Vistas::Gabarito g = GabaritoEfetivo();
    return Tema::Modelo(g == Vistas::Gabarito::Nenhum ? Vistas::Gabarito::ToggleButton : g).OpacidadeDesligado;
}

void ToggleButton::OnRender(DrawingContext &dc)
{
    Vistas::Gabarito g = GabaritoEfetivo();
    bool sobre = IsMouseOver(), apertado = IsPressed() || _apertadoTeclado, ligado = IsEnabledEfetivo();
    if (g == Vistas::Gabarito::CheckBox)
    {
        // a caixa de 13 px, funda, com o visto em fosforo
        const GabaritoBorda &m = Tema::Modelo(g);
        double lado = Tema::CaixaMarcarLado;
        Rect b(Limites.X, Arred(Limites.Y + (Limites.Height - lado) / 2), lado, lado);
        DesenhaGabarito(dc, b, m, sobre, false, false, ligado);
        if (IsChecked())
        {
            // Path Data="M1,5 L4,8 L10,1" com Margin 1 dentro da borda de 1
            double ox = b.X + 2, oy = b.Y + 2;
            nvgBeginPath(dc.vg);
            nvgMoveTo(dc.vg, (float) (ox + 1), (float) (oy + 5));
            nvgLineTo(dc.vg, (float) (ox + 4), (float) (oy + 8));
            nvgLineTo(dc.vg, (float) (ox + 10), (float) (oy + 1));
            Color c = m.TextoMarcado.Cor();
            nvgStrokeColor(dc.vg, nvgRGBA(c.R, c.G, c.B, c.A));
            nvgStrokeWidth(dc.vg, 2);
            nvgLineJoin(dc.vg, NVG_MITER);
            nvgStroke(dc.vg);
        }
        // fundo transparente do StackPanel do gabarito (recebe clique no texto)
        return;
    }
    if (g == Vistas::Gabarito::RadioButton)
    {
        BolinhaAero(dc, Limites, IsChecked(), sobre, apertado, ligado);
        return;
    }
    if (g == Vistas::Gabarito::Nenhum) g = Vistas::Gabarito::ToggleButton;
    DesenhaGabarito(dc, Limites, Tema::Modelo(g), sobre, apertado, IsChecked(), ligado, Background, BorderBrush);
}

void RadioButton::OnClick()
{
    // clicar no marcado nao desmarca
    if (!IsChecked()) IsChecked = true;
    ButtonBase::OnClick();
}

void RadioButton::AoMarcar(bool marcado)
{
    if (marcado)
    {
        // desmarca os outros do grupo: mesmo GroupName na mesma janela, ou (sem nome) os irmaos
        std::vector<UIElement *> pilha;
        UIElement *raiz = nullptr;
        if (GroupName.empty()) raiz = Parent;
        else
        {
            raiz = this;
            while (raiz->Parent) raiz = raiz->Parent;
        }
        if (raiz) pilha.push_back(raiz);
        while (!pilha.empty())
        {
            UIElement *e = pilha.back();
            pilha.pop_back();
            if (auto *r = dynamic_cast<RadioButton *>(e))
                if (r != this && r->IsChecked() && r->GroupName == GroupName && (!GroupName.empty() || r->Parent == Parent))
                    r->IsChecked = false;
            if (!GroupName.empty() || e == raiz) e->Filhos(pilha);
        }
    }
    ToggleButton::AoMarcar(marcado);
}

// =====================================================================================
// TextBox
// =====================================================================================
TextBox::TextBox()
    : Text(std::string(), [this](const std::string &antigo, const std::string &) {
          _desfazer.push_back(antigo);
          if (_desfazer.size() > 100) _desfazer.erase(_desfazer.begin());
          int n = CaracteresEm(Text());
          _cursor = std::min(_cursor, n);
          _ancora = std::min(_ancora, n);
          _xs.clear();
          TextChangedEventArgs a;
          a.Source = a.OriginalSource = this;
          TextChanged(a);
          InvalidateVisual();
      })
{
    VerticalContentAlignment = Vistas::VerticalAlignment::Center;
    Cursor = Vistas::Cursor::IBeam;
}

int TextBox::CaretIndex() const { return _cursor; }
void TextBox::SetCaretIndex(int i)
{
    _cursor = _ancora = std::clamp(i, 0, CaracteresEm(Text()));
    InvalidateVisual();
}
int TextBox::SelectionStart() const { return std::min(_cursor, _ancora); }
int TextBox::SelectionLength() const { return std::abs(_cursor - _ancora); }
std::string TextBox::SelectedText() const
{
    size_t a = ByteDoCaractere(Text(), SelectionStart()), b = ByteDoCaractere(Text(), SelectionStart() + SelectionLength());
    return Text().substr(a, b - a);
}
void TextBox::Select(int inicio, int n)
{
    int t = CaracteresEm(Text());
    _ancora = std::clamp(inicio, 0, t);
    _cursor = std::clamp(inicio + n, 0, t);
    InvalidateVisual();
}
void TextBox::SelectAll() { Select(0, CaracteresEm(Text())); }

void TextBox::Copy() const
{
    if (SelectionLength() > 0) Clipboard::SetText(SelectedText());
}

void TextBox::Cut()
{
    if (IsReadOnly || SelectionLength() == 0) return;
    Copy();
    ApagaSelecao();
}

void TextBox::Paste()
{
    if (IsReadOnly) return;
    std::string s = Clipboard::GetText();
    // uma linha so: as quebras viram nada (o TextBox do WPF sem AcceptsReturn corta na primeira)
    size_t q = s.find_first_of("\r\n");
    if (q != std::string::npos) s = s.substr(0, q);
    Insere(s);
}

bool TextBox::Undo()
{
    if (_desfazer.empty() || IsReadOnly) return false;
    std::string v = _desfazer.back();
    _desfazer.pop_back();
    size_t antes = _desfazer.size();
    Text = v;
    _desfazer.resize(antes);    // desfazer nao entra na pilha
    _cursor = _ancora = CaracteresEm(v);
    return true;
}

void TextBox::Insere(const std::string &s)
{
    if (IsReadOnly) return;
    std::string t = Text();
    size_t a = ByteDoCaractere(t, SelectionStart()), b = ByteDoCaractere(t, SelectionStart() + SelectionLength());
    std::string novo = s;
    if (MaxLength > 0)
    {
        int livre = MaxLength - (CaracteresEm(t) - SelectionLength());
        if (livre <= 0) return;
        if (CaracteresEm(novo) > livre) novo = novo.substr(0, ByteDoCaractere(novo, livre));
    }
    int pos = SelectionStart() + CaracteresEm(novo);
    t = t.substr(0, a) + novo + t.substr(b);
    _cursor = _ancora = pos;
    Text = t;
    _cursor = _ancora = pos;
    Mostra();
}

void TextBox::ApagaSelecao()
{
    if (SelectionLength() == 0) return;
    Insere("");
}

void TextBox::Posicoes()
{
    if (!_xs.empty()) return;
    Vistas::Texto::Posicoes(Fonte(), Text(), _xs, _bytes);
}

int TextBox::IndiceEm(double x) const
{
    const_cast<TextBox *>(this)->Posicoes();
    Thickness m = Soma(BorderThicknessEfetiva(), PaddingEfetivo());
    double rel = x - (Limites.X + m.Left) + _rolagem;
    int melhor = 0;
    double dist = 1e9;
    for (size_t i = 0; i < _xs.size(); i++)
    {
        double dd = std::fabs(_xs[i] - rel);
        if (dd < dist) dist = dd, melhor = (int) i;
    }
    // _xs tem uma posicao por caractere mais o fim
    return std::min(melhor, CaracteresEm(Text()));
}

void TextBox::Mostra()
{
    Posicoes();
    Thickness m = Soma(BorderThicknessEfetiva(), PaddingEfetivo());
    double larg = std::max(1.0, Limites.Width - m.Horizontal() - 2);
    double x = _cursor < (int) _xs.size() ? _xs[_cursor] : (_xs.empty() ? 0 : _xs.back());
    if (x - _rolagem > larg) _rolagem = x - larg;
    if (x - _rolagem < 0) _rolagem = x;
    double total = _xs.empty() ? 0 : _xs.back();
    if (total - _rolagem < larg) _rolagem = std::max(0.0, total - larg);
    if (Window *j = Janela()) j->ReiniciaPisca();
}

Point TextBox::PontoDoCursor() const
{
    const_cast<TextBox *>(this)->Posicoes();
    Thickness m = Soma(BorderThicknessEfetiva(), PaddingEfetivo());
    double x = _cursor < (int) _xs.size() ? _xs[_cursor] : 0;
    return Point(Limites.X + m.Left + x - _rolagem, Limites.Y + Limites.Height - m.Bottom);
}

Size TextBox::MeasureOverride(Size d)
{
    (void) d;
    Thickness m = Soma(BorderThicknessEfetiva(), PaddingEfetivo());
    FonteTexto f = Fonte();
    // o TextBox nao cresce com o texto quando esticado; sozinho, pede o texto mais o cursor
    double w = Vistas::Texto::Largura(f, Text()) + 2;
    return Size(w + m.Horizontal(), Vistas::Texto::AlturaLinha(f) + m.Vertical());
}

Size TextBox::ArrangeOverride(Size f)
{
    _xs.clear();
    return f;
}

void TextBox::OnRender(DrawingContext &dc)
{
    Brush fundo = BackgroundEfetivo(), borda = BorderBrushEfetivo();
    Thickness esp = BorderThicknessEfetiva();
    dc.Retangulo(Limites, fundo, borda, esp);
    // LINUX: o TextBox do Aero (o Tema nao troca o gabarito, o ListBoxChrome) acende a borda em azul
    // com o mouse por cima e com o foco. Fica igual ao original.
    if (IsEnabledEfetivo() && esp.Left > 0 && (IsKeyboardFocused() || IsMouseOver()))
    {
        bool foco = IsKeyboardFocused();
        Brush aero = Brush::Linear(Point(0, 0), Point(0, 1), {{Color::De(foco ? "#FF3D7BAD" : "#FF5794BF"), 0.05},
                                                              {Color::De(foco ? "#FFA4C9E3" : "#FFB7D5EA"), 0.07},
                                                              {Color::De(foco ? "#FFB7D9ED" : "#FFC7E2F1"), 1}});
        dc.Retangulo(Limites, Brush(), aero, esp);
    }
    Posicoes();
    Thickness m = Soma(BorderThicknessEfetiva(), PaddingEfetivo());
    FonteTexto f = Fonte();
    Rect in(Limites.X + m.Left, Limites.Y + m.Top, std::max(0.0, Limites.Width - m.Horizontal()), std::max(0.0, Limites.Height - m.Vertical()));
    double lh = Vistas::Texto::AlturaLinha(f);
    double y = in.Y + (in.Height - lh) / 2;
    double x0 = in.X - _rolagem;
    if (TextAlignment != Vistas::TextAlignment::Left && !_xs.empty() && _xs.back() < in.Width)
        x0 = in.X + (TextAlignment == Vistas::TextAlignment::Center ? (in.Width - _xs.back()) / 2 : in.Width - _xs.back() - 2);
    dc.Recorta(Rect(in.X, Limites.Y + esp.Top, in.Width, Limites.Height - esp.Vertical()));
    Brush cor = ForegroundEfetivo();
    if (!IsEnabledEfetivo()) cor = Brush(Tema::SistemaTextoCinza);
    dc.Texto(Text(), f, x0, y, cor);
    // a selecao por cima, translucida (SelectionOpacity 0,4), como no WPF 4
    if (SelectionLength() > 0 && (IsKeyboardFocused() || true))
    {
        int a = SelectionStart(), b = a + SelectionLength();
        double xa = a < (int) _xs.size() ? _xs[a] : 0, xb = b < (int) _xs.size() ? _xs[b] : (_xs.empty() ? 0 : _xs.back());
        Brush sel = SelectionBrush;
        if (sel.Nulo())
        {
            const Vistas::Style *s = EstiloEfetivo();
            sel = s && s->SelectionBrush ? *s->SelectionBrush : Brush(Tema::SistemaRealce);
        }
        sel.Opacity = SelectionOpacity;
        if (IsKeyboardFocused()) dc.Retangulo(Rect(x0 + xa, y, xb - xa, lh), sel);
    }
    // o cursor de texto
    Window *j = Janela();
    if (IsKeyboardFocused() && j && j->CursorAceso() && !IsReadOnly)
    {
        Brush c = CaretBrush;
        if (c.Nulo())
        {
            const Vistas::Style *s = EstiloEfetivo();
            c = s && s->CaretBrush ? *s->CaretBrush : cor;
        }
        double x = _cursor < (int) _xs.size() ? _xs[_cursor] : 0;
        double cx = Arred(x0 + x);
        dc.Retangulo(Rect(cx, Arred(y), 1, lh), c);
    }
    dc.Restaura();
}

void TextBox::OnMouseDown(MouseButtonEventArgs &e)
{
    if (e.ChangedButton != MouseButton::Left) return;
    Focus();
    e.Handled = true;
    int i = IndiceEm(e.Posicao.X);
    if (e.ClickCount == 2)
    {
        // palavra inteira
        std::string t = Text();
        int n = CaracteresEm(t);
        int a = i, b = i;
        auto letra = [&](int k) {
            size_t p = ByteDoCaractere(t, k);
            unsigned char c = p < t.size() ? (unsigned char) t[p] : ' ';
            return c >= 0x80 || isalnum(c);
        };
        while (a > 0 && letra(a - 1)) a--;
        while (b < n && letra(b)) b++;
        Select(a, b - a);
        return;
    }
    if (e.ClickCount >= 3)
    {
        SelectAll();
        return;
    }
    if (Tem(Keyboard::Modifiers(), ModifierKeys::Shift)) _cursor = i;
    else _cursor = _ancora = i;
    _arrastando = true;
    CaptureMouse();
    Mostra();
}

void TextBox::OnMouseMove(MouseEventArgs &e)
{
    if (!_arrastando) return;
    _cursor = IndiceEm(e.Posicao.X);
    Mostra();
}

void TextBox::OnMouseUp(MouseButtonEventArgs &e)
{
    if (e.ChangedButton != MouseButton::Left) return;
    _arrastando = false;
    ReleaseMouseCapture();
}

void TextBox::OnKeyDown(KeyEventArgs &e)
{
    bool ctrl = Tem(e.Modificadores, ModifierKeys::Control), shift = Tem(e.Modificadores, ModifierKeys::Shift);
    std::string t = Text();
    int n = CaracteresEm(t);
    auto letra = [&](int k) {
        size_t p = ByteDoCaractere(t, k);
        unsigned char c = p < t.size() ? (unsigned char) t[p] : ' ';
        return c >= 0x80 || isalnum(c);
    };
    auto palavraAntes = [&](int i) {
        while (i > 0 && !letra(i - 1)) i--;
        while (i > 0 && letra(i - 1)) i--;
        return i;
    };
    auto palavraDepois = [&](int i) {
        while (i < n && letra(i)) i++;
        while (i < n && !letra(i)) i++;
        return i;
    };
    auto move = [&](int para) {
        _cursor = std::clamp(para, 0, n);
        if (!shift) _ancora = _cursor;
        Mostra();
        InvalidateVisual();
        e.Handled = true;
    };
    switch (e.Key)
    {
    case Key::Left:
        if (!shift && SelectionLength() > 0) move(SelectionStart());
        else move(ctrl ? palavraAntes(_cursor) : _cursor - 1);
        return;
    case Key::Right:
        if (!shift && SelectionLength() > 0) move(SelectionStart() + SelectionLength());
        else move(ctrl ? palavraDepois(_cursor) : _cursor + 1);
        return;
    case Key::Home: move(0); return;
    case Key::End: move(n); return;
    case Key::Back:
        e.Handled = true;
        if (IsReadOnly) return;
        if (SelectionLength() == 0 && _cursor > 0) _ancora = ctrl ? palavraAntes(_cursor) : _cursor - 1;
        ApagaSelecao();
        return;
    case Key::Delete:
        e.Handled = true;
        if (IsReadOnly) return;
        if (shift) { Cut(); return; }
        if (SelectionLength() == 0 && _cursor < n) _ancora = ctrl ? palavraDepois(_cursor) : _cursor + 1;
        ApagaSelecao();
        return;
    case Key::Insert:
        if (ctrl) Copy(), e.Handled = true;
        else if (shift) Paste(), e.Handled = true;
        return;
    default: break;
    }
    if (ctrl)
    {
        switch (e.Key)
        {
        case Key::A: SelectAll(); e.Handled = true; return;
        case Key::C: Copy(); e.Handled = true; return;
        case Key::X: Cut(); e.Handled = true; return;
        case Key::V: Paste(); e.Handled = true; return;
        case Key::Z: Undo(); e.Handled = true; return;
        default: break;
        }
    }
}

void TextBox::OnTextInput(TextCompositionEventArgs &e)
{
    if (IsReadOnly) return;
    e.Handled = true;
    Insere(e.Text);
}

void TextBox::OnGotFocus()
{
    if (Window *j = Janela()) j->ReiniciaPisca();
}

void TextBox::OnLostFocus() { InvalidateVisual(); }

Vistas::Cursor TextBox::CursorEm(Point p) const
{
    (void) p;
    return Cursor == Vistas::Cursor::Herda ? Vistas::Cursor::IBeam : Cursor;
}

// =====================================================================================
// Barra de rolagem
// =====================================================================================
Rect BarraDeRolagem::Polegar() const
{
    bool v = Orientacao == Vistas::Orientation::Vertical;
    double total = v ? Area.Height : Area.Width;
    double ext = Maximo + Visivel;
    if (ext <= 0 || Maximo <= 0) return Rect();
    double tam = std::max(Tema::BarraPolegarMinimo, total * Visivel / ext);
    tam = std::min(tam, total);
    double pos = (total - tam) * std::clamp(Valor / Maximo, 0.0, 1.0);
    return v ? Rect(Area.X, Area.Y + pos, Area.Width, tam) : Rect(Area.X + pos, Area.Y, tam, Area.Height);
}

void BarraDeRolagem::Desenha(DrawingContext &dc) const
{
    // <Border Background="{TemplateBinding Background}"> com o polegar de cantos 3 e margem 2
    dc.Retangulo(Area, Tema::Recurso("ChapaFunda"));
    Rect p = Polegar();
    if (p.IsEmpty()) return;
    double m = Tema::BarraPolegarMargem;
    Rect in(p.X + m, p.Y + m, std::max(0.0, p.Width - 2 * m), std::max(0.0, p.Height - 2 * m));
    Color c = Arrastando ? Tema::BarraPolegarArrasto : Sobre ? Tema::BarraPolegarSobre : Tema::BarraPolegar;
    dc.Retangulo(in, Brush(c), Brush(), Thickness(), Vistas::CornerRadius(Tema::BarraPolegarCanto));
}

bool BarraDeRolagem::Aperta(Point p)
{
    if (!Area.Contains(p) || Maximo <= 0) return false;
    Rect t = Polegar();
    bool v = Orientacao == Vistas::Orientation::Vertical;
    if (t.Contains(p))
    {
        Arrastando = true;
        _inicioArrasto = v ? p.Y : p.X;
        _valorArrasto = Valor;
        return true;
    }
    // no trilho: uma pagina para o lado do clique (PageUp/PageDown)
    bool antes = v ? p.Y < t.Y : p.X < t.X;
    Valor = std::clamp(Valor + (antes ? -Visivel : Visivel), 0.0, Maximo);
    _repetindo = antes ? -1 : 1;
    return true;
}

void BarraDeRolagem::Move(Point p)
{
    Sobre = Polegar().Contains(p);
    if (!Arrastando) return;
    bool v = Orientacao == Vistas::Orientation::Vertical;
    Rect t = Polegar();
    double total = v ? Area.Height : Area.Width, tam = v ? t.Height : t.Width;
    double livre = std::max(1.0, total - tam);
    double d = (v ? p.Y : p.X) - _inicioArrasto;
    Valor = std::clamp(_valorArrasto + d / livre * Maximo, 0.0, Maximo);
}

void BarraDeRolagem::Solta()
{
    Arrastando = false;
    _repetindo = 0;
}

// =====================================================================================
// ScrollViewer
// =====================================================================================
ScrollViewer::ScrollViewer() : Content(this) {}

ScrollViewer::~ScrollViewer()
{
    if (auto e = Content.Elemento())
        if (e->Parent == this) e->Parent = nullptr;
}

void ScrollViewer::Filhos(std::vector<UIElement *> &s) const
{
    if (auto e = Content.Elemento()) s.push_back(e.get());
}

Size ScrollViewer::MeasureOverride(Size d)
{
    auto e = Content.Elemento();
    bool podeV = VerticalScrollBarVisibility != Vistas::ScrollBarVisibility::Disabled;
    bool podeH = HorizontalScrollBarVisibility != Vistas::ScrollBarVisibility::Disabled;
    _mostraV = VerticalScrollBarVisibility == Vistas::ScrollBarVisibility::Visible;
    _mostraH = HorizontalScrollBarVisibility == Vistas::ScrollBarVisibility::Visible;
    double b = Tema::BarraLargura;
    for (int volta = 0; volta < 3; volta++)
    {
        double w = d.Width - (_mostraV ? b : 0), h = d.Height - (_mostraH ? b : 0);
        if (e) e->Measure(Size(podeH ? Infinito : std::max(0.0, w), podeV ? Infinito : std::max(0.0, h)));
        Size ext = e ? e->DesiredSize : Size();
        bool v = _mostraV || (VerticalScrollBarVisibility == Vistas::ScrollBarVisibility::Auto && Finito(d.Height) && ext.Height > h + 0.5);
        bool hh = _mostraH || (HorizontalScrollBarVisibility == Vistas::ScrollBarVisibility::Auto && Finito(d.Width) && ext.Width > w + 0.5);
        if (v == _mostraV && hh == _mostraH) break;
        _mostraV = v;
        _mostraH = hh;
    }
    Size ext = e ? e->DesiredSize : Size();
    return Size(std::min(ext.Width + (_mostraV ? b : 0), d.Width), std::min(ext.Height + (_mostraH ? b : 0), d.Height));
}

void ScrollViewer::Limita()
{
    _v = std::clamp(_v, 0.0, ScrollableHeight());
    _h = std::clamp(_h, 0.0, ScrollableWidth());
}

Size ScrollViewer::ArrangeOverride(Size f)
{
    auto e = Content.Elemento();
    double b = Tema::BarraLargura;
    _janela = Size(std::max(0.0, f.Width - (_mostraV ? b : 0)), std::max(0.0, f.Height - (_mostraH ? b : 0)));
    bool podeV = VerticalScrollBarVisibility != Vistas::ScrollBarVisibility::Disabled;
    bool podeH = HorizontalScrollBarVisibility != Vistas::ScrollBarVisibility::Disabled;
    if (e)
    {
        // a extensao: o desejado do conteudo medido sem limite na direcao que rola
        Size ext = e->DesiredSize;
        _extensao = Size(podeH ? std::max(ext.Width, _janela.Width) : _janela.Width, podeV ? std::max(ext.Height, _janela.Height) : _janela.Height);
        if (_vAdiado)
        {
            _v = *_vAdiado;
            _vAdiado.reset();
        }
        Limita();
        e->Arrange(Rect(Limites.X - _h, Limites.Y - _v, _extensao.Width, _extensao.Height));
    }
    else
        _extensao = _janela;
    _barraV.Area = Rect(Limites.X + _janela.Width, Limites.Y, b, _janela.Height);
    _barraV.Maximo = ScrollableHeight();
    _barraV.Visivel = _janela.Height;
    _barraV.Valor = _v;
    _barraH.Area = Rect(Limites.X, Limites.Y + _janela.Height, _janela.Width, b);
    _barraH.Maximo = ScrollableWidth();
    _barraH.Visivel = _janela.Width;
    _barraH.Valor = _h;
    Avisa();
    return f;
}

void ScrollViewer::Avisa()
{
    if (_v == _vAvisado && _h == _hAvisado && _extAvisada == _extensao.Height && _janAvisada == _janela.Height) return;
    ScrollChangedEventArgs a;
    a.Source = a.OriginalSource = this;
    a.VerticalOffset = _v;
    a.VerticalChange = _v - _vAvisado;
    a.HorizontalOffset = _h;
    a.HorizontalChange = _h - _hAvisado;
    a.ExtentHeight = _extensao.Height;
    a.ExtentWidth = _extensao.Width;
    a.ViewportHeight = _janela.Height;
    a.ViewportWidth = _janela.Width;
    a.ExtentHeightChange = _extAvisada < 0 ? 0 : _extensao.Height - _extAvisada;
    a.ViewportHeightChange = _janAvisada < 0 ? 0 : _janela.Height - _janAvisada;
    _vAvisado = _v;
    _hAvisado = _h;
    _extAvisada = _extensao.Height;
    _janAvisada = _janela.Height;
    ScrollChanged(a);
}

void ScrollViewer::ScrollToVerticalOffset(double v)
{
    // No WPF o pedido vale no proximo arranjo, ja com a extensao nova: o ScrollToEnd logo depois de
    // acrescentar uma linha (o TxtRegistro da AQUISICAO) chega a linha nova. Aqui o que passa do fim
    // de agora fica guardado ate o ArrangeOverride; o resto vale na hora, como antes.
    _vAdiado = v > ScrollableHeight() ? std::optional<double>(v) : std::nullopt;
    _v = v;
    Limita();
    InvalidateVisual();
}

void ScrollViewer::ScrollToHorizontalOffset(double h)
{
    _h = h;
    Limita();
    InvalidateVisual();
}

void ScrollViewer::MostraRetangulo(const Rect &r)
{
    double topo = Limites.Y, base = Limites.Y + _janela.Height;
    if (r.Y < topo) ScrollToVerticalOffset(_v - (topo - r.Y));
    else if (r.Bottom() > base) ScrollToVerticalOffset(_v + std::min(r.Bottom() - base, r.Y - topo));
}

void ScrollViewer::OnRender(DrawingContext &dc)
{
    if (!Background.Nulo()) dc.Retangulo(Limites, Background);
    dc.Recorta(Rect(Limites.X, Limites.Y, _janela.Width, _janela.Height));
}

void ScrollViewer::OnRenderDepois(DrawingContext &dc)
{
    dc.Restaura();
    if (_mostraV) _barraV.Desenha(dc);
    if (_mostraH) _barraH.Desenha(dc);
    // a quina entre as duas barras: a cor de controle do sistema
    if (_mostraV && _mostraH)
        dc.Retangulo(Rect(Limites.X + _janela.Width, Limites.Y + _janela.Height, Tema::BarraLargura, Tema::BarraLargura),
                     Brush(Tema::SistemaControle));
}

bool ScrollViewer::AcertaProprio(Point p) const
{
    return !Background.Nulo() || (_mostraV && _barraV.Area.Contains(p)) || (_mostraH && _barraH.Area.Contains(p));
}

void ScrollViewer::OnMouseDown(MouseButtonEventArgs &e)
{
    if (e.ChangedButton != MouseButton::Left) return;
    if (_mostraV && _barraV.Aperta(e.Posicao))
    {
        ScrollToVerticalOffset(_barraV.Valor);
        CaptureMouse();
        e.Handled = true;
    }
    else if (_mostraH && _barraH.Aperta(e.Posicao))
    {
        ScrollToHorizontalOffset(_barraH.Valor);
        CaptureMouse();
        e.Handled = true;
    }
}

void ScrollViewer::OnMouseMove(MouseEventArgs &e)
{
    bool antes = _barraV.Sobre || _barraH.Sobre;
    _barraV.Move(e.Posicao);
    _barraH.Move(e.Posicao);
    if (_barraV.Arrastando) ScrollToVerticalOffset(_barraV.Valor);
    if (_barraH.Arrastando) ScrollToHorizontalOffset(_barraH.Valor);
    if (antes != (_barraV.Sobre || _barraH.Sobre)) InvalidateVisual();
}

void ScrollViewer::OnMouseUp(MouseButtonEventArgs &e)
{
    (void) e;
    if (_barraV.Arrastando || _barraH.Arrastando || _barraV._repetindo || _barraH._repetindo)
    {
        _barraV.Solta();
        _barraH.Solta();
        ReleaseMouseCapture();
        InvalidateVisual();
    }
}

void ScrollViewer::OnMouseLeave(MouseEventArgs &e)
{
    (void) e;
    if (_barraV.Sobre || _barraH.Sobre)
    {
        _barraV.Sobre = _barraH.Sobre = false;
        InvalidateVisual();
    }
}

void ScrollViewer::OnMouseWheel(MouseWheelEventArgs &e)
{
    if (e.Handled) return;
    // tres linhas de 16 px por degrau (SystemParameters.WheelScrollLines)
    if (VerticalScrollBarVisibility != Vistas::ScrollBarVisibility::Disabled && ScrollableHeight() > 0)
    {
        double antes = _v;
        ScrollToVerticalOffset(_v - e.Delta / 120.0 * 48);
        if (_v != antes) e.Handled = true;
    }
    else if (HorizontalScrollBarVisibility != Vistas::ScrollBarVisibility::Disabled && ScrollableWidth() > 0)
    {
        ScrollToHorizontalOffset(_h - e.Delta / 120.0 * 48);
        e.Handled = true;
    }
}

void ScrollViewer::OnKeyDown(KeyEventArgs &e)
{
    if (e.Handled) return;
    switch (e.Key)
    {
    case Key::PageUp: PageUp(); e.Handled = true; break;
    case Key::PageDown: PageDown(); e.Handled = true; break;
    case Key::Up: LineUp(); e.Handled = true; break;
    case Key::Down: LineDown(); e.Handled = true; break;
    default: break;
    }
}

// =====================================================================================
// Slider e ProgressBar
// =====================================================================================
Slider::Slider()
    : Value(0, [this](const double &a, const double &n) {
          RoutedPropertyChangedEventArgs<double> e;
          e.Source = e.OriginalSource = this;
          e.OldValue = a;
          e.NewValue = n;
          ValueChanged(e);
          InvalidateVisual();
      })
{
    Maximum = 10;
    SmallChange = 0.1;
    LargeChange = 1;
}

Size Slider::MeasureOverride(Size d)
{
    (void) d;
    // <Grid VerticalAlignment="Center" Height="16">
    return Orientation == Vistas::Orientation::Horizontal ? Size(Tema::SliderPolegar, Tema::SliderAltura)
                                                          : Size(Tema::SliderAltura, Tema::SliderPolegar);
}

Size Slider::ArrangeOverride(Size f) { return f; }

Rect Slider::Polegar() const
{
    double t = Tema::SliderPolegar, a = Tema::SliderAltura;
    double faixa = Maximum - Minimum;
    double r = faixa > 0 ? std::clamp((Value() - Minimum) / faixa, 0.0, 1.0) : 0;
    double cy = Limites.Y + Limites.Height / 2;
    (void) a;
    double x = Limites.X + (Limites.Width - t) * r;
    return Rect(Arred(x), Arred(cy - t / 2), t, t);
}

double Slider::ValorEm(double x) const
{
    double t = Tema::SliderPolegar;
    double livre = std::max(1.0, Limites.Width - t);
    double r = std::clamp((x - Limites.X - t / 2) / livre, 0.0, 1.0);
    return Minimum + r * (Maximum - Minimum);
}

void Slider::OnRender(DrawingContext &dc)
{
    // o trilho de 3 px (ProgressoRestante), a parte tocada (ProgressoTocado) e o polegar de 10 px
    double cy = Limites.Y + Limites.Height / 2;
    double tr = Tema::SliderTrilho;
    Rect trilho(Limites.X, Arred(cy - tr / 2), Limites.Width, tr);
    const Vistas::Style *s = Tema::EstiloPadrao("Slider");
    Brush resto = !Background.Nulo() ? Background : (s && s->Background ? *s->Background : Tema::Recurso("ProgressoRestante"));
    Brush tocado = !Foreground.Nulo() ? Foreground : (s && s->Foreground ? *s->Foreground : Tema::Recurso("ProgressoTocado"));
    dc.Retangulo(trilho, resto, Brush(), Thickness(), Vistas::CornerRadius(1));
    Rect p = Polegar();
    if (p.X > Limites.X) dc.Retangulo(Rect(Limites.X, trilho.Y, p.X - Limites.X, tr), tocado, Brush(), Thickness(), Vistas::CornerRadius(1));
    dc.Elipse(p, Brush(Tema::SliderPolegarFundo), Brush(Tema::SliderPolegarBorda), 1);
}

void Slider::OnMouseDown(MouseButtonEventArgs &e)
{
    if (e.ChangedButton != MouseButton::Left) return;
    Focus();
    e.Handled = true;
    Rect p = Polegar();
    if (p.Contains(e.Posicao))
    {
        _arrastando = true;
        _desvio = e.Posicao.X - (p.X + p.Width / 2);
        CaptureMouse();
        return;
    }
    if (IsMoveToPointEnabled)
    {
        // IsMoveToPointEnabled: o clique leva ao ponto, e o polegar ja vem preso ao mouse
        Value = ValorEm(e.Posicao.X);
        _arrastando = true;
        _desvio = 0;
        CaptureMouse();
    }
    else
        Value = std::clamp(Value() + (e.Posicao.X < p.X ? -LargeChange : LargeChange), Minimum, Maximum);
}

void Slider::OnMouseMove(MouseEventArgs &e)
{
    if (_arrastando) Value = ValorEm(e.Posicao.X - _desvio);
}

void Slider::OnMouseUp(MouseButtonEventArgs &e)
{
    if (e.ChangedButton != MouseButton::Left) return;
    // o ReleaseMouseCapture fica para depois do PreviewMouseLeftButtonUp das vistas (AoSoltarPosicao
    // le IsMouseCaptureWithin): aqui ja e a subida, o Preview ja passou
    _arrastando = false;
    ReleaseMouseCapture();
}

void Slider::OnKeyDown(KeyEventArgs &e)
{
    switch (e.Key)
    {
    case Key::Left: case Key::Down: Value = std::clamp(Value() - SmallChange, Minimum, Maximum); e.Handled = true; break;
    case Key::Right: case Key::Up: Value = std::clamp(Value() + SmallChange, Minimum, Maximum); e.Handled = true; break;
    case Key::PageDown: Value = std::clamp(Value() - LargeChange, Minimum, Maximum); e.Handled = true; break;
    case Key::PageUp: Value = std::clamp(Value() + LargeChange, Minimum, Maximum); e.Handled = true; break;
    case Key::Home: Value = Minimum; e.Handled = true; break;
    case Key::End: Value = Maximum; e.Handled = true; break;
    default: break;
    }
}

Size ProgressBar::MeasureOverride(Size d)
{
    (void) d;
    return Size(0, 0);
}

void ProgressBar::OnRender(DrawingContext &dc)
{
    // o gabarito da AquisicaoView: Border de cantos 2 recortado, e o indicador em Foreground
    Thickness esp = BorderThickness ? *BorderThickness : Thickness(1);
    Brush fundo = BackgroundEfetivo(), borda = BorderBrushEfetivo();
    Brush ind = Foreground.Nulo() ? Brush(Color::De("#FF01D328")) : Foreground;
    dc.Retangulo(Limites, fundo, borda, esp, Vistas::CornerRadius(2));
    Rect in(Limites.X + esp.Left, Limites.Y + esp.Top, std::max(0.0, Limites.Width - esp.Horizontal()), std::max(0.0, Limites.Height - esp.Vertical()));
    double faixa = Maximum - Minimum;
    double r = IsIndeterminate ? 0.25 : faixa > 0 ? std::clamp((Value - Minimum) / faixa, 0.0, 1.0) : 0;
    double x = in.X;
    if (IsIndeterminate)
    {
        double t = std::fmod(Agora(), 2.0) / 2.0;
        x = in.X + (in.Width * 1.25) * t - in.Width * 0.25;
        InvalidateVisual();
    }
    Rect i(x, in.Y, in.Width * r, in.Height);
    dc.Recorta(in);
    if (i.Width > 0) dc.Retangulo(i, ind, Brush(), Thickness(), Vistas::CornerRadius(2));
    dc.Restaura();
}

} // namespace ARCA::Vistas

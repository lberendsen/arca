// ARCA no xOS - Modelo/EspectrogramaCache (de Modelo\EspectrogramaCache.cs).
#include "EspectrogramaCache.h"

#include "Base/Arquivo.h"
#include "Interop/LeitorAudio.h"
#include "Modelo/OndaCache.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace ARCA::Modelo {

namespace {

constexpr int FftTam = EspectrogramaCache::FftTam;
constexpr int Bins = EspectrogramaCache::Bins;

std::vector<double> MontarJanela()
{
    std::vector<double> j(FftTam);
    for (int i = 0; i < FftTam; i++)
        j[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (FftTam - 1)));
    return j;
}

void Transformar(std::vector<double> &re, std::vector<double> &im)
{
    int n = (int) re.size();
    for (int i = 1, j = 0; i < n; i++)
    {
        int bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j)
        {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int tam = 2; tam <= n; tam <<= 1)
    {
        double ang = -2.0 * M_PI / tam;
        double wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += tam)
        {
            double cr = 1.0, ci = 0.0;
            for (int k = 0; k < tam / 2; k++)
            {
                int a = i + k, b = i + k + tam / 2;
                double xr = re[b] * cr - im[b] * ci;
                double xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr; im[a] += xi;
                double nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

/// <summary>Uma coluna: janela + FFT + magnitude em dB quantizada.</summary>
std::vector<uint8_t> Quadro(const std::vector<float> &ring, int rp, const std::vector<double> &jan,
                            std::vector<double> &re, std::vector<double> &im, double dbMin)
{
    int n = FftTam;
    for (int i = 0; i < n; i++)
    {
        re[i] = ring[(rp + i) % n] * jan[i];   // do mais antigo ao mais novo
        im[i] = 0;
    }
    Transformar(re, im);

    // Ganho coerente da janela (ver Fft): por n, o espectrograma ficava 6 dB abaixo.
    double somaJanela = 0;
    for (int i = 0; i < n; i++) somaJanela += jan[i];
    std::vector<uint8_t> col(Bins);
    for (int b = 0; b < Bins; b++)
    {
        double mag = std::sqrt(re[b] * re[b] + im[b] * im[b]) * 2.0 / somaJanela;
        double db = mag <= 1e-7 ? -140.0 : 20.0 * std::log10(mag);
        int qv = (int) ((db - dbMin) / (0.0 - dbMin) * 255.0);
        if (qv < 0) qv = 0;
        if (qv > 255) qv = 255;
        col[b] = (uint8_t) qv;
    }
    return col;
}

/// <summary>Mescla colunas duas a duas pelo máximo, cortando a resolução pela metade.</summary>
std::vector<std::vector<uint8_t>> Reduzir(std::vector<std::vector<uint8_t>> &cols)
{
    std::vector<std::vector<uint8_t>> novo;
    novo.reserve(cols.size() / 2 + 1);
    size_t i = 0;
    for (; i + 1 < cols.size(); i += 2)
    {
        std::vector<uint8_t> &a = cols[i], &b = cols[i + 1];
        for (int k = 0; k < Bins; k++) if (b[k] > a[k]) a[k] = b[k];
        novo.push_back(std::move(a));
    }
    if (i < cols.size()) novo.push_back(std::move(cols[i]));
    return novo;
}

template <class T> void Escreve(std::ofstream &w, T v) { w.write(reinterpret_cast<const char *>(&v), sizeof v); }
template <class T> T Le(std::ifstream &r)
{
    T v{};
    r.read(reinterpret_cast<char *>(&v), sizeof v);
    if (!r) throw std::runtime_error("fim do arquivo");
    return v;
}

} // namespace

double EspectrogramaCache::DbDeByte(uint8_t v)
{
    return DbMin + (v / 255.0) * (0.0 - DbMin);
}

double EspectrogramaCache::SegundosDeColuna(double coluna) const
{
    return Taxa > 0 ? coluna * HopEfetivo / (double) Taxa : 0;
}

double EspectrogramaCache::ColunaDeSegundos(double seg) const
{
    return HopEfetivo > 0 ? seg * Taxa / (double) HopEfetivo : 0;
}

double EspectrogramaCache::FreqDeBin(double bin) const
{
    return bin * Taxa / (double) FftTam;
}

// ===============================================================
//  GERAÇÃO (offline, thread de fundo)
// ===============================================================
std::shared_ptr<EspectrogramaCache> EspectrogramaCache::Gerar(const std::string &arquivo,
                                                              std::function<void(double)> progresso,
                                                              std::function<bool()> cancelou, std::string &strFalha)
{
    strFalha.clear();
    Interop::LeitorAudio leitor;
    try
    {
        if (!leitor.Abrir(arquivo))
        {
            strFalha = !leitor.Falha().empty() ? leitor.Falha() : "não consegui abrir o arquivo";
            return nullptr;
        }

        int canais = leitor.Canais();
        int bits = leitor.Bits();
        double fundoEscala = bits >= 32 ? 2147483648.0 : (1 << (bits - 1));

        std::vector<double> jan = MontarJanela();
        std::vector<double> re(FftTam);
        std::vector<double> im(FftTam);
        std::vector<float> ring(FftTam);
        int rp = 0; int64_t vistas = 0; int desdeHop = 0;

        std::vector<std::vector<uint8_t>> cols;
        int mescla = 1;
        std::vector<uint8_t> pend; int pendN = 0;   // pend vazio = null

        int64_t total = 0;
        int desdeCheca = 0;

        int quadros = 0;
        std::vector<std::vector<int>> bloco;
        while (!(bloco = leitor.ProximoBlocoInteiro(quadros)).empty())
        {
            for (int q = 0; q < quadros; q++)
            {
                double soma = 0;
                for (int c = 0; c < canais; c++) soma += bloco[c][q];
                float mono = (float) (soma / (canais * fundoEscala));

                ring[rp] = mono;
                rp = (rp + 1) % FftTam;
                vistas++;
                total++;

                if (++desdeHop >= HopBase && vistas >= FftTam)
                {
                    desdeHop = 0;
                    std::vector<uint8_t> col = Quadro(ring, rp, jan, re, im, DbMin);

                    if (mescla == 1) cols.push_back(std::move(col));
                    else
                    {
                        if (pend.empty()) { pend = std::move(col); pendN = 1; }
                        else { for (int i = 0; i < Bins; i++) if (col[i] > pend[i]) pend[i] = col[i]; pendN++; }
                        if (pendN >= mescla) { cols.push_back(std::move(pend)); pend.clear(); pendN = 0; }
                    }

                    if ((int) cols.size() >= MaxColunas)
                    {
                        cols = Reduzir(cols);
                        mescla *= 2; pend.clear(); pendN = 0;
                    }
                }
            }

            desdeCheca += quadros;
            if (desdeCheca >= 200000)
            {
                desdeCheca = 0;
                if (cancelou && cancelou()) return nullptr;
                if (progresso) progresso(leitor.TempoDoUltimoBloco());
            }
        }

        if (!leitor.Terminou())
        {
            strFalha = !leitor.Falha().empty() ? leitor.Falha() : "a decodificação parou antes do fim do arquivo";
            return nullptr;
        }
        double dblFalta = leitor.FaltaNoFim(total);
        if (dblFalta > 0)
        {
            char num[32];
            snprintf(num, sizeof num, "%.1f", dblFalta);
            for (char *p = num; *p; p++)
                if (*p == '.') *p = ',';
            strFalha = std::string("o arquivo acaba ") + num + " s antes do que o cabeçalho declara (cortado?)";
            return nullptr;
        }

        if (pendN > 0 && !pend.empty()) cols.push_back(std::move(pend));
        if (cols.empty()) { strFalha = "o arquivo é curto demais para o espectrograma"; return nullptr; }

        auto eg = std::make_shared<EspectrogramaCache>();
        eg->Taxa = leitor.Taxa();
        eg->HopEfetivo = HopBase * mescla;
        eg->Colunas = (int) cols.size();
        eg->TotalAmostras = total;
        eg->_dados.resize((int64_t) cols.size() * Bins <= INT_MAX ? cols.size() * Bins : 0);
        if (eg->_dados.empty()) return nullptr;   // grande demais (não deve ocorrer com o teto)
        for (size_t c = 0; c < cols.size(); c++)
            std::copy(cols[c].begin(), cols[c].end(), eg->_dados.begin() + c * Bins);
        return eg;
    }
    catch (const std::exception &ex)
    {
        strFalha = ex.what();
        return nullptr;
    }
}

// ===============================================================
//  PERSISTÊNCIA (sidecar)
// ===============================================================
std::string EspectrogramaCache::PastaCache()
{
    std::string p = Base::Combina(Base::PastaCache(), "Espectros");
    try { if (!Base::PastaExiste(p)) Base::CriaPasta(p); } catch (...) {}
    return p;
}

std::string EspectrogramaCache::CaminhoSidecar(const std::string &arquivo)
{
    return Base::Combina(PastaCache(), CacheSidecar::Chave(arquivo) + ".espg");
}

bool EspectrogramaCache::Salvar(const std::string &arquivoOrigem) const
{
    try
    {
        int64_t tamanho, ticks;
        if (!CacheSidecar::Identidade(arquivoOrigem, tamanho, ticks)) return false;
        std::ofstream w(CaminhoSidecar(arquivoOrigem), std::ios::binary | std::ios::trunc);
        if (!w) return false;
        Escreve(w, Magica);
        Escreve(w, tamanho);
        Escreve(w, ticks);
        Escreve<int32_t>(w, Taxa); Escreve<int32_t>(w, HopEfetivo); Escreve<int32_t>(w, Colunas); Escreve(w, TotalAmostras);
        Escreve<int32_t>(w, (int32_t) _dados.size());
        w.write(reinterpret_cast<const char *>(_dados.data()), (std::streamsize) _dados.size());
        w.close();
        return (bool) w;
    }
    catch (...) { return false; }
}

std::shared_ptr<EspectrogramaCache> EspectrogramaCache::Carregar(const std::string &arquivoOrigem)
{
    try
    {
        std::string s = CaminhoSidecar(arquivoOrigem);
        if (!Base::ArquivoExiste(s)) return nullptr;
        int64_t tamanho, ticks;
        if (!CacheSidecar::Identidade(arquivoOrigem, tamanho, ticks)) return nullptr;

        std::ifstream r(s, std::ios::binary);
        if (!r) return nullptr;
        if (Le<int32_t>(r) != Magica) return nullptr;
        if (Le<int64_t>(r) != tamanho) return nullptr;
        if (Le<int64_t>(r) != ticks) return nullptr;

        auto eg = std::make_shared<EspectrogramaCache>();
        eg->Taxa = Le<int32_t>(r);
        eg->HopEfetivo = Le<int32_t>(r);
        eg->Colunas = Le<int32_t>(r);
        eg->TotalAmostras = Le<int64_t>(r);
        int n = Le<int32_t>(r);
        if (n <= 0 || n != eg->Colunas * Bins) return nullptr;
        eg->_dados.resize(n);
        r.read(reinterpret_cast<char *>(eg->_dados.data()), n);
        if (r.gcount() != n) return nullptr;
        return eg;
    }
    catch (...) { return nullptr; }
}

} // namespace ARCA::Modelo

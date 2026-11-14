// ARCA no xOS - Modelo/OndaCache (de Modelo\OndaCache.cs).
#include "OndaCache.h"

#include "Base/Arquivo.h"
#include "Base/Hash.h"
#include "Base/Texto.h"
#include "Interop/LeitorAudio.h"
#include "Modelo/PonderacaoK.h"

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>

namespace ARCA::Modelo {

namespace {

constexpr double MenosInfinito = -std::numeric_limits<double>::infinity();

double MediaAnel(const double *anel, int len, int pos, int count, int quantos)
{
    int k = std::min(quantos, count);
    if (k == 0) return 0;
    double s = 0;
    for (int i = 0; i < k; i++) { int idx = (pos - 1 - i + len) % len; s += anel[idx]; }
    return s / k;
}

double Integrado(const std::vector<double> &gate)
{
    if (gate.empty()) return MenosInfinito;
    double soma = 0; int n = 0;
    for (size_t i = 0; i < gate.size(); i++)
        if (PonderacaoK::Lufs(gate[i]) >= -70.0) { soma += gate[i]; n++; }
    if (n == 0) return MenosInfinito;
    double rel = PonderacaoK::Lufs(soma / n) - 10.0;
    soma = 0; n = 0;
    for (size_t i = 0; i < gate.size(); i++)
    {
        double l = PonderacaoK::Lufs(gate[i]);
        if (l >= -70.0 && l >= rel) { soma += gate[i]; n++; }
    }
    return n == 0 ? MenosInfinito : PonderacaoK::Lufs(soma / n);
}

double Percentil(const std::vector<double> &ordenada, double p)
{
    int n = (int) ordenada.size();
    if (n == 0) return 0;
    double idx = p / 100.0 * (n - 1);
    int i0 = (int) idx; int i1 = std::min(i0 + 1, n - 1);
    double f = idx - i0;
    return ordenada[i0] * (1 - f) + ordenada[i1] * f;
}

double Lra(const std::vector<double> &stZ)
{
    std::vector<double> a;
    for (size_t i = 0; i < stZ.size(); i++)
        if (PonderacaoK::Lufs(stZ[i]) >= -70.0) a.push_back(stZ[i]);
    if (a.size() < 2) return 0;
    double soma = 0; for (size_t i = 0; i < a.size(); i++) soma += a[i];
    double rel = PonderacaoK::Lufs(soma / a.size()) - 20.0;
    std::vector<double> l;
    for (size_t i = 0; i < a.size(); i++)
    {
        double v = PonderacaoK::Lufs(a[i]);
        if (v >= rel) l.push_back(v);
    }
    if (l.size() < 2) return 0;
    std::sort(l.begin(), l.end());
    return Percentil(l, 95) - Percentil(l, 10);
}

// BinaryWriter / BinaryReader: little-endian, bool em um byte (o x86 já é little-endian)
template <class T> void Escreve(std::ofstream &w, T v) { w.write(reinterpret_cast<const char *>(&v), sizeof v); }
template <class T> T Le(std::ifstream &r)
{
    T v{};
    r.read(reinterpret_cast<char *>(&v), sizeof v);
    if (!r) throw std::runtime_error("fim do arquivo");
    return v;
}

} // namespace

// ---- o sidecar, igual para a onda e o espectrograma ----
bool CacheSidecar::Identidade(const std::string &arquivo, int64_t &tamanho, int64_t &ticks)
{
    struct stat st;
    if (stat(arquivo.c_str(), &st) != 0) return false;
    tamanho = (int64_t) st.st_size;
    // LINUX: FileInfo.LastWriteTimeUtc.Ticks = intervalos de 100 ns desde 01/01/0001 (UTC); o
    // st_mtim do Linux conta desde 01/01/1970: soma-se a diferença (621.355.968.000.000.000).
    ticks = (int64_t) st.st_mtim.tv_sec * 10000000LL + st.st_mtim.tv_nsec / 100 + 621355968000000000LL;
    return true;
}

std::string CacheSidecar::Chave(const std::string &arquivo)
{
    // sha.ComputeHash(Encoding.Unicode.GetBytes(arquivo.ToLowerInvariant()))
    std::vector<uint8_t> utf16;
    for (uint32_t cp : Base::CodePoints(Base::Minusculas(arquivo)))
    {
        if (cp >= 0x10000)
        {
            cp -= 0x10000;
            uint16_t a = (uint16_t) (0xD800 + (cp >> 10)), b = (uint16_t) (0xDC00 + (cp & 0x3FF));
            utf16.push_back((uint8_t) a); utf16.push_back((uint8_t) (a >> 8));
            utf16.push_back((uint8_t) b); utf16.push_back((uint8_t) (b >> 8));
        }
        else
        {
            utf16.push_back((uint8_t) cp);
            utf16.push_back((uint8_t) (cp >> 8));
        }
    }
    std::vector<uint8_t> h = Base::Hash(Base::Algoritmo::Sha1, utf16.data(), utf16.size());
    return Base::Hex(h);
}

int64_t OndaCache::AmostrasPorBin(int nivel) const
{
    int64_t n = BinBase;
    for (int i = 0; i < nivel; i++) n *= Fator;
    return n;
}

// ===============================================================
// Geração (o trabalho caro, feito uma vez, fora do caminho quente)
// ===============================================================
std::shared_ptr<OndaCache> OndaCache::Gerar(const std::string &arquivo, std::function<void(double)> progresso,
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
        double limiarClip = fundoEscala - 2;   // "no limite" digital

        // Acumuladores do bin corrente, por canal.
        std::vector<float> binMin(canais), binMax(canais);
        std::vector<double> binSq(canais);
        std::vector<char> binClip(canais);
        int contador = 0;

        std::vector<std::vector<float>> lsMin(canais), lsMax(canais), lsRms(canais);
        std::vector<std::vector<bool>> lsClip(canais);
        for (int c = 0; c < canais; c++)
        {
            binMin[c] = 1.0f; binMax[c] = -1.0f;
        }

        // Loudness de arquivo (mesma passada): K-weighting por canal,
        // sub-blocos de 100 ms, gate para o integrado e curto-prazo para o LRA.
        int amostras100 = std::max(1, leitor.Taxa() / 10);
        std::vector<std::unique_ptr<PonderacaoK::Filtro>> fSh(canais), fHp(canais);
        std::vector<double> accCh(canais);
        for (int c = 0; c < canais; c++)
        {
            fSh[c] = PonderacaoK::Shelf(leitor.Taxa());
            fHp[c] = PonderacaoK::PassaAlta(leitor.Taxa());
        }
        int accN = 0, subPos = 0, subCount = 0;
        double subZ[MaxSubL] = {};
        std::vector<double> gate;
        std::vector<double> stZ;
        double picoAbs = 0;

        int64_t total = 0;
        int desdeCheca = 0;

        int quadros = 0;
        std::vector<std::vector<int>> bloco;
        while (!(bloco = leitor.ProximoBlocoInteiro(quadros)).empty())
        {
            for (int q = 0; q < quadros; q++)
            {
                for (int c = 0; c < canais; c++)
                {
                    int amostra = bloco[c][q];
                    float norm = (float) (amostra / fundoEscala);
                    if (norm < binMin[c]) binMin[c] = norm;
                    if (norm > binMax[c]) binMax[c] = norm;
                    binSq[c] += (double) norm * norm;
                    if (amostra >= limiarClip || amostra <= -limiarClip) binClip[c] = true;

                    double kw = fHp[c]->P(fSh[c]->P(norm));
                    accCh[c] += kw * kw;
                    double an = norm < 0 ? -norm : norm;
                    if (an > picoAbs) picoAbs = an;
                }
                contador++;
                total++;

                if (contador >= BinBase)
                {
                    for (int c = 0; c < canais; c++)
                    {
                        lsMin[c].push_back(binMin[c]);
                        lsMax[c].push_back(binMax[c]);
                        lsRms[c].push_back((float) std::sqrt(binSq[c] / contador));
                        lsClip[c].push_back(binClip[c]);
                        binMin[c] = 1.0f; binMax[c] = -1.0f; binSq[c] = 0; binClip[c] = false;
                    }
                    contador = 0;
                }

                if (++accN >= amostras100)
                {
                    double z = 0;
                    for (int c = 0; c < canais; c++) { z += accCh[c] / accN; accCh[c] = 0; }
                    accN = 0;
                    subZ[subPos] = z; subPos = (subPos + 1) % MaxSubL;
                    if (subCount < MaxSubL) subCount++;
                    if (subCount >= 4) gate.push_back(MediaAnel(subZ, MaxSubL, subPos, subCount, 4));
                    if (subCount >= MaxSubL) stZ.push_back(MediaAnel(subZ, MaxSubL, subPos, subCount, MaxSubL));
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
            // string.Format("{0:F1}") na cultura pt-BR: vírgula decimal
            char num[32];
            snprintf(num, sizeof num, "%.1f", dblFalta);
            for (char *p = num; *p; p++)
                if (*p == '.') *p = ',';
            strFalha = std::string("o arquivo acaba ") + num + " s antes do que o cabeçalho declara (cortado?)";
            return nullptr;
        }

        // Bin final incompleto.
        if (contador > 0)
            for (int c = 0; c < canais; c++)
            {
                lsMin[c].push_back(binMin[c]);
                lsMax[c].push_back(binMax[c]);
                lsRms[c].push_back((float) std::sqrt(binSq[c] / contador));
                lsClip[c].push_back(binClip[c]);
            }

        auto oc = std::make_shared<OndaCache>();
        oc->Canais = canais; oc->Taxa = leitor.Taxa(); oc->Bits = bits;
        oc->TotalAmostras = total;
        oc->PicoArquivoDb = picoAbs > 1e-9 ? 20.0 * std::log10(picoAbs) : -140.0;
        oc->IntegradoLufs = Integrado(gate);
        oc->LraLu = Lra(stZ);

        std::vector<Faixa> nivel0(canais);
        for (int c = 0; c < canais; c++)
        {
            nivel0[c].Min = std::move(lsMin[c]);
            nivel0[c].Max = std::move(lsMax[c]);
            nivel0[c].Rms = std::move(lsRms[c]);
            nivel0[c].Clipou = std::move(lsClip[c]);
        }

        oc->MontarPiramide(std::move(nivel0));
        return oc;
    }
    catch (const std::exception &ex)
    {
        strFalha = ex.what();
        return nullptr;
    }
    // finally { leitor.Dispose(); }: o destrutor do LeitorAudio
}

std::shared_ptr<OndaCache> OndaCache::DeNivel0(int canais, int taxa, int bits, int64_t totalAmostras,
                                               std::vector<Faixa> nivel0)
{
    auto oc = std::make_shared<OndaCache>();
    oc->Canais = canais; oc->Taxa = taxa; oc->Bits = bits;
    oc->TotalAmostras = totalAmostras;
    oc->IntegradoLufs = MenosInfinito;
    oc->LraLu = 0;
    oc->PicoArquivoDb = -140.0;
    oc->MontarPiramide(std::move(nivel0));
    return oc;
}

void OndaCache::MontarPiramide(std::vector<Faixa> nivel0)
{
    std::vector<std::vector<Faixa>> niveis;
    niveis.push_back(std::move(nivel0));

    while (niveis.back()[0].Bins() > TetoBinsTopo)
    {
        const std::vector<Faixa> &atual = niveis.back();
        int novoBins = (atual[0].Bins() + Fator - 1) / Fator;
        std::vector<Faixa> acima(Canais);
        for (int c = 0; c < Canais; c++)
        {
            const Faixa &f = atual[c];
            Faixa &n = acima[c];
            n.Min.assign(novoBins, 0.0f); n.Max.assign(novoBins, 0.0f);
            n.Rms.assign(novoBins, 0.0f); n.Clipou.assign(novoBins, false);
            for (int b = 0; b < novoBins; b++)
            {
                int ini = b * Fator, fim = std::min(ini + Fator, f.Bins());
                float mn = 1.0f, mx = -1.0f; double sq = 0; bool cl = false; int k = 0;
                for (int i = ini; i < fim; i++)
                {
                    if (f.Min[i] < mn) mn = f.Min[i];
                    if (f.Max[i] > mx) mx = f.Max[i];
                    sq += (double) f.Rms[i] * f.Rms[i];
                    cl = cl || f.Clipou[i];
                    k++;
                }
                n.Min[b] = mn; n.Max[b] = mx;
                n.Rms[b] = k > 0 ? (float) std::sqrt(sq / k) : 0.0f;
                n.Clipou[b] = cl;
            }
        }
        niveis.push_back(std::move(acima));
    }

    _niveis = std::move(niveis);
}

// ===============================================================
// Consulta para desenho
// ===============================================================
double OndaCache::RmsDoTrecho(int canal, int64_t iniAmostra, int64_t fimAmostra) const
{
    if (_niveis.empty() || canal < 0 || canal >= Canais || fimAmostra <= iniAmostra) return 0;
    const Faixa &f = _niveis[0][canal];
    double energia = 0, peso = 0;
    int64_t b0 = std::max<int64_t>(0, iniAmostra / BinBase), b1 = std::min<int64_t>(f.Bins(), (fimAmostra + BinBase - 1) / BinBase);
    for (int64_t b = b0; b < b1; b++)
    {
        int64_t ini = std::max(iniAmostra, b * BinBase), fim = std::min(fimAmostra, (b + 1) * BinBase);
        if (fim <= ini) continue;
        double r = f.Rms[b];
        energia += r * r * (fim - ini);
        peso += fim - ini;
    }
    return peso > 0 ? std::sqrt(energia / peso) : 0;
}

void OndaCache::Colunas(int canal, int64_t iniAmostra, int64_t fimAmostra, int largura, std::vector<float> &outMin,
                        std::vector<float> &outMax, std::vector<float> &outRms, std::vector<bool> &outClip) const
{
    if (_niveis.empty() || largura <= 0 || canal < 0 || canal >= Canais) return;
    if (fimAmostra <= iniAmostra) fimAmostra = iniAmostra + 1;

    int64_t amostrasVisiveis = fimAmostra - iniAmostra;
    double amostrasPorColuna = (double) amostrasVisiveis / largura;

    // Escolhe o nível cujo bin é <= amostras por coluna (para ter pelo
    // menos um bin por coluna), mas o mais grosso possível.
    int nivel = 0;
    for (int n = 0; n < (int) _niveis.size(); n++)
    {
        if (AmostrasPorBin(n) <= amostrasPorColuna) nivel = n;
        else break;
    }

    const Faixa &f = _niveis[nivel][canal];
    int64_t binAmostras = AmostrasPorBin(nivel);

    for (int x = 0; x < largura; x++)
    {
        int64_t a0 = iniAmostra + (int64_t) (x * amostrasPorColuna);
        int64_t a1 = iniAmostra + (int64_t) ((x + 1) * amostrasPorColuna);
        int b0 = (int) (a0 / binAmostras);
        int b1 = (int) ((a1 + binAmostras - 1) / binAmostras);
        if (b1 <= b0) b1 = b0 + 1;
        if (b0 < 0) b0 = 0;
        if (b1 > f.Bins()) b1 = f.Bins();

        float mn = 1.0f, mx = -1.0f, rms = 0.0f; bool cl = false; int k = 0;
        for (int b = b0; b < b1; b++)
        {
            if (f.Min[b] < mn) mn = f.Min[b];
            if (f.Max[b] > mx) mx = f.Max[b];
            if (f.Rms[b] > rms) rms = f.Rms[b];
            cl = cl || f.Clipou[b];
            k++;
        }
        if (k == 0) { mn = 0; mx = 0; }
        outMin[x] = mn; outMax[x] = mx; outRms[x] = rms; outClip[x] = cl;
    }
}

// ===============================================================
// Persistência (sidecar)
// ===============================================================
std::string OndaCache::PastaCache()
{
    std::string p = Base::Combina(Base::PastaCache(), "Ondas");
    try { if (!Base::PastaExiste(p)) Base::CriaPasta(p); } catch (...) {}
    return p;
}

std::string OndaCache::CaminhoSidecar(const std::string &arquivo)
{
    return Base::Combina(PastaCache(), CacheSidecar::Chave(arquivo) + ".onda");
}

bool OndaCache::Salvar(const std::string &arquivoOrigem) const
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
        Escreve<int32_t>(w, Canais); Escreve<int32_t>(w, Taxa); Escreve<int32_t>(w, Bits);
        Escreve(w, TotalAmostras);
        Escreve(w, IntegradoLufs); Escreve(w, LraLu); Escreve(w, PicoArquivoDb);
        Escreve<int32_t>(w, (int32_t) _niveis.size());
        for (size_t n = 0; n < _niveis.size(); n++)
        {
            Escreve<int32_t>(w, _niveis[n][0].Bins());
            for (int c = 0; c < Canais; c++)
            {
                const Faixa &f = _niveis[n][c];
                w.write(reinterpret_cast<const char *>(f.Min.data()), f.Min.size() * sizeof(float));
                w.write(reinterpret_cast<const char *>(f.Max.data()), f.Max.size() * sizeof(float));
                w.write(reinterpret_cast<const char *>(f.Rms.data()), f.Rms.size() * sizeof(float));
                for (int i = 0; i < f.Bins(); i++) Escreve<uint8_t>(w, f.Clipou[i] ? 1 : 0);
            }
        }
        w.close();
        return (bool) w;
    }
    catch (...) { return false; }
}

std::shared_ptr<OndaCache> OndaCache::Carregar(const std::string &arquivoOrigem)
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
        if (Le<int64_t>(r) != tamanho) return nullptr;             // arquivo mudou
        if (Le<int64_t>(r) != ticks) return nullptr;

        auto oc = std::make_shared<OndaCache>();
        oc->Canais = Le<int32_t>(r); oc->Taxa = Le<int32_t>(r); oc->Bits = Le<int32_t>(r);
        oc->TotalAmostras = Le<int64_t>(r);
        oc->IntegradoLufs = Le<double>(r); oc->LraLu = Le<double>(r); oc->PicoArquivoDb = Le<double>(r);
        int nn = Le<int32_t>(r);
        // LINUX: o C# lançava (e o catch devolvia null) num tamanho absurdo; aqui o mesmo, antes de alocar
        if (nn < 0 || nn > 64 || oc->Canais <= 0 || oc->Canais > 64) return nullptr;
        std::vector<std::vector<Faixa>> niveis(nn);
        for (int n = 0; n < nn; n++)
        {
            int bins = Le<int32_t>(r);
            if (bins < 0 || bins > 200000000) return nullptr;
            niveis[n].resize(oc->Canais);
            for (int c = 0; c < oc->Canais; c++)
            {
                Faixa &f = niveis[n][c];
                f.Min.resize(bins); f.Max.resize(bins); f.Rms.resize(bins); f.Clipou.resize(bins);
                r.read(reinterpret_cast<char *>(f.Min.data()), (std::streamsize) bins * sizeof(float));
                r.read(reinterpret_cast<char *>(f.Max.data()), (std::streamsize) bins * sizeof(float));
                r.read(reinterpret_cast<char *>(f.Rms.data()), (std::streamsize) bins * sizeof(float));
                if (!r) return nullptr;
                for (int i = 0; i < bins; i++) f.Clipou[i] = Le<uint8_t>(r) != 0;
            }
        }
        oc->_niveis = std::move(niveis);
        return oc;
    }
    catch (...) { return nullptr; }
}

} // namespace ARCA::Modelo

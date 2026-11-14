// ARCA no xOS - Modelo/ProcessadorExtracaoAudio (de Modelo\ProcessadorExtracaoAudio.cs).
#include "ProcessadorExtracaoAudio.h"

#include "Base/Arquivo.h"
#include "Base/Excecoes.h"
#include "Base/Hash.h"
#include "Base/Texto.h"
#include "Interop/EditorFlac.h"
#include "Interop/EscritorFlac.h"
#include "Interop/LeitorAudio.h"
#include "Modelo/AccurateRip.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <regex>
#include <utility>

namespace ARCA::Modelo {

using namespace ARCA::Base;

namespace {

// LINUX: o Regex do .NET vira std::regex (ECMAScript, sem caixa), com as mesmas expressões.
std::regex Re(const char *strPadrao) { return std::regex(strPadrao, std::regex::ECMAScript | std::regex::icase); }

// string.Format("{0:D2}")
std::string D2(int n)
{
    char b[32];
    snprintf(b, sizeof b, "%02d", n);
    return b;
}

// string.Format("{0:F1}") na cultura da máquina do Lorenzo (pt-BR): vírgula decimal
std::string F1(double v)
{
    char b[64];
    snprintf(b, sizeof b, "%.1f", v);
    for (char *p = b; *p; p++)
        if (*p == '.') *p = ',';
    return b;
}

// "{0:+0;-0}": o sinal sempre (o zero cai na primeira seção: "+0")
std::string ComSinal(int64_t n)
{
    return (n >= 0 ? "+" : "-") + std::to_string(n >= 0 ? n : -n);
}

// "{0:+#;-#}": como acima, mas o zero sai só "+" (o # não escreve zero)
std::string ComSinalSemZero(int64_t n)
{
    return n == 0 ? "+" : ComSinal(n);
}

// o primeiro grupo que casou, ou o segundo: (!string.IsNullOrEmpty(m.Groups[1].Value) ? m.Groups[1].Value : m.Groups[2].Value)
std::string UmOuOutro(const std::smatch &m)
{
    return m[1].matched && m[1].length() > 0 ? m[1].str() : m[2].str();
}

// foreach (char c in Path.GetInvalidFileNameChars()) s = s.Replace(c.ToString(), "");
// Os caracteres proibidos são todos ASCII: tirá-los byte a byte não corta UTF-8.
std::string SemInvalidos(const std::string &s)
{
    const std::string &inv = CaracteresInvalidosEmNome();
    std::string r;
    r.reserve(s.size());
    for (char c : s)
        if (inv.find(c) == std::string::npos) r += c;
    return r;
}

// File.WriteAllLines(caminho, linhas, Encoding.UTF8): com BOM.
// LINUX: as linhas terminam em \r\n, como o Windows gravava (e como o SptiImageExtractor portado
// grava o AudioCD.cue).
void GravarLinhas(const std::string &strCaminho, const std::vector<std::string> &lstLinhas)
{
    std::string s = "\xEF\xBB\xBF";
    for (const std::string &l : lstLinhas) s += l + "\r\n";
    GravaTexto(strCaminho, s);
}

} // namespace

// ===============================================================
//  ParsearCue
// ===============================================================

std::vector<TrackInfo> ProcessadorExtracaoAudio::ParsearCue(const std::string &strCaminhoCue)
{
    std::string strAlbumCdText;
    std::string strArtistaCdText;
    int64_t lngUltimoLeadOut;
    return ParsearCue(strCaminhoCue, strAlbumCdText, strArtistaCdText, lngUltimoLeadOut);
}

std::vector<TrackInfo> ProcessadorExtracaoAudio::ParsearCue(const std::string &strCaminhoCue, std::string &strAlbumCdText,
                                                            std::string &strArtistaCdText)
{
    int64_t lngUltimoLeadOut;
    return ParsearCue(strCaminhoCue, strAlbumCdText, strArtistaCdText, lngUltimoLeadOut);
}

std::vector<TrackInfo> ProcessadorExtracaoAudio::ParsearCue(const std::string &strCaminhoCue, std::string &strAlbumCdText,
                                                            std::string &strArtistaCdText, int64_t &lngUltimoLeadOutSessao1Bytes)
{
    strAlbumCdText.clear();
    strArtistaCdText.clear();
    lngUltimoLeadOutSessao1Bytes = 0;
    std::vector<TrackInfo> lstFaixas;
    if (!ArquivoExiste(strCaminhoCue)) return lstFaixas;

    std::vector<std::string> arrLinhasCue = LeLinhas(strCaminhoCue);
    int intSessaoAtual = 1;
    std::string strMcnCue;

    static const std::regex reSessao = Re(R"(REM\s+SESSION\s+(\d+))");
    static const std::regex reLeadOut = Re(R"(REM\s+LEAD-OUT\s+(\d+):(\d+):(\d+))");
    static const std::regex reTrack = Re(R"(TRACK\s+(\d+)\s+(\S+))");
    static const std::regex reCatalog = Re(R"(CATALOG\s+(\S+))");
    static const std::regex reTitle = Re(R"re(^TITLE\s+(?:"([^"]+)"|(.+))$)re");
    static const std::regex rePerformer = Re(R"re(^PERFORMER\s+(?:"([^"]+)"|(.+))$)re");
    static const std::regex reSongwriter = Re(R"re(^SONGWRITER\s+(?:"([^"]+)"|(.+))$)re");
    static const std::regex reIsrc = Re(R"(ISRC\s+(\S+))");
    static const std::regex reIndex = Re(R"(INDEX 01 (\d+):(\d+):(\d+))");

    for (const std::string &linha : arrLinhasCue)
    {
        std::string t = Trim(linha);
        std::smatch m;
        if (ComecaCom(t, "REM SESSION", true))
        {
            if (std::regex_search(t, m, reSessao)) intSessaoAtual = std::stoi(m[1].str());
        }
        else if (ComecaCom(t, "REM LEAD-OUT", true))
        {
            if (std::regex_search(t, m, reLeadOut))
            {
                int min = std::stoi(m[1].str());
                int sec = std::stoi(m[2].str());
                int frames = std::stoi(m[3].str());
                int64_t totalFrames = (min * 60 * 75) + (sec * 75) + frames;
                if (intSessaoAtual == 1)
                {
                    lngUltimoLeadOutSessao1Bytes = totalFrames * 2352;
                }
            }
        }
        else if (ComecaCom(t, "TRACK", true))
        {
            if (std::regex_search(t, m, reTrack))
            {
                TrackInfo ti;
                ti.TrackNum = std::stoi(m[1].str());
                ti.TrackType = m[2].str();
                ti.SessionNumber = intSessaoAtual;
                ti.Title = "Faixa " + D2(ti.TrackNum);
                ti.Artist = strArtistaCdText;
                ti.ISRC = "";
                lstFaixas.push_back(ti);
            }
        }
        else if (lstFaixas.empty())
        {
            if (ComecaCom(t, "CATALOG", true))
            {
                if (std::regex_search(t, m, reCatalog)) strMcnCue = Trim(m[1].str());
            }
            else if (ComecaCom(t, "TITLE", true))
            {
                if (std::regex_search(t, m, reTitle)) strAlbumCdText = Trim(UmOuOutro(m));
            }
            else if (ComecaCom(t, "PERFORMER", true))
            {
                if (std::regex_search(t, m, rePerformer)) strArtistaCdText = Trim(UmOuOutro(m));
            }
        }
        else if (!lstFaixas.empty())
        {
            TrackInfo &currentInfo = lstFaixas.back();
            if (ComecaCom(t, "TITLE", true))
            {
                if (std::regex_search(t, m, reTitle)) currentInfo.Title = Trim(UmOuOutro(m));
            }
            else if (ComecaCom(t, "PERFORMER", true))
            {
                if (std::regex_search(t, m, rePerformer)) currentInfo.Artist = Trim(UmOuOutro(m));
            }
            else if (ComecaCom(t, "SONGWRITER", true))
            {
                if (std::regex_search(t, m, reSongwriter)) currentInfo.Composer = Trim(UmOuOutro(m));
            }
            else if (ComecaCom(t, "ISRC", true))
            {
                if (std::regex_search(t, m, reIsrc)) currentInfo.ISRC = Trim(m[1].str());
            }
            else if (ComecaCom(t, "INDEX 01", true))
            {
                if (std::regex_search(t, m, reIndex))
                {
                    int min = std::stoi(m[1].str());
                    int sec = std::stoi(m[2].str());
                    int frames = std::stoi(m[3].str());
                    int64_t totalFrames = (min * 60 * 75) + (sec * 75) + frames;
                    currentInfo.StartLba = totalFrames;
                    currentInfo.ByteOffset = totalFrames * 2352;
                }
            }
        }
    }

    for (TrackInfo &track : lstFaixas)
    {
        if (track.Artist.empty() && !strArtistaCdText.empty())
        {
            track.Artist = strArtistaCdText;
        }
        if (!strAlbumCdText.empty())
        {
            track.PossibleAlbums.push_back(strAlbumCdText);
        }
    }

    return lstFaixas;
}

// ===============================================================
//  TFluxoParaEscritor
// ===============================================================

/// Um Stream que entrega ao EscritorFlac (EscritorWma no original) o que o CopiarCorrigido
/// escreveria num WAV — é o que dispensa o arquivo temporário. Falha
/// do codificador vira IOException, que interrompe a cópia.
///
/// O CopiarCorrigido escreve blocos de 64 KB (múltiplos de 4); um resto
/// de quadro incompleto, se aparecesse, espera o bloco seguinte.
// LINUX: no original, uma subclasse de System.IO.Stream que só sabia escrever (Read, Seek e
// SetLength lançavam NotSupportedException). Aqui fica só o que se usa: Write e Length.
class ProcessadorExtracaoAudio::TFluxoParaEscritor
{
public:
    TFluxoParaEscritor(Interop::EscritorFlac &objEscritor, int64_t lngTotal, std::function<void(double)> actProgresso)
        : m_objEscritor(objEscritor), m_lngTotal(lngTotal), m_actProgresso(std::move(actProgresso))
    {
    }

    void Write(const uint8_t *buffer, int offset, int count)
    {
        // Completa o quadro que ficou pela metade no bloco anterior.
        while (m_intResto > 0 && m_intResto < 4 && count > 0)
        {
            m_arrResto[m_intResto++] = buffer[offset++];
            count--;
        }
        if (m_intResto == 4) { Entregar(m_arrResto, 0, 4); m_intResto = 0; }

        int intInteiros = count - count % 4;
        if (intInteiros > 0) Entregar(buffer, offset, intInteiros);
        for (int i = intInteiros; i < count; i++) m_arrResto[m_intResto++] = buffer[offset + i];
    }

    int64_t Length() const { return m_lngEscritos; }

private:
    void Entregar(const uint8_t *arr, int intInicio, int intBytes)
    {
        if (!m_objEscritor.Escrever(arr, intInicio, intBytes))
            throw IOException(!m_objEscritor.Falha().empty() ? m_objEscritor.Falha() : "o codificador recusou o áudio");
        m_lngEscritos += intBytes;
        if (m_actProgresso && m_lngTotal > 0)
        {
            int intPct = (int) (m_lngEscritos * 100 / m_lngTotal);
            if (intPct != m_intUltimoPct) { m_intUltimoPct = intPct; m_actProgresso(intPct); }
        }
    }

    Interop::EscritorFlac &m_objEscritor;
    const int64_t m_lngTotal;
    const std::function<void(double)> m_actProgresso;
    uint8_t m_arrResto[4] = { 0, 0, 0, 0 };
    int m_intResto = 0;
    int64_t m_lngEscritos = 0;
    int m_intUltimoPct = -1;
};

// ===============================================================
//  CopiarCorrigido, ConferirWma, TrechoTemSom
// ===============================================================

/// Copia lngTamanho bytes do .bin a partir de lngInicio (que pode cair
/// antes do início ou depois do fim do disco: aí entra silêncio) e
/// devolve o SHA-256 do que foi escrito — a assinatura do áudio que
/// entra no codificador.
// LINUX: no original o destino era um Stream qualquer; o único que se passava era o
// TFluxoParaEscritor, e é ele que se recebe aqui.
std::string ProcessadorExtracaoAudio::CopiarCorrigido(const std::string &strBin, int64_t lngTamanhoBin, int64_t lngInicio,
                                                      int64_t lngTamanho, TFluxoParaEscritor &objDestino)
{
    Hasher sha(Algoritmo::Sha256);
    // LINUX: o FileStream lançava FileNotFoundException, que é uma IOException: quem chama a
    // captura do mesmo jeito.
    FILE *fsBin = fopen(strBin.c_str(), "rb");
    if (!fsBin) throw IOException("Não foi possível abrir " + strBin + ": " + strerror(errno));
    try
    {
        std::vector<uint8_t> buf(65536);
        int64_t pos = lngInicio, fim = lngInicio + lngTamanho;
        while (pos < fim)
        {
            int n = (int) std::min<int64_t>((int64_t) buf.size(), fim - pos);
            if (pos < 0 || pos >= lngTamanhoBin)
            {
                // Fora do disco: silêncio até a borda mais próxima.
                int64_t lngAteBorda = pos < 0 ? -pos : fim - pos;
                n = (int) std::min<int64_t>(n, lngAteBorda);
                std::fill(buf.begin(), buf.begin() + n, 0);
            }
            else
            {
                n = (int) std::min<int64_t>(n, lngTamanhoBin - pos);
                fseeko(fsBin, (off_t) pos, SEEK_SET);
                int lidos = 0;
                while (lidos < n) { size_t r = fread(buf.data() + lidos, 1, (size_t) (n - lidos), fsBin); if (r == 0) break; lidos += (int) r; }
                if (lidos < n) std::fill(buf.begin() + lidos, buf.begin() + n, 0);
            }
            objDestino.Write(buf.data(), 0, n);
            sha.Acrescenta(buf.data(), (size_t) n);
            pos += n;
        }
    }
    catch (...)
    {
        fclose(fsBin);
        throw;
    }
    fclose(fsBin);
    return Hex(sha.Final());
}

void ProcessadorExtracaoAudio::ConferirWma(const std::string &strWma, TConferenciaWma &c)
{
    c.strFalha.clear();
    c.strArquivoWma = strWma;
    // LINUX: nos textos para o usuário, "WMA" vira "FLAC" (o formato do acervo no xOS).
    if (strWma.empty() || !ArquivoExiste(strWma)) { c.strFalha = "o FLAC não foi gerado"; return; }
    Interop::LeitorAudio l;
    Hasher sha(Algoritmo::Sha256);
    if (!l.Abrir(strWma)) { c.strFalha = "não consegui abrir o FLAC: " + l.Falha(); return; }
    if (l.Canais() != 2 || l.Bits() != 16 || l.Taxa() != 44100)
    {
        c.strFalha = "o FLAC saiu em " + std::to_string(l.Canais()) + " canais, " + std::to_string(l.Bits()) + " bits, " +
                     std::to_string(l.Taxa()) + " Hz";
        return;
    }
    int64_t n = 0;
    std::vector<uint8_t> b;
    while (!(b = l.ProximoBlocoBytes()).empty()) { sha.Acrescenta(b.data(), b.size()); n += (int64_t) b.size(); }
    std::vector<uint8_t> arrHash = sha.Final();
    if (!l.Terminou()) { c.strFalha = "erro ao decodificar o FLAC: " + l.Falha(); return; }
    c.lngBytesWma = n;
    c.strHashWma = Hex(arrHash);
    if (n != c.lngBytesFonte)
        c.strFalha = "tamanho diferente (" + ComSinalSemZero(n - c.lngBytesFonte) + " bytes)";
    else if (c.strHashWma != c.strHashFonte)
        c.strFalha = "mesmo tamanho, conteúdo diferente";
}

bool ProcessadorExtracaoAudio::TrechoTemSom(const std::string &strBin, int64_t lngInicio, int64_t lngTamanho, double &dblPicoDb)
{
    int intPico = 0;
    FILE *fs = fopen(strBin.c_str(), "rb");
    if (!fs) throw IOException("Não foi possível abrir " + strBin + ": " + strerror(errno));
    int64_t lngTamanhoArq = Tamanho(strBin);
    int64_t a = std::max<int64_t>(0, lngInicio) & ~3LL, b = std::min<int64_t>(lngTamanhoArq, lngInicio + lngTamanho);
    fseeko(fs, (off_t) a, SEEK_SET);
    std::vector<uint8_t> buf(1 << 16);
    int64_t pos = a;
    while (pos < b)
    {
        int n = (int) fread(buf.data(), 1, (size_t) std::min<int64_t>((int64_t) buf.size(), b - pos), fs) & ~1;
        if (n <= 0) break;
        for (int i = 0; i + 1 < n; i += 2)
        {
            int v = std::abs((int) (int16_t) (buf[i] | (buf[i + 1] << 8)));
            if (v > intPico) intPico = v;
        }
        pos += n;
    }
    fclose(fs);
    dblPicoDb = intPico > 0 ? 20.0 * std::log10(intPico / 32768.0) : -std::numeric_limits<double>::infinity();
    return intPico > 32;
}

// ===============================================================
//  FatiarEConverterParaWma
// ===============================================================

// LINUX: o nome do método fica o do original; as faixas saem em FLAC (.flac).
bool ProcessadorExtracaoAudio::FatiarEConverterParaWma(
    const std::string &strCaminhoCue,
    const std::string &strArtistaEleito,
    const std::string &strAlbumEleito,
    const std::string &strAnoEleito,
    const std::string &strGeneroEleito,
    const std::vector<uint8_t> &arrCapaOficialBytes,
    Relato relatar,
    Progresso notificarProgresso,
    int intOffsetAmostras,
    std::vector<TConferenciaWma> *lstConferencia)
{
    if (!relatar) relatar = [](const std::string &) {};

    if (!ArquivoExiste(strCaminhoCue))
    {
        relatar("Erro: Arquivo CUE não encontrado: " + strCaminhoCue);
        return false;
    }

    std::string strPasta = PastaDe(strCaminhoCue);
    std::string strAlbumCdText, strArtistaCdText;
    int64_t lngUltimoLeadOutSessao1Bytes;
    std::vector<TrackInfo> lstTodasFaixas = ParsearCue(strCaminhoCue, strAlbumCdText, strArtistaCdText, lngUltimoLeadOutSessao1Bytes);

    std::vector<TrackInfo> lstFaixasAudio;
    std::vector<TrackInfo> lstFaixasDados;
    for (const TrackInfo &ti : lstTodasFaixas)
    {
        if (IgualSemCaixa(ti.TrackType, "AUDIO"))
        {
            lstFaixasAudio.push_back(ti);
        }
        else if (!lstFaixasAudio.empty())
        {
            // Só dados DEPOIS do áudio são sessão de CD Extra. Dados antes
            // (CD mixed-mode) nem foram lidos — no .bin são zeros.
            lstFaixasDados.push_back(ti);
        }
    }

    if (lstFaixasAudio.empty())
    {
        relatar("Erro: Nenhuma faixa de áudio encontrada no CUE.");
        return false;
    }

    std::string strNomeBin = NomeSemExtensao(strCaminhoCue) + ".bin";
    std::string strCaminhoBin = Combina(strPasta, strNomeBin);
    if (!ArquivoExiste(strCaminhoBin))
    {
        strCaminhoBin = Combina(strPasta, "AudioCD.bin");
    }
    if (!ArquivoExiste(strCaminhoBin))
    {
        relatar("Erro: Arquivo BIN de áudio correspondente não encontrado: " + strCaminhoBin);
        return false;
    }

    int64_t lngTamanhoTotalBin = Tamanho(strCaminhoBin);

    // O fim do áudio vem do mesmo índice que o AccurateRip usa: em CD
    // Extra, 11.400 setores antes da faixa de dados (o intervalo entre
    // as sessões não é música), e não no início dela.
    std::shared_ptr<TTocAudio> objToc = TTocAudio::DeCue(strCaminhoCue, lngTamanhoTotalBin);

    for (size_t i = 0; i < lstFaixasAudio.size(); i++)
    {
        int64_t startByte = lstFaixasAudio[i].ByteOffset;
        int64_t endByte = 0;
        if (i + 1 < lstFaixasAudio.size())
        {
            endByte = lstFaixasAudio[i + 1].ByteOffset;
        }
        else if (objToc != nullptr && (int64_t) objToc->intLeadOut * 2352 > startByte)
        {
            endByte = (int64_t) objToc->intLeadOut * 2352;
        }
        else if (lngUltimoLeadOutSessao1Bytes > startByte)
        {
            endByte = lngUltimoLeadOutSessao1Bytes;
        }
        else if (!lstFaixasDados.empty())
        {
            endByte = lstFaixasDados[0].ByteOffset;
        }
        else
        {
            endByte = lngTamanhoTotalBin;
        }
        lstFaixasAudio[i].EndByteOffset = endByte;
    }
    if (intOffsetAmostras != 0)
        relatar("Corrigindo o offset do drive: " + ComSinal(intOffsetAmostras) + " amostras.");

    // Áudio escondido antes da faixa 1 (HTOA): só existe se a faixa 1 começa
    // depois do início do disco — e não em mixed-mode, onde o que vem antes
    // é a faixa de dados. Com som de verdade, vira a faixa 00; silêncio, não.
    if (!lstFaixasAudio.empty() && (objToc == nullptr || objToc->intDadosAntes < 0)
        && lstFaixasAudio[0].ByteOffset >= (int64_t) SetoresMinimosFaixaOculta * 2352)
    {
        int64_t lngFimOculta = lstFaixasAudio[0].ByteOffset;
        double dblPico;
        if (TrechoTemSom(strCaminhoBin, (int64_t) intOffsetAmostras * 4, lngFimOculta, dblPico))
        {
            TrackInfo tiOculta;
            tiOculta.TrackNum = 0;
            tiOculta.ByteOffset = 0;
            tiOculta.EndByteOffset = lngFimOculta;
            tiOculta.StartLba = 0;
            tiOculta.Title = TituloFaixaOculta;
            tiOculta.TrackType = "AUDIO";
            tiOculta.ISRC = "";
            lstFaixasAudio.insert(lstFaixasAudio.begin(), tiOculta);
            relatar("Áudio escondido antes da faixa 1: " + F1(lngFimOculta / 2352.0 / 75.0) + " s com som (pico " + F1(dblPico) +
                    " dBFS). Vira a faixa 00.");
        }
        else
            relatar("Antes da faixa 1 há " + F1(lngFimOculta / 2352.0 / 75.0) + " s de silêncio: não vira faixa.");
    }

    relatar("Iniciando fatiamento de " + std::to_string(lstFaixasAudio.size()) + " faixas de áudio a partir da imagem física...");

    // Codificação pelo Windows Media Format (EscritorWma), desde
    // 04/10/2026 — antes, Expression Encoder 4 sobre WAVs temporários.
    // O codec é o mesmo (o Encoder usava o wmvcore.dll do Windows); o
    // PCM agora vai do .bin direto para o codificador, sem WAV.
    // LINUX: no xOS o codificador é o EscritorFlac (libFLAC, nível 8, verificação ligada); o PCM
    // continua indo do .bin direto para ele. A mensagem dizia "Windows Media Audio 9 Lossless".
    relatar("Codificando áudio para FLAC (Bit-Perfect)...");
    for (size_t i = 0; i < lstFaixasAudio.size(); i++)
    {
        TrackInfo &faixaAtual = lstFaixasAudio[i];
        int64_t startByte = faixaAtual.ByteOffset;
        int64_t length = faixaAtual.EndByteOffset - startByte;

        if (length <= 0) continue;

        std::string strTituloBase = !faixaAtual.Title.empty() ? faixaAtual.Title : "Faixa " + D2(faixaAtual.TrackNum);
        std::string strNomeArquivoBase = D2(faixaAtual.TrackNum) + " - " + strTituloBase;

        strNomeArquivoBase = SemInvalidos(strNomeArquivoBase);
        strNomeArquivoBase = Trim(strNomeArquivoBase);
        // LINUX: .flac no lugar de .wma.
        faixaAtual.WmaFilePath = Combina(strPasta, strNomeArquivoBase + ".flac");

        // As mesmas tags que o Encoder gravava, com os mesmos tipos
        // (WM/TrackNumber é DWORD). Vazias ficam de fora.
        // LINUX: no FLAC tudo é texto; o EscritorFlac traduz os nomes ASF para os Vorbis do acervo
        // (TITLE, ARTIST, ALBUMARTIST...) e grava o número como número. O WM/EncodedBy era
        // "ARCA / Windows Media Format / WMA 9 Lossless".
        std::string strArtistaFinal = !faixaAtual.Artist.empty() ? faixaAtual.Artist : strArtistaEleito;
        std::vector<std::pair<std::string, Interop::ValorAtributo>> lstAtributos = {
            { "Title", strTituloBase },
            { "Author", strArtistaFinal },
            { "WM/AlbumArtist", strArtistaEleito },
            { "WM/AlbumTitle", strAlbumEleito },
            { "WM/Year", strAnoEleito },
            { "WM/Genre", strGeneroEleito },
            { "WM/TrackNumber", faixaAtual.TrackNum },
            { "WM/EncodedBy", Interop::EscritorFlac::CodificadoPor() }
        };

        std::string strFalhaFaixa;
        std::string strHashFonte;
        {
            Interop::EscritorFlac objEscritor;
            if (!objEscritor.Abrir(faixaAtual.WmaFilePath, lstAtributos))
            {
                strFalhaFaixa = objEscritor.Falha();
                // LINUX: o Falha do original nunca era null aqui; garante que a falha não se perca.
                if (strFalhaFaixa.empty()) strFalhaFaixa = "o codificador não abriu o arquivo";
            }
            else
            {
                if (i == 0 && !objEscritor.Formato().empty()) relatar("Formato do codec: " + objEscritor.Formato() + ".");
                int intFaixa = faixaAtual.TrackNum;
                int intTotal = (int) lstFaixasAudio.size();
                TFluxoParaEscritor objFluxo(objEscritor, length, [&notificarProgresso, intFaixa, intTotal](double dblPct) {
                    if (notificarProgresso) notificarProgresso(intFaixa, intTotal, dblPct);
                });
                // A faixa corrigida começa offset amostras adiante na
                // leitura crua. Antes do início ou depois do fim do
                // disco não há o que ler: silêncio, como em qualquer
                // ripador sem leitura além do lead-out — e são
                // exatamente as pontas que o AccurateRip ignora.
                try
                {
                    strHashFonte = CopiarCorrigido(strCaminhoBin, lngTamanhoTotalBin,
                                                   startByte + (int64_t) intOffsetAmostras * 4, length, objFluxo);
                }
                catch (const IOException &exEscrita) { strFalhaFaixa = exEscrita.what(); }
                if (strFalhaFaixa.empty() && !objEscritor.Concluir())
                {
                    strFalhaFaixa = objEscritor.Falha();
                    if (strFalhaFaixa.empty()) strFalhaFaixa = "o codificador não concluiu o arquivo";
                }
            }
        }

        if (!strFalhaFaixa.empty())
        {
            // FLAC (WMA no original) pela metade não fica na área de trabalho parecendo bom.
            try { if (ArquivoExiste(faixaAtual.WmaFilePath)) fs::remove(fs::u8path(faixaAtual.WmaFilePath)); } catch (...) {}
            relatar("Erro: a codificação da faixa " + D2(faixaAtual.TrackNum) + " falhou: " + strFalhaFaixa);
            return false;
        }
        if (lstConferencia != nullptr)
        {
            TConferenciaWma c;
            c.intFaixa = faixaAtual.TrackNum;
            c.strHashFonte = strHashFonte;
            c.lngBytesFonte = length;
            lstConferencia->push_back(c);
        }
    }

    // Elo 2 do bit-perfect: o FLAC (WMA no original) decodificado tem de ser,
    // byte a byte, o PCM que entrou no codificador.
    if (lstConferencia != nullptr)
    {
        relatar("Conferindo cada FLAC contra o áudio do disco, bit a bit...");
        for (const TrackInfo &ti : lstFaixasAudio)
        {
            auto it = std::find_if(lstConferencia->begin(), lstConferencia->end(),
                                   [&ti](const TConferenciaWma &x) { return x.intFaixa == ti.TrackNum; });
            if (it == lstConferencia->end()) continue;
            TConferenciaWma &c = *it;
            ConferirWma(ti.WmaFilePath, c);
            relatar(c.Confere()
                ? "  Faixa " + D2(c.intFaixa) + ": FLAC idêntico ao áudio do disco."
                : "  ⚠ Faixa " + D2(c.intFaixa) + ": FLAC DIFERENTE do áudio do disco — " + c.strFalha);
        }
    }

    relatar("Codificação FLAC concluída. Aplicando metadados nativos e capa...");

    for (const TrackInfo &ti : lstFaixasAudio)
    {
        if (!ArquivoExiste(ti.WmaFilePath)) continue;

        try
        {
            Interop::EditorFlac editor;
            if (editor.Abrir(ti.WmaFilePath))
            {
                if (!ti.ISRC.empty())
                {
                    editor.DefinirTexto("WM/ISRC", ti.ISRC);
                }
                if (!ti.Composer.empty())
                {
                    editor.DefinirTexto("WM/Composer", ti.Composer);
                }
                if (!arrCapaOficialBytes.empty())
                {
                    editor.DefinirImagem(arrCapaOficialBytes, "", "Cover");
                }
                editor.Salvar();
            }
        }
        catch (const std::exception &exTags)
        {
            relatar("Aviso: Falha ao embutir atributos complementares na faixa " + std::to_string(ti.TrackNum) + ": " + exTags.what());
        }
    }

    // 3. CD Extra: os vídeos da sessão de dados NÃO saem do .bin — ele
    //    termina no fim do áudio. Quem chama os copia da unidade montada
    //    (TCdExtraExtractor.CopiarVideosDaUnidade), conferidos.
    if (!lstFaixasDados.empty())
        relatar("Enhanced CD (CD Extra): " + std::to_string(lstFaixasAudio.size()) + " faixas de áudio e " +
                std::to_string(lstFaixasDados.size()) + " faixa(s) de dados; os vídeos vêm da unidade.");

    relatar("Processamento e fatiamento concluídos com sucesso.");
    return true;
}

// ===============================================================
//  RenomearETaggearFaixas
// ===============================================================

bool ProcessadorExtracaoAudio::RenomearETaggearFaixas(
    const std::string &strPastaTrabalho,
    const std::vector<std::string> &lstTitulos,
    const std::vector<std::string> &lstArtistasFaixas,
    const std::string &strArtistaAlbum,
    const std::string &strAlbum,
    const std::vector<uint8_t> &arrCapaBytes,
    Relato relatar)
{
    return RenomearETaggearFaixas(strPastaTrabalho, lstTitulos, lstArtistasFaixas, strArtistaAlbum, strAlbum, {}, arrCapaBytes, relatar);
}

bool ProcessadorExtracaoAudio::RenomearETaggearFaixas(
    const std::string &strPastaTrabalho,
    const std::vector<std::string> &lstTitulos,
    const std::vector<std::string> &lstArtistasFaixas,
    const std::string &strArtistaAlbum,
    const std::string &strAlbum,
    const std::vector<std::string> &lstCompositores,
    const std::vector<uint8_t> &arrCapaBytes,
    Relato relatar)
{
    return RenomearETaggearFaixas(strPastaTrabalho, lstTitulos, lstArtistasFaixas, strArtistaAlbum,
                                  strAlbum, lstCompositores, arrCapaBytes, "", "", relatar);
}

bool ProcessadorExtracaoAudio::RenomearETaggearFaixas(
    const std::string &strPastaTrabalho,
    const std::vector<std::string> &lstTitulos,
    const std::vector<std::string> &lstArtistasFaixas,
    const std::string &strArtistaAlbum,
    const std::string &strAlbum,
    const std::vector<std::string> &lstCompositores,
    const std::vector<uint8_t> &arrCapaBytes,
    const std::string &strAno,
    const std::string &strGenero,
    Relato relatar)
{
    if (!relatar) relatar = [](const std::string &) {};

    if (strPastaTrabalho.empty() || !PastaExiste(strPastaTrabalho))
    {
        relatar("Erro: Pasta de trabalho não encontrada.");
        return false;
    }

    if (lstTitulos.empty())
    {
        relatar("Aviso: Nenhuma lista de títulos informada para renomeação.");
        return false;
    }

    // LINUX: GetFiles("*.wma") vira os .flac da pasta.
    std::vector<std::string> arrArquivos = Arquivos(strPastaTrabalho, ".flac");
    if (arrArquivos.empty())
    {
        relatar("Aviso: Nenhum arquivo FLAC localizado na área de trabalho para renomear.");
        return false;
    }

    // Agrupa e ordena os arquivos pelo número da faixa indicado no início do nome
    static const std::regex reNumero(R"(^(\d{1,3})\s*[-_.])");
    std::vector<std::pair<int, std::string>> lstFaixasWma;
    for (const std::string &fi : arrArquivos)
    {
        std::string strNome = NomeDoArquivo(fi);
        std::smatch m;
        if (std::regex_search(strNome, m, reNumero))
        {
            int intNum = std::stoi(m[1].str());
            lstFaixasWma.emplace_back(intNum, fi);
        }
    }

    if (lstFaixasWma.empty())
    {
        relatar("Aviso: Nenhum arquivo FLAC com numeração inicial padrão localizado.");
        return false;
    }

    std::sort(lstFaixasWma.begin(), lstFaixasWma.end(),
              [](const std::pair<int, std::string> &a, const std::pair<int, std::string> &b) { return a.first < b.first; });

    relatar("Aplicando metadados e renomeando " + std::to_string(lstFaixasWma.size()) + " faixa(s) com base na identificação oficial...");

    static const std::regex reEspacos(R"(\s+)");

    for (size_t i = 0; i < lstFaixasWma.size(); i++)
    {
        int intNumFaixa = lstFaixasWma[i].first;
        std::string strCaminhoAtual = lstFaixasWma[i].second;
        std::string strNomeAtual = NomeDoArquivo(strCaminhoAtual);

        // A faixa 00 (áudio escondido antes da faixa 1) não está na lista
        // de títulos: pela posição, receberia o título da faixa 1. Leva
        // só as etiquetas do álbum.
        if (intNumFaixa == 0)
        {
            try
            {
                Interop::EditorFlac editor;
                if (editor.Abrir(strCaminhoAtual))
                {
                    if (!strArtistaAlbum.empty()) { editor.DefinirTexto("Author", strArtistaAlbum); editor.DefinirTexto("WM/AlbumArtist", strArtistaAlbum); }
                    if (!strAlbum.empty()) editor.DefinirTexto("WM/AlbumTitle", strAlbum);
                    if (!strAno.empty()) editor.DefinirTexto("WM/Year", Trim(strAno));
                    if (!strGenero.empty()) editor.DefinirTexto("WM/Genre", Trim(strGenero));
                    if (!arrCapaBytes.empty()) editor.DefinirImagem(arrCapaBytes, "", "Cover");
                    editor.Salvar();
                }
            }
            catch (const std::exception &exOculta) { relatar(std::string("Aviso: Falha ao atualizar as tags da faixa 00: ") + exOculta.what()); }
            continue;
        }

        // Encontra o título correspondente (pelo índice 0-based da contagem ou pela posição da faixa)
        int intIdxTitulo = (intNumFaixa >= 1 && intNumFaixa <= (int) lstTitulos.size()) ? (intNumFaixa - 1) : (int) i;
        if (intIdxTitulo < 0 || intIdxTitulo >= (int) lstTitulos.size()) continue;

        std::string strTituloOficial = lstTitulos[intIdxTitulo];
        if (VazioOuBranco(strTituloOficial)) continue;

        strTituloOficial = Trim(strTituloOficial);

        // Higieniza o nome para o sistema de arquivos
        std::string strTituloLimpo = SemInvalidos(strTituloOficial);
        strTituloLimpo = Trim(std::regex_replace(strTituloLimpo, reEspacos, " "));
        if (strTituloLimpo.empty()) strTituloLimpo = "Faixa " + D2(intNumFaixa);

        // LINUX: .flac no lugar de .wma.
        std::string strNovoNomeArquivo = D2(intNumFaixa) + " - " + strTituloLimpo + ".flac";
        std::string strNovoCaminho = Combina(strPastaTrabalho, strNovoNomeArquivo);

        // Determina o intérprete específico da faixa (para coletâneas)
        std::string strArtistaFaixa = (intIdxTitulo < (int) lstArtistasFaixas.size() && !lstArtistasFaixas[intIdxTitulo].empty())
            ? Trim(lstArtistasFaixas[intIdxTitulo])
            : strArtistaAlbum;

        // 1. Atualiza as tags (ASF nativas no original)
        try
        {
            Interop::EditorFlac editor;
            if (editor.Abrir(strCaminhoAtual))
            {
                editor.DefinirTexto("Title", strTituloOficial);
                if (!strArtistaFaixa.empty()) editor.DefinirTexto("Author", strArtistaFaixa);
                if (!strArtistaAlbum.empty()) editor.DefinirTexto("WM/AlbumArtist", strArtistaAlbum);
                if (!strAlbum.empty()) editor.DefinirTexto("WM/AlbumTitle", strAlbum);
                if (!strAno.empty()) editor.DefinirTexto("WM/Year", Trim(strAno));
                if (!strGenero.empty()) editor.DefinirTexto("WM/Genre", Trim(strGenero));
                if (intIdxTitulo >= 0 && intIdxTitulo < (int) lstCompositores.size() && !lstCompositores[intIdxTitulo].empty())
                {
                    editor.DefinirTexto("WM/Composer", Trim(lstCompositores[intIdxTitulo]));
                }
                if (!arrCapaBytes.empty())
                {
                    editor.DefinirImagem(arrCapaBytes, "", "Cover");
                }
                editor.Salvar();
            }
        }
        catch (const std::exception &exAsf)
        {
            relatar("Aviso: Falha ao atualizar tags da faixa " + std::to_string(intNumFaixa) + ": " + exAsf.what());
        }

        // 2. Renomeia o arquivo físico se o nome divergir
        // Dois casos que antes se confundiam: o nome novo difere só em
        // maiúsculas (o NTFS acha que é o mesmo arquivo: passa por um
        // nome temporário), ou já existe OUTRO arquivo com esse nome
        // (aí não se mexe em nada: a faixa ficava presa como
        // .tmp_rename, fora do manifesto, e não chegava ao álbum).
        // LINUX: a área de trabalho é ext4, que distingue maiúsculas; o caminho pelo nome temporário
        // funciona igual, e fica para quando a área estiver num disco que não distingue.
        if (strNomeAtual != strNovoNomeArquivo)
        {
            bool blnSoMaiusculas = IgualSemCaixa(strNomeAtual, strNovoNomeArquivo);
            std::string strTempCaminho = strCaminhoAtual + ".tmp_rename";
            try
            {
                if (blnSoMaiusculas)
                {
                    if (ArquivoExiste(strTempCaminho)) fs::remove(fs::u8path(strTempCaminho));
                    fs::rename(fs::u8path(strCaminhoAtual), fs::u8path(strTempCaminho));
                    fs::rename(fs::u8path(strTempCaminho), fs::u8path(strNovoCaminho));
                    relatar("  -> Faixa " + D2(intNumFaixa) + ": '" + strNomeAtual + "' => '" + strNovoNomeArquivo + "'");
                }
                else if (ArquivoExiste(strNovoCaminho))
                {
                    relatar("ATENÇÃO: não renomeei a faixa " + D2(intNumFaixa) + " ('" + strNomeAtual + "'): já existe outro arquivo chamado '" +
                            strNovoNomeArquivo + "' na área de trabalho. " +
                            "Os dois continuam lá; confira qual é o certo antes de aprovar.");
                }
                else
                {
                    fs::rename(fs::u8path(strCaminhoAtual), fs::u8path(strNovoCaminho));
                    relatar("  -> Faixa " + D2(intNumFaixa) + ": '" + strNomeAtual + "' => '" + strNovoNomeArquivo + "'");
                }
            }
            catch (const std::exception &exMove)
            {
                // Parou no meio do nome temporário: devolve o original,
                // para a faixa não sair do manifesto.
                try
                {
                    if (ArquivoExiste(strTempCaminho) && !ArquivoExiste(strCaminhoAtual))
                        fs::rename(fs::u8path(strTempCaminho), fs::u8path(strCaminhoAtual));
                }
                catch (...) {}
                relatar("Erro ao renomear arquivo '" + strNomeAtual + "' para '" + strNovoNomeArquivo + "': " + exMove.what());
            }
        }
    }

    // 3. Atualiza o AudioCD.cue correspondente se existir
    std::string strCaminhoCue = Combina(strPastaTrabalho, "AudioCD.cue");
    if (ArquivoExiste(strCaminhoCue))
    {
        AtualizarCueComTitulos(strCaminhoCue, strArtistaAlbum, strAlbum, lstTitulos, lstArtistasFaixas, lstCompositores, relatar);
    }

    return true;
}

// ===============================================================
//  AtualizarCueComTitulos
// ===============================================================

void ProcessadorExtracaoAudio::AtualizarCueComTitulos(
    const std::string &strCaminhoCue,
    const std::string &strArtistaAlbum,
    const std::string &strAlbum,
    const std::vector<std::string> &lstTitulos,
    const std::vector<std::string> &lstArtistasFaixas,
    Relato relatar)
{
    AtualizarCueComTitulos(strCaminhoCue, strArtistaAlbum, strAlbum, lstTitulos, lstArtistasFaixas, {}, relatar);
}

void ProcessadorExtracaoAudio::AtualizarCueComTitulos(
    const std::string &strCaminhoCue,
    const std::string &strArtistaAlbum,
    const std::string &strAlbum,
    const std::vector<std::string> &lstTitulos,
    const std::vector<std::string> &lstArtistasFaixas,
    const std::vector<std::string> &lstCompositores,
    Relato relatar)
{
    if (!ArquivoExiste(strCaminhoCue) || lstTitulos.empty()) return;

    // .Replace("\"", "'")
    auto SemAspas = [](const std::string &s) { return Substitui(s, "\"", "'"); };
    static const std::regex reTrk = Re(R"(TRACK\s+(\d+))");

    try
    {
        std::vector<std::string> arrLinhas = LeLinhas(strCaminhoCue);
        std::vector<std::string> lstNovasLinhas;
        int intFaixaAtual = 0;
        // O que a faixa corrente já tem. Antes isto se conferia olhando a
        // linha anterior (o TITLE vinha antes do ISRC e era repetido) e o
        // arquivo inteiro (o SONGWRITER da faixa 1 impedia o das outras).
        bool blnFaixaTemTitulo = false, blnFaixaTemCompositor = false;
        bool blnTituloAlbumInserido = false;
        bool blnArtistaAlbumInserido = false;

        for (size_t i = 0; i < arrLinhas.size(); i++)
        {
            const std::string &strLinha = arrLinhas[i];
            std::string strTrim = Trim(strLinha);

            if (intFaixaAtual == 0)
            {
                // Cabeçalho do CUE
                if (ComecaCom(strTrim, "TITLE ", true) && !strAlbum.empty())
                {
                    lstNovasLinhas.push_back("TITLE \"" + SemAspas(strAlbum) + "\"");
                    blnTituloAlbumInserido = true;
                    continue;
                }
                if (ComecaCom(strTrim, "PERFORMER ", true) && !strArtistaAlbum.empty())
                {
                    lstNovasLinhas.push_back("PERFORMER \"" + SemAspas(strArtistaAlbum) + "\"");
                    blnArtistaAlbumInserido = true;
                    continue;
                }

                if (ComecaCom(strTrim, "TRACK ", true))
                {
                    // Se ainda não tinha TITLE/PERFORMER no topo, insere antes da primeira faixa
                    if (!blnArtistaAlbumInserido && !strArtistaAlbum.empty())
                    {
                        lstNovasLinhas.push_back("PERFORMER \"" + SemAspas(strArtistaAlbum) + "\"");
                        blnArtistaAlbumInserido = true;
                    }
                    if (!blnTituloAlbumInserido && !strAlbum.empty())
                    {
                        lstNovasLinhas.push_back("TITLE \"" + SemAspas(strAlbum) + "\"");
                        blnTituloAlbumInserido = true;
                    }

                    std::smatch mTrk;
                    if (std::regex_search(strTrim, mTrk, reTrk)) intFaixaAtual = std::stoi(mTrk[1].str());
                    blnFaixaTemTitulo = blnFaixaTemCompositor = false;
                    lstNovasLinhas.push_back(strLinha);
                    continue;
                }

                lstNovasLinhas.push_back(strLinha);
            }
            else
            {
                // Dentro de uma faixa
                if (ComecaCom(strTrim, "TRACK ", true))
                {
                    std::smatch mTrk;
                    if (std::regex_search(strTrim, mTrk, reTrk)) intFaixaAtual = std::stoi(mTrk[1].str());
                    blnFaixaTemTitulo = blnFaixaTemCompositor = false;
                    lstNovasLinhas.push_back(strLinha);
                    continue;
                }

                // Substitui TITLE existente ou PERFORMER existente da faixa
                if (ComecaCom(strTrim, "TITLE ", true))
                {
                    blnFaixaTemTitulo = true;
                    int intIdx = intFaixaAtual - 1;
                    if (intIdx >= 0 && intIdx < (int) lstTitulos.size() && !lstTitulos[intIdx].empty())
                    {
                        lstNovasLinhas.push_back("    TITLE \"" + SemAspas(lstTitulos[intIdx]) + "\"");
                        continue;
                    }
                }

                if (ComecaCom(strTrim, "PERFORMER ", true))
                {
                    int intIdx = intFaixaAtual - 1;
                    std::string strArtistaTrk = (intIdx >= 0 && intIdx < (int) lstArtistasFaixas.size())
                        ? lstArtistasFaixas[intIdx]
                        : strArtistaAlbum;

                    if (!strArtistaTrk.empty())
                    {
                        lstNovasLinhas.push_back("    PERFORMER \"" + SemAspas(strArtistaTrk) + "\"");
                        continue;
                    }
                }

                if (ComecaCom(strTrim, "SONGWRITER ", true))
                {
                    blnFaixaTemCompositor = true;
                    int intIdx = intFaixaAtual - 1;
                    if (intIdx >= 0 && intIdx < (int) lstCompositores.size() && !lstCompositores[intIdx].empty())
                    {
                        lstNovasLinhas.push_back("    SONGWRITER \"" + SemAspas(lstCompositores[intIdx]) + "\"");
                        continue;
                    }
                }

                // Se encontrou INDEX 01 e a faixa não teve TITLE, insere antes do INDEX 01
                if (ComecaCom(strTrim, "INDEX 01", true))
                {
                    int intIdx = intFaixaAtual - 1;
                    if (!blnFaixaTemTitulo && intIdx >= 0 && intIdx < (int) lstTitulos.size() && !lstTitulos[intIdx].empty())
                    {
                        lstNovasLinhas.push_back("    TITLE \"" + SemAspas(lstTitulos[intIdx]) + "\"");
                    }

                    // Verifica se há compositor a inserir
                    if (intIdx >= 0 && intIdx < (int) lstCompositores.size() && !lstCompositores[intIdx].empty())
                    {
                        if (!blnFaixaTemCompositor)
                        {
                            lstNovasLinhas.push_back("    SONGWRITER \"" + SemAspas(lstCompositores[intIdx]) + "\"");
                        }
                    }
                }

                lstNovasLinhas.push_back(strLinha);
            }
        }

        GravarLinhas(strCaminhoCue, lstNovasLinhas);
        if (relatar) relatar("AudioCD.cue atualizado com os títulos oficiais das faixas.");
    }
    catch (const std::exception &exCue)
    {
        if (relatar) relatar(std::string("Aviso: Falha ao atualizar AudioCD.cue: ") + exCue.what());
    }
}

} // namespace ARCA::Modelo

// ARCA no xOS - Interop/SptiImageExtractor (de Interop\SptiImageExtractor.cs).
#include "SptiImageExtractor.h"
#include "SptiDiscReader.h"

#include "Modelo/AcquisitionLogger.h"
#include "Modelo/RelatorioExtracao.h"

#include "Base/Arquivo.h"
#include "Base/Excecoes.h"
#include "Base/Texto.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <thread>
#include <unistd.h>

namespace ARCA::Interop {

using namespace ARCA::Base;
using Modelo::TRelatorioExtracao;

namespace {

// string.Format("{0:F1}") na cultura pt-BR: virgula decimal, sem separador de milhar
std::string F(double v, int casas)
{
    char b[64];
    snprintf(b, sizeof b, "%.*f", casas, v);
    for (char *p = b; *p; p++)
        if (*p == '.') *p = ',';
    return b;
}

std::string X2(unsigned v)
{
    char b[8];
    snprintf(b, sizeof b, "%02X", v & 0xFF);
    return b;
}

std::string D2(int v)
{
    char b[16];
    snprintf(b, sizeof b, "%02d", v);
    return b;
}

// LINUX: no original a unidade era a letra, e as mensagens diziam "unidade E:". Aqui, o
// dispositivo, sem os dois-pontos.
std::string NomeUnidade(const std::string &strLetraDrive)
{
    return ComecaCom(strLetraDrive, "/") ? strLetraDrive : DispositivoCd();
}

struct FechaArquivo { void operator()(FILE *f) const { if (f) fclose(f); } };
using TArquivo = std::unique_ptr<FILE, FechaArquivo>;

void Escreve(FILE *f, const void *p, size_t n, const std::string &strCaminho)
{
    if (fwrite(p, 1, n, f) != n) throw IOException("erro ao gravar " + strCaminho + ": " + strerror(errno));
}

void ApagaArquivo(const std::string &strCaminho)
{
    std::error_code e;
    fs::remove(fs::u8path(strCaminho), e);
}

int LbaDe(const CdromTocReader::TRACK_DATA &td)
{
    return (td.Address_1 * 60 + td.Address_2) * 75 + td.Address_3 - 150;
}

} // namespace

SptiImageExtractor::SptiImageExtractor(Modelo::AcquisitionLogger &objLogger) : m_objLogger(objLogger) {}

std::optional<std::vector<SptiImageExtractor::TReleitura>> SptiImageExtractor::Reler(
    const std::string &strLetraDrive, const std::string &strBin, const std::vector<std::array<int, 3>> &lstFaixas,
    const std::function<void(int, double)> &progresso, const std::function<bool()> &cancelou, std::string &strFalha)
{
    strFalha.clear();
    std::vector<TReleitura> lstRes;
    if (lstFaixas.empty()) return lstRes;

    int64_t lngSetoresBin = Tamanho(strBin) / 2352;
    SptiDiscReader objLeitor(m_objLogger);
    TArquivo fsBin(fopen(strBin.c_str(), "rb"));
    if (!fsBin) throw IOException("não foi possível abrir " + strBin);
    {
        if (!objLeitor.AbrirUnidade(strLetraDrive)) { strFalha = "não consegui abrir a unidade para a releitura"; return std::nullopt; }

        std::vector<uint8_t> arrDisco(SetoresPorBlocoReleitura * 2352);
        std::vector<uint8_t> arrBin(arrDisco.size());
        uint8_t *hPin = arrDisco.data();
        {
            // Esvaziar o cache: ler longe de onde a releitura comeca.
            uint32_t dwPrimeira = (uint32_t) lstFaixas[0][1];
            uint32_t dwLonge = dwPrimeira < lngSetoresBin / 2 ? (uint32_t) std::max<int64_t>(0, lngSetoresBin - SetoresParaEsvaziarCache) : 0;
            for (uint32_t d = 0; d < SetoresParaEsvaziarCache && dwLonge + d < lngSetoresBin; d += SetoresPorBlocoReleitura)
            {
                uint32_t n = std::min<uint32_t>(SetoresPorBlocoReleitura, (uint32_t) (lngSetoresBin - dwLonge - d));
                uint32_t b; uint8_t s, k, a, q; int w;
                objLeitor.LerBlocoSpti(hPin, dwLonge + d, n, true, b, s, k, a, q, w);
            }

            for (size_t f = 0; f < lstFaixas.size(); f++)
            {
                TReleitura r;
                r.intFaixa = lstFaixas[f][0];
                uint32_t dwIni = (uint32_t) lstFaixas[f][1], dwFim = (uint32_t) std::min<int64_t>(lstFaixas[f][2], lngSetoresBin);
                for (uint32_t lba = dwIni; lba < dwFim; )
                {
                    uint32_t n = std::min(SetoresPorBlocoReleitura, dwFim - lba);
                    if (!LerComResgate(objLeitor, hPin, lba, n))
                    {
                        r.intSetoresIlegiveis++;   // nem um setor: conta e segue
                        lba++;
                        continue;
                    }
                    fseeko(fsBin.get(), (off_t) lba * 2352, SEEK_SET);
                    size_t intLidos = 0;
                    while (intLidos < n * 2352) { size_t x = fread(arrBin.data() + intLidos, 1, n * 2352 - intLidos, fsBin.get()); if (x == 0) break; intLidos += x; }
                    for (uint32_t sct = 0; sct < n; sct++)
                        for (int i = 0; i < 2352; i++)
                            if (arrDisco[sct * 2352 + i] != arrBin[sct * 2352 + i]) { r.intSetoresDiferentes++; break; }
                    lba += n;
                    if (progresso) progresso(r.intFaixa, (double) (lba - dwIni) / std::max<uint32_t>(1, dwFim - dwIni) * 100.0);
                    if (cancelou && cancelou()) throw OperationCanceledException();
                }
                if (!r.Identica())
                    m_objLogger.RegistrarAviso("Releitura da faixa " + std::to_string(r.intFaixa) + ": " + std::to_string(r.intSetoresDiferentes) +
                                               " setor(es) diferentes da primeira leitura, " + std::to_string(r.intSetoresIlegiveis) + " ilegível(is).");
                else
                    m_objLogger.RegistrarDiagnostico("Releitura da faixa " + std::to_string(r.intFaixa) + ": idêntica à primeira leitura.");
                lstRes.push_back(r);
            }
        }
    }
    return lstRes;
}

bool SptiImageExtractor::LerComResgate(SptiDiscReader &objLeitor, uint8_t *hPin, uint32_t lba, uint32_t &n)
{
    uint32_t b; uint8_t s, k, a, q; int w;
    if (objLeitor.LerBlocoSpti(hPin, lba, n, true, b, s, k, a, q, w) && b == n * 2352)
        return true;
    for (int t = 0; t < 3; t++)
        if (objLeitor.LerBlocoSpti(hPin, lba, 1, true, b, s, k, a, q, w) && b == 2352)
        {
            n = 1;
            return true;
        }
    return false;
}

bool SptiImageExtractor::Interromper(const std::string &strMotivo)
{
    m_strMotivoFalha = strMotivo;
    m_objLogger.RegistrarErro(strMotivo);
    return false;
}

bool SptiImageExtractor::EhFalhaDeCanal(int intWin32Error, uint8_t bSenseKey)
{
    if (intWin32Error != 0 && intWin32Error != 87) return true;   // 87: bloco grande demais, tratado a parte
    return bSenseKey == 0x02 || bSenseKey == 0x06;                 // NOT READY, UNIT ATTENTION (disco trocado)
}

std::string SptiImageExtractor::DescreverFalhaDeCanal(int intWin32Error, uint8_t bSenseKey, uint8_t bAsc, uint8_t bAscq)
{
    // LINUX: os codigos sao os do Windows, traduzidos do errno pelo SptiDiscReader; "o Windows
    // recusou" vira "o sistema recusou".
    if (intWin32Error == 5)
        return "o sistema recusou o acesso direto à unidade (Win32 5, acesso negado)";
    if (intWin32Error != 0)
        return "a unidade parou de responder (Win32 " + std::to_string(intWin32Error) + ")";
    if (bSenseKey == 0x02 && bAsc == 0x3A)
        return "não há disco na unidade (bandeja aberta ou disco retirado)";
    if (bSenseKey == 0x02)
        return "a unidade não ficou pronta (SCSI 02/" + X2(bAsc) + "/" + X2(bAscq) + ")";
    return "o disco foi trocado ou a unidade foi reiniciada no meio da leitura (SCSI 06)";
}

std::vector<std::pair<int, uint32_t>> SptiImageExtractor::InicioDasFaixas(const CdromTocReader::CDROM_TOC &objToc)
{
    std::vector<std::pair<int, uint32_t>> lst;
    int intTotal = objToc.LastTrack - objToc.FirstTrack + 1;
    for (int i = 0; i < intTotal; i++)
    {
        const CdromTocReader::TRACK_DATA &td = objToc.TrackData[i];
        int intLba = LbaDe(td);
        lst.emplace_back(td.TrackNumber, (uint32_t) std::max(0, intLba));
    }
    return lst;
}

int SptiImageExtractor::FaixaDoSetor(const std::vector<std::pair<int, uint32_t>> &lstInicios, uint32_t dwLba)
{
    int intFaixa = 0;   // antes da faixa 1: area oculta (HTOA)
    for (auto &kv : lstInicios)
        if (kv.second <= dwLba) intFaixa = kv.first;
    return intFaixa;
}

bool SptiImageExtractor::ExtrairImagem(const std::string &strLetraDrive, const std::string &strDestinoArquivo,
                                       const std::string &strPastaDestino, bool blnIsAudio, const ProgressoExtracao &delProgresso,
                                       const NotificacaoFase &delFase)
{
    m_strMotivoFalha.clear();
    m_objRelatorio.reset();
    TRelatorioExtracao::Apagar(strPastaDestino);
    bool blnOk = ExtrairImagemInterno(strLetraDrive, strDestinoArquivo, strPastaDestino, blnIsAudio, delProgresso, delFase);
    if (!blnOk)
    {
        if (m_strMotivoFalha.empty()) m_strMotivoFalha = "a extração falhou; o motivo está em aquisicao_detalhada.log";
        // Imagem interrompida nao pode ficar para tras com cara de imagem: ninguem deve fatia-la
        // depois achando que esta inteira.
        try { if (ArquivoExiste(strDestinoArquivo)) ApagaArquivo(strDestinoArquivo); } catch (...) {}
        try { std::string strCue = TrocaExtensao(strDestinoArquivo, ".cue"); if (ArquivoExiste(strCue)) ApagaArquivo(strCue); } catch (...) {}
    }
    return blnOk;
}

bool SptiImageExtractor::ExtrairImagemInterno(const std::string &strLetraDrive, const std::string &strDestinoArquivo,
                                              const std::string &strPastaDestino, bool blnIsAudio,
                                              const ProgressoExtracao &delProgresso, const NotificacaoFase &delFase)
{
    // LINUX: "via Pass Through (SPTI)" e "porta SCSI/SPTI" no original; aqui o SG_IO.
    std::string strUnidade = NomeUnidade(strLetraDrive);
    m_objLogger.RegistrarInfo("Iniciando extração física nativa via SCSI Generic (SG_IO) na unidade " + strUnidade + "...");
    if (delFase) delFase("SONDANDO HARDWARE…", "Abertura de porta SCSI/SG_IO na unidade " + strUnidade);

    SptiDiscReader objLeitor(m_objLogger);
    {
        if (!objLeitor.AbrirUnidade(strLetraDrive))
        {
            return Interromper("não consegui abrir a unidade " + strUnidade + " para leitura direta.");
        }

        // 1. Identificacao do Hardware
        std::string strVendor, strProduct, strRevision;
        if (objLeitor.ObterInquiryHardware(strVendor, strProduct, strRevision))
        {
            m_strFabricante = strVendor;
            m_strModelo = strProduct;
            m_objLogger.RegistrarInfo("Unidade Óptica: " + strVendor + " " + strProduct + " (Rev: " + strRevision + ")");
            if (delFase) delFase("SONDANDO HARDWARE…", "Unidade: " + strVendor + " " + strProduct);
        }

        // 2. Leitura da TOC
        if (delFase) delFase("LENDO TOC DO DISCO…", "Geometria física de faixas do CD");
        CdromTocReader::CDROM_TOC objToc;
        bool blnTemToc = objLeitor.LerToc(objToc);

        if (!blnTemToc && blnIsAudio)
        {
            return Interromper("não consegui ler o índice (TOC) do disco. Há um CD de áudio na unidade?");
        }

        // 3. Leitura de CD-TEXT (se for CD-DA)
        std::optional<std::string> strCdTextAlbum;
        std::optional<std::string> strCdTextArtista;
        std::map<int, std::string> dicCdTextTitulos;
        std::map<int, std::string> dicCdTextArtistas;

        if (blnIsAudio)
        {
            if (delFase) delFase("LENDO CD-TEXT…", "Metadados ópticos no Lead-In");
            if (objLeitor.LerCdText(strCdTextAlbum, strCdTextArtista, dicCdTextTitulos, dicCdTextArtistas))
            {
                m_objLogger.RegistrarInfo("CD-TEXT detectado no Lead-In: Álbum='" + strCdTextAlbum.value_or("") + "', Artista='" +
                                          strCdTextArtista.value_or("") + "'");
            }
            else
            {
                m_objLogger.RegistrarDiagnostico("Nenhum CD-TEXT detectado no Lead-In.");
            }
        }

        // 3.1 Leitura de Subchannel Q (MCN / Codigo de Barras e ISRCs)
        std::string strMcn;
        std::map<int, std::string> dicIsrcs;
        if (blnIsAudio)
        {
            if (delFase) delFase("LENDO SUBCANAL Q…", "Código de barras (MCN) e ISRCs");
            if (objLeitor.LerMcn(strMcn))
            {
                m_objLogger.RegistrarInfo("Código de Barras (MCN/UPC) detectado no Subchannel Q: " + strMcn);
            }

            if (blnTemToc)
            {
                int intTotalFaixas = objToc.LastTrack - objToc.FirstTrack + 1;
                for (int i = 0; i < intTotalFaixas; i++)
                {
                    int intFaixaNum = objToc.TrackData[i].TrackNumber;
                    std::string strIsrcFaixa;
                    if (objLeitor.LerIsrc(intFaixaNum, strIsrcFaixa))
                    {
                        dicIsrcs[intFaixaNum] = strIsrcFaixa;
                    }
                }
            }
        }

        // 4. Determinacao dos limites de LBA
        uint32_t dwSetorSize = blnIsAudio ? 2352u : 2048u;
        uint32_t dwLbaInicio = 0;
        uint32_t dwLbaFim = 0;
        bool blnLeadOutExato = false;   // fim do audio dito pelo drive (indice completo), e nao estimado

        if (blnIsAudio && blnTemToc)
        {
            int intIndiceLeadOut = objToc.LastTrack - objToc.FirstTrack + 1;
            int intTotalFaixas = intIndiceLeadOut;

            // Em CDs multissessao (Enhanced CD / Copy Control), a area de audio da Sessao 1
            // termina no LBA onde inicia a primeira faixa de dados ou o lead-out da sessao de audio.
            // Tentar ler alem disso com comando SCSI READ CD (2352) gera erro 0x05/0x64 (ILLEGAL MODE FOR THIS TRACK).
            uint32_t dwLbaPrimeiraFaixaDados = 0;
            int intIndicePrimeiraFaixaDados = -1;
            int intQtdFaixasAudio = 0;

            // CD mixed-mode (jogos, CD-ROM com trilha): a faixa de dados vem ANTES do audio, no
            // LBA 0. Ela nao e musica e nao se le como audio - le-la assim eram horas de setores
            // "ilegiveis" virando zero. O audio comeca na primeira faixa de audio.
            int intPrimeiroAudio = -1;
            for (int i = 0; i < intTotalFaixas; i++)
            {
                const CdromTocReader::TRACK_DATA &td = objToc.TrackData[i];
                if ((td.Control_Adr & 0x04) == 0) { intPrimeiroAudio = LbaDe(td); break; }
            }
            if (intPrimeiroAudio > 0 && (objToc.TrackData[0].Control_Adr & 0x04) != 0)
            {
                dwLbaInicio = (uint32_t) intPrimeiroAudio;
                m_objLogger.RegistrarInfo("CD mixed-mode: a faixa 1 é de dados (LBA 0 a " + std::to_string(intPrimeiroAudio) +
                                          "). O áudio começa no LBA " + std::to_string(intPrimeiroAudio) + "; a faixa de dados não é lida.");
            }

            for (int i = 0; i < intTotalFaixas; i++)
            {
                const CdromTocReader::TRACK_DATA &td = objToc.TrackData[i];
                bool blnIsAudioTrack = (td.Control_Adr & 0x04) == 0;
                if (blnIsAudioTrack)
                {
                    intQtdFaixasAudio++;
                }
                else if (dwLbaPrimeiraFaixaDados == 0)
                {
                    int intStartLba = LbaDe(td);
                    if (intStartLba > 0 && intStartLba > (int) dwLbaInicio)
                    {
                        dwLbaPrimeiraFaixaDados = (uint32_t) intStartLba;
                        intIndicePrimeiraFaixaDados = i;
                    }
                }
            }

            // Com sessao de dados, o fim do audio e o lead-out da sessao 1 - e quem sabe onde ele
            // esta e o drive, no indice completo. A regra de distancia abaixo fica so para drive
            // que nao responda: ela confundia uma ultima faixa real de ate 8 s com a "faixa
            // fantasma" do CDS-300 e a descartava.
            std::optional<std::vector<SptiDiscReader::TSessao>> lstSessoes;
            if (dwLbaPrimeiraFaixaDados > 0) lstSessoes = objLeitor.LerSessoes();
            if (lstSessoes && lstSessoes->size() >= 2 && (*lstSessoes)[0].intLeadOut > 0)
            {
                const SptiDiscReader::TSessao &s0 = (*lstSessoes)[0];
                dwLbaFim = (uint32_t) s0.intLeadOut;
                blnLeadOutExato = true;
                int intAudioS1 = 0;
                for (auto &f : s0.lstFaixas)
                    if (f[2] == 1 && f[1] < s0.intLeadOut) intAudioS1++;
                m_objLogger.RegistrarInfo("Disco com " + std::to_string(lstSessoes->size()) + " sessões: o áudio da sessão 1 (" +
                                          std::to_string(intAudioS1) + " faixa(s)) termina no LBA " + std::to_string(dwLbaFim) + ", dito pelo drive.");
                // Faixa marcada como audio que comeca no lead-out ou depois nao e musica: e a
                // "faixa fantasma" do CDS-300 (ou ja e a sessao 2).
                for (int i = 0; i < intTotalFaixas; i++)
                {
                    const CdromTocReader::TRACK_DATA &td = objToc.TrackData[i];
                    int intIni = LbaDe(td);
                    if ((td.Control_Adr & 0x04) == 0 && intIni >= (int) dwLbaFim)
                        m_objLogger.RegistrarInfo("Faixa " + std::to_string(td.TrackNumber) + " (LBA " + std::to_string(intIni) +
                                                  ") está depois do fim da sessão de áudio: não é música (faixa fantasma de proteção ou sessão 2).");
                }
            }
            else if (dwLbaPrimeiraFaixaDados > 0)
            {
                dwLbaFim = dwLbaPrimeiraFaixaDados;

                // Protecao Copy Control (CDS-300 / Macrovision):
                // Discos protegidos por CDS-300 costumam inserir uma faixa fantasma (marcada na TOC falsamente como audio)
                // exatamente no ponto de transicao multissessao (Lead-Out da Sessao 1 + Lead-In da Sessao 2, cerca de 11.400 setores).
                // A fantasma fica no lead-out da sessao 1, a ~11.400 setores dos dados
                // (lead-out 6.750 + lead-in 4.500 + pre-gap 150). Uma faixa real tem no
                // minimo 4 s (300 setores), entao comeca a 11.700 ou mais: a janela vai
                // ate 11.600 para nunca pegar faixa real. (Antes ia ate 12.000 e
                // descartava a ultima faixa real quando ela tinha ate 8 s.)
                if (intIndicePrimeiraFaixaDados > 0)
                {
                    const CdromTocReader::TRACK_DATA &tdAnterior = objToc.TrackData[intIndicePrimeiraFaixaDados - 1];
                    int intLbaAnterior = LbaDe(tdAnterior);
                    if (intLbaAnterior > 0)
                    {
                        int64_t lngGapSessao = (int64_t) dwLbaPrimeiraFaixaDados - intLbaAnterior;
                        if (lngGapSessao >= 11000 && lngGapSessao <= 11600)
                        {
                            dwLbaFim = (uint32_t) intLbaAnterior;
                            m_objLogger.RegistrarInfo("Proteção Copy Control CDS-300 identificada: Faixa " + std::to_string(tdAnterior.TrackNumber) +
                                                      " (LBA " + std::to_string(intLbaAnterior) + ", gap de " + std::to_string(lngGapSessao) +
                                                      " setores) é zona de transição multissessão. Delimitando áudio estritamente em " +
                                                      std::to_string(intLbaAnterior) + ".");
                        }
                    }
                }

                if (dwLbaFim == dwLbaPrimeiraFaixaDados)
                {
                    m_objLogger.RegistrarInfo("CD Multissessão / Enhanced CD detectado: Sessão de áudio delimitada no LBA " + std::to_string(dwLbaFim) +
                                              " (" + std::to_string(intQtdFaixasAudio) + " faixas de áudio antes da primeira faixa de dados).");
                }
            }
            else if (intIndiceLeadOut >= 0 && intIndiceLeadOut < 100)
            {
                uint8_t m = objToc.TrackData[intIndiceLeadOut].Address_1;
                uint8_t s = objToc.TrackData[intIndiceLeadOut].Address_2;
                uint8_t f = objToc.TrackData[intIndiceLeadOut].Address_3;
                int intLeadOutLba = (m * 60 + s) * 75 + f - 150;
                if (intLeadOutLba > 0)
                {
                    dwLbaFim = (uint32_t) intLeadOutLba;
                }
            }
        }

        if (dwLbaFim == 0)
        {
            uint32_t dwMaxLbaCap, dwBlockSizeCap;
            if (objLeitor.LerCapacidade(dwMaxLbaCap, dwBlockSizeCap))
            {
                dwLbaFim = dwMaxLbaCap + 1;
            }
        }

        if (dwLbaFim == 0)
        {
            return Interromper("não consegui determinar o tamanho do disco.");
        }

        int64_t lngTamanhoTotalBytes = (int64_t) dwLbaFim * dwSetorSize;
        m_objLogger.RegistrarInfo("Mídia detectada: " + std::to_string(dwLbaFim) + " setores (" + F((double) lngTamanhoTotalBytes / (1024 * 1024), 2) + " MB).");

        // 5. Preparacao dos arquivos de destino
        if (ArquivoExiste(strDestinoArquivo)) ApagaArquivo(strDestinoArquivo);
        std::string strCaminhoCue = TrocaExtensao(strDestinoArquivo, ".cue");
        if (blnIsAudio && ArquivoExiste(strCaminhoCue)) ApagaArquivo(strCaminhoCue);

        // 6. Loop de Leitura Resiliente com Buffer Binary Halving
        const uint32_t DW_MAX_SECTORS_INITIAL = 64;
        uint32_t dwMaxSectorsLimit = DW_MAX_SECTORS_INITIAL;
        uint32_t dwCurrentSectors = dwMaxSectorsLimit;
        std::vector<uint8_t> arrBuffer(DW_MAX_SECTORS_INITIAL * dwSetorSize);
        std::vector<uint8_t> arrZeroSector(dwSetorSize);
        uint8_t *hPin = arrBuffer.data();

        int intSetoresMascarados = 0;
        int intTotalRetries = 0;
        int intFalhasCanalSeguidas = 0;
        int intMascaradosSeguidos = 0;
        int intSetoresRecuperados = 0;
        std::vector<uint32_t> lstLbasMascarados;
        uint32_t dwLbaAtual = dwLbaInicio;
        auto swCronometro = std::chrono::steady_clock::now();
        auto Decorridos = [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - swCronometro).count(); };
        int64_t lngUltimoReportMs = 0;

        {
            // FileStream(FileMode.Create, FileShare.None, 65536)
            TArquivo fsDestino(fopen(strDestinoArquivo.c_str(), "wb"));
            if (!fsDestino) throw IOException("não foi possível criar " + strDestinoArquivo + ": " + strerror(errno));
            setvbuf(fsDestino.get(), nullptr, _IOFBF, 65536);
            {
                // Mixed-mode: a faixa de dados nao e lida; o .bin fica com zeros ali para
                // continuar alinhado ao LBA (o CUE, o corte e o AccurateRip contam posicoes a
                // partir do LBA 0).
                if (dwLbaInicio > 0)
                {
                    if (ftruncate(fileno(fsDestino.get()), (off_t) dwLbaInicio * dwSetorSize) != 0)
                        throw IOException("erro ao gravar " + strDestinoArquivo + ": " + strerror(errno));
                    fseeko(fsDestino.get(), 0, SEEK_END);
                }
                while (dwLbaAtual < dwLbaFim)
                {
                    if (dwLbaAtual + dwCurrentSectors > dwLbaFim)
                    {
                        dwCurrentSectors = dwLbaFim - dwLbaAtual;
                    }

                    uint32_t dwBytesTransferidos;
                    uint8_t bScsiStatus, bSenseKey, bAsc, bAscq;
                    int intWin32Error;

                    bool blnSucessoLeitura = objLeitor.LerBlocoSpti(
                        hPin,
                        dwLbaAtual,
                        dwCurrentSectors,
                        blnIsAudio,
                        dwBytesTransferidos,
                        bScsiStatus,
                        bSenseKey,
                        bAsc,
                        bAscq,
                        intWin32Error);

                    if (blnSucessoLeitura && dwBytesTransferidos == (dwCurrentSectors * dwSetorSize))
                    {
                        Escreve(fsDestino.get(), arrBuffer.data(), dwBytesTransferidos, strDestinoArquivo);
                        dwLbaAtual += dwCurrentSectors;
                        intFalhasCanalSeguidas = 0;
                        intMascaradosSeguidos = 0;

                        int64_t lngDecorridoMs = (int64_t) (Decorridos() * 1000.0);
                        if (lngDecorridoMs - lngUltimoReportMs > 1000)
                        {
                            double dblProgresso = (double) dwLbaAtual / (double) dwLbaFim * 100.0;
                            double dblSegundos = Decorridos();
                            double dblMbLidos = (double) (dwLbaAtual * dwSetorSize) / (1024 * 1024);
                            double dblVelocidadeMbS = dblSegundos > 0 ? (dblMbLidos / dblSegundos) : 0.0;

                            // So no arquivo de auditoria: na tela, o andamento e do painel STATUS DA OPERACAO.
                            // LINUX: "SPTI:" no original.
                            m_objLogger.RegistrarDiagnostico("SG_IO: " + F(dblProgresso, 1) + "% lido (" + F(dblMbLidos, 1) + " MB / " +
                                                             F((double) lngTamanhoTotalBytes / (1024 * 1024), 1) + " MB) - " + F(dblVelocidadeMbS, 2) + " MB/s");
                            if (delProgresso)
                            {
                                delProgresso(dblProgresso, dblMbLidos, (double) lngTamanhoTotalBytes / (1024 * 1024), dblVelocidadeMbS, dwLbaAtual, dwLbaFim);
                            }
                            lngUltimoReportMs = (int64_t) (Decorridos() * 1000.0);
                        }

                        if (dwCurrentSectors < dwMaxSectorsLimit)
                        {
                            dwCurrentSectors = dwMaxSectorsLimit;
                        }
                    }
                    else
                    {
                        m_objLogger.RegistrarDiagnostico("Falha em LBA " + std::to_string(dwLbaAtual) + " (Bloco " + std::to_string(dwCurrentSectors) +
                                                         " setores). ScsiStatus: 0x" + X2(bScsiStatus) + ", SenseKey: 0x" + X2(bSenseKey) +
                                                         ", ASC: 0x" + X2(bAsc) + ", ASCQ: 0x" + X2(bAscq) + ", Win32: " + std::to_string(intWin32Error));

                        // Antes de qualquer resgate: se o caminho ate o disco caiu, esperar e
                        // tentar o mesmo bloco; nao voltando, parar.
                        if (EhFalhaDeCanal(intWin32Error, bSenseKey))
                        {
                            std::string strCanal = DescreverFalhaDeCanal(intWin32Error, bSenseKey, bAsc, bAscq);
                            if (++intFalhasCanalSeguidas > intTentativasCanal)
                                return Interromper("Extração interrompida no setor " + std::to_string(dwLbaAtual) + " de " + std::to_string(dwLbaFim) +
                                                   ": " + strCanal + ". A imagem incompleta foi descartada; extraia de novo.");
                            m_objLogger.RegistrarAviso("LBA " + std::to_string(dwLbaAtual) + ": " + strCanal + ". Tentando de novo em 1 s (" +
                                                       std::to_string(intFalhasCanalSeguidas) + "/" + std::to_string(intTentativasCanal) + ")...");
                            if (delFase) delFase("AGUARDANDO A UNIDADE…", strCanal);
                            std::this_thread::sleep_for(std::chrono::milliseconds(intEsperaCanalMs));
                            continue;
                        }

                        if (intWin32Error == 87 && dwCurrentSectors > 1)
                        {
                            dwMaxSectorsLimit = dwCurrentSectors / 2;
                            if (dwMaxSectorsLimit == 0) dwMaxSectorsLimit = 1;
                            dwCurrentSectors = dwMaxSectorsLimit;
                            m_objLogger.RegistrarDiagnostico("[HBA Hardware] Controlador rejeitou bloco. Redefinindo teto para " + std::to_string(dwMaxSectorsLimit) +
                                                             " setores (" + std::to_string((dwMaxSectorsLimit * dwSetorSize) / 1024) + " KB)...");
                            continue;
                        }

                        if (dwCurrentSectors > 1)
                        {
                            uint32_t dwAnterior = dwCurrentSectors;
                            dwCurrentSectors /= 2;
                            if (dwCurrentSectors == 0) dwCurrentSectors = 1;
                            m_objLogger.RegistrarDiagnostico("[Buffer Halving] LBA " + std::to_string(dwLbaAtual) + ": Reduzindo buffer de " +
                                                             std::to_string(dwAnterior) + " para " + std::to_string(dwCurrentSectors) + " setores...");
                            continue;
                        }

                        // Se em bloco unitario (1 setor) o hardware optico retornar SenseKey 0x05 e ASC 0x64,
                        // a controladora esta declarando que o comando de leitura de audio (2352) e ilegal nesta area.
                        // Isso sinaliza o termino definitivo da sessao de audio Red Book (inicio do gap multissessao / dados).
                        if (blnIsAudio && bSenseKey == 0x05 && bAsc == 0x64)
                        {
                            m_objLogger.RegistrarInfo("Fim físico da sessão de áudio identificado no LBA " + std::to_string(dwLbaAtual) +
                                                      " (SCSI 0x05/0x64: ILLEGAL MODE FOR THIS TRACK). Concluindo aquisição de áudio.");
                            dwLbaFim = dwLbaAtual;
                            break;
                        }

                        m_objLogger.RegistrarAviso("Setor problemático isolado em LBA " + std::to_string(dwLbaAtual) +
                                                   ". Iniciando rotina de resgate com descarte de cache...");

                        bool blnRecuperouSetor = false;
                        for (int intTentativa = 1; intTentativa <= 3; intTentativa++)
                        {
                            intTotalRetries++;
                            m_objLogger.RegistrarDiagnostico("Tentativa " + std::to_string(intTentativa) + "/3 para LBA " + std::to_string(dwLbaAtual) +
                                                             ": Purgando cache óptico...");
                            objLeitor.DescartarCacheDrive(dwLbaAtual, dwLbaFim);
                            std::this_thread::sleep_for(std::chrono::milliseconds(200));

                            blnSucessoLeitura = objLeitor.LerBlocoSpti(
                                hPin,
                                dwLbaAtual,
                                1,
                                blnIsAudio,
                                dwBytesTransferidos,
                                bScsiStatus,
                                bSenseKey,
                                bAsc,
                                bAscq,
                                intWin32Error);

                            if (blnSucessoLeitura && dwBytesTransferidos == dwSetorSize)
                            {
                                m_objLogger.RegistrarInfo("Sucesso! LBA " + std::to_string(dwLbaAtual) + " recuperado após descarte de cache na tentativa " +
                                                          std::to_string(intTentativa) + ".");
                                Escreve(fsDestino.get(), arrBuffer.data(), dwSetorSize, strDestinoArquivo);
                                dwLbaAtual++;
                                blnRecuperouSetor = true;
                                intSetoresRecuperados++;
                                intMascaradosSeguidos = 0;
                                break;
                            }
                            if (EhFalhaDeCanal(intWin32Error, bSenseKey)) break;
                        }

                        // O canal caiu durante o resgate: o setor nao e culpado. Volta ao laco, que
                        // espera a unidade ou interrompe. Conta como tentativa de canal, para nao
                        // girar para sempre num drive que alterna entre setor ruim e "nao pronto".
                        if (!blnRecuperouSetor && EhFalhaDeCanal(intWin32Error, bSenseKey))
                        {
                            std::string strCanal = DescreverFalhaDeCanal(intWin32Error, bSenseKey, bAsc, bAscq);
                            if (++intFalhasCanalSeguidas > intTentativasCanal)
                                return Interromper("Extração interrompida no setor " + std::to_string(dwLbaAtual) + " de " + std::to_string(dwLbaFim) +
                                                   ": " + strCanal + ". A imagem incompleta foi descartada; extraia de novo.");
                            std::this_thread::sleep_for(std::chrono::milliseconds(intEsperaCanalMs));
                            dwCurrentSectors = 1;
                            continue;
                        }

                        if (!blnRecuperouSetor)
                        {
                            intSetoresMascarados++;
                            lstLbasMascarados.push_back(dwLbaAtual);
                            m_objLogger.RegistrarAviso("LBA " + std::to_string(dwLbaAtual) + " irrecuperável. Mascarando com " + std::to_string(dwSetorSize) +
                                                       " bytes de silêncio para salvar imagem.");
                            Escreve(fsDestino.get(), arrZeroSector.data(), dwSetorSize, strDestinoArquivo);
                            dwLbaAtual++;
                            if (++intMascaradosSeguidos >= intLimiteMascaradosSeguidos)
                                return Interromper("Extração interrompida: " + std::to_string(intMascaradosSeguidos) + " setores seguidos ilegíveis (" +
                                                   std::to_string(intMascaradosSeguidos / TRelatorioExtracao::SetoresPorSegundo) + " s de áudio) até o LBA " +
                                                   std::to_string(dwLbaAtual) + ". " +
                                                   "O disco está ilegível nesta região; limpe-o ou tente outra unidade. A imagem incompleta foi descartada.");
                        }

                        dwCurrentSectors = dwMaxSectorsLimit;
                    }
                }
            }
            // o Dispose do FileStream: o que estiver no buffer vai ao arquivo
            if (fflush(fsDestino.get()) != 0) throw IOException("erro ao gravar " + strDestinoArquivo + ": " + strerror(errno));
        }

        double dblTotalSegundos = Decorridos();
        m_objLogger.RegistrarInfo("Extração física concluída em " + F(dblTotalSegundos, 1) + "s! Setores: " + std::to_string(dwLbaFim) +
                                  ", Mascarados: " + std::to_string(intSetoresMascarados) + ", Retries: " + std::to_string(intTotalRetries));
        if (delProgresso)
        {
            delProgresso(100.0, (double) lngTamanhoTotalBytes / (1024 * 1024), (double) lngTamanhoTotalBytes / (1024 * 1024), 0.0, dwLbaFim, dwLbaFim);
        }

        // O que nao se leu sai daqui para a area de trabalho, por faixa: a aprovacao e o ledger
        // precisam saber, mesmo com o ARCA reaberto.
        auto objRelatorio = std::make_shared<TRelatorioExtracao>();
        objRelatorio->intSetoresMascarados = intSetoresMascarados;
        objRelatorio->intSetoresRecuperados = intSetoresRecuperados;
        if (!lstLbasMascarados.empty())
        {
            std::vector<std::pair<int, uint32_t>> lstInicios = blnTemToc ? InicioDasFaixas(objToc) : std::vector<std::pair<int, uint32_t>>();
            for (uint32_t dwLba : lstLbasMascarados)
            {
                int intFaixa = FaixaDoSetor(lstInicios, dwLba);
                objRelatorio->dicMascaradosPorFaixa[intFaixa] += 1;
            }
            m_objLogger.RegistrarAviso("EXTRAÇÃO NÃO EXATA: " + std::to_string(intSetoresMascarados) + " setor(es) ilegível(is) substituído(s) por silêncio — " +
                                       objRelatorio->ResumoPorFaixa() + ".");
        }
        try { objRelatorio->Gravar(strPastaDestino); }
        catch (const std::exception &exRel) { m_objLogger.RegistrarAviso(std::string("Não consegui gravar ") + TRelatorioExtracao::NomeArquivo + ": " + exRel.what()); }
        m_objRelatorio = objRelatorio;

        // 7. Geracao do Arquivo CUE
        if (blnIsAudio && blnTemToc)
        {
            if (delFase) delFase("GERANDO ARQUIVO CUE…", "Gravando índice de faixas e metadados no CUE");
            GerarArquivoCue(strCaminhoCue, strDestinoArquivo, objToc, strCdTextAlbum, strCdTextArtista, dicCdTextTitulos, dicCdTextArtistas, strMcn,
                            dicIsrcs, dwLbaFim, blnLeadOutExato);
        }

        return true;
    }
}

void SptiImageExtractor::GerarArquivoCue(const std::string &strCaminhoCue, const std::string &strCaminhoBin,
                                         const CdromTocReader::CDROM_TOC &objToc, const std::optional<std::string> &strAlbumCdText,
                                         const std::optional<std::string> &strArtistaCdText, const std::map<int, std::string> &dicTitulos,
                                         const std::map<int, std::string> &dicArtistas, const std::string &strMcn,
                                         const std::map<int, std::string> &dicIsrcs, uint32_t dwLbaFimAudio, bool blnLeadOutExato)
{
    m_objLogger.RegistrarDiagnostico("Gerando arquivo CUE Sheet em: " + strCaminhoCue);

    // LINUX: as linhas terminam em \r\n, como o StringBuilder.AppendLine do Windows gravava.
    std::string sbCue;
    sbCue += "REM SESSION 1\r\n";

    if (!strMcn.empty())
    {
        sbCue += "CATALOG " + strMcn + "\r\n";
    }

    if (strAlbumCdText && !strAlbumCdText->empty())
    {
        sbCue += "TITLE \"" + Substitui(*strAlbumCdText, "\"", "") + "\"\r\n";
    }
    if (strArtistaCdText && !strArtistaCdText->empty())
    {
        sbCue += "PERFORMER \"" + Substitui(*strArtistaCdText, "\"", "") + "\"\r\n";
    }

    std::string strNomeArquivoBin = NomeDoArquivo(strCaminhoBin);
    sbCue += "FILE \"" + strNomeArquivoBin + "\" BINARY\r\n";

    int intTotalFaixas = objToc.LastTrack - objToc.FirstTrack + 1;
    int intSessaoAtual = 1;
    int intPrimeiroAudioCue = INT_MAX;
    for (int i = 0; i < intTotalFaixas; i++)
    {
        const CdromTocReader::TRACK_DATA &tdA = objToc.TrackData[i];
        if ((tdA.Control_Adr & 0x04) == 0) { intPrimeiroAudioCue = std::max(0, LbaDe(tdA)); break; }
    }
    bool blnLeadOutSessao1Inserido = false;

    for (int i = 0; i < intTotalFaixas; i++)
    {
        const CdromTocReader::TRACK_DATA &td = objToc.TrackData[i];
        int intTrackNum = td.TrackNumber;
        bool blnIsAudioTrack = (td.Control_Adr & 0x04) == 0;

        int intStartLba = LbaDe(td);
        if (intStartLba < 0) intStartLba = 0;

        int intMin = (intStartLba / 75) / 60;
        int intSec = (intStartLba / 75) % 60;
        int intFrame = intStartLba % 75;

        // Em CDs multissessao / Copy Control, qualquer faixa que inicie a partir de dwLbaFimAudio
        // pertence a Sessao 2 ou a zona de transicao de dados.
        // So dados DEPOIS do audio abrem a sessao 2 (CD Extra). Dados antes do audio
        // (mixed-mode) sao a faixa 1 da propria sessao 1.
        bool blnDadosAntesDoAudio = !blnIsAudioTrack && intStartLba < intPrimeiroAudioCue;
        if ((!blnIsAudioTrack || (dwLbaFimAudio > 0 && (uint32_t) intStartLba >= dwLbaFimAudio)) && !blnDadosAntesDoAudio && intSessaoAtual == 1)
        {
            if (!blnLeadOutSessao1Inserido)
            {
                int intLoMin = ((int) dwLbaFimAudio / 75) / 60;
                int intLoSec = ((int) dwLbaFimAudio / 75) % 60;
                int intLoFrame = (int) dwLbaFimAudio % 75;
                sbCue += "REM LEAD-OUT " + D2(intLoMin) + ":" + D2(intLoSec) + ":" + D2(intLoFrame) + "\r\n";
                // O lead-out acima veio do drive (indice completo), nao de estimativa: quem le o
                // CUE pode confiar nele como fim exato do audio.
                if (blnLeadOutExato) sbCue += "REM ARCA-LEADOUT-EXATO\r\n";
                blnLeadOutSessao1Inserido = true;
            }
            intSessaoAtual = 2;
            sbCue += "REM SESSION " + std::to_string(intSessaoAtual) + "\r\n";
        }

        // Faixas da Sessao 2 ou alem do limite de audio sao marcadas como MODE2/2352 para nao serem tratadas como audio
        std::string strTipoFaixa = blnDadosAntesDoAudio ? "MODE1/2352"
            : (blnIsAudioTrack && (dwLbaFimAudio == 0 || (uint32_t) intStartLba < dwLbaFimAudio)) ? "AUDIO" : "MODE2/2352";
        sbCue += "  TRACK " + D2(intTrackNum) + " " + strTipoFaixa + "\r\n";

        auto itT = dicTitulos.find(intTrackNum);
        if (itT != dicTitulos.end() && !itT->second.empty())
        {
            sbCue += "    TITLE \"" + Substitui(itT->second, "\"", "") + "\"\r\n";
        }
        auto itA = dicArtistas.find(intTrackNum);
        if (itA != dicArtistas.end() && !itA->second.empty())
        {
            sbCue += "    PERFORMER \"" + Substitui(itA->second, "\"", "") + "\"\r\n";
        }
        auto itI = dicIsrcs.find(intTrackNum);
        if (itI != dicIsrcs.end() && !itI->second.empty())
        {
            sbCue += "    ISRC " + itI->second + "\r\n";
        }

        sbCue += "    INDEX 01 " + D2(intMin) + ":" + D2(intSec) + ":" + D2(intFrame) + "\r\n";
    }

    int intIndiceLeadOut = intTotalFaixas;
    if (intIndiceLeadOut >= 0 && intIndiceLeadOut < 100)
    {
        const CdromTocReader::TRACK_DATA &tdLo = objToc.TrackData[intIndiceLeadOut];
        int intLoLba = LbaDe(tdLo);
        if (intLoLba < 0) intLoLba = 0;
        int intLoMin = (intLoLba / 75) / 60;
        int intLoSec = (intLoLba / 75) % 60;
        int intLoFrame = intLoLba % 75;

        if (!blnLeadOutSessao1Inserido)
        {
            int intRealLoLba = (dwLbaFimAudio > 0) ? (int) dwLbaFimAudio : intLoLba;
            int intRealMin = (intRealLoLba / 75) / 60;
            int intRealSec = (intRealLoLba / 75) % 60;
            int intRealFrame = intRealLoLba % 75;
            sbCue += "REM LEAD-OUT " + D2(intRealMin) + ":" + D2(intRealSec) + ":" + D2(intRealFrame) + "\r\n";
        }
        else
        {
            sbCue += "REM LEAD-OUT-SESSION2 " + D2(intLoMin) + ":" + D2(intLoSec) + ":" + D2(intLoFrame) + "\r\n";
        }
    }

    // File.WriteAllText(..., Encoding.UTF8): UTF-8 com BOM
    GravaTexto(strCaminhoCue, "\xEF\xBB\xBF" + sbCue);
    m_objLogger.RegistrarInfo("Arquivo CUE Sheet gerado com sucesso em UTF-8.");
}

} // namespace ARCA::Interop

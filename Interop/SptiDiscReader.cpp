// ARCA no xOS - Interop/SptiDiscReader (de Interop\SptiDiscReader.cs).
#include "SptiDiscReader.h"

#include "Modelo/AcquisitionLogger.h"

#include "Base/Arquivo.h"
#include "Base/Texto.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iconv.h>
#include <linux/fs.h>
#include <scsi/sg.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

namespace ARCA::Interop {

using namespace ARCA::Base;

namespace {

// Encoding.ASCII.GetString(arr, p, n): byte acima de 0x7F vira '?'
std::string Ascii(const uint8_t *p, size_t n)
{
    std::string r;
    for (size_t i = 0; i < n; i++) r += p[i] < 0x80 ? (char) p[i] : '?';
    return r;
}

// LINUX: Encoding.GetEncoding(932/949/936).GetString pelo iconv da musl; byte que nao forma
// caractere vira U+FFFD, como o substituto padrao do .NET. false se o iconv nao conhece a
// codificacao (o original caia no Latin-1 quando GetEncoding falhava).
bool PorIconv(const char *strCodificacao, const std::vector<uint8_t> &arr, std::string &strSaida)
{
    iconv_t cd = iconv_open("UTF-8", strCodificacao);
    if (cd == (iconv_t) -1) return false;
    strSaida.clear();
    size_t i = 0;
    while (i < arr.size())
    {
        char *ent = (char *) arr.data() + i;
        size_t nEnt = arr.size() - i;
        char buf[256];
        char *sai = buf;
        size_t nSai = sizeof buf;
        size_t r = iconv(cd, &ent, &nEnt, &sai, &nSai);
        strSaida.append(buf, sai - buf);
        i = arr.size() - nEnt;
        if (r == (size_t) -1)
        {
            if (errno == E2BIG) continue;
            strSaida += "\xEF\xBF\xBD";        // U+FFFD
            i++;
            iconv(cd, nullptr, nullptr, nullptr, nullptr);
        }
    }
    iconv_close(cd);
    return true;
}

// enc.GetString(bytes) da codificacao escolhida pelo pacote 0x8F
std::string Decodificar(const std::vector<uint8_t> &arr, int intCodigo)
{
    std::string s;
    if (intCodigo == 0x80 && (PorIconv("SHIFT_JIS", arr, s))) return s;
    if (intCodigo == 0x81 && (PorIconv("CP949", arr, s) || PorIconv("EUC-KR", arr, s))) return s;
    if (intCodigo == 0x82 && (PorIconv("GBK", arr, s))) return s;
    if (intCodigo == 0x01) return Ascii(arr.data(), arr.size());
    return DeLatin1(arr.data(), arr.size());
}

// LINUX: errno do ioctl -> o codigo que o Windows daria (ver o cabecalho)
int CodigoWin32(int e)
{
    switch (e)
    {
        case EINVAL: case ENOMEM: case EOVERFLOW: return 87;
        case EACCES: case EPERM: return 5;
        case ETIMEDOUT: return 121;
        case ENOMEDIUM: case ENXIO: case ENODEV: return 21;
        case EBUSY: return 170;
        default: return 1117;
    }
}

// string.Format("{0:D2}")
std::string D2(int n)
{
    char b[16];
    snprintf(b, sizeof b, "%02d", n);
    return b;
}

} // namespace

SptiDiscReader::SptiDiscReader(Modelo::AcquisitionLogger &objLogger) : m_objLogger(objLogger) {}

SptiDiscReader::~SptiDiscReader()
{
    FecharUnidade();
}

bool SptiDiscReader::AbrirUnidade(const std::string &strLetraDrive)
{
    FecharUnidade();

    // LINUX: no original, string.Format(@"\\.\{0}:", letra). Aqui o dispositivo.
    std::string strCaminhoDevice = ComecaCom(strLetraDrive, "/") ? strLetraDrive : DispositivoCd();
    m_objLogger.RegistrarDiagnostico("Tentando abrir handle direto para " + strCaminhoDevice + "...");

    // LINUX: GENERIC_READ | GENERIC_WRITE = O_RDWR; O_NONBLOCK abre mesmo sem disco, como o
    // CreateFile do Windows.
    m_hDeviceHandle = open(strCaminhoDevice.c_str(), O_RDWR | O_NONBLOCK);
    if (m_hDeviceHandle < 0)
    {
        int intErroWin32 = errno;
        // LINUX: o numero e o errno do Linux (no original, Marshal.GetLastWin32Error()).
        m_objLogger.RegistrarDiagnostico("Abertura com GENERIC_WRITE falhou (errno: " + std::to_string(intErroWin32) + ", " +
                                         strerror(intErroWin32) + "). Tentando GENERIC_READ...");

        m_hDeviceHandle = open(strCaminhoDevice.c_str(), O_RDONLY | O_NONBLOCK);
        if (m_hDeviceHandle < 0)
        {
            intErroWin32 = errno;
            m_objLogger.RegistrarErro("Não foi possível acessar a unidade " + strCaminhoDevice + ". Código errno: " +
                                      std::to_string(intErroWin32) + " (" + strerror(intErroWin32) + ")");
            return false;
        }
    }

    // LINUX: o teto de transferencia da controladora (ver o cabecalho)
    unsigned short intSetores512 = 0;      // o kernel grava um unsigned short
    m_dwMaxTransferencia = ioctl(m_hDeviceHandle, BLKSECTGET, &intSetores512) == 0 && intSetores512 > 0 ? (uint32_t) intSetores512 * 512u : 0;

    // LINUX: "Handle SPTI" no original; aqui o descritor do SG_IO.
    m_objLogger.RegistrarDiagnostico("Handle SG_IO aberto com sucesso em " + strCaminhoDevice + ".");
    return true;
}

void SptiDiscReader::FecharUnidade()
{
    if (IsOpen())
    {
        close(m_hDeviceHandle);
        m_hDeviceHandle = -1;
        m_objLogger.RegistrarDiagnostico("Handle SG_IO fechado.");
    }
}

bool SptiDiscReader::ObterInquiryHardware(std::string &strVendor, std::string &strProduct, std::string &strRevision)
{
    strVendor = "Desconhecido";
    strProduct = "Desconhecido";
    strRevision = "N/A";

    if (!IsOpen()) return false;

    uint8_t arrInqBuffer[36] = {};
    SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER sptd = CriarSptdBasico(sizeof arrInqBuffer, arrInqBuffer, 10);
    sptd.sptd.CdbLength = 6;
    sptd.sptd.Cdb[0] = 0x12; // INQUIRY
    sptd.sptd.Cdb[4] = (uint8_t) sizeof arrInqBuffer;

    int intErro;
    bool blnOk = DeviceIoControlSpti(sptd, intErro);

    if (blnOk && sptd.sptd.ScsiStatus == 0)
    {
        strVendor = Trim(Ascii(arrInqBuffer + 8, 8));
        strProduct = Trim(Ascii(arrInqBuffer + 16, 16));
        strRevision = Trim(Ascii(arrInqBuffer + 32, 4));
        m_objLogger.RegistrarDiagnostico("Hardware Detectado: [" + strVendor + "] Modelo: [" + strProduct + "] Firmware: [" + strRevision + "]");
        return true;
    }
    return false;
}

bool SptiDiscReader::LerToc(CdromTocReader::CDROM_TOC &objToc)
{
    objToc = CdromTocReader::CDROM_TOC();
    if (!IsOpen()) return false;

    for (int intRetry = 0; intRetry < 5; intRetry++)
    {
        // LINUX: IOCTL_CDROM_READ_TOC = READ TOC formato 0 em MSF (CdromTocReader::LerTocDoDescritor)
        if (CdromTocReader::LerTocDoDescritor(m_hDeviceHandle, objToc))
        {
            m_objLogger.RegistrarDiagnostico("TOC lida com sucesso: Primeira Faixa=" + std::to_string(objToc.FirstTrack) +
                                             ", Última Faixa=" + std::to_string(objToc.LastTrack));
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    m_objLogger.RegistrarErro("Falha ao ler TOC da mídia óptica via IOCTL_CDROM_READ_TOC.");
    return false;
}

bool SptiDiscReader::LerCapacidade(uint32_t &dwMaxLba, uint32_t &dwBlockSize)
{
    dwMaxLba = 0;
    dwBlockSize = 2048;
    if (!IsOpen()) return false;

    uint8_t arrCapBuffer[8] = {};
    SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER sptd = CriarSptdBasico(sizeof arrCapBuffer, arrCapBuffer, 10);
    sptd.sptd.CdbLength = 10;
    sptd.sptd.Cdb[0] = 0x25; // READ CAPACITY (10)

    int intErro;
    bool blnOk = DeviceIoControlSpti(sptd, intErro);

    if (blnOk && sptd.sptd.ScsiStatus == 0)
    {
        dwMaxLba = ((uint32_t) arrCapBuffer[0] << 24) | ((uint32_t) arrCapBuffer[1] << 16) | ((uint32_t) arrCapBuffer[2] << 8) | (uint32_t) arrCapBuffer[3];
        dwBlockSize = ((uint32_t) arrCapBuffer[4] << 24) | ((uint32_t) arrCapBuffer[5] << 16) | ((uint32_t) arrCapBuffer[6] << 8) | (uint32_t) arrCapBuffer[7];
        m_objLogger.RegistrarDiagnostico("READ CAPACITY: MaxLBA=" + std::to_string(dwMaxLba) + ", BlockSize=" + std::to_string(dwBlockSize));
        return true;
    }
    return false;
}

std::optional<std::vector<SptiDiscReader::TSessao>> SptiDiscReader::LerSessoes()
{
    if (!IsOpen()) return std::nullopt;
    std::vector<uint8_t> arr(4096);
    try
    {
        SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER sptd = CriarSptdBasico((int) arr.size(), arr.data(), 10);
        sptd.sptd.CdbLength = 10;
        sptd.sptd.Cdb[0] = 0x43;                 // READ TOC/PMA/ATIP
        sptd.sptd.Cdb[1] = 0x02;                 // MSF
        sptd.sptd.Cdb[2] = 0x02;                 // formato 2: Full TOC
        sptd.sptd.Cdb[6] = 0x01;                 // a partir da sessao 1
        sptd.sptd.Cdb[7] = (uint8_t) (arr.size() >> 8);
        sptd.sptd.Cdb[8] = (uint8_t) (arr.size() & 0xFF);
        int intErro;
        if (!DeviceIoControlSpti(sptd, intErro) || sptd.sptd.ScsiStatus != 0)
        {
            m_objLogger.RegistrarDiagnostico("O drive não respondeu ao índice completo (READ TOC formato 2).");
            return std::nullopt;
        }

        int intTotal = std::min((int) arr.size(), ((arr[0] << 8) | arr[1]) + 2);
        std::map<int, TSessao> dic;
        for (int p = 4; p + 11 <= intTotal; p += 11)
        {
            int intSessao = arr[p];
            int intAdr = arr[p + 1] >> 4, intControle = arr[p + 1] & 0x0F;
            int intPonto = arr[p + 3];
            if (intAdr != 1) continue;           // so descritores de posicao
            int intLba = (arr[p + 8] * 60 + arr[p + 9]) * 75 + arr[p + 10] - 150;
            auto it = dic.find(intSessao);
            if (it == dic.end()) { TSessao s; s.intNumero = intSessao; it = dic.emplace(intSessao, s).first; }
            TSessao &s = it->second;
            if (intPonto >= 1 && intPonto <= 99) s.lstFaixas.push_back({ intPonto, intLba, (intControle & 0x04) == 0 ? 1 : 0 });
            else if (intPonto == 0xA2) s.intLeadOut = intLba;
        }
        std::vector<TSessao> lst;
        for (auto &kv : dic) lst.push_back(kv.second);
        for (const TSessao &s : lst)
            m_objLogger.RegistrarDiagnostico("Sessão " + std::to_string(s.intNumero) + ": " + std::to_string(s.lstFaixas.size()) +
                                             " faixa(s), lead-out no LBA " + std::to_string(s.intLeadOut) + ".");
        if (!lst.empty()) return lst;
        return std::nullopt;
    }
    catch (...) { return std::nullopt; }
}

bool SptiDiscReader::LerCdText(std::optional<std::string> &strAlbum, std::optional<std::string> &strArtista,
                               std::map<int, std::string> &dicTitulos, std::map<int, std::string> &dicArtistas)
{
    strAlbum.reset();
    strArtista.reset();
    dicTitulos.clear();
    dicArtistas.clear();

    if (!IsOpen()) return false;

    std::vector<uint8_t> arrCdTextBuffer(TamanhoMaximoCdText);
    bool blnLeuComSucesso = false;
    int intLength = 0;

    {
        SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER sptd = CriarSptdBasico((int) arrCdTextBuffer.size(), arrCdTextBuffer.data(), 10);
        sptd.sptd.CdbLength = 10;
        sptd.sptd.Cdb[0] = 0x43;
        sptd.sptd.Cdb[1] = 0x02;
        sptd.sptd.Cdb[2] = 0x05;
        sptd.sptd.Cdb[7] = (uint8_t) ((arrCdTextBuffer.size() >> 8) & 0xFF);
        sptd.sptd.Cdb[8] = (uint8_t) (arrCdTextBuffer.size() & 0xFF);

        int intErro;
        if (DeviceIoControlSpti(sptd, intErro) && sptd.sptd.ScsiStatus == 0)
        {
            intLength = (arrCdTextBuffer[0] << 8) | arrCdTextBuffer[1];
            blnLeuComSucesso = intLength > 2 && intLength + 2 <= (int) arrCdTextBuffer.size();
        }

        if (!blnLeuComSucesso)
        {
            // LINUX: o original tentava de novo pelo IOCTL_CDROM_READ_TOC_EX (Format = 5,
            // SessionTrack = 0, Msf = 0), que o driver do Windows transforma neste mesmo READ TOC
            // formato 5, so que sem o bit MSF. Aqui vai esse comando direto.
            std::fill(arrCdTextBuffer.begin(), arrCdTextBuffer.end(), 0);
            SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER sptdEx = CriarSptdBasico((int) arrCdTextBuffer.size(), arrCdTextBuffer.data(), 10);
            sptdEx.sptd.CdbLength = 10;
            sptdEx.sptd.Cdb[0] = 0x43;
            sptdEx.sptd.Cdb[1] = 0x00;
            sptdEx.sptd.Cdb[2] = 0x05;
            sptdEx.sptd.Cdb[7] = (uint8_t) ((arrCdTextBuffer.size() >> 8) & 0xFF);
            sptdEx.sptd.Cdb[8] = (uint8_t) (arrCdTextBuffer.size() & 0xFF);

            if (DeviceIoControlSpti(sptdEx, intErro) && sptdEx.sptd.ScsiStatus == 0)
            {
                intLength = (arrCdTextBuffer[0] << 8) | arrCdTextBuffer[1];
                blnLeuComSucesso = intLength > 2 && intLength + 2 <= (int) arrCdTextBuffer.size();
            }
        }
    }

    if (!blnLeuComSucesso) return false;

    int intDescartados;
    std::string strCodificacao;
    DecodificarCdText(arrCdTextBuffer, intLength + 2, strAlbum, strArtista, dicTitulos, dicArtistas, intDescartados, strCodificacao);
    if (intDescartados > 0)
        m_objLogger.RegistrarAviso("CD-TEXT: " + std::to_string(intDescartados) +
                                   " pacote(s) com CRC errado foram descartados; os textos que eles tocavam ficaram de fora em vez de saírem desalinhados.");
    m_objLogger.RegistrarDiagnostico("CD-TEXT decodificado (" + strCodificacao + "): Álbum='" + strAlbum.value_or("") + "', Artista='" +
                                     strArtista.value_or("") + "', Faixas=" + std::to_string(dicTitulos.size()));
    return strAlbum.has_value() || strArtista.has_value() || !dicTitulos.empty();
}

void SptiDiscReader::DecodificarCdText(const std::vector<uint8_t> &arr, int intTotal, std::optional<std::string> &strAlbum,
                                       std::optional<std::string> &strArtista, std::map<int, std::string> &dicTitulos,
                                       std::map<int, std::string> &dicArtistas, int &intDescartados, std::string &strCodificacao)
{
    strAlbum.reset();
    strArtista.reset();
    intDescartados = 0;
    intTotal = std::min(intTotal, (int) arr.size());

    // Salvaguarda: se NENHUM pacote passar no CRC, o drive nao faz a conta padrao (ou a entrega de
    // outro jeito) - descartar tudo perderia o CD-TEXT inteiro por um detalhe do drive. Ai os
    // pacotes valem como vem.
    int intBons = 0, intRuins = 0;
    for (int p = 4; p + 18 <= intTotal; p += 18) { if (CrcPacoteOk(arr, p)) intBons++; else intRuins++; }
    bool blnIgnorarCrc = intBons == 0 && intRuins > 0;

    // Codificacao: pacote 0x8F com "faixa" 0 do bloco 0, primeiro byte de dados.
    int intCodigo = 0x00;
    for (int p = 4; p + 18 <= intTotal; p += 18)
        if (arr[p] == 0x8F && (arr[p + 1] & 0x7F) == 0 && ((arr[p + 3] >> 4) & 0x07) == 0 && (blnIgnorarCrc || CrcPacoteOk(arr, p)))
        {
            intCodigo = arr[p + 4];
            break;
        }
    bool blnDuplo = intCodigo >= 0x80;
    // LINUX: o Encoding do .NET (932, 949, 936, ASCII, 28591) vira a escolha dentro de
    // Decodificar(), pelo iconv; codificacao que ele nao tenha cai no Latin-1, como o catch.
    strCodificacao = intCodigo == 0x80 ? "Shift-JIS" : intCodigo == 0x81 ? "coreano" : intCodigo == 0x82 ? "chinês"
                   : intCodigo == 0x01 ? "ASCII" : "Latin-1";

    std::map<int, std::string> dicTitulosTodos;
    std::map<int, std::string> dicArtistasTodos;
    intDescartados += DecodificarTipo(arr, intTotal, 0x80, blnDuplo, intCodigo, dicTitulosTodos, blnIgnorarCrc);
    intDescartados += DecodificarTipo(arr, intTotal, 0x81, blnDuplo, intCodigo, dicArtistasTodos, blnIgnorarCrc);
    if (blnIgnorarCrc) strCodificacao += ", CRC do drive não conferível: pacotes aceitos como vieram";

    for (auto &kv : dicTitulosTodos)
        if (kv.first == 0) strAlbum = kv.second; else dicTitulos[kv.first] = kv.second;
    for (auto &kv : dicArtistasTodos)
        if (kv.first == 0) strArtista = kv.second; else dicArtistas[kv.first] = kv.second;
}

int SptiDiscReader::DecodificarTipo(const std::vector<uint8_t> &arr, int intTotal, uint8_t bTipo, bool blnDuplo, int intCodigo,
                                    std::map<int, std::string> &dicSaida, bool blnIgnorarCrc)
{
    int intDescartados = 0;
    int intLargura = blnDuplo ? 2 : 1;
    std::vector<uint8_t> lstAtual;
    int intFaixa = -1;
    bool blnValido = false, blnSincronizado = false;
    std::optional<std::string> strAnterior;

    for (int p = 4; p + 18 <= intTotal; p += 18)
    {
        if (arr[p] != bTipo || ((arr[p + 3] >> 4) & 0x07) != 0) continue;
        if (!blnIgnorarCrc && !CrcPacoteOk(arr, p)) { intDescartados++; blnSincronizado = false; continue; }

        if (!blnSincronizado)
        {
            // Recomeca pelo que o proprio pacote diz: faixa do primeiro caractere e quantos
            // caracteres desse texto vieram antes. Se vieram (posicao > 0), o comeco se perdeu:
            // o texto dessa faixa nao sai.
            intFaixa = arr[p + 1] & 0x7F;
            blnValido = (arr[p + 3] & 0x0F) == 0;
            lstAtual.clear();
            blnSincronizado = true;
        }

        for (int c = 0; c + intLargura <= 12; c += intLargura)
        {
            bool blnFim = arr[p + 4 + c] == 0 && (!blnDuplo || arr[p + 5 + c] == 0);
            if (!blnFim)
            {
                for (int k = 0; k < intLargura; k++) lstAtual.push_back(arr[p + 4 + c + k]);
                continue;
            }
            if (blnValido && intFaixa >= 0)
            {
                // TAB: "igual a faixa anterior". Conferido antes do Trim, que o apagaria.
                std::string strBruto = Decodificar(lstAtual, intCodigo);
                std::string strTexto = Contem(strBruto, "\t") && Trim(strBruto, "\t ").empty()
                    ? strAnterior.value_or("") : Trim(strBruto);
                if (!strTexto.empty() && intFaixa > 0) strAnterior = strTexto;
                if (!strTexto.empty()) dicSaida[intFaixa] = strTexto;
            }
            intFaixa++;
            lstAtual.clear();
            blnValido = true;
        }
    }
    return intDescartados;
}

bool SptiDiscReader::CrcPacoteOk(const std::vector<uint8_t> &arr, int p)
{
    int intGuardado = (arr[p + 16] << 8) | arr[p + 17];
    if (intGuardado == 0) return true;
    int crc = 0;
    for (int i = 0; i < 16; i++)
    {
        crc ^= arr[p + i] << 8;
        for (int b = 0; b < 8; b++) crc = (crc & 0x8000) != 0 ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
    }
    return (crc ^ 0xFFFF) == intGuardado;
}

bool SptiDiscReader::LerMcn(std::string &strMcn)
{
    strMcn.clear();
    if (!IsOpen()) return false;

    uint8_t arrBuffer[24] = {};
    SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER sptd = CriarSptdBasico(sizeof arrBuffer, arrBuffer, 3);
    sptd.sptd.CdbLength = 10;
    sptd.sptd.Cdb[0] = 0x42;
    sptd.sptd.Cdb[1] = 0x02;
    sptd.sptd.Cdb[2] = 0x40;
    sptd.sptd.Cdb[3] = 0x02;
    sptd.sptd.Cdb[8] = 24;

    int intErro;
    bool blnOk = DeviceIoControlSpti(sptd, intErro);

    if (blnOk && sptd.sptd.ScsiStatus == 0)
    {
        if ((arrBuffer[8] & 0x80) != 0)
        {
            std::string strValor = Trim(Ascii(arrBuffer + 9, 13));
            // ARCA-ORIGINAL: o Trim() do .NET nao tira '\0'; um MCN que o drive entregue com zeros
            // no fim passa no "tamanho >= 10" com eles dentro.
            if (!strValor.empty() && Caracteres(strValor) >= 10)
            {
                strMcn = strValor;
                m_objLogger.RegistrarDiagnostico("[SUB-CHANNEL Q] Código de barras MCN/UPC lido com sucesso: " + strMcn);
                return true;
            }
        }
    }
    return false;
}

bool SptiDiscReader::LerIsrc(int intFaixa, std::string &strIsrc)
{
    strIsrc.clear();
    if (!IsOpen()) return false;

    uint8_t arrBuffer[24] = {};
    SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER sptd = CriarSptdBasico(sizeof arrBuffer, arrBuffer, 3);
    sptd.sptd.CdbLength = 10;
    sptd.sptd.Cdb[0] = 0x42;
    sptd.sptd.Cdb[1] = 0x02;
    sptd.sptd.Cdb[2] = 0x40;
    sptd.sptd.Cdb[3] = 0x03;
    sptd.sptd.Cdb[6] = (uint8_t) intFaixa;
    sptd.sptd.Cdb[8] = 24;

    int intErro;
    bool blnOk = DeviceIoControlSpti(sptd, intErro);

    if (blnOk && sptd.sptd.ScsiStatus == 0)
    {
        if ((arrBuffer[8] & 0x80) != 0)
        {
            std::string strValor = Trim(Ascii(arrBuffer + 9, 12));
            if (!strValor.empty() && Caracteres(strValor) == 12)
            {
                strIsrc = strValor;
                m_objLogger.RegistrarDiagnostico("[SUB-CHANNEL Q] ISRC da Faixa " + D2(intFaixa) + " lido com sucesso: " + strIsrc);
                return true;
            }
        }
    }
    return false;
}

bool SptiDiscReader::LerBlocoSpti(void *ptrDataBuffer, uint32_t dwLba, uint32_t dwSectors, bool blnIsAudio,
                                  uint32_t &dwBytesTransferidos, uint8_t &bScsiStatus, uint8_t &bSenseKey, uint8_t &bAsc,
                                  uint8_t &bAscq, int &intWin32Error)
{
    dwBytesTransferidos = 0;
    bScsiStatus = 0;
    bSenseKey = 0;
    bAsc = 0;
    bAscq = 0;
    intWin32Error = 0;

    if (!IsOpen())
    {
        intWin32Error = 6;
        return false;
    }

    uint32_t dwBytesPorSetor = blnIsAudio ? 2352u : 2048u;
    uint32_t dwTamanhoTransferencia = dwSectors * dwBytesPorSetor;

    SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER sptd = CriarSptdBasico((int) dwTamanhoTransferencia, ptrDataBuffer, 30);

    if (blnIsAudio)
    {
        // LINUX: o mesmo READ CD do original - so os 2352 bytes de dados do usuario, sem os
        // ponteiros C2 (que o ASUS DRW-24F1MT responde com erros falsos; o original nunca os
        // pediu) e tipo de setor "qualquer" (byte 1 = 0).
        sptd.sptd.CdbLength = 12;
        sptd.sptd.Cdb[0] = 0xBE; // READ CD
        sptd.sptd.Cdb[2] = (uint8_t) (dwLba >> 24);
        sptd.sptd.Cdb[3] = (uint8_t) (dwLba >> 16);
        sptd.sptd.Cdb[4] = (uint8_t) (dwLba >> 8);
        sptd.sptd.Cdb[5] = (uint8_t) (dwLba);
        sptd.sptd.Cdb[6] = (uint8_t) (dwSectors >> 16);
        sptd.sptd.Cdb[7] = (uint8_t) (dwSectors >> 8);
        sptd.sptd.Cdb[8] = (uint8_t) (dwSectors);
        sptd.sptd.Cdb[9] = 0x10; // User Data Only (2352 bytes)
    }
    else
    {
        sptd.sptd.CdbLength = 10;
        sptd.sptd.Cdb[0] = 0x28; // READ (10)
        sptd.sptd.Cdb[2] = (uint8_t) (dwLba >> 24);
        sptd.sptd.Cdb[3] = (uint8_t) (dwLba >> 16);
        sptd.sptd.Cdb[4] = (uint8_t) (dwLba >> 8);
        sptd.sptd.Cdb[5] = (uint8_t) (dwLba);
        sptd.sptd.Cdb[7] = (uint8_t) (dwSectors >> 8);
        sptd.sptd.Cdb[8] = (uint8_t) (dwSectors);
    }

    bool blnSucessoIo = DeviceIoControlSpti(sptd, intWin32Error);

    if (!blnSucessoIo)
    {
        return false;
    }

    bScsiStatus = sptd.sptd.ScsiStatus;
    if (bScsiStatus != 0)
    {
        bSenseKey = (uint8_t) (sptd.SenseBuf[2] & 0x0F);
        bAsc = sptd.SenseBuf[12];
        bAscq = sptd.SenseBuf[13];
        return false;
    }

    dwBytesTransferidos = sptd.sptd.DataTransferLength;
    return true;
}

bool SptiDiscReader::DescartarCacheDrive(uint32_t dwLbaAtual, uint32_t dwLbaMaximo)
{
    if (!IsOpen()) return false;

    // READ(10) - o comando que estava aqui - e de disco de dados; em CD de audio o drive recusa,
    // e o cache nunca era esvaziado: a "nova tentativa" de um setor ruim podia vir da memoria do
    // drive. Agora: READ CD de uma regiao longe, maior que o cache (~3,5 MB).
    const uint32_t dwSetoresPurga = 1500, dwBloco = 26;
    try
    {
        std::vector<uint8_t> arrDummy(dwBloco * 2352);
        // Longe de onde se esta: na outra metade do disco.
        uint32_t dwFlushLba = dwLbaMaximo > dwSetoresPurga * 2 && dwLbaAtual < dwLbaMaximo / 2
            ? dwLbaMaximo - dwSetoresPurga : 0;
        for (uint32_t d = 0; d < dwSetoresPurga; d += dwBloco)
        {
            uint32_t b; uint8_t s, k, a, q; int w;
            LerBlocoSpti(arrDummy.data(), dwFlushLba + d, dwBloco, true, b, s, k, a, q, w);
        }
        m_objLogger.RegistrarDiagnostico("Cache do drive esvaziado lendo " + std::to_string(dwSetoresPurga) + " setores a partir do LBA " +
                                         std::to_string(dwFlushLba) + ".");
        return true;
    }
    catch (...)
    {
        return false;
    }
}

SptiDiscReader::SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER SptiDiscReader::CriarSptdBasico(int intDataTransferLength, void *ptrBuffer,
                                                                                    uint32_t dwTimeoutSegundos)
{
    SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER sptd;
    sptd.sptd.DataTransferLength = (uint32_t) intDataTransferLength;
    sptd.sptd.TimeOutValue = dwTimeoutSegundos;
    sptd.sptd.DataBuffer = ptrBuffer;
    return sptd;
}

bool SptiDiscReader::DeviceIoControlSpti(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER &sptd, int &intWin32Error)
{
    // LINUX: ver o cabecalho. SCSI_IOCTL_DATA_IN = SG_DXFER_FROM_DEV; prazo em milissegundos.
    intWin32Error = 0;
    if (m_dwMaxTransferencia > 0 && sptd.sptd.DataTransferLength > m_dwMaxTransferencia)
    {
        intWin32Error = 87;                      // ERROR_INVALID_PARAMETER, como o Windows
        return false;
    }
    uint8_t arrSense[64] = {};
    sg_io_hdr_t h;
    memset(&h, 0, sizeof h);
    h.interface_id = 'S';
    h.dxfer_direction = sptd.sptd.DataTransferLength ? SG_DXFER_FROM_DEV : SG_DXFER_NONE;
    h.cmd_len = sptd.sptd.CdbLength;
    h.mx_sb_len = sizeof arrSense;
    h.dxfer_len = sptd.sptd.DataTransferLength;
    h.dxferp = sptd.sptd.DataBuffer;
    h.cmdp = sptd.sptd.Cdb;
    h.sbp = arrSense;
    h.timeout = sptd.sptd.TimeOutValue * 1000;

    if (ioctl(m_hDeviceHandle, SG_IO, &h) < 0)
    {
        intWin32Error = CodigoWin32(errno);
        return false;
    }
    // Erro de transporte (a controladora, o barramento, o prazo): o pedido nao chegou ao fim.
    const int DID_TIME_OUT = 0x03, DRIVER_TIMEOUT = 0x06;
    if (h.host_status != 0 || (h.driver_status & 0x0F) == DRIVER_TIMEOUT)
    {
        intWin32Error = h.host_status == DID_TIME_OUT || (h.driver_status & 0x0F) == DRIVER_TIMEOUT ? 121 : 1117;
        return false;
    }

    sptd.sptd.ScsiStatus = h.status;
    int intSense = std::min((int) h.sb_len_wr, (int) sizeof arrSense);
    if (intSense >= 4 && ((arrSense[0] & 0x7F) == 0x72 || (arrSense[0] & 0x7F) == 0x73))
    {
        // sense em formato de descritor: chave, ASC e ASCQ em 1, 2 e 3 -> nas posicoes do fixo
        memset(sptd.SenseBuf, 0, sizeof sptd.SenseBuf);
        sptd.SenseBuf[0] = 0x70;
        sptd.SenseBuf[2] = arrSense[1] & 0x0F;
        sptd.SenseBuf[12] = arrSense[2];
        sptd.SenseBuf[13] = arrSense[3];
    }
    else
        memcpy(sptd.SenseBuf, arrSense, std::min(intSense, (int) sizeof sptd.SenseBuf));
    if (sptd.sptd.ScsiStatus == 0 && (h.driver_status & 0x0F) != 0 && intSense > 0)
        sptd.sptd.ScsiStatus = 0x02;             // o driver viu CHECK CONDITION com sense
    int intResid = h.resid;
    if (intResid < 0 || intResid > (int) h.dxfer_len) intResid = 0;
    sptd.sptd.DataTransferLength = h.dxfer_len - (uint32_t) intResid;
    return true;
}

} // namespace ARCA::Interop

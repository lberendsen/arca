// ARCA no xOS - Interop/SptiDiscReader (de Interop\SptiDiscReader.cs).
//
// LINUX: o original falava com o drive pelo SCSI Pass-Through Direct do Windows
// (IOCTL_SCSI_PASS_THROUGH_DIRECT em \\.\X:). Aqui a mesma conversa vai pelo ioctl SG_IO em
// /dev/sr0, com os MESMOS CDBs, tamanhos de buffer e prazos. O que o Windows devolvia e se traduz:
//   - DeviceIoControl falhou (GetLastWin32Error)  ->  ioctl falhou ou a controladora acusou erro
//     de transporte (host_status/driver_status). O codigo vai para intWin32Error ja no numero que
//     o Windows daria, porque o extrator decide por ele: 87 (ERROR_INVALID_PARAMETER, bloco
//     grande demais) para EINVAL/ENOMEM/EOVERFLOW; 5 (acesso negado) para EACCES/EPERM;
//     121 (ERROR_SEM_TIMEOUT) para prazo vencido; 21 (ERROR_NOT_READY) para ENOMEDIUM/ENXIO/
//     ENODEV; 170 (ERROR_BUSY) para EBUSY; 1117 (ERROR_IO_DEVICE) para o resto.
//   - ScsiStatus e o SenseBuf (sense em formato fixo: chave em [2], ASC em [12], ASCQ em [13])
//     vem do sg_io_hdr; sense em formato de descritor (0x72/0x73) e convertido para o fixo.
//   - pedido maior que o teto da controladora (no OptiPlex, 128 KB: 64 setores de audio nao cabem)
//     sai com 87 antes de ir ao drive, como no Windows; o Linux daria EIO.
#pragma once

#include "Interop/CdromTocReader.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ARCA::Modelo { class AcquisitionLogger; }

namespace ARCA::Interop {

/// Leitor optico de baixo nivel via SCSI Pass-Through Direct (SPTI).
/// Permite comunicacao direta com a cabeca de leitura optica, bypassando drivers intermediarios
/// e fornecendo controle fino sobre LBA, timeouts, buffer halving e descarte de cache.
class SptiDiscReader
{
public:
    explicit SptiDiscReader(Modelo::AcquisitionLogger &objLogger);
    ~SptiDiscReader();                              // Dispose
    SptiDiscReader(const SptiDiscReader &) = delete;
    SptiDiscReader &operator=(const SptiDiscReader &) = delete;

    bool IsOpen() const { return m_hDeviceHandle >= 0; }

    /// LINUX: no original, a letra da unidade ("E", "E:", "E:\"). Aqui, o dispositivo
    /// ("/dev/sr0"); o que nao comecar com "/" (uma letra, ou "") vira Base::DispositivoCd().
    bool AbrirUnidade(const std::string &strLetraDrive);
    void FecharUnidade();

    bool ObterInquiryHardware(std::string &strVendor, std::string &strProduct, std::string &strRevision);
    bool LerToc(CdromTocReader::CDROM_TOC &objToc);
    bool LerCapacidade(uint32_t &dwMaxLba, uint32_t &dwBlockSize);

    /// Uma sessao do disco, como o indice completo a descreve.
    struct TSessao
    {
        int intNumero = 0;
        /// Onde a sessao acaba (ponto A2, LBA). -1 se o drive nao disse.
        int intLeadOut = -1;
        /// Faixas da sessao: numero, LBA inicial, e audio (1/0).
        std::vector<std::array<int, 3>> lstFaixas;
    };

    /// READ TOC formato 2 (indice completo, "Full TOC"): diz em que sessao cada faixa esta e onde
    /// cada sessao acaba. E o unico jeito de saber onde o audio termina num CD com sessao de
    /// dados - o indice comum so da o lead-out do disco inteiro, que fica depois dos dados.
    /// nullopt (null no original) se o drive nao responder (ai vale a estimativa antiga).
    std::optional<std::vector<TSessao>> LerSessoes();

    /// strAlbum/strArtista: nullopt = null no original (o CD-TEXT nao trouxe).
    bool LerCdText(std::optional<std::string> &strAlbum, std::optional<std::string> &strArtista,
                   std::map<int, std::string> &dicTitulos, std::map<int, std::string> &dicArtistas);

    /// Decodifica o CD-TEXT (so o bloco 0, o idioma principal) a partir da resposta do READ TOC
    /// formato 5: 4 bytes de cabecalho e pacotes de 18 bytes - tipo, faixa, sequencia,
    /// bloco/posicao, 12 bytes de texto e CRC-16.
    ///
    /// - Cada pacote tem o CRC conferido. Pacote ruim e descartado, e o texto que ele tocava nao
    ///   sai - o pacote seguinte diz em que faixa e em que posicao do texto comeca, entao a
    ///   leitura ressincroniza em vez de desalinhar todos os titulos dali em diante.
    /// - A codificacao vem do pacote de informacoes (0x8F, primeiro byte de dados): 0x00 Latin-1,
    ///   0x01 ASCII, 0x80 Shift-JIS (japones), 0x81 coreano, 0x82 chines. Nas de dois bytes, o
    ///   texto termina em dois zeros, nao em um - um zero sozinho e metade de um caractere.
    /// - TAB ("\t", ou "\t\t" nas de dois bytes) quer dizer "igual a faixa anterior".
    ///
    /// Publico e estatico para poder ser testado sem drive.
    static void DecodificarCdText(const std::vector<uint8_t> &arr, int intTotal, std::optional<std::string> &strAlbum,
                                  std::optional<std::string> &strArtista, std::map<int, std::string> &dicTitulos,
                                  std::map<int, std::string> &dicArtistas, int &intDescartados, std::string &strCodificacao);

    /// CRC-16 do pacote (CCITT, polinomio 0x1021, valor inicial 0, resultado invertido), sobre os
    /// 16 primeiros bytes, guardado nos 2 ultimos. Ha drives que entregam o campo zerado (nao
    /// calculam): ai vale o pacote.
    static bool CrcPacoteOk(const std::vector<uint8_t> &arr, int p);

    bool LerMcn(std::string &strMcn);                       // "" = null
    bool LerIsrc(int intFaixa, std::string &strIsrc);       // "" = null

    bool LerBlocoSpti(void *ptrDataBuffer, uint32_t dwLba, uint32_t dwSectors, bool blnIsAudio,
                      uint32_t &dwBytesTransferidos, uint8_t &bScsiStatus, uint8_t &bSenseKey, uint8_t &bAsc,
                      uint8_t &bAscq, int &intWin32Error);

    bool DescartarCacheDrive(uint32_t dwLbaAtual, uint32_t dwLbaMaximo);

private:
    /// CD-TEXT tem no maximo 8 blocos x 256 pacotes de 18 bytes; 4 KB recusava discos inteiros.
    static constexpr int TamanhoMaximoCdText = 4 + 8 * 256 * 18;

    /// Os textos de um tipo de pacote (0x80 titulos, 0x81 interpretes), por numero de faixa
    /// (0 = o disco). Devolve os pacotes descartados.
    static int DecodificarTipo(const std::vector<uint8_t> &arr, int intTotal, uint8_t bTipo, bool blnDuplo, int intCodigo,
                               std::map<int, std::string> &dicSaida, bool blnIgnorarCrc);

    // LINUX: o SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, so com o que o ARCA usa dele.
    struct SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER
    {
        struct
        {
            uint8_t ScsiStatus = 0;
            uint8_t CdbLength = 0;
            uint32_t DataTransferLength = 0;
            uint32_t TimeOutValue = 0;              // segundos
            void *DataBuffer = nullptr;
            uint8_t Cdb[16] = {};
        } sptd;
        uint8_t SenseBuf[32] = {};
    };
    static SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER CriarSptdBasico(int intDataTransferLength, void *ptrBuffer, uint32_t dwTimeoutSegundos);
    /// LINUX: DeviceIoControl(IOCTL_SCSI_PASS_THROUGH_DIRECT) pelo SG_IO. false = o pedido nao
    /// chegou ao fim (intWin32Error no codigo do Windows); true com ScsiStatus != 0 = o drive
    /// recusou (sense em SenseBuf), como no Windows.
    bool DeviceIoControlSpti(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER &sptd, int &intWin32Error);

    int m_hDeviceHandle = -1;
    // LINUX: o maior pedido que a controladora aceita (BLKSECTGET, em bytes); 0 = nao se sabe.
    // O Windows recusa pedido maior com ERROR_INVALID_PARAMETER (87), e o extrator reduz o bloco;
    // o Linux devolveria EIO, que o extrator tomaria por falha de canal.
    uint32_t m_dwMaxTransferencia = 0;
    Modelo::AcquisitionLogger &m_objLogger;
};

} // namespace ARCA::Interop

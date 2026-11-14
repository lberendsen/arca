// ARCA no xOS - Interop/CdromTocReader (de Interop\CdromTocReader.cs).
// O indice (TOC) do disco e o DiscID do ledger.
// LINUX: o original abria \\.\X: e pedia IOCTL_CDROM_READ_TOC ao Windows. Aqui a unidade e o
// dispositivo (/dev/sr0, Base::DispositivoCd()) e o indice vem do READ TOC (CDB 43, MSF, formato 0)
// pelo SG_IO - e o mesmo comando que o driver de CD do Windows manda por baixo do IOCTL, e a
// resposta tem o mesmo leiaute do CDROM_TOC.
#pragma once

#include <cstdint>
#include <string>

namespace ARCA::Interop {

class CdromTocReader
{
public:
    struct TRACK_DATA
    {
        uint8_t Reserved = 0;
        uint8_t Control_Adr = 0;       // CONTROL nos 4 bits baixos (0x04 = dados), ADR nos altos
        uint8_t TrackNumber = 0;
        uint8_t Reserved1 = 0;
        uint8_t Address_0 = 0;
        uint8_t Address_1 = 0;         // M
        uint8_t Address_2 = 0;         // S
        uint8_t Address_3 = 0;         // F (MSF absoluto, com os 150 quadros iniciais)
    };

    struct CDROM_TOC
    {
        uint16_t Length = 0;           // como veio (big-endian cru, como no Windows); ninguem usa
        uint8_t FirstTrack = 0;
        uint8_t LastTrack = 0;
        TRACK_DATA TrackData[100];     // as faixas e, depois da ultima, o lead-out
    };

    /// LINUX: no original, GetToc(char driveLetter). Aqui, o dispositivo; "" = Base::DispositivoCd().
    /// Lanca std::runtime_error.
    static CDROM_TOC GetToc(const std::string &strDispositivo);

    /// O mesmo ID de GetDiscId, calculado a partir do AudioCD.cue da area de trabalho, sem o disco
    /// no drive.
    ///
    /// Existe porque o ID lido na extracao so vivia na memoria: fechar o ARCA entre a extracao e a
    /// aprovacao fazia a aprovacao pular o ledger. O CUE tem tudo o que o MD5 usa - primeira e
    /// ultima faixa, inicio de cada uma e o lead-out. As posicoes do CUE contam do LBA 0, e o TOC
    /// lido do drive vem em MSF absoluto, com os 2 s (150 quadros) iniciais; dai o +150.
    ///
    /// "" (null no original) quando o CUE nao basta: sem lead-out, sem faixas, ou disco
    /// multissessao (o lead-out do CUE e o da sessao 1, e o TOC do drive termina na ultima sessao -
    /// os IDs nao coincidiriam).
    static std::string CalcularIdDoCue(const std::string &strCaminhoCue);

    /// LINUX: no original, GetDiscId(char driveLetter). Lanca std::runtime_error.
    static std::string GetDiscId(const std::string &strDispositivo);

    /// LINUX: o IOCTL_CDROM_READ_TOC sobre um descritor ja aberto (o SptiDiscReader tambem o usa,
    /// como o original usava o mesmo IOCTL). false se o drive nao respondeu.
    static bool LerTocDoDescritor(int fd, CDROM_TOC &objToc);

private:
    /// Quadros absolutos no mesmo formato que GetDiscId usa: M, S e F em dois digitos hexadecimais.
    static std::string Msf(int intQuadros);
};

} // namespace ARCA::Interop

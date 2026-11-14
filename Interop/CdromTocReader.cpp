// ARCA no xOS - Interop/CdromTocReader (de Interop\CdromTocReader.cs).
#include "CdromTocReader.h"

#include "Base/Arquivo.h"
#include "Base/Hash.h"
#include "Base/Texto.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <regex>
#include <scsi/sg.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

namespace ARCA::Interop {

using namespace ARCA::Base;

namespace {

// LINUX: CreateFile(\\.\X:, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE). O O_NONBLOCK deixa
// abrir mesmo sem disco (como o Windows), e nada aqui tranca a unidade para outro programa.
int AbrirLeitura(const std::string &strDispositivo)
{
    std::string strCaminho = strDispositivo.empty() ? DispositivoCd() : strDispositivo;
    return open(strCaminho.c_str(), O_RDONLY | O_NONBLOCK);
}

std::string NomeUnidade(const std::string &strDispositivo)
{
    return strDispositivo.empty() ? DispositivoCd() : strDispositivo;
}

std::string Md5Hex16(const std::string &s)
{
    // MD5 sobre os bytes ASCII; hexadecimal maiusculo; os 16 primeiros caracteres
    std::vector<uint8_t> h = Hash(Algoritmo::Md5, s.data(), s.size());
    return Hex(h, true).substr(0, 16);
}

int Quadros(const std::smatch &objM)
{
    return (std::stoi(objM[1].str()) * 60 + std::stoi(objM[2].str())) * 75 + std::stoi(objM[3].str());
}

} // namespace

bool CdromTocReader::LerTocDoDescritor(int fd, CDROM_TOC &objToc)
{
    // LINUX: IOCTL_CDROM_READ_TOC = READ TOC/PMA/ATIP com MSF, formato 0, a partir da faixa 0,
    // alocando o tamanho do CDROM_TOC (4 + 100 x 8 = 804 bytes).
    uint8_t arr[804];
    memset(arr, 0, sizeof arr);
    uint8_t cdb[10] = { 0x43, 0x02, 0x00, 0, 0, 0, 0x00, (uint8_t) (sizeof arr >> 8), (uint8_t) (sizeof arr & 0xFF), 0 };
    uint8_t sense[32];
    sg_io_hdr_t h;
    memset(&h, 0, sizeof h);
    h.interface_id = 'S';
    h.dxfer_direction = SG_DXFER_FROM_DEV;
    h.cmd_len = sizeof cdb;
    h.mx_sb_len = sizeof sense;
    h.dxfer_len = sizeof arr;
    h.dxferp = arr;
    h.cmdp = cdb;
    h.sbp = sense;
    h.timeout = 10000;
    if (ioctl(fd, SG_IO, &h) < 0) return false;
    if (h.status != 0 || h.host_status != 0 || (h.driver_status & 0x0F) != 0) return false;
    int intRecebidos = (int) h.dxfer_len - h.resid;
    if (intRecebidos < 4) return false;

    objToc = CDROM_TOC();
    objToc.Length = (uint16_t) ((arr[1] << 8) | arr[0]);   // o ushort do C# lido sobre os bytes crus
    objToc.FirstTrack = arr[2];
    objToc.LastTrack = arr[3];
    for (int i = 0; i < 100 && 4 + i * 8 + 8 <= intRecebidos; i++)
    {
        const uint8_t *p = arr + 4 + i * 8;
        TRACK_DATA &td = objToc.TrackData[i];
        td.Reserved = p[0];
        td.Control_Adr = p[1];
        td.TrackNumber = p[2];
        td.Reserved1 = p[3];
        td.Address_0 = p[4];
        td.Address_1 = p[5];
        td.Address_2 = p[6];
        td.Address_3 = p[7];
    }
    return true;
}

CdromTocReader::CDROM_TOC CdromTocReader::GetToc(const std::string &strDispositivo)
{
    int hDrive = AbrirLeitura(strDispositivo);

    if (hDrive < 0)
    {
        // LINUX: no original, "a unidade X:"; aqui o nome do dispositivo.
        throw std::runtime_error("Não foi possível acessar a unidade " + NomeUnidade(strDispositivo) + " (Vazia ou em uso exclusivo).");
    }

    CDROM_TOC toc;
    bool success = LerTocDoDescritor(hDrive, toc);
    close(hDrive);

    if (!success)
    {
        throw std::runtime_error("Falha ao ler a TOC (Table of Contents) da mídia.");
    }
    return toc;
}

std::string CdromTocReader::CalcularIdDoCue(const std::string &strCaminhoCue)
{
    try
    {
        if (!ArquivoExiste(strCaminhoCue)) return "";
        std::vector<std::string> arrLinhas = LeLinhas(strCaminhoCue);

        static const std::regex reSessao(R"(^REM\s+SESSION\s+0*([2-9]|\d{2,})\b)", std::regex::icase);
        static const std::regex reTrack(R"(^TRACK\s+(\d+)\s)", std::regex::icase);
        static const std::regex reIndex(R"(^INDEX\s+01\s+(\d+):(\d+):(\d+))", std::regex::icase);
        static const std::regex reLeadOut(R"(^REM\s+LEAD-OUT\s+(\d+):(\d+):(\d+))", std::regex::icase);

        std::vector<int> lstNumeros;
        std::vector<int> lstInicios;
        int intLeadOut = -1;
        bool blnEsperandoIndice = false;
        for (const std::string &strBruta : arrLinhas)
        {
            std::string strLinha = Trim(strBruta);
            std::smatch objM;
            if (std::regex_search(strLinha, objM, reSessao)) return "";
            if (std::regex_search(strLinha, objM, reTrack))
            {
                lstNumeros.push_back(std::stoi(objM[1].str()));
                blnEsperandoIndice = true;
            }
            else if (blnEsperandoIndice && std::regex_search(strLinha, objM, reIndex))
            {
                lstInicios.push_back(Quadros(objM));
                blnEsperandoIndice = false;
            }
            else if (std::regex_search(strLinha, objM, reLeadOut))
            {
                intLeadOut = Quadros(objM);
            }
        }
        if (lstNumeros.empty() || lstInicios.size() != lstNumeros.size() || intLeadOut <= 0) return "";

        std::string sb = std::to_string(lstNumeros[0]) + "-" + std::to_string(lstNumeros.back()) + "-";
        for (int intInicio : lstInicios) sb += Msf(intInicio + 150);
        sb += Msf(intLeadOut + 150);

        return Md5Hex16(sb);
    }
    catch (...) { return ""; }
}

std::string CdromTocReader::Msf(int intQuadros)
{
    char b[16];
    snprintf(b, sizeof b, "%02X%02X%02X", (unsigned) (intQuadros / 4500), (unsigned) ((intQuadros / 75) % 60),
             (unsigned) (intQuadros % 75));
    return b;
}

std::string CdromTocReader::GetDiscId(const std::string &strDispositivo)
{
    int hDrive = AbrirLeitura(strDispositivo);

    if (hDrive < 0)
    {
        // LINUX: no original, "a unidade X:"; aqui o nome do dispositivo.
        throw std::runtime_error("Não foi possível acessar a unidade " + NomeUnidade(strDispositivo) + " (Vazia ou em uso exclusivo).");
    }

    CDROM_TOC toc;
    bool success = LerTocDoDescritor(hDrive, toc);
    close(hDrive);

    if (!success)
    {
        throw std::runtime_error("Falha ao ler a TOC (Table of Contents) da mídia.");
    }

    std::string sb = std::to_string(toc.FirstTrack) + "-" + std::to_string(toc.LastTrack) + "-";

    for (int i = 0; i <= toc.LastTrack - toc.FirstTrack + 1; i++)
    {
        const TRACK_DATA &td = toc.TrackData[i];
        char b[16];
        snprintf(b, sizeof b, "%02X%02X%02X", td.Address_1, td.Address_2, td.Address_3);
        sb += b;
    }

    return Md5Hex16(sb);
}

} // namespace ARCA::Interop

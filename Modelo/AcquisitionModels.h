// ARCA no xOS - Modelo/AcquisitionModels (de Modelo\AcquisitionModels.cs).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ARCA::Modelo {

/// Metadados e geometria fisica de uma faixa de CD para extracao e identificacao.
/// Utiliza notacao hungara e tipos estritos.
/// As strings vazias valem o null do original.
class TrackInfo
{
public:
    int TrackNum = 0;
    int64_t ByteOffset = 0;
    int64_t EndByteOffset = 0;
    int64_t StartLba = 0;
    int SessionNumber = 1;
    std::string Title;
    std::string Artist;
    std::string Album;
    std::string ISRC;
    std::string Composer;
    bool blnIdentificadoMusicBrainz = false;
    std::string TrackType = "AUDIO";
    // LINUX: o nome do campo fica como no original (WmaFilePath), mas aqui o arquivo e o .flac.
    std::string WmaFilePath;
    std::string ExtractedFilePath;
    std::string CoverUrl;
    uint32_t Year = 0;
    std::string FullDate;
    std::string Genre;

    bool WmaCreated = false;
    bool MetadataWritten = false;
    bool MetadataValidated = false;
    bool CodecValidated = false;
    std::string FailureReason;

    std::vector<std::string> PossibleAlbums;
    std::vector<std::string> PossibleCoverUrls;
    std::vector<std::string> PossibleFullDates;
    std::map<std::string, int> GenreVotes;
};

} // namespace ARCA::Modelo

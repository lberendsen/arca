// ARCA no xOS - Modelo/CandidatoIdentificacao (de Modelo\CandidatoIdentificacao.cs).
#include "CandidatoIdentificacao.h"

#include "Base/Texto.h"

#include <algorithm>
#include <cmath>

namespace ARCA::Modelo {

std::shared_ptr<TCandidatoIdentificacao> TCandidatoIdentificacao::DeItunes(const std::shared_ptr<TItunesAlbumInfo> &objAlbum,
                                                                           const std::vector<uint8_t> &arrCapa)
{
    if (!objAlbum) return nullptr;
    auto objC = std::make_shared<TCandidatoIdentificacao>();
    objC->strFonte = "Apple Music";
    objC->strArtista = objAlbum->strArtistName;
    objC->strAlbum = objAlbum->strCollectionName;
    objC->strAno = objAlbum->strReleaseYear;
    objC->strGenero = objAlbum->strPrimaryGenreName;
    objC->strDetalhe = objAlbum->strUpc.empty() ? "" : "UPC " + objAlbum->strUpc;
    {
        for (const TItunesTrackInfo &objF : objAlbum->lstFaixas)
        {
            objC->lstTitulos.push_back(objF.strTrackName);
            objC->lstArtistasFaixas.push_back(objF.strArtistName);
            // Math.Round: arredonda ao par no meio (o nearbyint, no modo padrao)
            objC->lstDuracoes.push_back(objF.intTrackTimeMillis > 0
                                            ? (int) std::nearbyint(objF.intTrackTimeMillis / 1000.0) : -1);
        }
    }
    objC->arrCapa = arrCapa;
    return objC;
}

std::shared_ptr<TCandidatoIdentificacao> TCandidatoIdentificacao::DeDiscogs(const std::shared_ptr<TDiscogsReleaseInfo> &objRelease)
{
    if (!objRelease) return nullptr;
    auto objC = std::make_shared<TCandidatoIdentificacao>();
    objC->strFonte = "Discogs";
    objC->strArtista = objRelease->strArtist;
    objC->strAlbum = objRelease->strAlbum;
    objC->strAno = objRelease->strYear;
    std::vector<std::string> lstGeneros(objRelease->lstGenres);
    lstGeneros.insert(lstGeneros.end(), objRelease->lstStyles.begin(), objRelease->lstStyles.end());
    objC->strGenero = !lstGeneros.empty() ? Base::Junta(lstGeneros, ", ") : "";

    std::vector<std::string> lstDetalhe;
    if (!objRelease->strGravadora.empty()) lstDetalhe.push_back(objRelease->strGravadora);
    if (!objRelease->strPais.empty()) lstDetalhe.push_back(objRelease->strPais);
    if (objRelease->intReleaseId > 0) lstDetalhe.push_back("release " + std::to_string(objRelease->intReleaseId));
    objC->strDetalhe = !lstDetalhe.empty() ? Base::Junta(lstDetalhe, " · ") : "";

    objC->lstTitulos.insert(objC->lstTitulos.end(), objRelease->lstTrackTitles.begin(), objRelease->lstTrackTitles.end());
    objC->lstArtistasFaixas.insert(objC->lstArtistasFaixas.end(), objRelease->lstTrackArtists.begin(),
                                   objRelease->lstTrackArtists.end());
    objC->lstDuracoes.insert(objC->lstDuracoes.end(), objRelease->lstTrackDurations.begin(),
                             objRelease->lstTrackDurations.end());
    if (std::any_of(objRelease->lstTrackComposers.begin(), objRelease->lstTrackComposers.end(),
                    [](const std::string &c) { return !c.empty(); }))
        objC->lstCompositores = objRelease->lstTrackComposers;
    objC->arrCapa = objRelease->arrCoverBytes;
    return objC;
}

} // namespace ARCA::Modelo

// ARCA no xOS - Modelo/CandidatoIdentificacao (de Modelo\CandidatoIdentificacao.cs).
#pragma once

#include "DiscogsClient.h"
#include "ItunesResolver.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ARCA::Modelo {

/// Um resultado de identificacao, na mesma forma venha de onde vier.
///
/// Existe para que a escolha entre Apple Music e Discogs seja feita pela pessoa, lado a lado, e para
/// que aplicar o escolhido seja um caminho so - sem um ramo para cada servico no momento de gravar
/// as tags.
class TCandidatoIdentificacao
{
public:
    std::string strFonte;                 // "Apple Music" ou "Discogs"
    std::string strArtista;
    std::string strAlbum;
    std::string strAno;
    std::string strGenero;                // "" = null
    /// Gravadora/catalogo/pais, quando a fonte informa ("" = null).
    std::string strDetalhe;

    std::vector<std::string> lstTitulos;
    std::vector<std::string> lstArtistasFaixas;
    /// nullopt (null no original) quando a fonte nao traz compositores.
    std::optional<std::vector<std::string>> lstCompositores;
    /// Segundos por faixa; -1 quando a fonte nao informa.
    std::vector<int> lstDuracoes;

    std::vector<uint8_t> arrCapa;         // vazio = null
    /// A capa sem reducao (scan cortado), para a pasta Artwork. Vazio (null) quando a capa so existe
    /// no tamanho de arrCapa (internet) - ai vai arrCapa.
    std::vector<uint8_t> arrCapaPlena;
    /// Paginas do encarte digitalizadas, em ordem, ja em JPEG. Vazio (null) quando nao ha.
    std::vector<std::vector<uint8_t>> lstEncarte;

    // nullptr quando a entrada e nullptr (null no original)
    static std::shared_ptr<TCandidatoIdentificacao> DeItunes(const std::shared_ptr<TItunesAlbumInfo> &objAlbum,
                                                             const std::vector<uint8_t> &arrCapa);
    static std::shared_ptr<TCandidatoIdentificacao> DeDiscogs(const std::shared_ptr<TDiscogsReleaseInfo> &objRelease);
};

} // namespace ARCA::Modelo

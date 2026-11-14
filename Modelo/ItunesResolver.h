// ARCA no xOS - Modelo/ItunesResolver (de Modelo\ItunesResolver.cs).
// TItunesTrackInfo, TItunesAlbumInfo e ItunesResolver: a consulta ao Apple Music (iTunes Search API).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ARCA::Modelo {

// Action<string> do original: o registro de cada passo (pode ser vazio)
using ActLog = std::function<void(const std::string &)>;

class TItunesTrackInfo
{
public:
    int intTrackNumber = 0;
    /// Disco do album (1, 2...): num album duplo, a faixa 1 existe uma vez por disco.
    int intDiscNumber = 0;
    std::string strTrackName;
    std::string strArtistName;
    int intTrackTimeMillis = 0;
};

class TItunesAlbumInfo
{
public:
    int64_t lngCollectionId = 0;
    std::string strArtistName;
    std::string strCollectionName;
    std::string strPrimaryGenreName;
    std::string strReleaseDate;
    std::string strReleaseYear;
    std::string strArtworkUrl100;
    std::string strArtworkUrl600;
    std::string strArtworkUrl1400;
    int intTrackCount = 0;
    std::string strUpc;
    std::vector<TItunesTrackInfo> lstFaixas;
};

class ItunesResolver
{
public:
    // nullptr = null no original (nao achou, ou falhou: o motivo vai para actLog)
    static std::shared_ptr<TItunesAlbumInfo> ConsultarPorUpc(const std::string &strUpc, const ActLog &actLog);

    static std::shared_ptr<TItunesAlbumInfo> ConsultarPorUrlOuId(const std::string &strUrlOuId, const ActLog &actLog);

    /// O texto aponta um album exato (link do Apple Music/iTunes ou ID numerico), e nao e um termo
    /// de busca? Os mesmos tres formatos que ConsultarPorUrlOuId reconhece.
    static bool EhLinkOuId(const std::string &strEntrada);

    static std::shared_ptr<TItunesAlbumInfo> ConsultarPorUrlOuId(const std::string &strUrlOuId,
                                                                 int intQtdFaixasEsperadas, const ActLog &actLog);

    static std::shared_ptr<TItunesAlbumInfo> ConsultarPorTermo(const std::string &strTermo, const ActLog &actLog);

    static std::shared_ptr<TItunesAlbumInfo> ConsultarAlbum(const std::string &strArtista, const std::string &strAlbum,
                                                            int intQtdFaixasEsperadas, const ActLog &actLog);

    /// blnExigirContagem: true (identificacao automatica): album sem um disco com o numero de faixas
    /// do CD e recusado - ninguem confere depois. false (janela de escolha): devolve o disco mais
    /// proximo, porque a pessoa ve a duracao de cada faixa ao lado e decide.
    static std::shared_ptr<TItunesAlbumInfo> ConsultarAlbum(const std::string &strArtista, const std::string &strAlbum,
                                                            int intQtdFaixasEsperadas, bool blnExigirContagem,
                                                            const ActLog &actLog);

private:
    class TItunesAlbumCandidato
    {
    public:
        int64_t lngCollectionId = 0;
        std::string strArtistName;
        std::string strCollectionName;
        int intTrackCount = 0;
        std::string strArtworkUrl100;
        int intScore = 0;
    };

    static std::vector<TItunesAlbumCandidato> BuscarCandidatos(const std::string &strTermo, int intLimit);
    static std::set<std::string> Palavras(const std::string &strTexto);
    static int CalcularPontuacao(const TItunesAlbumCandidato &cand, const std::string &strArtistaEsperado,
                                 const std::string &strAlbumEsperado, int intQtdFaixasEsperadas);
    static std::string NormalizarTextoParaComparacao(const std::string &strTexto);
    static bool EscolherDisco(TItunesAlbumInfo &objAlbum, int intQtdFaixasEsperadas, bool blnExigirContagem,
                              const ActLog &actLog);
    static std::shared_ptr<TItunesAlbumInfo> ExecutarConsultaItunes(const std::string &strUrl, const ActLog &actLog,
                                                                    int intQtdFaixasEsperadas = 0,
                                                                    bool blnExigirContagem = true);
};

} // namespace ARCA::Modelo

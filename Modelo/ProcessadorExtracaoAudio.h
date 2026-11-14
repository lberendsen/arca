// ARCA no xOS - Modelo/ProcessadorExtracaoAudio (de Modelo\ProcessadorExtracaoAudio.cs).
// TConferenciaWma e ProcessadorExtracaoAudio.
// LINUX: o original codificava em WMA 9 Lossless (EscritorWma) e etiquetava pelo EditorAsf; aqui
// o codificador é o EscritorFlac (libFLAC) e o editor, o EditorFlac, com a mesma API e os mesmos
// nomes de atributo ASF. Os nomes de classes, métodos e campos ficam os do original
// (TConferenciaWma, FatiarEConverterParaWma, strArquivoWma...); as faixas saem em .flac.
#pragma once

#include "Modelo/AcquisitionModels.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ARCA::Modelo {

/// Conferência de uma faixa entre o áudio do disco (o que entrou no
/// codificador) e o FLAC (WMA no original) que saiu dele.
class TConferenciaWma
{
public:
    int intFaixa = 0;
    std::string strArquivoWma;                 // LINUX: o .flac
    std::string strHashFonte, strHashWma;      // "" = null
    int64_t lngBytesFonte = 0, lngBytesWma = 0;
    std::string strFalha;                      // "" = null
    bool Confere() const { return strFalha.empty() && !strHashWma.empty() && strHashWma == strHashFonte; }
};

/// Fatiador de imagem BIN/CUE gerada por SPTI (LINUX: SG_IO) em arquivos FLAC (WMA Lossless no original).
/// Fatiamento exato por setor óptico (2352 bytes/setor, 44.1kHz 16-bit PCM estéreo),
/// e codificação pelo EscritorFlac (no original, pelo Windows Media Format do próprio Windows,
/// EscritorWma), com o PCM indo do .bin direto ao codificador (até 04/10/2026: Expression Encoder 4 sobre WAVs).
/// Embute tags e capas de alta resolução via EditorFlac (no original, EditorAsf/wmvcore.dll).
class ProcessadorExtracaoAudio
{
public:
    using Relato = std::function<void(const std::string &mensagem)>;
    using Progresso = std::function<void(int faixaAtual, int totalFaixas, double percentualFaixa)>;

    static std::vector<TrackInfo> ParsearCue(const std::string &strCaminhoCue);
    static std::vector<TrackInfo> ParsearCue(const std::string &strCaminhoCue, std::string &strAlbumCdText, std::string &strArtistaCdText);
    static std::vector<TrackInfo> ParsearCue(const std::string &strCaminhoCue, std::string &strAlbumCdText, std::string &strArtistaCdText,
                                             int64_t &lngUltimoLeadOutSessao1Bytes);

    /// Decodifica o FLAC (WMA no original) e compara tamanho e SHA-256 com os do áudio de origem.
    static void ConferirWma(const std::string &strWma, TConferenciaWma &c);

    /// Título da faixa 00: o áudio escondido antes da faixa 1 (HTOA).
    static constexpr const char *TituloFaixaOculta = "Faixa oculta (antes da faixa 1)";

    /// Mínimo para o trecho antes da faixa 1 virar faixa: 1 s.
    static constexpr int SetoresMinimosFaixaOculta = 75;

    /// O trecho do .bin tem som de verdade? Pico acima de −60 dBFS
    /// (amplitude 32 em 16 bits): silêncio digital e ruído de fundo de
    /// fita ficam abaixo; música, mesmo baixinha, passa. Fora do disco
    /// (offset negativo) conta como silêncio.
    static bool TrechoTemSom(const std::string &strBin, int64_t lngInicio, int64_t lngTamanho, double &dblPicoDb);

    /// arrCapaOficialBytes vazio = null. lstConferencia nullptr = null.
    static bool FatiarEConverterParaWma(
        const std::string &strCaminhoCue,
        const std::string &strArtistaEleito,
        const std::string &strAlbumEleito,
        const std::string &strAnoEleito,
        const std::string &strGeneroEleito,
        const std::vector<uint8_t> &arrCapaOficialBytes,
        Relato relatar,
        Progresso notificarProgresso,
        int intOffsetAmostras = 0,
        std::vector<TConferenciaWma> *lstConferencia = nullptr);

    // Nas listas, vazia = null.
    static bool RenomearETaggearFaixas(
        const std::string &strPastaTrabalho,
        const std::vector<std::string> &lstTitulos,
        const std::vector<std::string> &lstArtistasFaixas,
        const std::string &strArtistaAlbum,
        const std::string &strAlbum,
        const std::vector<uint8_t> &arrCapaBytes,
        Relato relatar);

    /// Renomeia os arquivos FLAC (WMA no original) fatiados na pasta de trabalho para o formato canônico
    /// (ex: '01 - Nome da Faixa.flac') e atualiza as etiquetas (cabeçalhos ASF no original) com Title, Author, Album, Composer e Capa.
    static bool RenomearETaggearFaixas(
        const std::string &strPastaTrabalho,
        const std::vector<std::string> &lstTitulos,
        const std::vector<std::string> &lstArtistasFaixas,
        const std::string &strArtistaAlbum,
        const std::string &strAlbum,
        const std::vector<std::string> &lstCompositores,
        const std::vector<uint8_t> &arrCapaBytes,
        Relato relatar);

    /// Igual à versão acima, gravando também ano (WM/Year) e gênero
    /// (WM/Genre) quando informados — vazios não apagam o que o arquivo já tem.
    static bool RenomearETaggearFaixas(
        const std::string &strPastaTrabalho,
        const std::vector<std::string> &lstTitulos,
        const std::vector<std::string> &lstArtistasFaixas,
        const std::string &strArtistaAlbum,
        const std::string &strAlbum,
        const std::vector<std::string> &lstCompositores,
        const std::vector<uint8_t> &arrCapaBytes,
        const std::string &strAno,
        const std::string &strGenero,
        Relato relatar);

    static void AtualizarCueComTitulos(
        const std::string &strCaminhoCue,
        const std::string &strArtistaAlbum,
        const std::string &strAlbum,
        const std::vector<std::string> &lstTitulos,
        const std::vector<std::string> &lstArtistasFaixas,
        Relato relatar);

    /// Atualiza o arquivo CUE da extração inserindo os títulos oficiais, artistas e compositores nas faixas.
    static void AtualizarCueComTitulos(
        const std::string &strCaminhoCue,
        const std::string &strArtistaAlbum,
        const std::string &strAlbum,
        const std::vector<std::string> &lstTitulos,
        const std::vector<std::string> &lstArtistasFaixas,
        const std::vector<std::string> &lstCompositores,
        Relato relatar);

private:
    class TFluxoParaEscritor;

    static std::string CopiarCorrigido(const std::string &strBin, int64_t lngTamanhoBin, int64_t lngInicio, int64_t lngTamanho,
                                       TFluxoParaEscritor &objDestino);
};

} // namespace ARCA::Modelo

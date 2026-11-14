// ARCA no xOS - Modelo/GnuDbResolver (de Modelo\GnuDbResolver.cs).
// A consulta ao GnuDB (freedb) pelo indice do disco, e a limpeza de titulo para busca.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ARCA::Modelo {

class GnuDbResolver
{
public:
    /// Consulta o GnuDB (freedb) pelo indice do disco, montado do AudioCD.cue com a mesma regra do
    /// AccurateRip (TTocAudio) - que ja confere contra discos reais. Antes o ID era calculado sem os
    /// 150 setores do pre-gap (nao achava o disco, ou achava outro), o CD Extra ia com o fim errado,
    /// e sem o .bin o fim do disco era inventado (+3 min). Agora, sem fim conhecido, nao consulta.
    /// Devolve "OK" ou a mensagem do que houve. Saidas: "" = null no original.
    static std::string ResolveToc(const std::string &strCue, int64_t lngBytesBin, std::string &artist,
                                  std::string &album, std::string &genre, std::string &year,
                                  std::vector<std::string> &trackTitles);

    /// Versao "enxuta" do titulo, SO para montar termo de busca: tira o artista do comeco (com
    /// separador) e parenteses do fim, que costumam atrapalhar a procura. Nunca usar o resultado
    /// como titulo gravado - o nome do album e o que a fonte diz.
    static std::string HigienizarTituloAlbum(const std::string &strArtista, const std::string &strAlbum);

private:
    static int CddbSum(int n);
    static std::string FetchUrl(const std::string &url);     // "" = null (falhou)
    static std::string RemoverAcentos(const std::string &strTexto);
};

} // namespace ARCA::Modelo

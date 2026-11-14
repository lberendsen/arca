// ARCA no xOS - Modelo/RenomeacaoFaixa (de Modelo\RenomeacaoFaixa.cs).
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ARCA::Modelo {

class TResultadoRenomeacao
{
public:
    bool blnOk = false;
    /// <summary>Por que não renomeou (blnOk false). Nada foi mexido.</summary>
    std::string strFalha;            // "" = null
    std::string strCaminhoNovo;
    /// <summary>O que foi atualizado junto, para o resumo.</summary>
    std::vector<std::string> lstFeito;
    /// <summary>O que não deu, sem desfazer a renomeação.</summary>
    std::vector<std::string> lstAvisos;
};

/// <summary>
/// Renomeia uma faixa do acervo e atualiza tudo o que aponta para ela pelo
/// caminho, para que nada fique quebrado:
///
/// 1. o arquivo;
/// 2. os caches de onda e espectrograma (a chave é o caminho; sem mover,
///    a onda seria recalculada inteira);
/// 3. as playlists do WMP (.wpl, .m3u, .m3u8, .asx) em Minhas Músicas\Playlists,
///    com caminho absoluto ou relativo;
/// 4. as partes de obra que apontam para a faixa (banco ARCA_Obras.sdf);
/// 5. o ledger, com uma linha nova (ele é append-only).
///
/// A biblioteca do WMP fica com a Biblioteca.TrocarCaminho, que precisa do
/// player.
///
/// O nome é livre: só se recusa o que o próprio Windows não aceita. Nada
/// no ARCA pode depender do nome do arquivo para saber a ordem das faixas —
/// ela vem do WM/TrackNumber (TOrdemFaixas).
/// </summary>
// LINUX: o acervo e montado so de leitura; Renomear abre um Base::GravacaoNoAcervo durante a troca
// do arquivo e das playlists (o banco de obras e o ledger abrem o seu). O banco e o
// ARCA_Obras.sqlite (Modelo/BancoObras).
class TRenomeacaoFaixa
{
public:
    /// <summary>
    /// Confere o nome novo (sem extensão) para o arquivo atual. null se
    /// serve; senão, o motivo, para mostrar à pessoa. Só o que o Windows
    /// recusa ou o que apagaria outro arquivo.
    /// </summary>
    /// "" = null
    static std::string Validar(const std::string &strCaminhoAtual, const std::string &strNovoSemExtensao);

    /// <summary>
    /// Renomeia o arquivo e atualiza caches, playlists e ledger. Se a
    /// renomeação do arquivo falhar, nada mais é tocado.
    /// </summary>
    static TResultadoRenomeacao Renomear(const std::string &strCaminhoAtual, const std::string &strNovoSemExtensao,
                                         const std::string &strRaizAcervo, const std::string &strArtista,
                                         const std::string &strAlbum);

    // Os caminhos dos caches de onda e de espectrograma de um arquivo (OndaCache.CaminhoSidecar e
    // EspectrogramaCache.CaminhoSidecar no original). Os dois caches sao do EM EXECUCAO, que e outra
    // frente do porte: quem define estes dois e o EM EXECUCAO (Modelo/OndaCache e
    // Modelo/EspectrogramaCache, ao ser portado); quem chama e o Renomear. Vazios = ainda nao
    // ligados: a renomeacao nao move cache nenhum (ele e refeito ao abrir a faixa).
    static std::function<std::string(const std::string &arquivo)> CaminhoSidecarOnda;
    static std::function<std::string(const std::string &arquivo)> CaminhoSidecarEspectrograma;

private:
    static int MoverSidecar(const std::string &strDe, const std::string &strPara, TResultadoRenomeacao &objR);
    static int AtualizarPlaylists(const std::string &strAntigo, const std::string &strNovo, TResultadoRenomeacao &objR);
    static std::string Trocar(const std::string &strTexto, const std::string &strDe, const std::string &strPara);
    static std::string Relativo(const std::string &strPasta, const std::string &strArquivo);
};

} // namespace ARCA::Modelo

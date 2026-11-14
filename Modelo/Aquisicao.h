// ARCA no xOS - Modelo/Aquisicao (de Modelo\Aquisicao.cs).
// ItemManifesto, ResultadoIntegracao e Integracao: o portão de aprovação.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ARCA::Modelo {

class TRelatorioExtracao;

/// Uma faixa extraída, com a identidade congelada no momento do rip.
class ItemManifesto
{
public:
    std::string Nome;        // nome do arquivo no destino
    std::string Origem;      // onde está agora, na área de trabalho
    int64_t Tamanho = 0;
    std::string Sha256;
};

class ResultadoIntegracao
{
public:
    bool Ok = false;
    std::string Erro;              // "" = null
    std::string PastaDestino;      // "" = null
    int Arquivos = 0;
    std::vector<std::string> Integrados;

    /// Arquivos que estavam na área de trabalho, não eram do manifesto nem
    /// subproduto conhecido da extração, e por isso ficaram lá.
    std::vector<std::string> NaoIntegrados;
};

/// A passagem da área de trabalho para o acervo.
///
/// **A regra do Lorenzo:** nada entra na biblioteca antes de ele aprovar o
/// rip e os metadados. Até lá, o material fica numa pasta de trabalho, fora
/// do alcance da biblioteca — e é isso que impede o acervo de ser poluído
/// por extração ruim ou identificação errada.
///
/// Portado do `BtnCommit_Click` do ARCAwm, preservando o desenho, que é bom:
/// não é um `File.Move`, é um **commit transacional em quatro passagens**,
/// com o hash conferido três vezes — na origem, na cópia e no destino.
/// Nada é destruído antes da terceira passagem, e o que for substituído
/// deixa um `.backup`.
///
/// O manifesto é congelado no fim do rip. Se um arquivo mudar entre o rip e
/// a aprovação, a primeira passagem recusa — o que se aprova é o que foi
/// extraído, não o que estiver lá na hora.
///
/// LINUX: o DADOS é montado só de leitura; toda gravação no acervo fica dentro de um
/// Base::GravacaoNoAcervo (Integrar o mantém aberto do começo ao fim; GravarNoAcervo,
/// RetirarDoAcervo e o ledger abrem o seu, que se aninha).
class Integracao
{
public:
    using Relato = std::function<void(const std::string &mensagem)>;
    using ProgressoIntegracao = std::function<void(double dblPercentual, const std::string &strDescricaoPasso)>;

    /// Congela o estado do que foi extraído. A partir daqui, qualquer
    /// alteração nos arquivos é detectada na aprovação.
    /// padrao "" (null), "*.flac" ou "*.wma": as faixas e os vídeos; outro: só o que casar com ele.
    static std::vector<ItemManifesto> CongelarManifesto(const std::string &pastaTrabalho,
                                                        const std::string &padrao, Relato relatar);

    /// Arquivos que a extração produz na área de trabalho e que não vão
    /// para o acervo (os WAVs intermediários saem à parte, por extensão).
    /// A aprovação os apaga; o que não estiver aqui nem no manifesto fica.
    static const std::vector<std::string> SubprodutosDaExtracao;

    /// Subpasta do álbum com capa e encarte digitalizados — na área de trabalho e no acervo.
    static constexpr const char *PastaArtwork = "Artwork";

    /// Item do manifesto que é imagem do encarte, e não faixa: não vai para a biblioteca do WMP
    /// (LINUX: nem conta como faixa na BIBLIOTECA do ARCA).
    static bool EhArtwork(const std::string &strNome);

    static ResultadoIntegracao Integrar(const std::vector<ItemManifesto> &manifesto,
                                        const std::string &pastaTrabalho,
                                        const std::string &raizDoAcervo,
                                        const std::string &artista, const std::string &album,
                                        const std::string &idDoDisco,
                                        Relato relatar,
                                        ProgressoIntegracao progresso = nullptr);

    /// Grava bytes num arquivo do acervo sem nunca deixá-lo pela metade:
    /// escreve ao lado (.partial), desce até o disco, confere o hash e só
    /// então promove. Se já existe arquivo diferente com o nome, ele vira
    /// "nome.backup-aaaaMMddHHmmss" — com data, para que uma segunda troca
    /// não apague o backup da primeira. Idêntico: não toca. Lança em falha.
    static void GravarNoAcervo(const std::vector<uint8_t> &arrDados, const std::string &strFinal);

    /// Tira uma imagem do álbum sem apagá-la: ela vira backup datado ao
    /// lado, com o mesmo nome que uma substituição deixaria. Devolve o
    /// caminho do backup.
    static std::string RetirarDoAcervo(const std::string &strArquivo);

    /// Registra no ledger imagens acrescentadas a um álbum que já estava
    /// no acervo (capa e encarte digitalizados depois). Mesmo formato das
    /// aquisições, com "evento" dizendo o que foi — a procedência do
    /// acervo inclui o que entrou depois do disco. Também serve às
    /// imagens alteradas (giro, divisão) e às retiradas (aí o arquivo
    /// registrado é o backup).
    static bool RegistrarEncarteNoLedger(const std::string &raiz, const std::string &artista, const std::string &album,
                                         const std::string &pastaAlbum, const std::vector<std::string> &arquivos,
                                         const std::string &evento = "encarte acrescentado");

    /// Registra no ledger que uma faixa do acervo mudou de nome. O ledger é
    /// append-only: a linha da aquisição continua com o nome antigo, e esta
    /// diz qual era e qual é — com o SHA-256 do arquivo, que prova que é o
    /// mesmo conteúdo.
    static bool RegistrarRenomeacaoNoLedger(const std::string &raiz, const std::string &artista, const std::string &album,
                                            const std::string &pastaAlbum, const std::string &strNomeAntigo,
                                            const std::string &strCaminhoNovo);

    static std::string Hash(const std::string &arquivo);

    /// DECISAO-LORENZO (08/11/2026): antes de integrar, grava nas faixas FLAC da area de trabalho
    /// o artista e o album das caixas (WM/AlbumArtist e WM/AlbumTitle; caixa vazia nao mexe; o
    /// artista de cada faixa, WM/Author, fica) e refaz tamanho e SHA-256 do item regravado.
    /// Devolve "" se deu certo (intRegravadas = quantas mudaram), ou a mensagem de falha.
    static std::string GravarArtistaEAlbum(std::vector<ItemManifesto> &manifesto, const std::string &artista,
                                           const std::string &album, int &intRegravadas);

private:
    static std::string NomeDeBackup(const std::string &strArquivo);

    static bool EscreverLedger(const std::string &raiz, const std::string &artista, const std::string &album,
                               const std::string &destino, const std::string &idDoDisco,
                               const std::vector<ItemManifesto> &manifesto);

    static bool EscreverLedger(const std::string &raiz, const std::string &artista, const std::string &album,
                               const std::string &destino, const std::string &idDoDisco,
                               const std::vector<ItemManifesto> &manifesto, const std::string &strEvento,
                               const TRelatorioExtracao *objRelatorio = nullptr);

    static std::string Escapar(const std::string &s);

    /// Nome de pasta seguro para artista ou álbum. Além dos caracteres
    /// proibidos: o Windows corta pontos e espaços do fim (".." e "..."
    /// viravam a pasta de cima, e o material caía fora do álbum), e nomes
    /// de dispositivo (CON, NUL, COM1…) não podem ser pasta.
    static std::string Limpar(const std::string &nome, const std::string &padrao);

    static void Relatar(const Relato &r, const std::string &m) { if (r) r(m); }
};

} // namespace ARCA::Modelo

// ARCA no xOS - Modelo/BancoObras (de Modelo\BancoObras.cs).
// LINUX: o original era SQL Server Compact 3.5 SP2 (ARCA_Obras.sdf). Aqui e SQLite, com o MESMO
// esquema (tabelas Obra, ParteObra e Versao, as mesmas colunas, a mesma chave estrangeira com
// ON DELETE CASCADE e os mesmos indices), num arquivo novo ao lado: ARCA_Obras.sqlite. O .sdf nao
// e tocado: foi lido uma vez fora do xOS (src/arca/Ferramentas/ExportaObras.cs, numa copia) e
// convertido por ImportarExportacaoDoSdf.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace ARCA::Modelo {

enum class TipoParteObra { Album = 1, Faixa = 2 };

/// <summary>Uma obra: título, autor e capa próprios. É só agrupamento — nada dela vai para os arquivos de áudio.</summary>
class TObra
{
public:
    int intId = 0;
    std::string strTitulo;
    std::string strAutor;
    /// <summary>JPEG, ou null.</summary>
    // vazio = null
    std::vector<uint8_t> arrCapa;
    // LINUX: DateTime como texto "aaaa-MM-dd HH:mm:ss.fff" (hora local), que ordena como data
    std::string dtAlterada;
    int intQtdPartes = 0;

    std::string ToString() const { return strTitulo + (strAutor.empty() ? "" : " — " + strAutor); }
};

/// <summary>
/// Uma parte da obra: um álbum (a pasta dele) ou uma faixa (o arquivo).
///
/// O vínculo é o caminho, mantido em dia pelo próprio ARCA (a renomeação
/// atualiza o banco). O índice do CD (WM/MCDI) e o número da faixa ficam
/// guardados como impressão digital, para reencontrar a parte se o caminho
/// mudar por fora do ARCA.
/// </summary>
class TParteObra
{
public:
    int intId = 0;
    int intIdObra = 0;
    TipoParteObra eTipo = TipoParteObra::Album;
    int intOrdem = 0;
    /// <summary>Álbum: a pasta. Faixa: o arquivo.</summary>
    std::string strCaminho;
    /// <summary>Título do álbum, para mostrar quando a pasta não puder ser lida.</summary>
    std::string strAlbum;            // "" = null
    std::string strMcdi;             // "" = null
    /// <summary>Faixa: o número dela no disco; -1 se não houver.</summary>
    int intFaixa = -1;
};

/// <summary>
/// O banco de obras do ARCA: SQL Server Compact 3.5 SP2, um arquivo só
/// (ARCA_Obras.sdf) na raiz do acervo, ao lado do ARCA_Ledger.jsonl — vai
/// junto em qualquer cópia de segurança do acervo.
///
/// Decisões do Lorenzo (04/10/2026): uma obra tem título, um autor e uma
/// capa; um álbum pode estar em mais de uma obra; a gestão fica no banco,
/// não nos arquivos.
///
/// Uma operação por conexão: o SQL CE abre em milissegundos e o banco
/// nunca fica preso aberto entre um clique e outro.
/// </summary>
// LINUX: SQLite (ver o topo). O acervo e montado so de leitura: quem grava no banco abre a
// conexao dentro de um Base::GravacaoNoAcervo, que remonta o DADOS em leitura e escrita so durante
// a operacao. Erros lancam std::runtime_error, como o SqlCeException do original.
class TBancoObras
{
public:
    // LINUX: "ARCA_Obras.sdf" no original
    static constexpr const char *NomeArquivo = "ARCA_Obras.sqlite";
    // LINUX: o arquivo do original, que a conversao le (e nunca grava)
    static constexpr const char *NomeArquivoSdf = "ARCA_Obras.sdf";

    explicit TBancoObras(const std::string &strRaizAcervo);

    const std::string &Arquivo() const { return m_strArquivo; }

    /// <summary>O banco já existe? Quem só atualiza (renomeação) não deve criar um vazio.</summary>
    bool Existe() const;

    // ===============================================================
    /// <summary>Todas as obras, a usada por último primeiro (é a provável do próximo envio).</summary>
    std::vector<TObra> ListarObras();
    /// intId = 0 no TObra devolvido: nao achou (null no original)
    TObra Obter(int intId);
    int CriarObra(const std::string &strTitulo, const std::string &strAutor, const std::vector<uint8_t> &arrCapa);
    void AlterarTituloEAutor(int intId, const std::string &strTitulo, const std::string &strAutor);
    void AlterarCapa(int intId, const std::vector<uint8_t> &arrCapa);
    /// <summary>Apaga a obra e a lista de partes dela. Os arquivos do acervo não são tocados.</summary>
    void ExcluirObra(int intId);

    // ===============================================================
    /// <summary>As partes da obra, na ordem.</summary>
    std::vector<TParteObra> Partes(int intIdObra);
    /// <summary>
    /// Põe a parte no fim da obra. false se ela já estava (mesmo tipo e
    /// caminho): mandar duas vezes não duplica.
    /// </summary>
    bool AcrescentarParte(int intIdObra, const TParteObra &objParte);
    void RetirarParte(int intIdParte);
    /// <summary>Troca a parte de lugar com a vizinha (intDirecao -1 sobe, +1 desce).</summary>
    void Mover(int intIdObra, int intIdParte, int intDirecao);

    // ===============================================================
    /// <summary>
    /// Uma faixa foi renomeada pelo ARCA: as partes que apontavam para o
    /// caminho antigo passam a apontar para o novo. Devolve quantas.
    /// </summary>
    int AtualizarCaminhoDaFaixa(const std::string &strAntigo, const std::string &strNovo);
    /// <summary>O álbum da pasta mudou de nome (edição do álbum inteiro): o rótulo guardado acompanha.</summary>
    int AtualizarAlbumDaPasta(const std::string &strPasta, const std::string &strAlbum);

    // ===============================================================
    // LINUX: a conversao do .sdf (nao havia no original). strJsonl e o que o ExportaObras.exe
    // (src/arca/Ferramentas) gravou de uma COPIA do ARCA_Obras.sdf: uma linha JSON por registro
    // ({"tabela":"Obra",...}, {"tabela":"ParteObra",...}, {"tabela":"Versao",...}). Cria o banco
    // novo com os mesmos Id, ordens e datas; os caminhos do Windows ("E:\Músicas\Artista\Álbum")
    // passam a ser os do xOS (<strRaizAcervo>/Artista/Álbum). Recusa se o banco ja existir.
    // Devolve o resumo ("1 obra, 3 partes"); lanca em falha.
    static std::string ImportarExportacaoDoSdf(const std::string &strJsonl, const std::string &strRaizAcervo);
    // LINUX: um caminho do Windows do acervo antigo no caminho do xOS ("" se nao for do acervo)
    static std::string CaminhoDoWindows(const std::string &strCaminhoWindows, const std::string &strRaizAcervo);

private:
    static constexpr int intVersaoAtual = 1;
    std::string m_strArquivo;

    class Conexao;
    void Criar(sqlite3 *db);
    static TObra LerObra(void *st);
};

} // namespace ARCA::Modelo

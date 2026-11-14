// ARCA no xOS - Modelo/Biblioteca (de Modelo\Biblioteca.cs): Categoria, AlbumDaLista e Biblioteca.
#pragma once

#include "Interop/ColecaoMidia.h"
#include "Modelo/BancoObras.h"
#include "Modelo/ItemMidia.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ARCA::Modelo {

// LINUX: a comparacao de texto da cultura pt-BR sem distinguir caixa (StringComparer.Create(pt-BR,
// true), string.Compare(..., pt-BR, IgnoreCase)): primeiro sem acento, depois o acento.
int ComparaPtBrSemCaixa(const std::string &a, const std::string &b);
// LINUX: CompareInfo(pt-BR).IndexOf(a, b, IgnoreCase | IgnoreNonSpace) >= 0
bool ContemSemCaixaNemAcento(const std::string &a, const std::string &b);

class Categoria
{
public:
    std::string Nome;
    std::string Atributo;            // "" = null
    std::string Valor;               // "" = null
    /// <summary>
    /// Os MediaType do WMP em que a categoria mora. A consulta do WMP exige
    /// um tipo por vez; "Tudo" são três.
    /// </summary>
    std::vector<std::string> Tipos;
    bool Disponivel = true;
    std::string Observacao;
    /// <summary>
    /// Obras: a lista de "álbuns" é a das obras do banco do ARCA, e as
    /// faixas vêm das partes de cada uma — não de uma consulta do WMP.
    /// </summary>
    bool PorObra = false;

    Categoria(const std::string &nome, const std::string &atributo, const std::string &valor,
              std::vector<std::string> arrTipos = {});

    /// <summary>Categoria ainda não construída — aparece na navegação, mas anunciada como tal.</summary>
    static Categoria EmBreve(const std::string &nome, const std::string &observacao);

    /// <summary>
    /// A categoria se define pela classe de mídia do arquivo, e não só pelo
    /// MediaType: cada item tem de ser conferido nela.
    /// </summary>
    bool PorClasse() const;

    static Categoria Obras();

    std::string ToString() const { return Nome; }
};

/// <summary>
/// Uma linha da lista de álbuns. A primeira é a virtual &lt;todos&gt;, que
/// tira o filtro; as outras são os títulos de álbum como o WMP os guarda.
/// </summary>
class AlbumDaLista
{
public:
    std::string Nome;
    bool Todos = false;
    /// <summary>Na categoria Obras, a obra desta linha; 0 nas outras.</summary>
    int IdObra = 0;

    explicit AlbumDaLista(const std::string &strNome) : Nome(strNome) {}

    static AlbumDaLista DeObra(const TObra &objObra);
    static AlbumDaLista CriarTodos();

    std::string ToString() const { return Nome; }
};

/// <summary>
/// A biblioteca do Windows Media Player, lida pelo IWMPMediaCollection.
/// É a mesma base que o wmplayer.exe usa — a sonda confirmou os 5.154
/// itens vindo de uma instância nossa do controle.
/// </summary>
// LINUX: a "biblioteca do WMP" e a ColecaoMidia do ARCA (Interop/ColecaoMidia), com as mesmas
// operacoes. O original recebia o player e chegava a colecao por _player.Colecao; aqui recebe a
// colecao direto (o tocador e outra parte do programa e pode nem existir).
class Biblioteca
{
public:
    explicit Biblioteca(Interop::ColecaoMidia *colecao) : _colecao(colecao) {}

    /// <summary>A consulta em vigor. Cada ItemMidia guarda a sua e só lê dela.</summary>
    int Geracao() const { return _intGeracao; }

    int Total = 0;
    std::string Falha;               // "" = null
    /// <summary>Da última CarregarArquivos: quantos arquivos pedidos não estão na biblioteca do WMP.</summary>
    int ForaDaBiblioteca = 0;

    /// <summary>
    /// Audiolivro é áudio que não é música (WM/MediaClassPrimaryID) —
    /// audiolivro e fala juntos. A sonda de 04/10/2026 mostrou que o WMP
    /// põe esses arquivos no MediaType "other": por isso Música, que é
    /// "audio", já não os traz, e "Tudo" passou a incluir "other".
    /// </summary>
    static std::vector<Categoria> Categorias();

    /// <summary>
    /// Carrega exatamente estes arquivos, nesta ordem — as faixas de uma
    /// obra. Quantos não estão na biblioteca do WMP (ou não existem mais)
    /// fica em ForaDaBiblioteca.
    /// </summary>
    std::vector<ItemMidia> CarregarArquivos(const std::vector<std::string> &lstArquivos);

    /// <summary>
    /// Carrega uma categoria. Devolve apenas os marcadores das linhas: os
    /// campos de cada item são lidos quando a linha aparece na tela.
    /// </summary>
    /// strAlbum "" = null (o original nunca tem album de nome vazio: Juntar os descarta); categoria nullptr = null
    std::vector<ItemMidia> Carregar(const Categoria *categoria, const std::string &strAlbum = "");

    /// <summary>Busca por trecho em título, artista e álbum.</summary>
    std::vector<ItemMidia> Buscar(const Categoria *categoria, const std::string &texto);

    /// <summary>
    /// Os títulos de álbum da categoria, sem repetição, em ordem
    /// alfabética, com &lt;todos&gt; na frente.
    /// </summary>
    std::vector<AlbumDaLista> Albuns(const Categoria *categoria);

    /// <summary>Acrescenta um arquivo à biblioteca do player.</summary>
    bool Acrescentar(const std::string &caminho);

    /// <summary>
    /// Leva ao banco do WMP campos de texto que acabaram de ser gravados nos
    /// arquivos (Álbum, Artista, Ano…). Devolve quantos arquivos ficaram com
    /// todos os campos conferidos.
    /// </summary>
    int AtualizarNoPlayer(const std::vector<std::string> &lstCaminhos, const std::map<std::string, std::string> &dicCampos);

    /// <summary>
    /// Uma faixa mudou de caminho (renomeada): troca o item do WMP pelo do
    /// caminho novo. Devolve o resumo para a pessoa.
    /// </summary>
    std::string TrocarCaminho(const std::string &strAntigo, const std::string &strNovo);

    /// <summary>
    /// Tira os arquivos da biblioteca do WMP e os acrescenta de novo, para
    /// que ele releia o cabeçalho. Devolve quantos arquivos voltaram à biblioteca.
    /// </summary>
    int Reinscrever(const std::vector<std::string> &lstCaminhos);

    /// <summary>
    /// Objeto IWMPMedia da posição indicada. Quem chama é dono e precisa
    /// liberar — o ItemMidia faz isso logo depois de copiar os campos.
    /// </summary>
    Interop::MidiaWmpPtr ObterMedia(int indice, int geracao);

private:
    Interop::ColecaoMidia *_colecao;
    // O resultado em vigor: uma lista do WMP por tipo de mídia consultado
    // (a consulta exige um tipo por vez), lidas uma depois da outra.
    std::vector<Interop::ListaWmpPtr> _lstListas;
    // Posição, no resultado inteiro, do primeiro item de cada lista.
    std::vector<int> _lstInicios;
    int _intGeracao = 0;
    // Tipo de mídia de cada lista, na ordem. Vazio quando a lista é o
    // getAll/getByAttribute, que não tem tipo único.
    std::vector<std::string> _arrTipos;
    // Posições conferidas: quando a consulta do WMP não é confiável, cada
    // item é conferido no próprio arquivo, e a lista mostra só os que
    // passaram. null = a lista inteira vale.
    std::shared_ptr<std::vector<int>> _lstMapa;
    // Os álbuns de cada tipo de mídia, guardados pelo Albuns(): dizem se
    // vale consultar um tipo por um álbum.
    std::map<std::string, std::set<std::string>> _dicAlbunsPorTipo;

    static std::vector<std::string> TiposDa(const Categoria *categoria);
    static bool DaCategoria(const Categoria *categoria, const Interop::MidiaWmp &m);
    static bool MesmoGuid(const std::string &strA, const std::string &strB);
    void Consultar(Interop::ColecaoMidia *colecao, const Interop::ConsultaWmp &consulta,
                   const std::vector<std::string> &arrTipos, const std::string &strOrdem);
    void Guardar(Interop::ListaWmpPtr objLista);
    bool NenhumaLista() const;
    static int Qtd(const Interop::ListaWmpPtr &objLista);
    int QtdBruta() const;
    int ListaDe(int intIndice, int &intDentro) const;
    void Conferir(const std::function<bool(const Interop::MidiaWmp &)> &aceita, bool blnPoupaAudio);
    static std::string Campo(const Interop::MidiaWmp &m, const std::string &strAtributo);
    static bool Contem(const std::string &strCampo, const std::string &strTrecho);
    static std::shared_ptr<std::set<std::string>> AlbunsDoIndice(Interop::ColecaoMidia *colecao, const std::string &tipo);
    static std::shared_ptr<std::set<std::string>> AlbunsDosItens(Interop::ListaWmpPtr objLista, const Categoria *categoria);
    static void Juntar(std::set<std::string> &setNomes, const std::string &strNome);
    static std::vector<Interop::MidiaWmpPtr> AcharPorCaminho(Interop::ColecaoMidia *colecao, const std::string &strCaminho);
    Interop::MidiaWmpPtr ItemBruto(int indice) const;
    void Liberar();
};

} // namespace ARCA::Modelo

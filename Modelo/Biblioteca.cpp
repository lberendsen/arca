// ARCA no xOS - Modelo/Biblioteca (de Modelo\Biblioteca.cs).
// LINUX: cada chamada ao COM do WMP (PlayerWmp.Chama(colecao, "getAll") e afins) vira a operacao de
// mesmo nome da ColecaoMidia. Os Marshal.ReleaseComObject somem: os itens sao std::shared_ptr.
#include "Biblioteca.h"

#include "Base/Texto.h"
#include "Interop/EditorFlac.h"
#include "Modelo/ClasseMidia.h"

#include <algorithm>

namespace ARCA::Modelo {

using namespace ARCA::Base;
using Interop::ConsultaWmp;
using Interop::ListaWmpPtr;
using Interop::MidiaWmp;
using Interop::MidiaWmpPtr;

int ComparaPtBrSemCaixa(const std::string &a, const std::string &b)
{
    std::string pa = Minusculas(SemAcentos(a)), pb = Minusculas(SemAcentos(b));
    int c = pa.compare(pb);
    if (c) return c < 0 ? -1 : 1;
    c = Minusculas(a).compare(Minusculas(b));
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

bool ContemSemCaixaNemAcento(const std::string &a, const std::string &b)
{
    return Base::Contem(SemAcentos(a), SemAcentos(b), true);
}

// =====================================================================================
// Categoria
// =====================================================================================
Categoria::Categoria(const std::string &nome, const std::string &atributo, const std::string &valor,
                     std::vector<std::string> arrTipos)
    : Nome(nome), Atributo(atributo), Valor(valor), Tipos(std::move(arrTipos))
{
    Disponivel = true;
}

Categoria Categoria::EmBreve(const std::string &nome, const std::string &observacao)
{
    Categoria c(nome, "", "");
    c.Disponivel = false;
    c.Observacao = observacao;
    return c;
}

bool Categoria::PorClasse() const
{
    return Atributo == TClasseMidia::AtributoPrimario;
}

Categoria Categoria::Obras()
{
    Categoria c("Obras", "", "", {"audio", "other"});
    c.PorObra = true;
    return c;
}

// =====================================================================================
// AlbumDaLista
// =====================================================================================
AlbumDaLista AlbumDaLista::DeObra(const TObra &objObra)
{
    AlbumDaLista a(objObra.ToString());
    a.IdObra = objObra.intId;
    return a;
}

AlbumDaLista AlbumDaLista::CriarTodos()
{
    AlbumDaLista a("<todos>");
    a.Todos = true;
    return a;
}

// =====================================================================================
// Biblioteca
// =====================================================================================
std::vector<Categoria> Biblioteca::Categorias()
{
    std::vector<Categoria> l;
    l.push_back(Categoria("Tudo", "", "", {"audio", "video", "other"}));
    l.push_back(Categoria("Música", "MediaType", "audio", {"audio"}));
    l.push_back(Categoria("Audiolivro", TClasseMidia::AtributoPrimario,
                          "{" + Maiusculas(TClasseMidia::guidPrimariaAudioNaoMusica) + "}", {"other"}));
    l.push_back(Categoria("Vídeos", "MediaType", "video", {"video"}));
    l.push_back(Categoria::Obras());
    return l;
}

/// <summary>
/// Carrega exatamente estes arquivos, nesta ordem — as faixas de uma
/// obra. Quantos não estão na biblioteca do WMP (ou não existem mais)
/// fica em ForaDaBiblioteca.
///
/// O WMP não procura por caminho, mas procura bem por álbum: cada
/// título de álbum dos arquivos vira uma consulta (em "audio" e em
/// "other", que é onde ficam audiolivro e fala), e o resultado é
/// casado com os caminhos pedidos. Álbum de mesmo nome de outro artista
/// vem junto na consulta e fica de fora no casamento.
/// </summary>
std::vector<ItemMidia> Biblioteca::CarregarArquivos(const std::vector<std::string> &lstArquivos)
{
    Falha.clear();
    Total = 0;
    ForaDaBiblioteca = 0;
    Liberar();

    std::vector<ItemMidia> itens;
    try
    {
        Interop::ColecaoMidia *colecao = _colecao;
        if (colecao == nullptr) { Falha = "O player não expôs a coleção de mídia."; return itens; }

        std::set<std::string> setAlbuns;
        // LINUX: a ordem do HashSet do .NET e a da insercao; aqui a ordem em que apareceram
        std::vector<std::string> lstAlbuns;
        for (const std::string &strArquivo : lstArquivos)
        {
            Interop::EditorFlac objEditor;
            if (!objEditor.Abrir(strArquivo)) continue;
            for (const Interop::AtributoFlac &a : objEditor.LerTudo())
                if (a.Nome == "WM/AlbumTitle" && !a.Valor.empty())
                {
                    if (setAlbuns.insert(a.Valor).second) lstAlbuns.push_back(a.Valor);
                    break;
                }
        }

        std::vector<std::string> lstTipos;
        for (const std::string &strAlbum : lstAlbuns)
        {
            Interop::ConsultaWmpPtr consulta = colecao->createQuery();
            if (!consulta) { Falha = "Esta versão do player não oferece consulta (IWMPMediaCollection2)."; return itens; }
            consulta->addCondition("WM/AlbumTitle", "Equals", strAlbum);
            for (const char *strTipo : {"audio", "other"})
            {
                Guardar(colecao->getPlaylistByQuery(*consulta, strTipo, "WM/TrackNumber", true));
                lstTipos.push_back(strTipo);
            }
        }
        _arrTipos = lstTipos;

        // Caminho → posição no resultado bruto.
        std::map<std::string, int> dicPosicao;      // OrdinalIgnoreCase: a chave vai em minusculas
        int intBruto = QtdBruta();
        for (int i = 0; i < intBruto; i++)
        {
            MidiaWmpPtr m = ItemBruto(i);
            if (!m) continue;
            std::string strCaminho = Campo(*m, "SourceURL");
            if (!strCaminho.empty() && !dicPosicao.count(Minusculas(strCaminho))) dicPosicao[Minusculas(strCaminho)] = i;
        }

        auto lstMapa = std::make_shared<std::vector<int>>();
        for (const std::string &strArquivo : lstArquivos)
        {
            auto it = dicPosicao.find(Minusculas(strArquivo));
            if (it != dicPosicao.end()) lstMapa->push_back(it->second);
            else ForaDaBiblioteca++;
        }
        _lstMapa = lstMapa;

        Total = (int) _lstMapa->size();
        for (int i = 0; i < Total; i++) itens.emplace_back(this, i, _intGeracao);
    }
    catch (const std::exception &ex)
    {
        Falha = std::string("Exception: ") + ex.what();
    }
    return itens;
}

std::vector<ItemMidia> Biblioteca::Carregar(const Categoria *categoria, const std::string &strAlbum)
{
    Falha.clear();
    Total = 0;
    Liberar();

    std::vector<ItemMidia> itens;
    try
    {
        Interop::ColecaoMidia *colecao = _colecao;
        if (colecao == nullptr) { Falha = "O player não expôs a coleção de mídia."; return itens; }

        if (!strAlbum.empty())
        {
            // Com álbum escolhido, só o campo Álbum, igual por inteiro —
            // nada de "contém", que traria álbuns de nome parecido.
            //
            // Só se consulta o tipo de mídia em que o álbum existe. O
            // WMP não indexa Álbum nos vídeos e, na consulta de vídeo,
            // descarta a condição calado: devolvia o acervo inteiro de
            // vídeos embaixo das faixas do álbum ("Alma Lírica" vinha
            // com "A liberdade é azul").
            std::vector<std::string> lstTipos;
            for (const std::string &tipo : TiposDa(categoria))
            {
                auto it = _dicAlbunsPorTipo.find(tipo);
                if (it == _dicAlbunsPorTipo.end() || it->second.count(strAlbum)) lstTipos.push_back(tipo);
            }
            if (lstTipos.empty()) return itens;   // álbum que sumiu da biblioteca

            Interop::ConsultaWmpPtr consulta = colecao->createQuery();
            if (!consulta)
            {
                Falha = "Esta versão do player não oferece consulta (IWMPMediaCollection2).";
                return itens;
            }
            consulta->addCondition("WM/AlbumTitle", "Equals", strAlbum);
            Consultar(colecao, *consulta, lstTipos, "WM/TrackNumber");

            // E mesmo assim confere item por item: são poucos (um
            // álbum), e a consulta do WMP já provou que pode ignorar a
            // condição sem avisar. Só fica o que tem este álbum no
            // próprio arquivo, letra por letra — e, em Audiolivro, a
            // classe: as entrevistas do Thriller, não as canções.
            if (QtdBruta() > 0)
                Conferir([&](const MidiaWmp &m) { return Campo(m, "WM/AlbumTitle") == strAlbum && DaCategoria(categoria, m); }, false);
        }
        else
        {
            ListaWmpPtr objLista = categoria == nullptr || categoria->Atributo.empty()
                ? colecao->getAll()
                : colecao->getByAttribute(categoria->Atributo, categoria->Valor);
            Guardar(objLista);
        }

        if (NenhumaLista()) { Falha = "A consulta à biblioteca não devolveu nada."; return itens; }

        Total = _lstMapa ? (int) _lstMapa->size() : QtdBruta();
        itens.reserve((size_t) Total);
        for (int i = 0; i < Total; i++) itens.emplace_back(this, i, _intGeracao);
    }
    catch (const std::exception &ex)
    {
        Falha = std::string("Exception: ") + ex.what();
    }
    return itens;
}

/// <summary>
/// Busca por trecho em título, artista e álbum.
///
/// Quem procura é o banco do WMP, não o ARCA. Filtrar no cliente
/// obrigaria a ler os campos dos 5.154 itens para comparar — justamente
/// o que a carga preguiçosa evita. O getPlaylistByQuery responde pelo
/// índice do próprio player.
/// </summary>
std::vector<ItemMidia> Biblioteca::Buscar(const Categoria *categoria, const std::string &texto)
{
    Falha.clear();
    Total = 0;
    Liberar();

    std::vector<ItemMidia> itens;
    if (texto.empty()) return itens;

    try
    {
        Interop::ColecaoMidia *colecao = _colecao;
        if (colecao == nullptr) { Falha = "O player não expôs a coleção."; return itens; }

        Interop::ConsultaWmpPtr consulta = colecao->createQuery();
        if (!consulta)
        {
            Falha = "Esta versão do player não oferece consulta (IWMPMediaCollection2).";
            return itens;
        }

        // Cada beginNextGroup abre um grupo novo, e grupos se combinam
        // por OU: casa quem tiver o trecho no título, no artista ou no
        // álbum.
        consulta->addCondition("Title", "Contains", texto);
        consulta->beginNextGroup();
        consulta->addCondition("Author", "Contains", texto);
        consulta->beginNextGroup();
        consulta->addCondition("WM/AlbumTitle", "Contains", texto);

        Consultar(colecao, *consulta, TiposDa(categoria), "Title");

        if (NenhumaLista()) { Falha = "A consulta não devolveu resultado."; return itens; }

        // Nos vídeos o WMP descarta a condição de Álbum, e um grupo do
        // OU que fica vazio casa com tudo: a busca devolveria todos os
        // vídeos. Fora do áudio — onde a consulta funciona — cada item
        // é conferido; em "other" também a classe, porque lá pode haver
        // o que não é audiolivro.
        Conferir([&](const MidiaWmp &m) {
            return (Contem(Campo(m, "Title"), texto) || Contem(Campo(m, "Author"), texto)
                    || Contem(Campo(m, "WM/AlbumTitle"), texto)) && DaCategoria(categoria, m);
        }, true);

        Total = _lstMapa ? (int) _lstMapa->size() : QtdBruta();
        for (int i = 0; i < Total; i++) itens.emplace_back(this, i, _intGeracao);
    }
    catch (const std::exception &ex)
    {
        Falha = std::string("Exception: ") + ex.what();
    }
    return itens;
}

/// <summary>
/// Os tipos de mídia da categoria. Antes "Tudo" ia só com "audio", e a
/// busca nunca encontrava vídeo; sem categoria, valem os três.
/// </summary>
std::vector<std::string> Biblioteca::TiposDa(const Categoria *categoria)
{
    return categoria == nullptr || categoria->Tipos.empty()
        ? std::vector<std::string>{"audio", "video", "other"}
        : categoria->Tipos;
}

/// <summary>
/// O item pertence à categoria? Só as categorias por classe conferem
/// algo: Audiolivro exige a classe "áudio que não é música" no item.
/// </summary>
bool Biblioteca::DaCategoria(const Categoria *categoria, const MidiaWmp &m)
{
    if (categoria == nullptr || !categoria->PorClasse()) return true;
    return MesmoGuid(Campo(m, categoria->Atributo), categoria->Valor);
}

/// <summary>O WMP devolve "{01CD0F29-…}"; compara como GUID, sem depender de chaves nem caixa.</summary>
bool Biblioteca::MesmoGuid(const std::string &strA, const std::string &strB)
{
    if (strA.empty() || strB.empty()) return false;
    std::string a = Interop::EditorFlac::NormalizarGuid(strA), b = Interop::EditorFlac::NormalizarGuid(strB);
    return !a.empty() && a == b;
}

/// <summary>Roda a consulta em cada tipo de mídia, uma lista por tipo.</summary>
void Biblioteca::Consultar(Interop::ColecaoMidia *colecao, const ConsultaWmp &consulta,
                           const std::vector<std::string> &arrTipos, const std::string &strOrdem)
{
    _arrTipos = arrTipos;
    for (const std::string &strTipo : arrTipos)
        Guardar(colecao->getPlaylistByQuery(consulta, strTipo, strOrdem, true));
}

/// <summary>
/// Acrescenta uma lista ao resultado. Lista que não veio também ocupa
/// a sua vaga (vazia), para que a posição continue casando com o tipo.
/// </summary>
void Biblioteca::Guardar(ListaWmpPtr objLista)
{
    _lstInicios.push_back(QtdBruta());
    _lstListas.push_back(std::move(objLista));
}

/// <summary>O WMP não devolveu lista nenhuma — falha, não "zero itens".</summary>
bool Biblioteca::NenhumaLista() const
{
    for (const ListaWmpPtr &objLista : _lstListas)
        if (objLista) return false;
    return true;
}

int Biblioteca::Qtd(const ListaWmpPtr &objLista)
{
    return objLista ? (int) objLista->size() : 0;
}

int Biblioteca::QtdBruta() const
{
    int intUltima = (int) _lstListas.size() - 1;
    return intUltima < 0 ? 0 : _lstInicios[(size_t) intUltima] + Qtd(_lstListas[(size_t) intUltima]);
}

/// <summary>A lista em que cai a posição bruta, e a posição dentro dela.</summary>
int Biblioteca::ListaDe(int intIndice, int &intDentro) const
{
    for (int k = (int) _lstListas.size() - 1; k >= 0; k--)
    {
        if (intIndice >= _lstInicios[(size_t) k])
        {
            intDentro = intIndice - _lstInicios[(size_t) k];
            return k;
        }
    }
    intDentro = intIndice;
    return -1;
}

/// <summary>
/// Confere cada item do resultado no próprio IWMPMedia e guarda só as
/// posições aprovadas. Com blnPoupaAudio, o áudio — onde a consulta do
/// WMP é confiável — passa sem conferência.
/// </summary>
void Biblioteca::Conferir(const std::function<bool(const MidiaWmp &)> &aceita, bool blnPoupaAudio)
{
    auto lstMapa = std::make_shared<std::vector<int>>();
    int intTotal = QtdBruta();
    bool blnConferiu = false;
    for (int i = 0; i < intTotal; i++)
    {
        int intDentro;
        int intLista = ListaDe(i, intDentro);
        std::string strTipo = intLista >= 0 && intLista < (int) _arrTipos.size() ? _arrTipos[(size_t) intLista] : "";
        if (blnPoupaAudio && strTipo == "audio") { lstMapa->push_back(i); continue; }

        blnConferiu = true;
        MidiaWmpPtr m = ItemBruto(i);
        if (!m) continue;
        if (aceita(*m)) lstMapa->push_back(i);
    }
    if (blnConferiu) _lstMapa = lstMapa;
}

std::string Biblioteca::Campo(const MidiaWmp &m, const std::string &strAtributo)
{
    return m.getItemInfo(strAtributo);
}

bool Biblioteca::Contem(const std::string &strCampo, const std::string &strTrecho)
{
    // CompareInfo(pt-BR).IndexOf(..., IgnoreCase): sem caixa, com acento
    return !strCampo.empty() && Base::Contem(strCampo, strTrecho, true);
}

/// <summary>
/// Os títulos de álbum da categoria, sem repetição, em ordem
/// alfabética, com &lt;todos&gt; na frente. Vêm prontos do índice do WMP
/// (getAttributeStringCollection): não se lê item por item.
///
/// O título é a chave: dois álbuns de artistas diferentes com o mesmo
/// nome ("Greatest Hits") aparecem como um só, e escolhê-lo mostra os
/// dois.
///
/// Exceção: Audiolivro. O índice dá os álbuns de todo o "other", não
/// só dos audiolivros, então ali os nomes saem dos próprios itens —
/// que são poucos. É o que faz o "Thriller" aparecer em Audiolivro
/// só com as entrevistas.
/// </summary>
std::vector<AlbumDaLista> Biblioteca::Albuns(const Categoria *categoria)
{
    std::vector<AlbumDaLista> lstAlbuns;
    lstAlbuns.push_back(AlbumDaLista::CriarTodos());
    Interop::ColecaoMidia *colecao = _colecao;
    if (colecao == nullptr) return lstAlbuns;

    _dicAlbunsPorTipo.clear();
    std::set<std::string> setVistos;
    std::vector<std::string> lstNomes;
    for (const std::string &tipo : TiposDa(categoria))
    {
        std::shared_ptr<std::set<std::string>> setDoTipo;
        if (categoria != nullptr && categoria->PorClasse())
            setDoTipo = AlbunsDosItens(colecao->getByAttribute(categoria->Atributo, categoria->Valor), categoria);
        else
        {
            setDoTipo = AlbunsDoIndice(colecao, tipo);
            // O índice não respondeu por este tipo: lê dos itens.
            if (!setDoTipo) setDoTipo = AlbunsDosItens(colecao->getByAttribute("MediaType", tipo), nullptr);
        }
        if (!setDoTipo) continue;
        _dicAlbunsPorTipo[tipo] = *setDoTipo;
        for (const std::string &strNome : *setDoTipo)
            if (setVistos.insert(strNome).second) lstNomes.push_back(strNome);
    }

    std::sort(lstNomes.begin(), lstNomes.end(), [](const std::string &a, const std::string &b) { return ComparaPtBrSemCaixa(a, b) < 0; });
    for (const std::string &strNome : lstNomes) lstAlbuns.push_back(AlbumDaLista(strNome));
    return lstAlbuns;
}

/// <summary>Os álbuns de um tipo de mídia pelo índice do WMP, ou null se ele não responder.</summary>
std::shared_ptr<std::set<std::string>> Biblioteca::AlbunsDoIndice(Interop::ColecaoMidia *colecao, const std::string &tipo)
{
    auto objNomes = colecao->getAttributeStringCollection("WM/AlbumTitle", tipo);
    if (!objNomes) return nullptr;
    auto setDoTipo = std::make_shared<std::set<std::string>>();
    for (const std::string &s : *objNomes) Juntar(*setDoTipo, s);
    return setDoTipo;
}

/// <summary>Os álbuns lidos item por item de uma lista — só dos itens da categoria.</summary>
std::shared_ptr<std::set<std::string>> Biblioteca::AlbunsDosItens(ListaWmpPtr objLista, const Categoria *categoria)
{
    if (!objLista) return nullptr;
    auto setDoTipo = std::make_shared<std::set<std::string>>();
    for (const MidiaWmpPtr &m : *objLista)
        if (m && DaCategoria(categoria, *m)) Juntar(*setDoTipo, Campo(*m, "WM/AlbumTitle"));
    return setDoTipo;
}

void Biblioteca::Juntar(std::set<std::string> &setNomes, const std::string &strNome)
{
    // Sem álbum não é um álbum: essas faixas continuam em <todos>.
    if (!Trim(strNome).empty()) setNomes.insert(strNome);
}

/// <summary>
/// Acrescenta um arquivo à biblioteca do player.
///
/// É o que fecha o ciclo de AQUISIÇÃO: aprovado e movido, o material
/// aparece na BIBLIOTECA na hora, sem esperar uma varredura futura do
/// Windows Media Player.
/// </summary>
bool Biblioteca::Acrescentar(const std::string &caminho)
{
    try
    {
        Interop::ColecaoMidia *colecao = _colecao;
        if (colecao == nullptr) return false;
        MidiaWmpPtr m = colecao->add(caminho);
        if (!m) return false;
        return true;
    }
    catch (...) { return false; }
}

/// <summary>
/// Leva ao banco do WMP campos de texto que acabaram de ser gravados nos
/// arquivos (Álbum, Artista, Ano…). Sem isto o banco fica com o valor
/// antigo até o WMP reler a pasta por conta própria — que, como o
/// audiolivro de 04/10/2026 mostrou, pode só acontecer quando o
/// wmplayer.exe abre.
///
/// Pelo setItemInfo do próprio player, e não tirando e pondo de volta:
/// assim ele não esquece contagem de reproduções nem data de entrada.
/// Devolve quantos arquivos ficaram com todos os campos conferidos.
/// </summary>
int Biblioteca::AtualizarNoPlayer(const std::vector<std::string> &lstCaminhos, const std::map<std::string, std::string> &dicCampos)
{
    Interop::ColecaoMidia *colecao = _colecao;
    if (colecao == nullptr || lstCaminhos.empty() || dicCampos.empty()) return 0;

    std::set<std::string> setCaminhos;      // OrdinalIgnoreCase
    for (const std::string &c : lstCaminhos) setCaminhos.insert(Minusculas(c));
    int intAtualizados = 0;
    for (const char *strTipo : {"audio", "other"})
    {
        ListaWmpPtr objLista = colecao->getByAttribute("MediaType", strTipo);
        if (!objLista) continue;
        for (const MidiaWmpPtr &m : *objLista)
        {
            try
            {
                std::string strCaminho = m ? Campo(*m, "SourceURL") : "";
                if (strCaminho.empty() || !setCaminhos.count(Minusculas(strCaminho))) continue;

                bool blnTodos = true;
                for (const auto &objCampo : dicCampos)
                {
                    m->setItemInfo(objCampo.first, objCampo.second);
                    // Conferido: o setItemInfo falha calado em atributo que o player trata como só leitura.
                    if (Campo(*m, objCampo.first) != objCampo.second) blnTodos = false;
                }
                // LINUX: a classe de midia decide o MediaType so na inscricao; aqui so guarda
                colecao->Guardar(m);
                if (blnTodos) intAtualizados++;
            }
            catch (...) {}
        }
    }
    return intAtualizados;
}

namespace {
/// <summary>
/// Dados que só o banco do WMP guarda de uma faixa e que se perderiam ao
/// trocar o item antigo pelo novo. São levados ao item novo e
/// conferidos — o que o player não aceitar é dito no resumo.
/// </summary>
const char *const s_arrDadosDoPlayer[][2] = {
    {"UserPlayCount", "contagem de reproduções"},
    {"UserRating", "nota"},
    {"UserLastPlayedTime", "última reprodução"},
};
} // namespace

/// <summary>
/// Uma faixa mudou de caminho (renomeada): troca o item do WMP pelo do
/// caminho novo. O WMP não tem "renomear" — o SourceURL é só leitura —,
/// então sai o antigo e entra o novo, levando contagem de reproduções,
/// nota e última reprodução. Devolve o resumo para a pessoa.
/// </summary>
std::string Biblioteca::TrocarCaminho(const std::string &strAntigo, const std::string &strNovo)
{
    // LINUX: "WMP:" nos textos do original vira "Biblioteca:" (no xOS a biblioteca e a do ARCA)
    Interop::ColecaoMidia *colecao = _colecao;
    if (colecao == nullptr) return "Biblioteca: o player não expôs a biblioteca; a faixa entra quando ele reler a pasta.";

    std::map<std::string, std::string> dicDados;
    std::vector<MidiaWmpPtr> lstAntigos = AcharPorCaminho(colecao, strAntigo);
    for (const MidiaWmpPtr &m : lstAntigos)
    {
        for (const auto &arrDado : s_arrDadosDoPlayer)
            if (!dicDados.count(arrDado[0]))
            {
                std::string strValor = Campo(*m, arrDado[0]);
                if (!strValor.empty()) dicDados[arrDado[0]] = strValor;
            }
        colecao->remove(m, false);   // false: o arquivo fica
    }

    // O monitor de pastas do WMP pode ter visto a renomeação e posto o
    // novo antes de nós: aí o add falha, mas o item já existe.
    MidiaWmpPtr objNovo = colecao->add(strNovo);
    if (!objNovo)
    {
        std::vector<MidiaWmpPtr> lstNovos = AcharPorCaminho(colecao, strNovo);
        objNovo = !lstNovos.empty() ? lstNovos[0] : nullptr;
    }
    if (!objNovo)
        return std::string("Biblioteca: a faixa com o nome novo ainda não entrou na biblioteca; entra quando ele reler a pasta") +
               (!dicDados.empty() ? " (sem contagem de reproduções e nota anteriores)." : ".");

    std::vector<std::string> lstLevados, lstPerdidos;
    for (const auto &arrDado : s_arrDadosDoPlayer)
    {
        auto it = dicDados.find(arrDado[0]);
        if (it == dicDados.end()) continue;
        objNovo->setItemInfo(arrDado[0], it->second);
        if (Campo(*objNovo, arrDado[0]) == it->second) lstLevados.push_back(arrDado[1]);
        else lstPerdidos.push_back(arrDado[1]);
    }
    colecao->Guardar(objNovo);

    std::string strResumo = "Biblioteca: atualizada";
    if (!lstLevados.empty()) strResumo += ", com " + Junta(lstLevados, ", ");
    if (!lstPerdidos.empty()) strResumo += "; o player não aceitou: " + Junta(lstPerdidos, ", ");
    if (lstAntigos.empty()) strResumo += " (o nome antigo não estava nela)";
    return strResumo + ".";
}

/// <summary>Os itens do WMP com este caminho, em áudio e "other". Quem chama libera.</summary>
std::vector<MidiaWmpPtr> Biblioteca::AcharPorCaminho(Interop::ColecaoMidia *colecao, const std::string &strCaminho)
{
    std::vector<MidiaWmpPtr> lstAchados;
    for (const char *strTipo : {"audio", "other"})
    {
        ListaWmpPtr objLista = colecao->getByAttribute("MediaType", strTipo);
        if (!objLista) continue;
        for (const MidiaWmpPtr &m : *objLista)
            if (m && IgualSemCaixa(Campo(*m, "SourceURL"), strCaminho)) lstAchados.push_back(m);
    }
    return lstAchados;
}

/// <summary>
/// Tira os arquivos da biblioteca do WMP e os acrescenta de novo, para
/// que ele releia o cabeçalho. É na inscrição que o WMP decide o
/// MediaType: a sonda de 04/10/2026 mostrou que áudio que não é música
/// vai para "other" — mas só de arquivo inscrito já com a marca. Nada
/// garante que ele releia sozinho um arquivo que já conhece.
///
/// Custo: o WMP esquece o que só ele guarda dessas faixas (contagem de
/// reproduções, data em que entraram). O arquivo não é tocado aqui.
/// Devolve quantos arquivos voltaram à biblioteca.
/// </summary>
int Biblioteca::Reinscrever(const std::vector<std::string> &lstCaminhos)
{
    Interop::ColecaoMidia *colecao = _colecao;
    if (colecao == nullptr || lstCaminhos.empty()) return 0;

    // Os itens são achados pelo caminho, numa passada só por tipo: a
    // marca antiga pode ter deixado a faixa em "audio" ou em "other".
    std::set<std::string> setCaminhos;
    for (const std::string &c : lstCaminhos) setCaminhos.insert(Minusculas(c));
    std::vector<MidiaWmpPtr> lstAchados;
    for (const char *strTipo : {"audio", "other"})
    {
        ListaWmpPtr objLista = colecao->getByAttribute("MediaType", strTipo);
        if (!objLista) continue;
        for (const MidiaWmpPtr &m : *objLista)
        {
            std::string strCaminho = m ? Campo(*m, "SourceURL") : "";
            if (!strCaminho.empty() && setCaminhos.count(Minusculas(strCaminho))) lstAchados.push_back(m);
        }
    }

    // Primeiro sai tudo, depois entra: remover durante a varredura
    // mexeria na lista que está sendo lida.
    for (const MidiaWmpPtr &m : lstAchados) colecao->remove(m, false);   // false: o arquivo fica

    int intVoltaram = 0;
    for (const std::string &strCaminho : lstCaminhos)
        if (Acrescentar(strCaminho)) intVoltaram++;
    return intVoltaram;
}

MidiaWmpPtr Biblioteca::ObterMedia(int indice, int geracao)
{
    // Item de uma consulta anterior: a posição dele não quer dizer mais
    // nada na lista atual.
    if (geracao != _intGeracao) return nullptr;
    if (_lstMapa)
    {
        if (indice < 0 || indice >= (int) _lstMapa->size()) return nullptr;
        indice = (*_lstMapa)[(size_t) indice];
    }
    return ItemBruto(indice);
}

/// <summary>Posição no resultado do WMP, antes da conferência.</summary>
MidiaWmpPtr Biblioteca::ItemBruto(int indice) const
{
    int intDentro;
    int intLista = ListaDe(indice, intDentro);
    ListaWmpPtr objLista = intLista >= 0 ? _lstListas[(size_t) intLista] : nullptr;
    if (!objLista) return nullptr;
    if (intDentro < 0 || intDentro >= (int) objLista->size()) return nullptr;
    return (*objLista)[(size_t) intDentro];
}

void Biblioteca::Liberar()
{
    // Toda consulta nova começa uma geração nova: os itens da lista
    // anterior deixam de valer, em vez de ler a faixa que agora ocupa
    // a mesma posição na lista nova.
    _intGeracao++;
    _lstListas.clear();
    _lstInicios.clear();
    _lstMapa = nullptr;
    _arrTipos.clear();
}

} // namespace ARCA::Modelo

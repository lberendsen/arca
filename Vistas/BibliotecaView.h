// ARCA no xOS - Vistas/BibliotecaView: o modulo BIBLIOTECA (do MainWindow.xaml, o Grid
// ModuloBiblioteca, e do MainWindow.xaml.cs, de AoTrocarCategoria ate Pular, mais o que o AoCarregar
// e o AoTrocarModulo faziam por ela).
// LINUX: no original a BIBLIOTECA mora DENTRO da MainWindow (os mesmos campos e metodos, na mesma
// classe). Aqui e um VistaModulo proprio, que recebe a MainWindow para mexer no casco (TxtTrilha,
// CaixaBusca, o adiador da busca, a aba EM EXECUCAO, o _player) e liga os ganchos dela
// (AplicarBusca, Pular, TocarSelecionadaSeOutra, AtualizarEstadoEdicaoSeFaixa, Avisar). Os nomes dos
// campos e metodos sao os do original.
#pragma once

#include "MainWindow.h"
#include "Interop/EditorFlac.h"
#include "Modelo/BancoObras.h"
#include "Modelo/Biblioteca.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ARCA::Vistas {

class BibliotecaView : public VistaModulo
{
public:
    explicit BibliotecaView(MainWindow &janela);

    // MainWindow.AoTrocarModulo: o bloco "if (naBiblioteca)" do original
    void Ativar(bool ativo) override;

    // ---- os elementos com x:Name do Grid ModuloBiblioteca ----
    std::shared_ptr<ListBox> ListaCategorias;
    std::shared_ptr<TextBox> CaixaAlbum;
    std::shared_ptr<TextBlock> DicaAlbum;
    std::shared_ptr<ListBox> ListaAlbuns;
    std::shared_ptr<TextBlock> TxtRodape;
    std::shared_ptr<DataGrid> ListaItens;
    std::shared_ptr<Border> PainelAviso;
    std::shared_ptr<TextBlock> TxtAvisoTitulo, TxtAvisoTexto;
    std::shared_ptr<TextBlock> TxtArquivoSel;
    std::shared_ptr<Button> BtnArtworkAlbum, BtnConferirAlbum, BtnConteudoAlbum, BtnEnviarObra;
    std::shared_ptr<TextBlock> TxtEstadoEdicao;
    std::shared_ptr<Button> BtnSalvarTags;
    std::shared_ptr<StackPanel> PainelMetadados;

private:
    MainWindow &_janela;

    /// <summary>Uma caixa de edição ligada a um atributo do arquivo.</summary>
    struct CampoEditor
    {
        std::string Atributo;
        std::string Original;
        std::shared_ptr<TextBox> Caixa;
    };

    std::vector<std::shared_ptr<CampoEditor>> _campos;
    std::string _arquivoSel;                      // "" = null
    // Com álbum escolhido e nenhuma faixa selecionada, o painel edita o
    // álbum inteiro: estas são as faixas que recebem a gravação. null =
    // o painel é de uma faixa (ou está vazio).
    std::optional<std::vector<std::string>> _lstArquivosAlbum;

    // A faixa, não a posição dela: posição muda com a ordenação da grade e
    // com outra busca ou categoria, e aí apontaria para outra música.
    std::optional<Modelo::ItemMidia> _objItemTocando;

    std::unique_ptr<Modelo::Biblioteca> _biblioteca;

    // o ItemsSource da ListaCategorias (Biblioteca.Categorias())
    std::vector<Modelo::Categoria> _lstCategorias;
    // Lista de álbuns da BIBLIOTECA. O filtro vale pelo nome guardado aqui,
    // não pela seleção da lista: procurar outro nome na caixa pode tirar o
    // álbum escolhido da vista sem desfazer o filtro das faixas.
    // (o SourceCollection do ListCollectionView; o filtro e a vista ficam na propria ListaAlbuns)
    std::shared_ptr<std::vector<Modelo::AlbumDaLista>> _vistaAlbuns;
    std::string _strAlbumFiltro;                  // "" = null = <todos>
    // Na categoria Obras, a obra escolhida (0 = <todos>); o _strAlbumFiltro
    // fica com o nome dela, para a lista e a trilha.
    int _intObraFiltro = 0;
    // O painel da direita está editando esta obra (0 = não é painel de obra).
    int _intObraPainel = 0;
    std::unique_ptr<Modelo::TBancoObras> _objBancoObras;
    bool _blnAjustandoAlbuns = false;           // seleção mexida pelo código, não pelo usuário
    bool _blnAlbunsDesatualizados = false;      // a AQUISIÇÃO acrescentou material

    // ListaItens.ItemsSource as List<ItemMidia> (nullptr = null)
    std::shared_ptr<std::vector<Modelo::ItemMidia>> _lstItens;

    static constexpr const char *strCampoNomeArquivo = "ARCA:NomeDoArquivo";
    static constexpr const char *strCampoObraTitulo = "OBRA:Titulo";
    static constexpr const char *strCampoObraAutor = "OBRA:Autor";

    // ---- montagem e ligacoes ----
    void InitializeComponent();
    void AoCarregar();
    // LINUX: a ColecaoMidia terminou de conferir as pastas (no original o WMP fazia isso sozinho)
    void AoAtualizarColecao(int intMudancas);
    std::string RaizAcervo() const;                  // ModuloAquisicao.RaizAcervo
    Modelo::TBancoObras &BancoObras();
    const Modelo::Categoria *CategoriaSelecionada() const;
    // LINUX: ListaItens.ItemsSource = itens (ver o .cpp)
    void DefinirItens(std::shared_ptr<std::vector<Modelo::ItemMidia>> itens);

    // ---- portado do MainWindow.xaml.cs ----
    void AoTrocarCategoria();
    void CarregarAlbuns();
    void FiltrarAlbuns();
    void MarcarAlbumEscolhido();
    void EscolherPrimeiroAlbum();
    void AoEscolherAlbum();
    std::string TrilhaAlbum() const;
    void AplicarBusca();
    void CarregarCategoria();
    void CarregarObras(const Modelo::Categoria &c);
    std::vector<std::string> ArquivosDaObra(int intIdObra, int &intPerdidas);
    void ResolverPendentes();
    void AoSelecionarItem();
    UIElementPtr LinhaNomeArquivo(const std::string &strCaminho);
    void LimparPainel();
    void MostrarObra();
    std::shared_ptr<TextBlock> Rotulo(const std::string &strTexto);
    std::shared_ptr<Button> BotaoPequeno(const std::string &strTexto, std::function<void()> objAcao);
    UIElementPtr LinhaParte(const Modelo::TParteObra &p, int intIndice, int intTotal);
    std::shared_ptr<Button> BotaoParte(const std::string &strTexto, const std::string &strDica, bool blnAtivo, std::function<void()> objAcao);
    void AcaoNaObra(std::function<void()> objAcao, const std::string &strResumo);
    void TrocarCapaDaObra(const std::shared_ptr<BitmapSource> &objImagem, const std::string &strErro);
    std::optional<std::string> SalvarObra(bool blnReler);
    void AoEnviarObra();
    static void LerIdentidade(const std::string &strArquivo, std::string &strAlbum, std::string &strArtista, std::string &strMcdi,
                              int &intFaixa);
    static std::vector<uint8_t> CapaDoAlbum(const std::string &strArquivo);
    void MostrarAlbum();
    bool TocandoAlgumDoAlbum() const;
    std::optional<std::string> GravarAlbum(const std::vector<std::shared_ptr<CampoEditor>> &lstMudou, bool blnReler);
    void MostrarAtributos(const std::vector<Interop::AtributoFlac> &atributos);
    std::shared_ptr<Grid> LinhaBase(const std::string &rotulo, const std::string &dica);
    UIElementPtr LinhaLeitura(const Interop::AtributoFlac &a);
    UIElementPtr LinhaEditavel(const std::string &atributo, const std::string &rotulo, const std::string &valor);
    bool TocandoEsteArquivo() const;
    std::vector<std::shared_ptr<CampoEditor>> Alterados();
    void AtualizarEstadoEdicao();
    void AoArtworkAlbum();
    void AoConferirAlbum();
    void AoConteudoAlbum();
    void AoSalvarTags();
    std::string SalvarFaixa(const std::string &strArquivo, const std::vector<std::shared_ptr<CampoEditor>> &lstMudou, bool blnReler, bool &blnFalhou);
    void RelerESelecionar(const std::string &strCaminho);
    std::string GravarCampos(const std::string &strArquivo, const std::vector<std::shared_ptr<CampoEditor>> &lstCampos);
    void AoAbrirItem(MouseButtonEventArgs &e);
    bool EhACarregada(const Modelo::ItemMidia &objItem) const;
    void Tocar(const Modelo::ItemMidia &objItem);
    void Pular(int passo);
    void Avisar(const std::string &titulo, const std::string &texto);
    void EsconderAviso();
};

} // namespace ARCA::Vistas

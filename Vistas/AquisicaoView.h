// ARCA no xOS - Vistas/AquisicaoView (de Vistas\AquisicaoView.xaml e AquisicaoView.xaml.cs).
#pragma once

#include "MainWindow.h"
#include "clsPainelProgressoAquisicao.h"
#include "Modelo/Aquisicao.h"
#include "Modelo/CandidatoIdentificacao.h"
#include "Modelo/ClasseMidia.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ARCA::Vistas {

/// AQUISIÇÃO — extrair, identificar, aprovar.
///
/// **A regra que define o módulo:** nada entra na biblioteca antes de o
/// Lorenzo aprovar o rip *e* os metadados. Até lá o material fica na área
/// de trabalho, fora do alcance da biblioteca. É isso que impede o acervo
/// de ser poluído por extração ruim ou identificação errada — e é por isso
/// que o botão de aprovar fica embaixo, sozinho, sempre visível, e
/// desabilitado até que as duas condições sejam verdadeiras.
class AquisicaoView : public VistaModulo
{
public:
    struct LinhaFaixa
    {
        int Numero = 0;
        std::string Nome;
        std::string Tamanho;
        std::string Hash;
    };

    AquisicaoView();

    /// Extração, releitura, integração ou identificação em andamento.
    bool Ocupado() const override { return _ocupado || _blnEmIdentificacao; }

    /// Identificador do disco físico, que vai para o ledger. Fica em aberto
    /// até a extração por SPTI ser trazida — é ela que lê o TOC. ("" = null)
    std::string IdDoDisco;

    /// Preenchido pela janela principal: acrescenta à biblioteca do player.
    // LINUX: no original, o MainWindow ligava aqui o Biblioteca.Acrescentar, que punha a faixa na
    // biblioteca do WMP (mediaCollection.add). No xOS não há WMP: quem liga é a BIBLIOTECA do ARCA,
    // que relê o álbum (no original, o _blnAlbunsDesatualizados = true que vinha junto). Vazio = null.
    std::function<bool(const std::string &caminho)> AcrescentarNaBiblioteca;

    /// Onde o material fica em quarentena até ser aprovado.
    std::string PastaTrabalho;

    /// Raiz do acervo — para onde o aprovado é promovido.
    std::string RaizAcervo;

    // ---- os elementos com x:Name do AquisicaoView.xaml ----
    std::shared_ptr<StackPanel> PainelPassos;
    std::shared_ptr<TextBlock> TxtPorta, TxtTrabalho;
    std::shared_ptr<Button> BtnAprovar;
    std::shared_ptr<ScrollViewer> AreaRegistro;
    std::shared_ptr<TextBlock> TxtRegistro;
    std::shared_ptr<StackPanel> PnlProgressoPercentual;
    std::shared_ptr<TextBlock> TxtFaseLongaTitulo, TxtPercentualNumero;
    std::shared_ptr<ProgressBar> PbProgressoFase;
    std::shared_ptr<StackPanel> PnlFaseRapida;
    std::shared_ptr<TextBlock> TxtFaseVerbo, TxtFaseDetalhe;
    std::shared_ptr<ComboBox> ListaUnidades;
    std::shared_ptr<Button> BtnExtrairCd;
    std::shared_ptr<TextBlock> TxtExtracao;
    std::shared_ptr<Button> BtnReler, BtnDescartar;
    std::shared_ptr<TextBlock> TxtFaixasTitulo;
    std::shared_ptr<DataGrid> ListaFaixas;
    std::shared_ptr<TextBlock> TxtSemCapa;
    std::shared_ptr<Image> ImgCapa;
    std::shared_ptr<TextBox> CaixaArtista, CaixaAlbum, CaixaBuscaAlbum;
    std::shared_ptr<Button> BtnIdentificar, BtnCatalogarManual, BtnConteudo;
    std::shared_ptr<TextBlock> TxtIdentificacao;

private:
    std::vector<std::shared_ptr<TextBlock>> _passos;
    // vazio = null: em todo uso do original, null e lista vazia davam no mesmo
    std::vector<Modelo::ItemManifesto> _manifesto;
    std::vector<LinhaFaixa> _lstLinhas;  // o ItemsSource da ListaFaixas
    bool _ocupado = false;
    bool _blnEmIdentificacao = false;

    // Um erro mostrado no medidor fica lá até a próxima operação: o
    // AvaliarPorta, chamado logo depois, o trocava por "PRONTO" na hora.
    bool _blnErroNoMedidor = false;
    bool _blnAutoIdentificacaoJaExecutada = false;
    std::unique_ptr<clsPainelProgressoAquisicao> m_objIndicador;
    // O que a fonte escolhida diz do disco ("Non-Music", "Spoken Word"…),
    // para a janela de conteúdo sugerir. Quem marca é a pessoa. ("" = null)
    std::string _strSugestaoConteudo;

    std::vector<uint8_t> _capaBytes;     // vazio = null
    std::string _caminhoCapaSalva;       // "" = null

    void InitializeComponent();
    void Iniciar();
    void MontarPassos();
    void MarcarPasso(int ativo);
    void ListarUnidades();
    std::vector<std::string> MaterialNaAreaDeTrabalho();
    bool ConfirmarDescarte(const std::string &strPergunta);
    std::vector<std::string> LimparAreaDeTrabalho(std::string &strFalha);
    void ZerarTela();
    void AoDescartar(RoutedEventArgs &e);
    void AoExtrair(RoutedEventArgs &e);
    void AoReler(RoutedEventArgs &e);
    void Reler();
    void MostrarFaixas();
    void DefinirEmIdentificacao(bool blnEm);
    void AoIdentificar(RoutedEventArgs &e);
    void IdentificarInterno(bool bManual);
    void ConcluirIdentificacao(const std::string &strArtistaIdentificado, const std::string &strAlbumIdentificado,
                               const std::vector<uint8_t> &arrCapa, bool blnRenomeou, bool bManual);
    void FalharIdentificacao(const std::string &strMensagem);
    void IdentificarComparando();
    void AoCatalogarManual(RoutedEventArgs &e);
    void AplicarCandidato(std::shared_ptr<Modelo::TCandidatoIdentificacao> objC);
    static std::string SugestaoDeConteudo(const std::shared_ptr<Modelo::TCandidatoIdentificacao> &objC);
    void AoDefinirConteudo(RoutedEventArgs &e);
    static std::string NomeDoConteudo(Modelo::ConteudoFaixa eConteudo);
    bool GravarArtwork(const Modelo::TCandidatoIdentificacao &objC);
    std::vector<int> DuracoesDoDisco();
    void ExibirCapaNaUi(const std::vector<uint8_t> &arrBytes);
    void AvaliarPorta();
    void AoAprovar(RoutedEventArgs &e);
    void Registrar(const std::string &m);
    // O Dispatcher.BeginInvoke((Action)delegate { Registrar(m); }) dos fios de trabalho.
    void RegistrarDeOutroFio(const std::string &m);
};

} // namespace ARCA::Vistas

// ARCA no xOS - Vistas/MainWindow (de MainWindow.xaml e MainWindow.xaml.cs): o CASCO da janela
// principal. As abas dos modulos no alto, a trilha de navegacao e a busca, a area do modulo e o
// rodape do tocador, com o layout, as medidas e as cores do original.
//
// O conteudo dos tres modulos e das outras frentes do porte:
//   - AQUISICAO: Vistas/AquisicaoView (de AquisicaoView.xaml + .cs)
//   - BIBLIOTECA: no original mora DENTRO da MainWindow (o Grid ModuloBiblioteca e boa parte do
//     MainWindow.xaml.cs). Aqui vira um VistaModulo proprio, que recebe a MainWindow para mexer no
//     casco (TxtTrilha, CaixaBusca, BtnTocar...).
//   - EM EXECUCAO: Vistas/ExecucaoView (de ExecucaoView.xaml + .cs)
// Cada frente define a sua funcao CriaModuloXxx (ver o fim deste arquivo); o xos-arca.cpp as chama.
#pragma once

#include "Dialogos.h"
#include "Janela.h"
#include "Interop/PlayerWmp.h"

#include <functional>
#include <memory>
#include <string>

namespace ARCA::Vistas {

class MainWindow;

// O ponto de encaixe de um modulo na area do corpo (no original: UserControl / o Grid
// ModuloBiblioteca). A troca de modulo chama Ativar(true) no que entra e Ativar(false) nos outros.
class VistaModulo : public UserControl
{
public:
    // ModuloExecucao.Ativar(m.Execucao): toda troca de modulo, com true so no que fica visivel
    virtual void Ativar(bool ativo) { (void) ativo; }
    // ModuloAquisicao.Ocupado: extracao, identificacao ou integracao em curso (AoPedirFechar)
    virtual bool Ocupado() const { return false; }
    const char *Tipo() const override { return "UserControl"; }
};

class MainWindow : public Window
{
public:
    MainWindow();

    // ---- os elementos com x:Name do MainWindow.xaml (o casco) ----
    std::shared_ptr<StackPanel> AbasModulos;
    std::shared_ptr<TextBlock> TxtMotor;
    std::shared_ptr<TextBlock> TxtTrilha;
    std::shared_ptr<TextBox> CaixaBusca;
    std::shared_ptr<TextBlock> DicaBusca;
    std::shared_ptr<Border> Rodape;
    std::shared_ptr<Slider> SliderPosicao;
    std::shared_ptr<Border> VagaVideo;
    std::shared_ptr<TextBlock> TxtTitulo, TxtArtista, TxtTempo;
    std::shared_ptr<Button> BtnAnterior, BtnParar, BtnTocar, BtnPausar, BtnProximo;
    std::shared_ptr<TextBlock> IconeTocar;
    std::shared_ptr<Slider> SliderVolume;
    std::shared_ptr<Grid> Corpo;                       // o Grid sem nome que guarda os modulos
    std::shared_ptr<VistaModulo> ModuloBiblioteca, ModuloExecucao, ModuloAquisicao;

    // poe os modulos no corpo (antes de Show); nullptr = o modulo ainda nao foi portado
    void EncaixaModulos(std::shared_ptr<VistaModulo> aquisicao, std::shared_ptr<VistaModulo> biblioteca,
                        std::shared_ptr<VistaModulo> execucao);

    // ---- o que fica para as outras frentes: os manipuladores do original, com os mesmos nomes ----
    // BIBLIOTECA: MainWindow.AplicarBusca (o adiador de 250 ms e o Enter da CaixaBusca chamam)
    std::function<void()> AplicarBusca;
    // tocador: os Click do transporte e os deslizantes
    std::function<void()> AoTocar, AoPausar, AoParar;
    std::function<void(int passo)> Pular;                            // BtnAnterior (-1), BtnProximo (+1)
    std::function<void(double novo)> AoMudarVolume;                 // SliderVolume.ValueChanged
    std::function<void()> AoSoltarPosicao;                          // SliderPosicao.PreviewMouseLeftButtonUp
    // tocador: PosicionarVideo (a troca de modulo o agenda depois do layout)
    std::function<void()> PosicionarVideo;
    // tocador: "_player != null && _player.Vivo" (o TomTransporte pergunta)
    std::function<bool()> PlayerVivo;
    // tocador: AtualizarTransporte(_player.Estado) com o estado atual (o TomTransporte chama)
    std::function<void()> AtualizarTransporteAgora;
    // AoCarregar: o casco monta as abas e dispara Carregado; o tocador (player, relogio de 300 ms,
    // ModuloExecucao.ObterPosicao...) e a BIBLIOTECA (categorias) ligam o resto aqui
    Evento<> Carregado;
    // AoFechar: o tocador para o relogio e o player
    Evento<> Fechou;

    // ---- ganchos entre a BIBLIOTECA e o tocador/EM EXECUCAO (no original, tudo na MainWindow) ----
    // Define a BIBLIOTECA; chama o AoTocar do tocador, logo no comeco: "se a faixa selecionada na
    // lista nao e a carregada, toca a selecionada" (o Tocar(objSelecionado); return; do original).
    // true = tocou a selecionada (o AoTocar para ai); false = segue (retomar/recomecar).
    std::function<bool()> TocarSelecionadaSeOutra;
    // Define a BIBLIOTECA; chama o tocador quando o estado do player muda (AoBater) e no AoParar: o
    // "if (!string.IsNullOrEmpty(_arquivoSel)) AtualizarEstadoEdicao();" do original.
    std::function<void()> AtualizarEstadoEdicaoSeFaixa;
    // Define a BIBLIOTECA (o PainelAviso mora nela); chama quem precisar avisar no lugar da lista,
    // como o AoCarregar do original quando o motor nao sobe ("O controle ... nao inicializou").
    std::function<void(const std::string &titulo, const std::string &texto)> Avisar;
    // Define o tocador; chama o Tocar(ItemMidia) da BIBLIOTECA antes de carregar a faixa: o
    // "_paradoPeloUsuario = false; _ultimaPosicao = 0; _ultimaDuracao = 0;" do original.
    std::function<void()> AoComecarFaixa;
    // Define o tocador; chama o AoConteudoAlbum da BIBLIOTECA antes de parar o player para gravar:
    // o "_paradoPeloUsuario = true;" do original (parar a pedido nao avanca para a proxima).
    std::function<void()> MarcarParadoPeloUsuario;
    // Define o EM EXECUCAO (ModuloExecucao.RecarregarEncarteSeDoAlbum); chama o AoArtworkAlbum da
    // BIBLIOTECA quando a janela de capa e encarte gravou algo nessa pasta de album.
    std::function<void(const std::string &strPastaAlbum)> RecarregarEncarteSeDoAlbum;

    // ---- portado do casco (MainWindow.xaml.cs) ----
    void AplicarTomRodape(bool escuro);
    void TomTransporte(const Brush &cor);
    // AtualizarTransporte(EstadoPlayer): blnPausado = Estado == Pausado; blnEmCurso = Pausado ou Tocando
    void AtualizarTransporte(bool blnPausado, bool blnEmCurso);
    bool EmExecucao() const { return _emExecucao; }
    // a aba EM EXECUCAO (o duplo clique da BIBLIOTECA a marca: _abaExecucao.IsChecked = true)
    std::shared_ptr<RadioButton> AbaExecucao() const { return _abaExecucao; }
    // a trilha do modulo atual ("Biblioteca", "Aquisição", "Em execução")
    std::string TrilhaDoModulo() const { return _trilhaAtual; }

    DispatcherTimer _adiadorBusca;

    // o reprodutor (no original: _hospedeiro + _player, campos da MainWindow). Quem cria e liga
    // (AoCarregar, relogio AoBater) e a frente do tocador/EM EXECUCAO; a BIBLIOTECA usa
    // (Tocar(ItemMidia), TocandoAlgumDoAlbum...). nullptr = ainda nao criado.
    std::shared_ptr<Interop::PlayerWmp> _player;

private:
    struct Modulo
    {
        std::string Nome;
        std::string Trilha;
        bool Execucao = false;
        bool Aquisicao = false;
        /// <summary>Qual módulo aparece ao abrir. Exatamente um.</summary>
        bool Inicial = false;
    };

    Brush _objCorTransporte;
    bool _emExecucao = false;
    std::shared_ptr<RadioButton> _abaExecucao;
    bool _iniciado = false;
    std::string _trilhaAtual;

    void InitializeComponent();
    void AoCarregar();
    void AoPedirFechar(CancelEventArgs &e);
    void AoFechar();
    void MontarAbas();
    void AoTrocarModulo(RadioButton &r);
};

// ---- o encaixe dos modulos: cada frente define a sua (o xos-arca.cpp chama as que existirem) ----
// Declaradas fracas: enquanto a frente nao entrega, o programa monta sem ela e mostra o lugar vazio.
std::shared_ptr<VistaModulo> CriaModuloAquisicao(MainWindow &janela) __attribute__((weak));
std::shared_ptr<VistaModulo> CriaModuloBiblioteca(MainWindow &janela) __attribute__((weak));
std::shared_ptr<VistaModulo> CriaModuloExecucao(MainWindow &janela) __attribute__((weak));

} // namespace ARCA::Vistas

// ARCA no xOS - Vistas/ExecucaoView (de Vistas\ExecucaoView.xaml e ExecucaoView.xaml.cs).
#pragma once

#include "EspectroAnalise.h"
#include "EspectrogramaArquivo.h"
#include "Instrumentos.h"
#include "MainWindow.h"
#include "MedidoresPainel.h"
#include "OndaArquivo.h"
#include "PainelEncarte.h"
#include "Modelo/AnalisadorAudio.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ARCA::Vistas {

/// <summary>
/// O módulo escuro. Os instrumentos são alimentados pelo AnalisadorAudio,
/// que decodifica o arquivo em paralelo à reprodução — ou seja, medem o que
/// está na gravação, não o que a saída do player fez com ela.
/// </summary>
class ExecucaoView : public VistaModulo
{
public:
    ExecucaoView();
    ~ExecucaoView() override;

    /// <summary>Pedido de busca ao clicar na onda. A janela principal liga ao player.</summary>
    std::function<void(double)> Buscar;
    /// <summary>De onde vem a posição da reprodução. A janela principal preenche.</summary>
    std::function<double()> ObterPosicao;
    /// <summary>A reprodução está andando (e não pausada)? Os instrumentos só correm assim.</summary>
    std::function<bool()> EstaTocando;
    /// <summary>
    /// Preenchido pela janela principal: põe o player em tela cheia.
    /// Devolve null se deu certo, ou o motivo.
    /// </summary>
    // LINUX: "" no lugar do null (deu certo)
    std::function<std::string()> PedirTelaCheia;

    /// <summary>
    /// Os instrumentos só desenham com o módulo à vista. Decodificar e
    /// pintar quatro telas enquanto ninguém olha seria gastar a máquina à
    /// toa — e ela é a máquina que é.
    /// </summary>
    void Ativar(bool ligado) override;

    /// <summary>
    /// Relê a pasta Artwork da faixa em execução, se ela for deste álbum —
    /// para o encarte recém-digitalizado na BIBLIOTECA aparecer sem
    /// precisar trocar de faixa.
    /// </summary>
    void RecarregarEncarteSeDoAlbum(const std::string &strPastaAlbum);

    /// <summary>Tocando vídeo: o módulo mostra o palco, não os instrumentos.</summary>
    bool ModoVideo() const { return _modoVideo; }
    /// <summary>Onde a janela principal põe o controle do WMP quando toca vídeo.</summary>
    std::shared_ptr<Border> AreaDoVideo() const { return AreaVideo; }

    /// <summary>
    /// Começou a tocar um vídeo. Vídeo se assiste, não se analisa (Lorenzo,
    /// 04/10/2026): um filme de 2 h disparava a forma de onda, o
    /// espectrograma e os instrumentos sobre o áudio dele. Aqui nada disso
    /// abre — e o que estivesse aberto da faixa anterior é fechado.
    /// </summary>
    void DefinirVideo(const std::string &caminho, const std::string &titulo, const std::string &artista);
    /// <summary>Recado no alto do palco de vídeo (vermelho, ao lado do botão).</summary>
    void AvisarVideo(const std::string &strTexto);
    /// <summary>Chamado quando a faixa em execução muda.</summary>
    void DefinirFaixa(const std::string &caminho, const std::string &titulo, const std::string &artista,
                      const std::string &album);
    /// <summary>Chamado a cada quadro, com a posição do player.</summary>
    void Atualizar(double posicao);
    /// <summary>
    /// Chamado quando a pessoa aperta Parar. Cancela a geração de onda e
    /// espectrograma (a geração seguinte é descartada pelo contador, e as
    /// threads largam o arquivo) e fecha o analisador ao vivo: antes, os
    /// três seguravam o arquivo aberto, e gravar as tags falhava.
    /// </summary>
    void Parar();

private:
    Modelo::AnalisadorAudio _analisador;
    std::vector<float> _esq = std::vector<float>(Modelo::AnalisadorAudio::Janela);
    std::vector<float> _dir = std::vector<float>(Modelo::AnalisadorAudio::Janela);

    std::string _arquivoAtual;          // "" = null
    std::shared_ptr<TextBlock> _picoE, _picoD, _rms, _correl, _taxa, _quadros, _custo, _desenho;
    double _somaCusto = 0;

    std::shared_ptr<Goniometro> _gonio;
    std::shared_ptr<IInstrumento> _espec, _histo, _osc;
    std::shared_ptr<EspectroAnalise> _especAnalise;   // aba Espectro: o modo Análise (CPU, rico)
    std::shared_ptr<Medidores> _medidores;            // aba Medidores: nível, dinâmica e estéreo
    int _naPlaca = 0;
    std::string _falhaShader;           // "" = null

    // Cada instrumento mora para sempre dentro da sua moldura; o que se
    // reparenta entre as abas é a moldura, nunca o instrumento — assim não
    // há a dança de "elemento já tem pai" do WPF.
    std::shared_ptr<Border> _hostOsc, _hostEspec, _hostHisto, _hostGonio;

    /// <summary>
    /// Só os instrumentos visíveis são atualizados. É daqui que vem a
    /// economia: um por vez é ~1/4 do trabalho de fill-rate na GT210, e
    /// pula o Esmaecer do goniômetro e a FFT do espectro quando não estão à
    /// vista.
    /// </summary>
    std::vector<IInstrumento *> _ativos;

    /// <summary>A onda do arquivo (categoria A). Mora na aba "Forma de onda".</summary>
    std::shared_ptr<OndaArquivo> _onda;
    std::shared_ptr<EspectrogramaArquivo> _espectrograma;   // aba Espectrograma (categoria A, offline)
    int _abaAtual = 0;
    // lidos pelos fios de geração (no C#, int lidos sem trava; aqui atômicos e compartilhados, para o
    // fio que sobreviver à vista não ler memória solta)
    std::shared_ptr<std::atomic<int>> _geracaoOnda = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> _geracaoEg = std::make_shared<std::atomic<int>>(0);

    double _segundosDoUltimoQuadro = 0;
    bool _ligado = false;

    double _inicioCronometro;           // Stopwatch.StartNew()
    int _contaQuadros = 0;
    double _marcaQuadros = 0;

    std::shared_ptr<CheckBox> _caixaDb;
    bool _blnAssinado = false;
    bool _modoVideo = false;
    // LINUX: CompositionTarget.Rendering não existe; um relógio de 16 ms faz o papel do ciclo de vídeo
    DispatcherTimer _rendering;

    // ---- os x:Name do ExecucaoView.xaml ----
    std::shared_ptr<Grid> PalcoVideo;
    std::shared_ptr<Button> BtnTelaCheia;
    std::shared_ptr<TextBlock> TxtAvisoVideo, TxtTituloVideo, TxtDetalheVideo;
    std::shared_ptr<Border> AreaVideo;
    std::shared_ptr<DockPanel> Instrumentacao;
    std::shared_ptr<TextBlock> TxtSemCapa;
    std::shared_ptr<Image> ImgCapa;
    std::shared_ptr<TextBlock> TxtTitulo, TxtArtista, TxtAlbum, TxtFormato;
    std::shared_ptr<StackPanel> PainelLeituras;
    std::shared_ptr<PainelEncarte> Encarte;
    std::shared_ptr<StackPanel> Abas;
    std::shared_ptr<Grid> Palco;

    double Cronometro() const;          // _cronometro.Elapsed.TotalSeconds
    void InitializeComponent();
    void InstalarInstrumentos();
    static std::shared_ptr<Border> Moldurar(const std::shared_ptr<IInstrumento> &inst);
    void MontarAbas();
    void Mostrar(int aba);
    std::shared_ptr<Grid> MontarPainel();
    static UIElementPtr Celula(const std::string &rotulo, const std::shared_ptr<Border> &host);
    static void Colocar(const std::shared_ptr<Grid> &g, const UIElementPtr &fe, int linha, int coluna);
    static void Soltar(const UIElementPtr &fe);
    void AtualizarAssinatura();
    void AoRenderizar();
    void MontarLeituras();
    std::shared_ptr<TextBlock> Leitura(const std::string &rotulo, const std::string &gabarito);
    static std::string Coluna(const std::string &numero, int largura);
    void AoPedirTelaCheia();
    void MostrarPalcoDeVideo(bool blnVideo);
    void GerarOnda(const std::string &caminho);
    void GerarEspectrograma(const std::string &caminho);
    void LimparLeituras();
    void CarregarCapa(const std::string &caminho);
};

} // namespace ARCA::Vistas

// ARCA no xOS - Vistas/MainWindowTocador: a parte do tocador do MainWindow.xaml.cs (o player no
// AoCarregar, AoFechar, AoTocar, AoPausar, Retomar, AtualizarTransporte, AoParar, EhVideo,
// EsconderControlesSeOcioso, PosicionarVideo, AoMudarVolume, AoSoltarPosicao, AoBater) e o encaixe
// do EM EXECUCAO (CriaModuloExecucao).
// LINUX: no original tudo isto e da propria classe MainWindow. Aqui os campos do tocador moram numa
// estrutura deste arquivo (Tocador), e os metodos se ligam aos ganchos std::function da MainWindow
// (MainWindow.h). O que e da BIBLIOTECA (a lista, a selecao, _arquivoSel, Pular) chega pelos ganchos
// que ela define: TocarSelecionadaSeOutra, AtualizarEstadoEdicaoSeFaixa, Avisar, Pular.
#include "ExecucaoView.h"
#include "MainWindow.h"
#include "Base/Arquivo.h"
#include "Base/Texto.h"
#include "Modelo/Formato.h"

extern "C" {
#include <libavutil/avutil.h>
}
#include <alsa/asoundlib.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace ARCA::Vistas {

using Interop::EstadoPlayer;
using Interop::PlayerWmp;

namespace {

struct Tocador
{
    MainWindow &janela;
    std::shared_ptr<ExecucaoView> ModuloExecucao;

    EstadoPlayer _estadoAnterior = EstadoPlayer::Indefinido;
    double _ultimaPosicao = 0, _ultimaDuracao = 0;
    bool _paradoPeloUsuario = false;
    std::string _urlAnalisada;          // "" = null
    // A mídia em curso é vídeo (vai para o palco, sem análise).
    bool _blnVideoAtual = false;
    // O ARCA pôs o WMP em tela cheia (com a interface "full"): ao sair, volta a "none".
    bool _blnEmTelaCheia = false;
    // Esconder a barra do WMP na tela cheia com o mouse parado (EsconderControlesSeOcioso).
    bool _blnControlesEscondidos = false;
    bool _blnEsconderFalhou = false;
    Point _ptMouseAnterior;
    double _dtUltimoMovimento = 0;
    static constexpr double SegundosAteEsconder = 3.0;
    // Estado do player na batida anterior do relógio (só para o "parou sozinho" do vídeo).
    EstadoPlayer _estadoAnteriorBatida = EstadoPlayer::Indefinido;
    DispatcherTimer _relogio;

    Tocador(MainWindow &j, std::shared_ptr<ExecucaoView> m) : janela(j), ModuloExecucao(std::move(m)) {}

    std::shared_ptr<PlayerWmp> &_player() { return janela._player; }

    void AoCarregar();
    void AoFechar();
    void AoTocar();
    void AoPausar();
    void Retomar();
    void AtualizarTransporte(EstadoPlayer objEstado);
    void AoParar();
    bool EhVideo(const std::string &strUrl);
    void EsconderControlesSeOcioso();
    void PosicionarVideo();
    void AoMudarVolume(double novo);
    void AoSoltarPosicao();
    void AoBater();
    void Avisar(const std::string &titulo, const std::string &texto)
    {
        // PainelAviso mora no Grid da BIBLIOTECA: ela define o gancho
        if (janela.Avisar) janela.Avisar(titulo, texto);
    }
};

// ===============================================================
void Tocador::AoCarregar()
{
    // (MontarAbas: o casco, em MainWindow::AoCarregar)

    // LINUX: HospedeiroWmp.Disponivel() vira "alguma saída de áudio do ALSA existe" (PlayerWmp::Vivo)
    if (!_player() || !_player()->Vivo())
    {
        Avisar("A saída de áudio (ALSA) não está nesta máquina",
               "O ARCA usa o FFmpeg e o ALSA para tocar e para ler a biblioteca. "
               "Sem uma placa de som que o ALSA abra, a reprodução fica desligada.");
        janela.TxtMotor->Text = "motor indisponível";
        return;
    }

    try
    {
        // (o _hospedeiro, AreaPlayer.Child e CreateControl: LINUX, não há OCX a hospedar; o player
        // foi criado em CriaModuloExecucao, antes do Carregado, para a BIBLIOTECA o achar pronto)
        _player()->DefinirModoInterface("none");
        // No palco do EM EXECUÇÃO o vídeo enche a área, na proporção dele.
        _player()->EsticarVideo(true);
        _player()->Volume((int) janela.SliderVolume->Value());

        // LINUX: no original, "motor: Windows Media Player " + versionInfo
        janela.TxtMotor->Text = std::string("motor: FFmpeg ") + av_version_info() + " + ALSA " + snd_asoundlib_version();
    }
    catch (const std::exception &ex)
    {
        Avisar("Não consegui hospedar o player", std::string("exception: ") + ex.what());
        return;
    }

    // O módulo de execução tem relógio próprio e busca a posição aqui.
    // Tocando ou pausado, a posição vale (o cursor da onda acompanha);
    // parado, -1. Os instrumentos perguntam à parte se está tocando.
    ModuloExecucao->ObterPosicao = [this] {
        if (!_player() || !_player()->Vivo()) return -1.0;
        EstadoPlayer objEstado = _player()->Estado();
        return objEstado == EstadoPlayer::Tocando || objEstado == EstadoPlayer::Pausado ? _player()->Posicao() : -1.0;
    };
    ModuloExecucao->EstaTocando = [this] {
        return _player() && _player()->Vivo() && _player()->Estado() == EstadoPlayer::Tocando;
    };

    // Clicar na onda do arquivo salta a reprodução para ali.
    ModuloExecucao->Buscar = [this](double segundos) {
        if (_player() && _player()->Vivo()) _player()->Posicao(segundos);
    };

    // Tela cheia do próprio WMP, pedida pelo palco de vídeo. O WMP só
    // aceita com a mídia em curso; lido de volta para saber se pegou.
    //
    // Durante a tela cheia a interface do WMP vai para "full": é assim
    // que ele mostra a barra de controles que aparece quando o mouse se
    // mexe e some sozinha — progresso, play/pause, stop, avançar,
    // retroceder, volume e sair da tela cheia. Ao voltar (Esc ou o
    // botão), AoBater devolve "none": no palco e no rodapé só a imagem.
    ModuloExecucao->PedirTelaCheia = [this]() -> std::string {
        if (!_player() || !_player()->Vivo()) return "o player não está disponível";
        EstadoPlayer objEstado = _player()->Estado();
        if (objEstado != EstadoPlayer::Tocando && objEstado != EstadoPlayer::Pausado)
            return "a tela cheia só vale com o vídeo tocando ou pausado";
        _player()->DefinirModoInterface("full");
        _player()->TelaCheia(true);
        if (_player()->TelaCheia())
        {
            _blnEmTelaCheia = true;
            _blnControlesEscondidos = false;
            _ptMouseAnterior = Mouse::PosicaoNaTela();
            _dtUltimoMovimento = Agora();
            return "";
        }
        _player()->DefinirModoInterface("none");
        // LINUX: no original, "o Windows Media Player não aceitou a tela cheia"; aqui a imagem do
        // vídeo ainda não é desenhada (Interop/PlayerWmp.cpp, pendência)
        return "a tela cheia do vídeo ainda não existe no xOS (só o som do vídeo toca)";
    };

    // (_biblioteca = new Biblioteca(_player), ModuloAquisicao.AcrescentarNaBiblioteca, as categorias:
    // da BIBLIOTECA, no Carregado dela)

    _relogio.Interval = TimeSpan::FromMilliseconds(300);
    _relogio.Tick += [this] { AoBater(); };
    _relogio.Start();

    // O vídeo acompanha a janela e o palco na hora, sem esperar o relógio.
    janela.SizeChanged += [this](SizeChangedEventArgs &) { PosicionarVideo(); };
    ModuloExecucao->AreaDoVideo()->SizeChanged += [this](SizeChangedEventArgs &) { PosicionarVideo(); };
}

void Tocador::AoFechar()
{
    _relogio.Stop();
    try { if (_player() && _player()->Vivo()) _player()->Parar(); }
    catch (...) {}
}

// ===============================================================
/// <summary>
/// Tocar obedece à seleção, em qualquer estado: se a faixa escolhida
/// na lista não é a que está carregada, toca a escolhida — inclusive
/// com outra tocando. Se é a mesma (ou não há seleção): pausada,
/// retoma; parada, recomeça; tocando, não há o que fazer. Não troca de
/// módulo — isso é do duplo clique.
/// </summary>
void Tocador::AoTocar()
{
    if (!_player() || !_player()->Vivo()) return;

    // ItemMidia objSelecionado = ListaItens.SelectedItem; if (... && !EhACarregada) { Tocar; return; }
    // (a lista é da BIBLIOTECA: ela responde pelo gancho)
    if (janela.TocarSelecionadaSeOutra && janela.TocarSelecionadaSeOutra()) return;

    if (_player()->Estado() == EstadoPlayer::Tocando) return;
    Retomar();
}

/// <summary>
/// Pausar segura e solta a faixa atual, ignorando a seleção: tocando,
/// pausa; pausada, retoma. É o que deixa pausar, navegar pela lista
/// (inclusive selecionando outras faixas para editar) e voltar à mesma
/// música — coisa que o Tocar, por obedecer à seleção, não faria.
/// </summary>
void Tocador::AoPausar()
{
    if (!_player() || !_player()->Vivo()) return;
    EstadoPlayer objEstado = _player()->Estado();
    if (objEstado == EstadoPlayer::Tocando) _player()->Pausar();
    else if (objEstado == EstadoPlayer::Pausado) Retomar();
    // LINUX: com o WMP, pause/play mudam o playState na hora; aqui o pedido vai ao fio do
    // reprodutor e o estado lido logo depois pode ainda ser o anterior: o transporte se acerta na
    // batida seguinte do relógio (até 300 ms).
    AtualizarTransporte(_player()->Estado());
}

void Tocador::Retomar()
{
    // Retomar depois de um Parar devolve o avanço automático; sem isto
    // a faixa terminava e nada mais tocava.
    _paradoPeloUsuario = false;
    _player()->Tocar();
}

void Tocador::AtualizarTransporte(EstadoPlayer objEstado)
{
    bool blnPausado = objEstado == EstadoPlayer::Pausado;
    bool blnEmCurso = blnPausado || objEstado == EstadoPlayer::Tocando;
    janela.AtualizarTransporte(blnPausado, blnEmCurso);
}

void Tocador::AoParar()
{
    if (!_player() || !_player()->Vivo()) return;
    // Marca a intenção: parar a pedido não deve avançar para a próxima.
    _paradoPeloUsuario = true;
    _player()->Parar();
    // EM EXECUÇÃO solta o arquivo (análise e medidores): é o que deixa
    // gravar as tags depois de Parar. Tocar de novo reabre tudo.
    ModuloExecucao->Parar();
    _urlAnalisada.clear();
    _blnVideoAtual = false;
    PosicionarVideo();
    // if (!string.IsNullOrEmpty(_arquivoSel)) AtualizarEstadoEdicao();
    if (janela.AtualizarEstadoEdicaoSeFaixa) janela.AtualizarEstadoEdicaoSeFaixa();
}

// ===============================================================
//  VÍDEO
// ===============================================================
const char *const s_arrExtensoesVideo[] = {".wmv", ".avi", ".mpg", ".mpeg", ".mp4", ".m4v", ".mov",
                                           ".mkv", ".dvr-ms", ".wtv", ".ts", ".m2ts", ".vob"};

/// <summary>
/// A mídia que começou a tocar é vídeo? Pelo que o WMP sabe dela
/// (MediaType), pela imagem já decodificada, ou pela extensão — a
/// primeira resposta que disser sim. .asf fica com o WMP e a imagem:
/// pode ser só áudio.
/// </summary>
bool Tocador::EhVideo(const std::string &strUrl)
{
    if (_player()->MidiaEhVideo() || _player()->TemVideo()) return true;
    std::string strExtensao = strUrl.empty() ? "" : Base::Minusculas(Base::Extensao(strUrl));
    for (const char *e : s_arrExtensoesVideo)
        if (strExtensao == e) return true;
    return false;
}

/// <summary>
/// Na tela cheia, o WMP embutido mostra a barra de controles e nunca a
/// esconde (Lorenzo, 04/10/2026 — não era o relógio do ARCA: com ele
/// quieto, continuou). O ARCA faz o que o WMP não faz: mouse parado por
/// 3 s, a interface vai para "none" e a barra some; o mouse mexeu, volta
/// "full" e a barra aparece.
///
/// Se o WMP sair da tela cheia ao trocar a interface, o ARCA volta para
/// a tela cheia com a barra, desliga isto na sessão e avisa no palco —
/// o defeito não pode virar um pior.
/// </summary>
void Tocador::EsconderControlesSeOcioso()
{
    Point ptAgora = Mouse::PosicaoNaTela();
    double dtAgora = Agora();
    if (ptAgora.X != _ptMouseAnterior.X || ptAgora.Y != _ptMouseAnterior.Y)
    {
        _ptMouseAnterior = ptAgora;
        _dtUltimoMovimento = dtAgora;
        if (_blnControlesEscondidos)
        {
            _blnControlesEscondidos = false;
            _player()->DefinirModoInterface("full");
        }
        return;
    }
    if (_blnControlesEscondidos || _blnEsconderFalhou) return;
    if (dtAgora - _dtUltimoMovimento < SegundosAteEsconder) return;

    _player()->DefinirModoInterface("none");
    _blnControlesEscondidos = true;
    if (!_player()->TelaCheia())
    {
        _blnEsconderFalhou = true;
        _blnControlesEscondidos = false;
        _player()->DefinirModoInterface("full");
        _player()->TelaCheia(true);
        ModuloExecucao->AvisarVideo("O WMP sai da tela cheia ao esconder a barra; ela fica visível nesta sessão.");
    }
}

/// <summary>
/// Põe a moldura do controle do WMP (na CamadaVideo, por cima da
/// janela) onde o vídeo deve aparecer: o palco inteiro do EM EXECUÇÃO
/// tocando vídeo; a vaga de 110 px do rodapé nos outros módulos; em
/// lugar nenhum com música ou parado. A moldura nunca sai da árvore
/// visual — só muda de posição e tamanho —, então a reprodução não para.
/// </summary>
void Tocador::PosicionarVideo()
{
    if (!ModuloExecucao) return;
    bool blnVideo = _blnVideoAtual && _player() && _player()->Vivo();
    bool blnPalco = blnVideo && ModuloExecucao->IsVisible() && ModuloExecucao->ModoVideo();

    double dblVaga = blnVideo && !blnPalco ? 110 : 0;
    if (janela.VagaVideo->Width != dblVaga)
    {
        janela.VagaVideo->Width = dblVaga;
        // A vaga só ganha posição depois do próximo layout.
        janela.DepoisDoLayout([this] { PosicionarVideo(); });
    }

    // LINUX: a MolduraVideo (o controle do WMP numa camada por cima da janela) ainda não existe: a
    // imagem do vídeo não é desenhada (Interop/PlayerWmp.cpp, pendência). O alvo fica calculado
    // aqui, como no original, para quem for desenhá-la.
    UIElement *objAlvo = blnPalco ? (UIElement *) ModuloExecucao->AreaDoVideo().get()
                                  : (blnVideo ? (UIElement *) janela.VagaVideo.get() : nullptr);
    if (!objAlvo || !objAlvo->IsVisible() || objAlvo->ActualWidth < 1 || objAlvo->ActualHeight < 1) return;
}

void Tocador::AoMudarVolume(double novo)
{
    if (!_player() || !_player()->Vivo()) return;
    _player()->Volume((int) novo);
}

void Tocador::AoSoltarPosicao()
{
    if (!_player() || !_player()->Vivo()) return;
    double dur = _player()->Duracao();
    if (dur <= 0) return;
    _player()->Posicao(janela.SliderPosicao->Value() * dur);
}

// ===============================================================
void Tocador::AoBater()
{
    if (!_player() || !_player()->Vivo()) return;

    EstadoPlayer estado = _player()->Estado();
    double pos = _player()->Posicao();
    double dur = _player()->Duracao();

    // Fim de faixa.
    //
    // A primeira versão esperava o estado MidiaTerminou, e não
    // funcionou: lendo de 300 em 300 ms, um estado transitório pode
    // passar inteiro entre duas leituras. Detectar estado transitório
    // por amostragem é apostar na sorte.
    //
    // O que não é transitório é a posição ter chegado ao fim. Então a
    // regra é: estava tocando, deixou de tocar, a posição estava no
    // fim, e não fui eu que mandei parar.
    // Pausado não é fim, mesmo perto do fim: pausar no último segundo
    // pulava para a próxima faixa.
    if (_estadoAnterior == EstadoPlayer::Tocando && estado != EstadoPlayer::Tocando && estado != EstadoPlayer::Pausado)
    {
        bool chegouAoFim = _ultimaDuracao > 0 && _ultimaPosicao >= _ultimaDuracao - 1.5;
        if ((estado == EstadoPlayer::MidiaTerminou || chegouAoFim) && !_paradoPeloUsuario)
        {
            _estadoAnterior = estado;
            if (janela.Pular) janela.Pular(+1);
            return;
        }
    }
    // if (estado != _estadoAnterior && !string.IsNullOrEmpty(_arquivoSel)) AtualizarEstadoEdicao();
    if (estado != _estadoAnterior && janela.AtualizarEstadoEdicaoSeFaixa) janela.AtualizarEstadoEdicaoSeFaixa();
    _estadoAnterior = estado;

    bool tocando = estado == EstadoPlayer::Tocando;
    if (tocando) { _ultimaPosicao = pos; _ultimaDuracao = dur; }

    // Em tela cheia o relógio não mexe na janela por baixo: atualizar
    // tempo, progresso, botões e a posição do vídeo a cada 300 ms
    // fazia o Windows mandar ao WMP um "movimento de mouse" artificial,
    // e a barra de controles dele nunca sumia (Lorenzo, 04/10/2026).
    // O fim de faixa (acima) e a saída da tela cheia continuam valendo.
    if (_blnEmTelaCheia && _player()->TelaCheia()) { EsconderControlesSeOcioso(); return; }

    AtualizarTransporte(estado);

    janela.TxtTempo->Text = Modelo::Formato::Tempo(pos) + " / " + Modelo::Formato::Tempo(dur);

    if (!janela.SliderPosicao->IsMouseCaptureWithin())
        janela.SliderPosicao->Value = dur > 0 ? std::min(1.0, pos / dur) : 0.0;

    std::string titulo = _player()->TituloAtual();
    janela.TxtTitulo->Text = titulo.empty() ? "—" : titulo;
    janela.TxtArtista->Text = _player()->ArtistaAtual();

    // --- EM EXECUÇÃO ---
    // A faixa mudou? Avisa o módulo, que reabre a análise e a capa.
    std::string url = _player()->Url();
    // Só com a faixa em curso (tocando ou pausada): parado, o player
    // continua com o endereço, e reabrir a análise prenderia de novo o
    // arquivo que o Parar acabou de soltar.
    bool blnEmCurso = estado == EstadoPlayer::Tocando || estado == EstadoPlayer::Pausado;
    bool mesma = !_urlAnalisada.empty() && Base::IgualSemCaixa(url, _urlAnalisada);
    if (url.empty() && _urlAnalisada.empty()) mesma = true;
    if (blnEmCurso && !mesma)
    {
        _urlAnalisada = url;
        // Vídeo se assiste; música se analisa.
        _blnVideoAtual = EhVideo(url);
        if (_blnVideoAtual)
            ModuloExecucao->DefinirVideo(url, titulo, _player()->ArtistaAtual());
        else
            ModuloExecucao->DefinirFaixa(url, titulo, _player()->ArtistaAtual(), _player()->AlbumAtual());
    }
    else if (blnEmCurso && !_blnVideoAtual && _player()->TemVideo())
    {
        // Tinha cara de áudio (um .asf, por exemplo) e apareceu imagem.
        _blnVideoAtual = true;
        ModuloExecucao->DefinirVideo(url, titulo, _player()->ArtistaAtual());
    }
    else if (estado == EstadoPlayer::Parado && _estadoAnteriorBatida == EstadoPlayer::Parado && _blnVideoAtual)
    {
        // A lista acabou num vídeo (parou sozinho, duas batidas seguidas —
        // não é a passagem rápida entre faixas): o palco sai.
        _blnVideoAtual = false;
        _urlAnalisada.clear();
        ModuloExecucao->Parar();
    }
    _estadoAnteriorBatida = estado;

    // Saiu da tela cheia (Esc, o botão dos controles do WMP, ou o
    // vídeo acabou): a interface volta a ser só a imagem, e o volume
    // mexido lá aparece no controle do rodapé.
    if (_blnEmTelaCheia && !_player()->TelaCheia())
    {
        _blnEmTelaCheia = false;
        _blnControlesEscondidos = false;
        _player()->DefinirModoInterface("none");
        int intVolume = _player()->Volume();
        if (std::fabs(janela.SliderVolume->Value() - intVolume) >= 1) janela.SliderVolume->Value = (double) intVolume;
    }
    PosicionarVideo();

    // Os instrumentos têm relógio próprio, de 30 quadros por segundo.
    // Este aqui bate a cada 300 ms e serviria só para picotar.
}

} // namespace

// ---- o encaixe do EM EXECUCAO (ver o fim de MainWindow.h) ----
std::shared_ptr<VistaModulo> CriaModuloExecucao(MainWindow &janela)
{
    auto view = std::make_shared<ExecucaoView>();
    auto t = std::make_shared<Tocador>(janela, view);

    // LINUX: o original criava o player no começo do AoCarregar, antes da Biblioteca. Aqui ele nasce
    // já no encaixe: o Carregado da BIBLIOTECA (ligado antes deste, na ordem do xos-arca.cpp) acha
    // o _player pronto. O resto do AoCarregar do tocador roda no Carregado, como no original.
    if (!janela._player) janela._player = std::make_shared<PlayerWmp>();

    janela.AoTocar = [t] { t->AoTocar(); };
    janela.AoPausar = [t] { t->AoPausar(); };
    janela.AoParar = [t] { t->AoParar(); };
    janela.AoMudarVolume = [t](double novo) { t->AoMudarVolume(novo); };
    janela.AoSoltarPosicao = [t] { t->AoSoltarPosicao(); };
    janela.PosicionarVideo = [t] { t->PosicionarVideo(); };
    janela.PlayerVivo = [t] { return t->_player() && t->_player()->Vivo(); };
    janela.AtualizarTransporteAgora = [t] { t->AtualizarTransporte(t->_player()->Estado()); };
    // o Tocar(ItemMidia) da BIBLIOTECA zera estes antes de carregar a faixa
    janela.AoComecarFaixa = [t] {
        t->_paradoPeloUsuario = false;
        t->_ultimaPosicao = 0;
        t->_ultimaDuracao = 0;
    };
    // o AoConteudoAlbum da BIBLIOTECA para o player para gravar: parar a pedido não avança
    janela.MarcarParadoPeloUsuario = [t] { t->_paradoPeloUsuario = true; };
    janela.RecarregarEncarteSeDoAlbum = [view](const std::string &pasta) { view->RecarregarEncarteSeDoAlbum(pasta); };
    janela.Carregado += [t] { t->AoCarregar(); };
    janela.Fechou += [t] { t->AoFechar(); };
    return view;
}

} // namespace ARCA::Vistas

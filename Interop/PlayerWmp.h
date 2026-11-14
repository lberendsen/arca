// ARCA no xOS - Interop/PlayerWmp (de Interop\PlayerWmp.cs e Interop\HospedeiroWmp.cs).
//
// O original e uma fachada tipada sobre o controle COM do Windows Media Player (ligacao tardia,
// InvokeMember). O resto do programa so conhece esta fachada - por isso ela mantem a MESMA
// interface (nomes, unidades, o enum EstadoPlayer com os valores do playState do WMP).
//
// LINUX: nao ha WMP. O reprodutor e proprio: FFmpeg (libav*) decodifica (FLAC, WMA, MP3, video),
// libswresample converte, ALSA toca; o video (quando houver) e desenhado pela interface na
// VagaVideo do rodape. Vivo diz se o reprodutor abriu a saida de audio (no original: se o OCX
// foi criado). A "colecao" do WMP (mediaCollection) nao existe: quem a usava relia a pasta.
//
// Regra que vale para a classe inteira (como no original): tudo e chamado do fio da tela. A
// decodificacao e a saida rodam num fio proprio, escondido aqui dentro.
#pragma once

#include <memory>
#include <string>

namespace ARCA::Interop {

enum class EstadoPlayer
{
    Indefinido = 0, Parado = 1, Pausado = 2, Tocando = 3,
    Avancando = 4, Retrocedendo = 5, Armazenando = 6, Esperando = 7,
    MidiaTerminou = 8, EmTransicao = 9, Pronto = 10, Reconectando = 11
};

class PlayerWmp
{
public:
    // LINUX: o original recebia o OCX (HospedeiroWmp.Ocx); aqui o reprodutor e criado direto.
    PlayerWmp();
    ~PlayerWmp();
    PlayerWmp(const PlayerWmp &) = delete;
    PlayerWmp &operator=(const PlayerWmp &) = delete;

    bool Vivo() const;

    // ---- reproducao ----
    std::string Url() const;                 // "" = nada carregado (null no original)
    void Url(const std::string &valor);      // carrega (nao toca); "" descarrega
    EstadoPlayer Estado() const;

    void Tocar();
    void Pausar();
    /// Parar de verdade, nao pausar: interrompe e zera a posicao (o play seguinte comeca do 0).
    void Parar();

    double Posicao() const;                  // segundos
    void Posicao(double segundos);
    std::string PosicaoTexto() const;        // "m:ss" ("0:00" sem midia), como currentPositionString

    double Duracao() const;                  // segundos (0 sem midia)
    std::string DuracaoTexto() const;        // como durationString

    /// 0 a 100.
    int Volume() const;
    void Volume(int valor);
    bool Mudo() const;
    void Mudo(bool valor);

    /// "none" some com a interface do player e deixa so a imagem. LINUX: guarda o modo (sem
    /// interface propria do WMP, "full" e "none" so mudam o que a tela cheia mostra).
    void DefinirModoInterface(const std::string &modo);
    void DefinirRepeticao(bool repetir);

    // ---- midia atual ----
    bool TemMidia() const;                   // LINUX: no lugar de "MidiaAtual != null"
    std::string TituloAtual() const;
    std::string ArtistaAtual() const;
    std::string AlbumAtual() const;

    int LarguraVideo() const;
    int AlturaVideo() const;
    bool TemVideo() const { return AlturaVideo() > 0; }
    /// A midia atual e video (pelo conteiner), valendo desde o inicio.
    bool MidiaEhVideo() const;
    /// O video ocupa a area que tiver, mantendo a proporcao.
    void EsticarVideo(bool esticar);
    /// Tela cheia. So vale com midia tocando ou pausada; o Esc volta.
    bool TelaCheia() const;
    void TelaCheia(bool valor);

    /// getItemInfo do WMP. LINUX: aceita os nomes ASF que o ARCA usa ("Title", "Author",
    /// "WM/AlbumArtist", "WM/AlbumTitle", "MediaType") e le as tags da midia (Vorbis no FLAC, ASF
    /// no WMA) pelo mesmo mapa do EditorFlac. "" = sem valor (null no original).
    std::string InfoAtual(const std::string &atributo) const;

    struct Impl;
private:
    std::unique_ptr<Impl> _impl;
};

} // namespace ARCA::Interop

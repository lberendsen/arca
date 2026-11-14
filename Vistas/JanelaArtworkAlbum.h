// ARCA no xOS - Vistas/JanelaArtworkAlbum (de Vistas\JanelaArtworkAlbum.cs).
#pragma once

#include "ImagensDoEncarte.h"
#include "Janela.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ARCA::Vistas {

/// <summary>
/// Capa e encarte de um álbum que já está no acervo.
///
/// A AQUISIÇÃO digitaliza o encarte antes de aprovar; esta janela é para
/// o disco que entrou sem ele — como o de João do Morro &amp; Pé da Serra,
/// integrado antes de a digitalização existir. Mostra o que a pasta
/// Artwork do álbum já tem, junta o que for digitalizado numa área "a
/// acrescentar", e só grava ao pedir.
///
/// Gravar no acervo é diferente de gravar na área de trabalho, então cada
/// imagem vai por <see cref="Integracao.GravarNoAcervo"/>: .partial
/// conferido por hash, e o que já existir com o mesmo nome vira backup
/// datado. Páginas novas continuam a numeração das que existem (nunca
/// sobrescrevem). Tudo o que entra fica no ledger como "encarte
/// acrescentado". A capa embutida nas faixas não muda aqui.
///
/// As imagens que já estão no álbum também podem ser giradas, divididas
/// e tiradas, e também só ao gravar. Nada é apagado: a regravada deixa a
/// anterior como backup datado, e a tirada vira ela mesma o backup. No
/// ledger, "encarte alterado" e "encarte retirado".
/// </summary>
class JanelaArtworkAlbum : public Window
{
public:
    JanelaArtworkAlbum(const std::string &strPastaAlbum, const std::string &strArtista, const std::string &strAlbum,
                       const std::string &strRaizAcervo);

    /// <summary>true se algo foi gravado no álbum (quem chama pode querer recarregar o encarte em exibição).</summary>
    bool Gravou() const { return _blnGravou; }

private:
    /// <summary>
    /// Uma imagem da pasta Artwork, com o que se pediu para fazer com ela.
    /// Nada toca o disco até "Gravar no álbum": girar e dividir ficam
    /// pendentes aqui, como nas imagens novas.
    /// </summary>
    class TImagemNoAlbum
    {
    public:
        std::string strArquivo;           // caminho no acervo
        bool blnExiste = false;           // false: metade direita de uma divisão, ainda por criar
        std::vector<uint8_t> arrConteudo; // vazio (null): o conteúdo é o do disco
        int intGiro = 0;                  // pendente, em graus no sentido horário
        bool blnTirar = false;            // sai do álbum (vira backup) ao gravar

        bool Alterada() const { return !blnExiste || !arrConteudo.empty() || intGiro != 0 || blnTirar; }
        void Girar(int intGraus) { intGiro = ((intGiro + intGraus) % 360 + 360) % 360; }
        void Substituir(const std::vector<uint8_t> &arrNovo);
        std::shared_ptr<BitmapSource> Imagem() const;
        /// <summary>Guardada: girar redesenha a lista inteira, e reabrir vinte scans a cada clique pesaria.</summary>
        std::shared_ptr<BitmapSource> Miniatura(int intLargura);
        /// <summary>
        /// O que gravar no lugar do arquivo, no formato dele; null se o
        /// conteúdo não mudou. O giro é aplicado aqui, uma vez só.
        /// </summary>
        std::vector<uint8_t> ConteudoFinal() const;

    private:
        std::shared_ptr<BitmapSource> m_objMiniatura;   // do conteúdo atual, sem giro
    };

    std::string _strPastaAlbum, _strArtista, _strAlbum, _strRaizAcervo;
    std::shared_ptr<WrapPanel> _pnlExistentes, _pnlNovos;
    std::shared_ptr<TextBlock> _txtExistentes, _txtNovos, _txtAviso;
    std::shared_ptr<Button> _btnGravar;

    std::vector<uint8_t> _arrCapaNova;                  // plena; vazio = null
    std::vector<TPaginaPendente> _lstPaginasNovas;
    std::vector<TImagemNoAlbum> _lstNoAlbum;
    bool _blnGravou = false;

    UIElementPtr MontarCabecalho();
    UIElementPtr MontarBarraNovos();
    std::shared_ptr<Button> Botao(const std::string &strTexto, std::function<void()> objAcao);
    UIElementPtr MontarRodape();
    std::string PastaArtwork() const;
    std::vector<std::string> ImagensExistentes() const;
    void CarregarExistentes();
    void AtualizarExistentes();
    static std::string LegendaNoAlbum(const TImagemNoAlbum &objItem);
    int QtdAlteradas() const;
    void AtualizarBotaoGravar();
    void TirarNoAlbum(int intIndice);
    void DividirNoAlbum(int intIndice);
    std::string NomeDaMetadeDireita(const std::string &strArquivo) const;
    void AtualizarNovos();
    void DividirPagina(int intIndice, const std::string &strNome);
    std::shared_ptr<StackPanel> Cartao(const std::shared_ptr<BitmapSource> &objImg, const std::string &strLegenda, UIElementPtr objAcoes);
    int ProximaPagina() const;
    void AoDigitalizarCapa();
    void AoCapaDeArquivo();
    void AoDigitalizarPagina();
    void AoPaginaDeArquivo();
    void UsarCapa(const std::shared_ptr<BitmapSource> &objImagem);
    void UsarPagina(const std::shared_ptr<BitmapSource> &objImagem);
    void AoGravar();
    void AoFechar(CancelEventArgs &e);
    static Brush Recurso(const std::string &strChave) { return Tema::Recurso(strChave); }
};

} // namespace ARCA::Vistas

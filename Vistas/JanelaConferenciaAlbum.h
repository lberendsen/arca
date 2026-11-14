// ARCA no xOS - Vistas/JanelaConferenciaAlbum (de Vistas\JanelaConferenciaAlbum.cs).
#pragma once

#include "Janela.h"
#include "Modelo/AccurateRip.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ARCA::Vistas {

/// <summary>
/// Confere no AccurateRip um álbum que já está no acervo, sem o disco.
///
/// O Windows Media Player (e o ARCA) gravam em cada faixa o índice do CD
/// de origem, WM/MCDI. Com ele, as faixas são remontadas como o disco e
/// cada uma é comparada com a assinatura de quem ripou o mesmo CD. É a
/// única forma de saber, anos depois, se uma faixa antiga é bit-perfect.
///
/// Uma pasta pode guardar mais de um CD (álbum duplo numa pasta só): as
/// faixas são agrupadas pelo índice que cada uma guarda, e cada disco é
/// conferido por si.
///
/// Só lê. Gravar o veredito nas faixas (ARCA/Verification) é um botão à
/// parte; o ledger recebe um registro "conferência AccurateRip" sempre.
/// </summary>
class JanelaConferenciaAlbum : public Window
{
public:
    JanelaConferenciaAlbum(const std::string &strPasta, const std::string &strArtista, const std::string &strAlbum,
                           const std::string &strRaizAcervo);
    ~JanelaConferenciaAlbum() override;

private:
    /// <summary>Um CD da pasta: as faixas que guardam o mesmo índice, e o que a conferência deu.</summary>
    struct TDisco
    {
        std::string strMcdi;
        std::vector<std::string> lstArquivos;
        std::optional<Modelo::TResultadoAccurateRip> objResultado;   // nullopt = null
        std::string strMensagem;      // quando não deu para conferir ("" = null)
        std::string strObservacao;    // "" = null
    };

    // LINUX: o que a linha de execucao da conferencia preenche, guardado a parte da janela: a
    // janela pode fechar (e ser destruida) com a conferencia ainda rodando; o fio so volta a ela
    // pelo Despacha e se ela ainda existir (blnJanelaViva, so mexido no fio da tela).
    struct TConferencia
    {
        std::atomic<bool> blnFechando{false};
        bool blnJanelaViva = true;
        std::vector<TDisco> lstDiscos;
        int intSemIndice = 0;
    };

    std::string _strPasta, _strArtista, _strAlbum, _strRaizAcervo;
    std::shared_ptr<TextBlock> _txtEstado, _txtVeredito;
    std::shared_ptr<StackPanel> _pnlFaixas;
    std::shared_ptr<Button> _btnGravar;
    std::shared_ptr<TConferencia> _objConf;

    void Iniciar();
    static void Conferir(JanelaConferenciaAlbum *objJanela, std::shared_ptr<TConferencia> c, std::string strPasta,
                         std::string strArtista, std::string strAlbum, std::string strRaizAcervo);
    void Mostrar();
    UIElementPtr Linha(const std::string &strNome, const Modelo::TResultadoAccurateRip &r, const Modelo::TFaixaAccurateRip &f);
    void AoGravar();
};

} // namespace ARCA::Vistas

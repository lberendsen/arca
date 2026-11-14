// ARCA no xOS - Vistas/clsPainelProgressoAquisicao (de Vistas\clsPainelProgressoAquisicao.cs).
#include "clsPainelProgressoAquisicao.h"

#include "Base/Despacho.h"

#include <cmath>
#include <cstdio>

namespace ARCA::Vistas {

clsPainelProgressoAquisicao::clsPainelProgressoAquisicao(
    std::shared_ptr<FrameworkElement> pnlProgressoPercentual,
    std::shared_ptr<FrameworkElement> pnlFaseRapida,
    std::shared_ptr<TextBlock> txtFaseLongaTitulo,
    std::shared_ptr<TextBlock> txtPercentualNumero,
    std::shared_ptr<ProgressBar> pbProgressoFase,
    std::shared_ptr<TextBlock> txtFaseVerbo,
    std::shared_ptr<TextBlock> txtFaseDetalhe)
    : m_pnlProgressoPercentual(std::move(pnlProgressoPercentual)),
      m_pnlFaseRapida(std::move(pnlFaseRapida)),
      m_txtFaseLongaTitulo(std::move(txtFaseLongaTitulo)),
      m_txtPercentualNumero(std::move(txtPercentualNumero)),
      m_pbProgressoFase(std::move(pbProgressoFase)),
      m_txtFaseVerbo(std::move(txtFaseVerbo)),
      m_txtFaseDetalhe(std::move(txtFaseDetalhe))
{
}

void clsPainelProgressoAquisicao::DefinirFaseRapida(const std::string &strVerbo, const std::string &strDetalhe)
{
    if (Base::NoFioDaTela())
    {
        AplicarFaseRapida(strVerbo, strDetalhe);
    }
    else
    {
        Base::Despacha([this, strVerbo, strDetalhe] {
            AplicarFaseRapida(strVerbo, strDetalhe);
        });
    }
}

void clsPainelProgressoAquisicao::AtualizarProgresso(const std::string &strTituloFase, double dblPercentual,
                                                     const std::optional<std::string> &strDetalhe)
{
    if (Base::NoFioDaTela())
    {
        AplicarProgresso(strTituloFase, dblPercentual, strDetalhe);
    }
    else
    {
        Base::Despacha([this, strTituloFase, dblPercentual, strDetalhe] {
            AplicarProgresso(strTituloFase, dblPercentual, strDetalhe);
        });
    }
}

void clsPainelProgressoAquisicao::DefinirAguardandoAprovacao(const std::optional<std::string> &strDetalhe)
{
    DefinirFaseRapida("AGUARDANDO APROVAÇÃO", strDetalhe ? *strDetalhe : "Pronto para integrar ao acervo");
}

void clsPainelProgressoAquisicao::Resetar(const std::string &strMensagem, const std::string &strDetalhe)
{
    DefinirFaseRapida(strMensagem, strDetalhe);
}

void clsPainelProgressoAquisicao::AplicarFaseRapida(const std::string &strVerbo, const std::string &strDetalhe)
{
    if (m_pnlProgressoPercentual)
    {
        m_pnlProgressoPercentual->Visibility = Visibility::Collapsed;
    }

    if (m_pnlFaseRapida)
    {
        m_pnlFaseRapida->Visibility = Visibility::Visible;
    }

    if (m_txtFaseVerbo)
    {
        m_txtFaseVerbo->Text = strVerbo;
    }

    if (m_txtFaseDetalhe)
    {
        m_txtFaseDetalhe->Text = strDetalhe;
    }
}

void clsPainelProgressoAquisicao::AplicarProgresso(const std::string &strTituloFase, double dblPercentual,
                                                   const std::optional<std::string> &strDetalhe)
{
    if (dblPercentual < 0.0) dblPercentual = 0.0;
    if (dblPercentual > 100.0) dblPercentual = 100.0;

    if (m_pnlFaseRapida)
    {
        m_pnlFaseRapida->Visibility = Visibility::Collapsed;
    }

    if (m_pnlProgressoPercentual)
    {
        m_pnlProgressoPercentual->Visibility = Visibility::Visible;
    }

    if (m_txtFaseLongaTitulo)
    {
        m_txtFaseLongaTitulo->Text = strTituloFase;
    }

    if (m_txtPercentualNumero)
    {
        // string.Format("{0:F0}", Math.Floor(dblPercentual))
        char sz[32];
        snprintf(sz, sizeof sz, "%.0f", std::floor(dblPercentual));
        m_txtPercentualNumero->Text = sz;
    }

    if (m_pbProgressoFase)
    {
        m_pbProgressoFase->Value = dblPercentual;
    }

    if (m_txtFaseDetalhe && strDetalhe)
    {
        m_txtFaseDetalhe->Text = *strDetalhe;
    }
}

} // namespace ARCA::Vistas

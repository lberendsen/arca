// ARCA no xOS - Vistas/clsPainelProgressoAquisicao (de Vistas\clsPainelProgressoAquisicao.cs).
#pragma once

#include "Controles.h"

#include <memory>
#include <optional>
#include <string>

namespace ARCA::Vistas {

/// Controlador do medidor visual de fases e progresso do módulo de Aquisição.
/// Exibe verbos de ação para operações rápidas (< 30s) e número grande percentual
/// com barra de progresso suave e métricas para operações longas (> 30s).
/// Adota estritamente notação húngara e separação de responsabilidades (SRP).
///
/// Pode ser chamado de qualquer fio: fora do fio da tela, o pedido vai por Base::Despacha.
class clsPainelProgressoAquisicao
{
public:
    // LINUX: o original recebia também o Dispatcher da vista (para CheckAccess e BeginInvoke).
    // Aqui o despachante é um só, o de Base/Despacho (NoFioDaTela e Despacha).
    clsPainelProgressoAquisicao(
        std::shared_ptr<FrameworkElement> pnlProgressoPercentual,
        std::shared_ptr<FrameworkElement> pnlFaseRapida,
        std::shared_ptr<TextBlock> txtFaseLongaTitulo,
        std::shared_ptr<TextBlock> txtPercentualNumero,
        std::shared_ptr<ProgressBar> pbProgressoFase,
        std::shared_ptr<TextBlock> txtFaseVerbo,
        std::shared_ptr<TextBlock> txtFaseDetalhe);

    /// Define uma fase rápida (ex: sondagem, CD-TEXT, mineração ou manifesto).
    /// Oculta o número percentual e exibe com destaque o verbo de ação e o detalhe contextual.
    /// (strDetalhe "" = o null do original: os dois davam o texto vazio)
    void DefinirFaseRapida(const std::string &strVerbo, const std::string &strDetalhe = "");

    /// Atualiza o progresso para fases de longa duração (> 30s, ex: extração SPTI, codificação WMA ou integração).
    /// Ativa o painel de porcentagem com o número grande, barra de progresso e informações de taxa/tempo.
    /// (strDetalhe nullopt = null: o detalhe anterior fica)
    void AtualizarProgresso(const std::string &strTituloFase, double dblPercentual,
                            const std::optional<std::string> &strDetalhe = std::nullopt);

    /// Ativa a indicação de aguardando decisão humana para o commit.
    void DefinirAguardandoAprovacao(const std::optional<std::string> &strDetalhe = std::nullopt);

    /// Reseta o medidor para o estado inicial ou ocioso.
    void Resetar(const std::string &strMensagem = "PRONTO", const std::string &strDetalhe = "Aguardando início da extração");

private:
    void AplicarFaseRapida(const std::string &strVerbo, const std::string &strDetalhe);
    void AplicarProgresso(const std::string &strTituloFase, double dblPercentual, const std::optional<std::string> &strDetalhe);

    std::shared_ptr<FrameworkElement> m_pnlProgressoPercentual;
    std::shared_ptr<FrameworkElement> m_pnlFaseRapida;
    std::shared_ptr<TextBlock> m_txtFaseLongaTitulo;
    std::shared_ptr<TextBlock> m_txtPercentualNumero;
    std::shared_ptr<ProgressBar> m_pbProgressoFase;
    std::shared_ptr<TextBlock> m_txtFaseVerbo;
    std::shared_ptr<TextBlock> m_txtFaseDetalhe;
};

} // namespace ARCA::Vistas

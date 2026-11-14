// ARCA no xOS - Interop/ScannerSane (de Interop\ScannerWia.cs).
//
// O original digitaliza pelo dialogo padrao do Windows (WIA Automation, ShowAcquireImage com
// UseCommonUI): o Windows mostra a previa da mesa, a pessoa escolhe area e qualidade, e o
// resultado vem sem perda (PNG ou BMP), com a resolucao que o scanner informou. As tres correcoes
// que o ARCA fez sobre o ARCA antigo continuam valendo aqui:
//   - qualidade maxima de verdade (aqui: a resolucao pedida, sem compressao no caminho);
//   - "nenhum scanner" vira mensagem legivel, nao um clique que nao faz nada;
//   - a imagem vem sem perda (PNG) e a unica compressao e a final, depois do corte.
//
// LINUX: WIA vira SANE (libsane; a Canon PIXMA G7000 pelo backend pixma). O dialogo comum do
// Windows nao existe; o ARCA tem o seu (a vista registra DialogoComum ao abrir). Digitalizar()
// mantem a assinatura do original e chama esse dialogo; as funcoes de baixo (Previa,
// DigitalizarArea) sao o que o dialogo usa para falar com o scanner.
#pragma once

#include <functional>
#include <string>

namespace ARCA::Interop {

class ScannerSane
{
public:
    /// Abre o dialogo do scanner e salva o resultado num arquivo temporario. Devolve o caminho, ou
    /// "" (null no original) se a pessoa cancelou. Lanca InvalidOperationException com mensagem
    /// legivel se nao houver scanner, ou se o scanner falhar. Quem chama apaga o arquivo depois de
    /// le-lo. dblDpi e a resolucao usada (0 se nao se sabe) - e ela que transforma "12 cm" em
    /// pixels no corte; ela tambem vai carimbada no PNG (pHYs).
    /// Tem de ser chamado do fio da tela (o dialogo e modal).
    static std::string Digitalizar(double &dblDpi);

    /// O dialogo comum (previa, area, resolucao), registrado pela interface. Devolve o caminho do
    /// PNG digitalizado ou "" se cancelou; preenche o dpi. Sem dialogo registrado, Digitalizar
    /// digitaliza a mesa inteira a ResolucaoPadrao.
    static std::function<std::string(double &)> DialogoComum;

    static constexpr int ResolucaoPadrao = 600;     // a que o acervo ja usa (ledger: capa a 600 dpi)
    static constexpr int ResolucaoPrevia = 75;

    // ---- o que o dialogo usa ----
    /// O primeiro scanner encontrado ("" se nenhum), e o nome para mostrar ("CANON Canon PIXMA G7000 Series").
    static std::string Dispositivo(std::string *strNome = nullptr);
    /// Tamanho da mesa, em mm.
    static void Mesa(double &dblLarguraMm, double &dblAlturaMm);
    /// Digitaliza a area (mm, a partir do canto de cima a esquerda; largura/altura <= 0 = mesa
    /// inteira) em intDpi, colorido, num PNG sem perda com o dpi carimbado. progresso(0..1) e
    /// chamado do fio que digitaliza; cancelou() true interrompe (lanca OperationCanceledException).
    /// Lanca InvalidOperationException com mensagem legivel em falha. Devolve o caminho do PNG
    /// (em /tmp, "ARCA_scan_<aleatorio>.png").
    static std::string DigitalizarArea(double x, double y, double largura, double altura, int intDpi,
                                       const std::function<void(double)> &progresso,
                                       const std::function<bool()> &cancelou);
    /// A mesa inteira a ResolucaoPrevia (para o dialogo mostrar e a pessoa escolher a area).
    static std::string Previa(const std::function<void(double)> &progresso, const std::function<bool()> &cancelou);
};

} // namespace ARCA::Interop

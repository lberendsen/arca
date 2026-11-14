// ARCA no xOS - Vistas/JanelaScanner.
//
// LINUX: este arquivo inteiro e do xOS; nao ha .cs de origem. No Windows, ScannerWia.Digitalizar
// abria o dialogo comum do WIA (ShowAcquireImage com UseCommonUI): o proprio Windows mostrava a
// previa da mesa, a pessoa escolhia a area e a resolucao e clicava em Digitalizar, e o ARCA so
// recebia o arquivo sem perda e o dpi. O Linux nao tem esse dialogo. A JanelaScanner faz o mesmo
// papel, no visual do Tema: previa da mesa (75 dpi), escolha da area sobre a previa (comeca em volta
// do que esta na mesa), resolucao (75, 150, 300, 600 ou 1200
// dpi; 600 e o padrao do acervo), Visualizar, Digitalizar e Cancelar, com progresso e o scanner
// trabalhando em segundo plano (a janela nao trava; Cancelar interrompe a leitura).
//
// RegistrarJanelaScanner() a poe em Interop::ScannerSane::DialogoComum; o xos-arca.cpp chama ao
// abrir. Daí em diante ScannerSane::Digitalizar (que ImagensDoEncarte::DoScanner chama, com a
// assinatura do original) abre esta janela e devolve o PNG e o dpi, ou "" se a pessoa cancelou.
// As falhas saem como no ScannerWia: InvalidOperationException com "Nenhum scanner encontrado.
// Confira se está ligado e conectado, e tente de novo." ou "O scanner falhou: ...".
#pragma once

#include "Janela.h"

#include <memory>
#include <string>

namespace ARCA::Vistas {

class VeuDaArea;

// LINUX: o dialogo comum do WIA (ver o comentario do arquivo)
class JanelaScanner : public Window
{
public:
    explicit JanelaScanner(const std::string &strNomeScanner);
    ~JanelaScanner() override;

    /// O PNG digitalizado (em /tmp; quem chama apaga), ou "" se a pessoa cancelou.
    std::string Arquivo;
    /// A resolução da digitalização (0 se cancelou).
    double Dpi = 0;
    /// A falha do scanner na digitalização final ("" se não houve): quem chama a lança como
    /// InvalidOperationException, como o ScannerWia.
    std::string Erro;

private:
    struct Trabalho;

    static const int arrResolucoes[5];
    static int s_intUltimaResolucao;            // lembrada entre uma digitalização e outra (padrão 600)
    static constexpr double dblRaioAlca = 7;    // px de tela em volta de cada canto
    static constexpr double dblLadoMinimoMm = 5;

    enum class Modo { Nenhum, Mover, Redimensionar, Desenhar };

    double _dblMesaL = 216.069, _dblMesaA = 297.011;   // mm (a mesa da G7000 até o scanner dizer a sua)
    Rect _objArea;                                      // em mm, a partir do canto de cima à esquerda
    double _dblEscala = 1;                              // px de tela por mm
    double _dblX0 = 0, _dblY0 = 0;                      // onde a mesa começa no palco
    std::shared_ptr<BitmapSource> _objPrevia;

    std::shared_ptr<ComboBox> _cmbResolucao;
    std::shared_ptr<TextBlock> _txtArea, _txtEstado, _txtAguarde;
    std::shared_ptr<ProgressBar> _pbProgresso;
    std::shared_ptr<Button> _btnVisualizar, _btnDigitalizar, _btnMesaInteira;
    std::shared_ptr<Canvas> _objPalco;
    std::shared_ptr<Rectangle> _objMesa, _objMoldura;
    std::shared_ptr<Image> _objVista;
    std::shared_ptr<VeuDaArea> _objVeu;
    std::shared_ptr<Rectangle> _arrAlcas[4];

    Modo _objModo = Modo::Nenhum;
    Point _objAncora;
    Rect _objAreaNoInicio;

    std::shared_ptr<Trabalho> _objTrabalho;     // a leitura em andamento (nullptr = parado)
    bool _blnFecharAoTerminar = false;

    UIElementPtr MontarBarra();
    UIElementPtr MontarRodape();
    int ResolucaoEscolhida() const;
    void Ocupar(bool blnOcupado);
    void Progresso(const std::string &strTexto, double dblFracao);
    void Ajuda();
    void IniciarPrevia();
    void AoTerminarPrevia(const std::string &strArquivo);
    void Digitalizar();
    void AoCancelado();
    void AoFechar(CancelEventArgs &e);
    void Diagramar();
    void Desenhar();
    Point ParaTela(Point objMm) const;
    Point ParaMesa(Point objTela) const;
    Rect Limitado(Rect objR) const;
    void AoApertar(MouseButtonEventArgs &e);
    void AoMover(MouseEventArgs &e);
    void Soltar();
    Vistas::Cursor CursorPara(Point objTela) const;
};

// LINUX: liga a JanelaScanner em Interop::ScannerSane::DialogoComum (chamada pelo xos-arca.cpp ao abrir)
void RegistrarJanelaScanner();

} // namespace ARCA::Vistas

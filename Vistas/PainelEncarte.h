// ARCA no xOS - Vistas/PainelEncarte (de Vistas\PainelEncarte.xaml e PainelEncarte.xaml.cs).
#pragma once

#include "Janela.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ARCA::Vistas {

/// <summary>
/// As páginas do encarte, em lista rolável na largura do painel. Duplo
/// clique abre a página em tela cheia.
///
/// **O problema é memória, e a resposta é `DecodePixelWidth`.**
///
/// Um scan a 300 dpi tem cerca de 1400x1400 pixels — 7,8 MB depois de
/// decodificado. Definindo `DecodePixelWidth` antes de abrir, o
/// decodificador de JPEG produz a imagem **já reduzida**: nunca chega a
/// alocar o tamanho original. Não é carregar e diminuir, é decodificar
/// menor.
///
/// Como aqui as páginas aparecem grandes — na largura do painel —, cada uma
/// custa mais que uma miniatura pequena. Por isso **só se decodifica o que
/// entra em vista**: rolar a lista carrega o que aparece, e um livreto de
/// quarenta páginas não cobra pelas trinta que ninguém olhou.
/// </summary>
class PainelEncarte : public UserControl
{
public:
    PainelEncarte();
    ~PainelEncarte() override;

    void Carregar(const std::string &arquivoDaFaixa);

    /// <summary>
    /// Abre pelo arquivo, não por Uri. Num Uri, `#` inicia um fragmento e
    /// `%` uma sequência de escape: uma pasta "Vol. #2" seria cortada em
    /// "Vol. " e a página sairia vazia sem aviso. O .NET 4.5 corrigiu
    /// isso, mas o ARCA mira o 4.0 e herda o comportamento antigo.
    /// </summary>
    static std::shared_ptr<BitmapSource> Abrir(const std::string &arquivo, int larguraAlvo);

    /// <summary>
    /// Ao parar, o painel volta ao estado vazio de verdade. Antes só se
    /// soltavam as imagens: as molduras ficavam como tiras finas e
    /// clicáveis, e a informação continuava dizendo "N páginas · X MB".
    /// Tocar de novo chama Carregar, que remonta tudo.
    /// </summary>
    void Soltar();

private:
    static constexpr int AlturaProvisoria = 300;   // reserva antes de conhecer a página

    struct Pagina
    {
        std::string Arquivo;
        std::shared_ptr<Border> Moldura;
        std::shared_ptr<Image> Imagem;
        /// <summary>
        /// Largura do último pedido de decodificação; 0 = nunca pedida.
        /// Guardar a largura, e não só "já pedida", é o que permite
        /// redecodificar quando o painel cresce — senão a página aberta
        /// com o painel escondido ficaria pequena para sempre.
        /// </summary>
        int intLarguraPedida = 0;
    };

    /// <summary>
    /// Só redecodifica se a largura útil passar a pedida em mais de 25%:
    /// arrastar a borda da janela não pode disparar um JPEG por pixel.
    /// </summary>
    static constexpr double dblFolgaRedecodificar = 1.25;

    // os x:Name do PainelEncarte.xaml
    std::shared_ptr<TextBlock> TxtInfo;
    std::shared_ptr<ScrollViewer> AreaLista;
    std::shared_ptr<StackPanel> Paginas;
    std::shared_ptr<TextBlock> TxtVazio;

    std::vector<std::shared_ptr<Pagina>> _paginas;
    // a geração é lida pelos fios de decodificação (no C#, um int lido sem trava)
    std::shared_ptr<std::atomic<int>> _geracao = std::make_shared<std::atomic<int>>(0);
    int64_t _bytes = 0;
    int _carregadas = 0;

    void InitializeComponent();
    std::shared_ptr<Pagina> MontarPagina(const std::string &arquivo, int indice);
    void CarregarVisiveis();
    void Decodificar(std::shared_ptr<Pagina> p, int larguraAlvo, int geracao);
    void AtualizarInfo();
    void AbrirTelaCheia(int indice);
};

} // namespace ARCA::Vistas

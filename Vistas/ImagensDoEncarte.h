// ARCA no xOS - Vistas/ImagensDoEncarte (de Vistas\ImagensDoEncarte.cs).
#pragma once

#include "Janela.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ARCA::Vistas {

class TPaginaPendente;

/// <summary>
/// O caminho comum de toda imagem de capa e encarte: obter (scanner ou
/// arquivo), cortar, codificar. Usado pelo editor da AQUISIÇÃO e pela
/// janela de encarte da BIBLIOTECA — o mesmo tratamento nos dois lugares.
/// </summary>
// Os byte[] do original sao std::vector<uint8_t>, com o vetor vazio no papel do null; o BitmapSource
// e um std::shared_ptr<BitmapSource> (nullptr = null). Os "out string strErro" sao referencias que
// voltam "" quando o original deixava null.
class ImagensDoEncarte
{
public:
    using RoutedEventHandler = std::function<void(RoutedEventArgs &)>;

    /// <summary>Lado maior da capa embutida nas faixas. Scan de encarte tem milhares de px.</summary>
    static constexpr int LadoMaximoCapaEmbutida = 1000;

    static const std::string FiltroImagens;

    /// <summary>
    /// Abre o scanner e devolve a imagem inteira, ou null. Em falha,
    /// strErro diz o motivo; em cancelamento, fica null. O temporário do
    /// scanner é apagado assim que lido.
    /// </summary>
    static std::shared_ptr<BitmapSource> DoScanner(std::string &strErro);

    /// <summary>
    /// A resolução da imagem em pontos por polegada, ou 0 se o arquivo não
    /// diz. O WPF devolve 96 quando falta a informação, e nenhum scanner
    /// trabalha a 96 — então 96 conta como "não informada".
    /// </summary>
    static double DpiDe(const std::shared_ptr<BitmapSource> &objImagem);

    static std::shared_ptr<BitmapSource> DeArquivo(Window *objDono, const std::string &strTitulo, std::string &strErro);

    /// <summary>Lê a imagem inteira para a memória e solta o arquivo. Abre por FileStream: "#" e "%" no caminho não quebram.</summary>
    static std::shared_ptr<BitmapSource> Abrir(const std::string &strArquivo);

    /// <summary>A imagem inteira, de bytes já na memória (JPEG pleno de uma página pendente).</summary>
    static std::shared_ptr<BitmapSource> Abrir(const std::vector<uint8_t> &arrBytes);

    /// <summary>
    /// Abre a janela da linha vertical e põe as duas metades no lugar da
    /// página intIndice — a esquerda fica com a posição dela, a direita
    /// vem logo depois. false se cancelado ou se falhou (strErro diz o
    /// motivo).
    /// </summary>
    static bool DividirPagina(Window *objDono, std::vector<TPaginaPendente> &lstPaginas, int intIndice,
                              const std::string &strTitulo, std::string &strErro);

    /// <summary>
    /// Abre a janela da linha vertical e devolve as duas metades já
    /// codificadas no formato de strExtensao. false se cancelado ou se
    /// falhou (strErro diz o motivo).
    /// </summary>
    static bool PedirDivisao(Window *objDono, const std::shared_ptr<BitmapSource> &objImagem, const std::string &strTitulo,
                             const std::string &strExtensao, std::vector<uint8_t> &arrEsquerda,
                             std::vector<uint8_t> &arrDireita, std::string &strErro);

    /// <summary>A imagem girada em graus no sentido horário (0 devolve a mesma).</summary>
    static std::shared_ptr<BitmapSource> Girar(const std::shared_ptr<BitmapSource> &objImagem, int intGiro);

    /// <summary>
    /// Codifica no formato da extensão, para uma imagem do acervo
    /// regravada continuar sendo o que era: PNG, BMP e TIFF ficam sem
    /// perda; JPEG (e o que não se reconhece) sai como a plena, qualidade 95.
    /// </summary>
    static std::vector<uint8_t> CodificarComo(const std::shared_ptr<BitmapSource> &objImagem, const std::string &strExtensao);

    /// <summary>
    /// Os botões de uma página já digitalizada, em duas linhas: girar para
    /// a esquerda e para a direita; dividir em duas e tirar.
    /// </summary>
    /// <param name="strTirar">Rótulo do último botão: "Repor" quando a imagem já está marcada para sair.</param>
    static UIElementPtr AcoesDaPagina(RoutedEventHandler objGirarEsquerda, RoutedEventHandler objGirarDireita,
                                      RoutedEventHandler objDividir, RoutedEventHandler objTirar,
                                      const std::string &strTirar = "Tirar");

    /// <summary>Passa a imagem pela janela de corte. null se cancelado.</summary>
    static std::shared_ptr<BitmapSource> Cortar(Window *objDono, const std::shared_ptr<BitmapSource> &objImagem,
                                                const std::string &strTitulo, bool blnQuadrado);

    /// <summary>
    /// JPEG sem redução, qualidade 95: o original de preservação, que vai
    /// para Artwork\. Um scan de 300 dpi de página de 12 cm dá ~1400 px e
    /// cerca de 1 MB.
    /// </summary>
    static std::vector<uint8_t> CodificarPlena(const std::shared_ptr<BitmapSource> &objImagem);

    /// <summary>
    /// JPEG reduzido ao lado máximo, para embutir nas faixas. O WM/Picture
    /// declara JPEG, e a capa vai repetida em cada faixa: um scan de 4 MB
    /// em 20 arquivos seriam 80 MB a mais no acervo.
    /// </summary>
    static std::vector<uint8_t> CodificarReduzida(const std::shared_ptr<BitmapSource> &objImagem);

    /// <summary>Miniatura decodificada já pequena (não aloca o tamanho original). null se não for imagem.</summary>
    static std::shared_ptr<BitmapSource> Miniatura(const std::vector<uint8_t> &arrBytes, int intLargura);

    static std::shared_ptr<BitmapSource> Miniatura(const std::string &strArquivo, int intLargura);

private:
    static std::shared_ptr<BitmapSource> ComDpi(const std::shared_ptr<BitmapSource> &objImagem, double dblDpi);
    static void BotaoDaPagina(const std::shared_ptr<Grid> &objGrade, int intLinha, int intColuna, const std::string &strTexto,
                              const std::string &strDica, RoutedEventHandler objAcao, bool blnSimbolo);
    static std::vector<uint8_t> Codificar(const std::shared_ptr<BitmapSource> &objImagem, int intQualidade);
};

/// <summary>
/// Página do encarte já digitalizada, ainda na memória, antes de ir para
/// o álbum.
///
/// O giro fica pendente em vez de recodificar o JPEG a cada clique: girar
/// quatro vezes não pode custar quatro gerações de compressão. Ele só é
/// aplicado uma vez, em <see cref="JpegFinal"/>, e uma página que voltou
/// à posição original sai byte a byte como veio do corte.
/// </summary>
// Guardada por valor nas listas (std::vector<TPaginaPendente>): ninguem a guarda fora da lista.
class TPaginaPendente
{
public:
    /// <summary>JPEG pleno, como saiu do corte (ou da divisão).</summary>
    // readonly no original: ninguem fora da classe o troca
    std::vector<uint8_t> arrJpeg;

    /// <summary>Giro pendente, em graus no sentido horário: 0, 90, 180 ou 270.</summary>
    int intGiro() const { return _intGiro; }

    explicit TPaginaPendente(std::vector<uint8_t> arrJpeg);

    /// <summary>Gira 90° para a direita (intGraus positivo) ou para a esquerda (negativo).</summary>
    void Girar(int intGraus);

    /// <summary>A página inteira, com o giro aplicado. null se não abrir.</summary>
    std::shared_ptr<BitmapSource> Imagem() const;

    /// <summary>Miniatura decodificada já pequena, com o giro aplicado.</summary>
    std::shared_ptr<BitmapSource> Miniatura(int intLargura) const;

    /// <summary>O JPEG que vai para o álbum: o original se não houver giro, recodificado uma vez se houver.</summary>
    std::vector<uint8_t> JpegFinal() const;

private:
    int _intGiro = 0;
    std::shared_ptr<BitmapSource> Aplicar(const std::shared_ptr<BitmapSource> &objImagem) const;
};

} // namespace ARCA::Vistas

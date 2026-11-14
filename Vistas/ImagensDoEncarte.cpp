// ARCA no xOS - Vistas/ImagensDoEncarte (de Vistas\ImagensDoEncarte.cs). Ver ImagensDoEncarte.h.
#include "ImagensDoEncarte.h"

#include "Dialogos.h"
#include "JanelaCorteImagem.h"
#include "JanelaDivisaoImagem.h"

#include "../Base/Excecoes.h"
#include "../Base/Texto.h"
#include "../Interop/ScannerSane.h"

#include <cmath>
#include <filesystem>

namespace ARCA::Vistas {

const std::string ImagensDoEncarte::FiltroImagens =
    "Imagens (*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff)|*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff";

std::shared_ptr<BitmapSource> ImagensDoEncarte::DoScanner(std::string &strErro)
{
    strErro = "";
    std::string strArquivo;
    double dblDpi = 0;
    // LINUX: ScannerWia vira ScannerSane (mesma assinatura, mesmas mensagens; o dialogo e a
    // JanelaScanner, registrada em ScannerSane::DialogoComum)
    try { strArquivo = Interop::ScannerSane::Digitalizar(dblDpi); }
    catch (const Base::InvalidOperationException &ex) { strErro = ex.what(); return nullptr; }
    if (strArquivo.empty()) return nullptr;   // cancelado no diálogo do scanner

    std::shared_ptr<BitmapSource> objImagem = Abrir(strArquivo);
    std::error_code ec;
    std::filesystem::remove(strArquivo, ec);
    if (!objImagem) { strErro = "O scanner entregou uma imagem que não consegui abrir."; return nullptr; }
    return ComDpi(objImagem, dblDpi);
}

double ImagensDoEncarte::DpiDe(const std::shared_ptr<BitmapSource> &objImagem)
{
    if (!objImagem) return 0;
    double dblDpi = objImagem->DpiX;
    return dblDpi <= 0 || std::fabs(dblDpi - 96) < 0.5 ? 0 : dblDpi;
}

/// <summary>
/// Carimba na imagem a resolução que o scanner informou, quando o
/// arquivo veio sem ela ou com outra. Os pixels não mudam; o corte e a
/// gravação passam a saber o tamanho físico.
/// </summary>
std::shared_ptr<BitmapSource> ImagensDoEncarte::ComDpi(const std::shared_ptr<BitmapSource> &objImagem, double dblDpi)
{
    if (dblDpi < 50 || dblDpi > 4800 || std::fabs(objImagem->DpiX - dblDpi) < 0.5) return objImagem;
    try
    {
        // BitmapSource.Create com os mesmos pixels e o dpi novo
        std::shared_ptr<BitmapSource> objNova = BitmapSource::CreateRgba(objImagem->PixelWidth, objImagem->PixelHeight,
                                                                         objImagem->Pixels);
        objNova->DpiX = dblDpi;
        objNova->DpiY = dblDpi;
        return objNova;
    }
    catch (...) { return objImagem; }
}

std::shared_ptr<BitmapSource> ImagensDoEncarte::DeArquivo(Window *objDono, const std::string &strTitulo, std::string &strErro)
{
    strErro = "";
    OpenFileDialog objDialogo;
    objDialogo.Title = strTitulo;
    objDialogo.Filter = FiltroImagens;
    if (objDialogo.ShowDialog(objDono) != true) return nullptr;
    std::shared_ptr<BitmapSource> objImagem = Abrir(objDialogo.FileName);
    if (!objImagem) strErro = "Não consegui abrir essa imagem.";
    return objImagem;
}

std::shared_ptr<BitmapSource> ImagensDoEncarte::Abrir(const std::string &strArquivo)
{
    // LINUX: o BitmapImage com CacheOption = OnLoad vira a decodificacao inteira da camada (JPEG pela
    // libjpeg; PNG e BMP pelo stb_image). TIFF a camada ainda nao le: devolve null, como um arquivo
    // que nao abre.
    try { return BitmapSource::DeArquivo(strArquivo); }
    catch (...) { return nullptr; }
}

std::shared_ptr<BitmapSource> ImagensDoEncarte::Abrir(const std::vector<uint8_t> &arrBytes)
{
    if (arrBytes.empty()) return nullptr;
    try { return BitmapSource::DeBytes(arrBytes); }
    catch (...) { return nullptr; }
}

bool ImagensDoEncarte::DividirPagina(Window *objDono, std::vector<TPaginaPendente> &lstPaginas, int intIndice,
                                     const std::string &strTitulo, std::string &strErro)
{
    std::vector<uint8_t> arrEsquerda, arrDireita;
    if (!PedirDivisao(objDono, lstPaginas[(size_t) intIndice].Imagem(), strTitulo, ".jpg", arrEsquerda, arrDireita, strErro))
        return false;
    lstPaginas[(size_t) intIndice] = TPaginaPendente(arrEsquerda);
    lstPaginas.insert(lstPaginas.begin() + intIndice + 1, TPaginaPendente(arrDireita));
    return true;
}

bool ImagensDoEncarte::PedirDivisao(Window *objDono, const std::shared_ptr<BitmapSource> &objImagem, const std::string &strTitulo,
                                    const std::string &strExtensao, std::vector<uint8_t> &arrEsquerda,
                                    std::vector<uint8_t> &arrDireita, std::string &strErro)
{
    arrEsquerda.clear();
    arrDireita.clear();
    strErro = "";
    if (!objImagem) { strErro = "Não consegui abrir a imagem para dividir."; return false; }
    if (objImagem->PixelWidth < 64) { strErro = "A imagem é estreita demais para dividir."; return false; }

    auto objJanela = std::make_shared<JanelaDivisaoImagem>(objImagem, strTitulo);
    objJanela->Owner = objDono;
    objJanela->ShowDialog();
    if (!objJanela->Esquerda || !objJanela->Direita) return false;

    arrEsquerda = CodificarComo(objJanela->Esquerda, strExtensao);
    arrDireita = CodificarComo(objJanela->Direita, strExtensao);
    if (arrEsquerda.empty() || arrDireita.empty()) { strErro = "Não consegui preparar as duas metades."; return false; }
    return true;
}

std::shared_ptr<BitmapSource> ImagensDoEncarte::Girar(const std::shared_ptr<BitmapSource> &objImagem, int intGiro)
{
    if (!objImagem || intGiro == 0) return objImagem;
    // TransformedBitmap com RotateTransform(intGiro)
    return objImagem->Girada(intGiro);
}

std::vector<uint8_t> ImagensDoEncarte::CodificarComo(const std::shared_ptr<BitmapSource> &objImagem, const std::string &strExtensao)
{
    // PngBitmapEncoder, BmpBitmapEncoder e TiffBitmapEncoder { Compression = Lzw } sao os da camada
    std::string strExt = Base::Minusculas(strExtensao);
    int intCodificador;
    if (strExt == ".png") intCodificador = 1;
    else if (strExt == ".bmp") intCodificador = 2;
    else if (strExt == ".tif" || strExt == ".tiff") intCodificador = 3;
    else return CodificarPlena(objImagem);
    try
    {
        switch (intCodificador)
        {
        case 1: return objImagem->CodificaPng();
        case 2: return objImagem->CodificaBmp();
        default: return objImagem->CodificaTiffLzw();
        }
    }
    catch (...) { return {}; }
}

UIElementPtr ImagensDoEncarte::AcoesDaPagina(RoutedEventHandler objGirarEsquerda, RoutedEventHandler objGirarDireita,
                                             RoutedEventHandler objDividir, RoutedEventHandler objTirar,
                                             const std::string &strTirar)
{
    auto objGrade = std::make_shared<Grid>();
    objGrade->Margin = Thickness(0, 3, 0, 0);
    objGrade->ColumnDefinitions.push_back(ColumnDefinition());
    objGrade->ColumnDefinitions.push_back(ColumnDefinition());
    objGrade->RowDefinitions.push_back(RowDefinition(GridLength::Auto()));
    objGrade->RowDefinitions.push_back(RowDefinition(GridLength::Auto()));
    // Os glifos de giro vêm da Segoe UI Symbol, que a Windows 7 já traz.
    // LINUX: nao ha Segoe UI Symbol; a camada desenha ↺ e ↻ com as fontes de simbolos que acompanham o
    // Noto Sans (Noto Sans Symbols / DejaVu Sans)
    BotaoDaPagina(objGrade, 0, 0, "↺", "Girar 90° para a esquerda", objGirarEsquerda, true);
    BotaoDaPagina(objGrade, 0, 1, "↻", "Girar 90° para a direita", objGirarDireita, true);
    BotaoDaPagina(objGrade, 1, 0, "Dividir", "Dividir em duas páginas por uma linha vertical (duas páginas digitalizadas juntas)", objDividir, false);
    BotaoDaPagina(objGrade, 1, 1, strTirar, strTirar == "Tirar" ? "Tirar esta imagem" : "Manter esta imagem no álbum", objTirar, false);
    return objGrade;
}

void ImagensDoEncarte::BotaoDaPagina(const std::shared_ptr<Grid> &objGrade, int intLinha, int intColuna, const std::string &strTexto,
                                     const std::string &strDica, RoutedEventHandler objAcao, bool blnSimbolo)
{
    auto objB = std::make_shared<Button>();
    objB->Content = strTexto;
    objB->ToolTip = strDica;
    objB->FontSize = 10;
    objB->Padding = Thickness(0, 1, 0, 1);
    objB->Margin = Thickness(intColuna == 0 ? 0 : 2, intLinha == 0 ? 0 : 3, intColuna == 0 ? 2 : 0, 0);
    if (blnSimbolo)
    {
        objB->FontFamily = "Segoe UI Symbol";
        objB->FontSize = 12;
        objB->Padding = Thickness(0);
    }
    objB->Click += objAcao;
    Grid::SetRow(objB, intLinha);
    Grid::SetColumn(objB, intColuna);
    objGrade->Children.Add(objB);
}

std::shared_ptr<BitmapSource> ImagensDoEncarte::Cortar(Window *objDono, const std::shared_ptr<BitmapSource> &objImagem,
                                                       const std::string &strTitulo, bool blnQuadrado)
{
    auto objCorte = std::make_shared<JanelaCorteImagem>(objImagem, strTitulo, blnQuadrado);
    objCorte->Owner = objDono;
    objCorte->ShowDialog();
    return objCorte->Resultado;
}

std::vector<uint8_t> ImagensDoEncarte::CodificarPlena(const std::shared_ptr<BitmapSource> &objImagem)
{
    return Codificar(objImagem, 95);
}

std::vector<uint8_t> ImagensDoEncarte::CodificarReduzida(const std::shared_ptr<BitmapSource> &objImagem)
{
    std::shared_ptr<BitmapSource> objFonte = objImagem;
    int intMaior = std::max(objImagem->PixelWidth, objImagem->PixelHeight);
    if (intMaior > LadoMaximoCapaEmbutida)
    {
        double dblEscala = (double) LadoMaximoCapaEmbutida / intMaior;
        // TransformedBitmap com ScaleTransform: os pixels diminuem e o dpi fica o da fonte (a
        // Escalada da camada recalcula o dpi para manter o tamanho fisico; aqui volta o do WPF)
        objFonte = objImagem->Escalada(dblEscala);
        objFonte->DpiX = objImagem->DpiX;
        objFonte->DpiY = objImagem->DpiY;
    }
    return Codificar(objFonte, 90);
}

std::vector<uint8_t> ImagensDoEncarte::Codificar(const std::shared_ptr<BitmapSource> &objImagem, int intQualidade)
{
    // JpegBitmapEncoder { QualityLevel = intQualidade } -> a libjpeg da camada, mesma escala de qualidade
    try { return objImagem->CodificaJpeg(intQualidade); }
    catch (...) { return {}; }
}

std::shared_ptr<BitmapSource> ImagensDoEncarte::Miniatura(const std::vector<uint8_t> &arrBytes, int intLargura)
{
    if (arrBytes.empty()) return nullptr;
    try { return BitmapSource::DeBytes(arrBytes, intLargura); }   // DecodePixelWidth
    catch (...) { return nullptr; }
}

std::shared_ptr<BitmapSource> ImagensDoEncarte::Miniatura(const std::string &strArquivo, int intLargura)
{
    try { return BitmapSource::DeArquivo(strArquivo, intLargura); }
    catch (...) { return nullptr; }
}

// =====================================================================================
// TPaginaPendente
// =====================================================================================
TPaginaPendente::TPaginaPendente(std::vector<uint8_t> arrJpeg) : arrJpeg(std::move(arrJpeg)) {}

void TPaginaPendente::Girar(int intGraus)
{
    _intGiro = ((_intGiro + intGraus) % 360 + 360) % 360;
}

std::shared_ptr<BitmapSource> TPaginaPendente::Imagem() const
{
    return Aplicar(ImagensDoEncarte::Abrir(arrJpeg));
}

std::shared_ptr<BitmapSource> TPaginaPendente::Miniatura(int intLargura) const
{
    return Aplicar(ImagensDoEncarte::Miniatura(arrJpeg, intLargura));
}

std::vector<uint8_t> TPaginaPendente::JpegFinal() const
{
    if (_intGiro == 0) return arrJpeg;
    std::shared_ptr<BitmapSource> objImagem = Imagem();
    return !objImagem ? std::vector<uint8_t>() : ImagensDoEncarte::CodificarPlena(objImagem);
}

std::shared_ptr<BitmapSource> TPaginaPendente::Aplicar(const std::shared_ptr<BitmapSource> &objImagem) const
{
    return ImagensDoEncarte::Girar(objImagem, _intGiro);
}

} // namespace ARCA::Vistas

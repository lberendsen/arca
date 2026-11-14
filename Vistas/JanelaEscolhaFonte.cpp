// ARCA no xOS - Vistas/JanelaEscolhaFonte (de Vistas\JanelaEscolhaFonte.cs).
#include "JanelaEscolhaFonte.h"

#include "Base/Texto.h"
#include "Modelo/Formato.h"

#include <cstdlib>

namespace ARCA::Vistas {

using Modelo::Formato;
using Modelo::TCandidatoIdentificacao;

JanelaEscolhaFonte::JanelaEscolhaFonte(std::shared_ptr<TCandidatoIdentificacao> objItunes,
                                       std::shared_ptr<TCandidatoIdentificacao> objDiscogs,
                                       const std::vector<int> &lstDuracoesDisco, const std::string &strBuscado)
{
    _lstDuracoesDisco = lstDuracoesDisco;

    Title = "Identificar álbum — escolher a fonte";
    Width = 1000;
    Height = 700;
    MinWidth = 760;
    MinHeight = 480;
    WindowStartupLocation = Vistas::WindowStartupLocation::CenterOwner;
    ShowInTaskbar = false;
    Background = Recurso("Canvas");

    auto objRaiz = std::make_shared<DockPanel>();
    objRaiz->Children.Add(MontarCabecalho(strBuscado));
    objRaiz->Children.Add(MontarRodape());

    auto objColunas = std::make_shared<Grid>();
    objColunas->Margin = Thickness(12);
    objColunas->ColumnDefinitions.push_back(ColumnDefinition());
    objColunas->ColumnDefinitions.push_back(ColumnDefinition(GridLength(12)));
    objColunas->ColumnDefinitions.push_back(ColumnDefinition());

    UIElementPtr objApple = MontarColuna("APPLE MUSIC", objItunes);
    UIElementPtr objDiscogsCol = MontarColuna("DISCOGS", objDiscogs);
    Grid::SetColumn(objApple, 0);
    Grid::SetColumn(objDiscogsCol, 2);
    objColunas->Children.Add(objApple);
    objColunas->Children.Add(objDiscogsCol);
    objRaiz->Children.Add(objColunas);

    Content = objRaiz;
}

// ===============================================================
UIElementPtr JanelaEscolhaFonte::MontarCabecalho(const std::string &strBuscado)
{
    int intTotalDisco = 0;
    for (int intD : _lstDuracoesDisco) if (intD > 0) intTotalDisco += intD;

    auto objPilha = std::make_shared<StackPanel>();
    auto objPergunta = std::make_shared<TextBlock>("De onde vêm os metadados deste disco?");
    objPergunta->FontSize = 15;
    objPergunta->Foreground = Recurso("Texto");
    objPilha->Children.Add(objPergunta);
    std::string strDisco = !_lstDuracoesDisco.empty()
        ? "Disco na área de trabalho: " + std::to_string(_lstDuracoesDisco.size()) + " faixas · " + Formato::Tempo(intTotalDisco)
        : "Disco na área de trabalho: durações indisponíveis (sem AudioCD.cue)";
    if (!strBuscado.empty()) strDisco += "   ·   buscado: " + strBuscado;
    auto objDisco = std::make_shared<TextBlock>(strDisco);
    objDisco->Style = Tema::Estilo("Fraco");
    objDisco->Margin = Thickness(0, 3, 0, 0);
    objPilha->Children.Add(objDisco);

    auto objBorda = std::make_shared<Border>();
    objBorda->Background = Recurso("CabecalhoColuna");
    objBorda->BorderBrush = Recurso("Borda");
    objBorda->BorderThickness = Thickness(0, 0, 0, 1);
    objBorda->Padding = Thickness(14, 10, 14, 10);
    objBorda->SetChild(objPilha);
    DockPanel::SetDock(objBorda, Dock::Top);
    return objBorda;
}

UIElementPtr JanelaEscolhaFonte::MontarRodape()
{
    auto objGrade = std::make_shared<Grid>();
    objGrade->ColumnDefinitions.push_back(ColumnDefinition());
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));

    auto objDica = std::make_shared<TextBlock>(
        "Nada muda nos arquivos até a escolha. Se nenhum dos dois for o disco certo, cancele e cole o link exato na busca assistida.");
    objDica->Style = Tema::Estilo("Fraco");
    objDica->VerticalAlignment = Vistas::VerticalAlignment::Center;
    objDica->TextWrapping = Vistas::TextWrapping::Wrap;
    auto objCancelar = std::make_shared<Button>();
    objCancelar->Content = "Cancelar";
    objCancelar->IsCancel = true;
    objCancelar->Padding = Thickness(18, 6, 18, 6);
    objCancelar->Margin = Thickness(12, 0, 0, 0);
    objCancelar->Click += [this](RoutedEventArgs &) { _escolhido = nullptr; Close(); };
    Grid::SetColumn(objCancelar, 1);
    objGrade->Children.Add(objDica);
    objGrade->Children.Add(objCancelar);

    auto objBorda = std::make_shared<Border>();
    objBorda->Background = Recurso("BarraFerramentas");
    objBorda->BorderBrush = Recurso("Borda");
    objBorda->BorderThickness = Thickness(0, 1, 0, 0);
    objBorda->Padding = Thickness(14, 8, 14, 8);
    objBorda->SetChild(objGrade);
    DockPanel::SetDock(objBorda, Dock::Bottom);
    return objBorda;
}

// ===============================================================
UIElementPtr JanelaEscolhaFonte::MontarColuna(const std::string &strRotulo, const std::shared_ptr<TCandidatoIdentificacao> &objC)
{
    auto objCol = std::make_shared<DockPanel>();

    auto objRotulo = std::make_shared<TextBlock>(strRotulo);
    objRotulo->Style = Tema::Estilo("Rotulo");
    objRotulo->Margin = Thickness(0, 0, 0, 8);
    DockPanel::SetDock(objRotulo, Dock::Top);
    objCol->Children.Add(objRotulo);

    if (!objC)
    {
        auto objNada = std::make_shared<TextBlock>("Nada encontrado.");
        objNada->Style = Tema::Estilo("Fraco");
        objNada->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
        objNada->VerticalAlignment = Vistas::VerticalAlignment::Center;
        objCol->Children.Add(objNada);
        return Moldura(objCol);
    }

    // Botão de escolha embaixo, sempre visível.
    auto objUsar = std::make_shared<Button>();
    objUsar->Content = "Usar os dados do " + objC->strFonte;
    objUsar->Padding = Thickness(0, 8, 0, 8);
    objUsar->Margin = Thickness(0, 10, 0, 0);
    objUsar->HorizontalAlignment = Vistas::HorizontalAlignment::Stretch;
    objUsar->Click += [this, objC](RoutedEventArgs &) { _escolhido = objC; Close(); };
    DockPanel::SetDock(objUsar, Dock::Bottom);
    objCol->Children.Add(objUsar);

    UIElementPtr objIdentidade = MontarIdentidade(*objC);
    DockPanel::SetDock(objIdentidade, Dock::Top);
    objCol->Children.Add(objIdentidade);

    auto objRolagem = std::make_shared<ScrollViewer>();
    objRolagem->VerticalScrollBarVisibility = ScrollBarVisibility::Auto;
    objRolagem->HorizontalScrollBarVisibility = ScrollBarVisibility::Disabled;
    objRolagem->Margin = Thickness(0, 10, 0, 0);
    objRolagem->Content = MontarFaixas(*objC);
    objCol->Children.Add(objRolagem);

    return Moldura(objCol);
}

std::shared_ptr<Border> JanelaEscolhaFonte::Moldura(UIElementPtr objConteudo)
{
    auto objBorda = std::make_shared<Border>();
    objBorda->Background = Recurso("FundoLista");
    objBorda->BorderBrush = Recurso("Borda");
    objBorda->BorderThickness = Thickness(1);
    objBorda->Padding = Thickness(12);
    objBorda->SetChild(std::move(objConteudo));
    return objBorda;
}

/// Capa à esquerda; artista, álbum, ano, detalhe e a conferência da contagem à direita.
UIElementPtr JanelaEscolhaFonte::MontarIdentidade(const TCandidatoIdentificacao &objC)
{
    auto objGrade = std::make_shared<Grid>();
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength(170)));
    objGrade->ColumnDefinitions.push_back(ColumnDefinition());

    auto objCapa = std::make_shared<Border>();
    objCapa->Width = 160;
    objCapa->Height = 160;
    objCapa->Background = Recurso("PainelLateral");
    objCapa->BorderBrush = Recurso("Borda");
    objCapa->BorderThickness = Thickness(1);
    objCapa->HorizontalAlignment = Vistas::HorizontalAlignment::Left;
    objCapa->VerticalAlignment = Vistas::VerticalAlignment::Top;
    ImageSource objImagem = AbrirCapa(objC.arrCapa);
    if (objImagem)
    {
        auto objImg = std::make_shared<Image>();
        objImg->Source = objImagem;
        objImg->Stretch = Vistas::Stretch::UniformToFill;
        objCapa->SetChild(objImg);
        // A miniatura é decodificada reduzida; o tamanho que interessa
        // (é o que vai para o arquivo) é o original, lido do cabeçalho.
        objCapa->ToolTip = TamanhoOriginal(objC.arrCapa) + " · " + std::to_string(objC.arrCapa.size() / 1024) + " KB";
    }
    else
    {
        auto objSem = std::make_shared<TextBlock>("sem capa");
        objSem->Style = Tema::Estilo("Fraco");
        objSem->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
        objSem->VerticalAlignment = Vistas::VerticalAlignment::Center;
        objCapa->SetChild(objSem);
    }
    objGrade->Children.Add(objCapa);

    auto objTexto = std::make_shared<StackPanel>();
    objTexto->Children.Add(Linha(objC.strArtista, 13, "Texto", FontWeights::SemiBold));
    objTexto->Children.Add(Linha(objC.strAlbum, 15, "Texto", FontWeights::Normal));

    std::vector<std::string> lstAnoGenero;
    if (!objC.strAno.empty()) lstAnoGenero.push_back(objC.strAno);
    if (!objC.strGenero.empty()) lstAnoGenero.push_back(objC.strGenero);
    if (!lstAnoGenero.empty())
        objTexto->Children.Add(Linha(Base::Junta(lstAnoGenero, " · "), 12, "TextoFraco", FontWeights::Normal));
    if (!objC.strDetalhe.empty())
    {
        // Quebra em vez de cortar: gravadora/catálogo é o que separa prensagens.
        auto objDetalhe = Linha(objC.strDetalhe, 12, "TextoFraco", FontWeights::Normal);
        objDetalhe->TextWrapping = Vistas::TextWrapping::Wrap;
        objTexto->Children.Add(objDetalhe);
    }

    // A conferência que mais pesa: a contagem de faixas.
    int intQtd = (int) objC.lstTitulos.size();
    int intQtdDisco = (int) _lstDuracoesDisco.size();
    bool blnConfere = intQtdDisco == 0 || intQtd == intQtdDisco;
    std::string strContagem = intQtdDisco == 0
        ? std::to_string(intQtd) + " faixas"
        : blnConfere
            ? std::to_string(intQtd) + " faixas — confere com o disco"
            : std::to_string(intQtd) + " faixas — o disco tem " + std::to_string(intQtdDisco);
    auto objContagem = Linha(strContagem, 12, blnConfere ? "Texto" : "Alarme", FontWeights::SemiBold);
    objContagem->Margin = Thickness(0, 8, 0, 0);
    objTexto->Children.Add(objContagem);

    int intTotalFonte = 0, intTotalDisco = 0;
    bool blnTotalCompleto = intQtd > 0;
    for (int intD : objC.lstDuracoes) { if (intD > 0) intTotalFonte += intD; else blnTotalCompleto = false; }
    for (int intD : _lstDuracoesDisco) if (intD > 0) intTotalDisco += intD;
    if (blnTotalCompleto && intTotalDisco > 0)
    {
        // A soma tolera a mesma folga por faixa que cada linha tolera.
        bool blnTotalConfere = std::abs(intTotalDisco - intTotalFonte)
                               <= intToleranciaSegundos * std::max(1, intQtd);
        objTexto->Children.Add(Linha("Total " + Formato::Tempo(intTotalFonte) + " (disco " + Formato::Tempo(intTotalDisco) + ")", 12,
                                     blnTotalConfere ? "TextoFraco" : "Alarme",
                                     blnTotalConfere ? FontWeights::Normal : FontWeights::SemiBold));
    }

    Grid::SetColumn(objTexto, 1);
    objGrade->Children.Add(objTexto);
    return objGrade;
}

/// Uma linha por faixa: número, título (com intérprete e compositor
/// embaixo, quando houver), duração da fonte e diferença para o disco.
UIElementPtr JanelaEscolhaFonte::MontarFaixas(const TCandidatoIdentificacao &objC)
{
    // Margem à direita: a barra de rolagem não pode cobrir a coluna da diferença.
    auto objGrade = std::make_shared<Grid>();
    objGrade->Margin = Thickness(0, 0, 10, 0);
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength(26)));
    objGrade->ColumnDefinitions.push_back(ColumnDefinition());
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength(48)));
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength(56)));

    AdicionarCelula(*objGrade, 0, 0, Linha("#", 11, "TextoFraco", FontWeights::SemiBold), false);
    AdicionarCelula(*objGrade, 0, 1, Linha("Faixa", 11, "TextoFraco", FontWeights::SemiBold), false);
    AdicionarCelula(*objGrade, 0, 2, Linha("Duração", 11, "TextoFraco", FontWeights::SemiBold), true);
    auto objCabDif = Linha("vs. disco", 11, "TextoFraco", FontWeights::SemiBold);
    objCabDif->ToolTip = "Diferença para a duração da faixa lida do próprio CD.";
    AdicionarCelula(*objGrade, 0, 3, objCabDif, true);

    for (size_t i = 0; i < objC.lstTitulos.size(); i++)
    {
        int intLinha = (int) i + 1;

        AdicionarCelula(*objGrade, intLinha, 0, Linha(std::to_string(i + 1), 12, "TextoFraco", FontWeights::Normal), false);

        auto objTitulo = std::make_shared<StackPanel>();
        auto objNome = Linha(objC.lstTitulos[i], 12, "Texto", FontWeights::Normal);
        objNome->TextWrapping = Vistas::TextWrapping::Wrap;
        objTitulo->Children.Add(objNome);

        std::string strArtistaFaixa = i < objC.lstArtistasFaixas.size() ? objC.lstArtistasFaixas[i] : "";
        if (!strArtistaFaixa.empty()
            && !Base::IgualSemCaixa(strArtistaFaixa, objC.strArtista))
        {
            auto objArt = Linha(strArtistaFaixa, 11, "TextoFraco", FontWeights::Normal);
            objArt->TextWrapping = Vistas::TextWrapping::Wrap;
            objTitulo->Children.Add(objArt);
        }
        std::string strCompositor = objC.lstCompositores && i < objC.lstCompositores->size() ? (*objC.lstCompositores)[i] : "";
        if (!strCompositor.empty())
            objTitulo->Children.Add(Linha("comp. " + strCompositor, 11, "TextoFraco", FontWeights::Normal));
        AdicionarCelula(*objGrade, intLinha, 1, objTitulo, false);

        int intDur = i < objC.lstDuracoes.size() ? objC.lstDuracoes[i] : -1;
        AdicionarCelula(*objGrade, intLinha, 2,
                        Linha(intDur > 0 ? Formato::Tempo(intDur) : "?", 12, "TextoFraco", FontWeights::Normal), true);

        int intDisco = i < _lstDuracoesDisco.size() ? _lstDuracoesDisco[i] : -1;
        std::shared_ptr<TextBlock> objDif;
        if (intDisco <= 0)
        {
            objDif = Linha(!_lstDuracoesDisco.empty() ? "sem faixa" : "", 11, "Alarme", FontWeights::Normal);
        }
        else if (intDur <= 0)
        {
            objDif = Linha("?", 12, "TextoFraco", FontWeights::Normal);
        }
        else
        {
            int intDif = intDisco - intDur;
            bool blnConfere = std::abs(intDif) <= intToleranciaSegundos;
            objDif = Linha((intDif >= 0 ? "+" : "−") + std::to_string(std::abs(intDif)) + " s", 12,
                           blnConfere ? "TextoFraco" : "Alarme",
                           blnConfere ? FontWeights::Normal : FontWeights::SemiBold);
        }
        if (intDisco > 0) objDif->ToolTip = "Faixa no disco: " + Formato::Tempo(intDisco);
        AdicionarCelula(*objGrade, intLinha, 3, objDif, true);
    }
    return objGrade;
}

// ===============================================================
void JanelaEscolhaFonte::AdicionarCelula(Grid &objGrade, int intLinha, int intColuna, const UIElementPtr &objEl, bool blnDireita)
{
    while ((int) objGrade.RowDefinitions.size() <= intLinha)
        objGrade.RowDefinitions.push_back(RowDefinition(GridLength::Auto()));
    objEl->Margin = Thickness(0, 0, intColuna == 3 ? 0 : 6, 6);
    objEl->VerticalAlignment = Vistas::VerticalAlignment::Top;
    if (blnDireita) objEl->HorizontalAlignment = Vistas::HorizontalAlignment::Right;
    Grid::SetRow(objEl, intLinha);
    Grid::SetColumn(objEl, intColuna);
    objGrade.Children.Add(objEl);
}

std::shared_ptr<TextBlock> JanelaEscolhaFonte::Linha(const std::string &strTexto, double dblTamanho, const std::string &strCor,
                                                     Vistas::FontWeight objPeso)
{
    auto objLinha = std::make_shared<TextBlock>(strTexto);
    objLinha->FontSize = dblTamanho;
    objLinha->FontWeight = objPeso;
    objLinha->Foreground = Recurso(strCor);
    objLinha->TextTrimming = Vistas::TextTrimming::CharacterEllipsis;
    return objLinha;
}

Brush JanelaEscolhaFonte::Recurso(const std::string &strChave)
{
    return Tema::Recurso(strChave);
}

/// Largura x altura da imagem original, lidas sem decodificar os pixels.
// LINUX: o original pedia ao BitmapDecoder do WPF (DelayCreation); aqui o cabeçalho é lido à mão,
// nos formatos de capa que o WPF lia: JPEG (o marcador SOF), PNG (IHDR), GIF e BMP.
std::string JanelaEscolhaFonte::TamanhoOriginal(const std::vector<uint8_t> &arrBytes)
{
    const std::vector<uint8_t> &b = arrBytes;
    size_t n = b.size();
    auto be16 = [&](size_t p) { return (int) ((b[p] << 8) | b[p + 1]); };
    auto be32 = [&](size_t p) { return (int) (((uint32_t) b[p] << 24) | ((uint32_t) b[p + 1] << 16) | ((uint32_t) b[p + 2] << 8) | b[p + 3]); };
    auto le16 = [&](size_t p) { return (int) (b[p] | (b[p + 1] << 8)); };
    auto le32 = [&](size_t p) { return (int32_t) ((uint32_t) b[p] | ((uint32_t) b[p + 1] << 8) | ((uint32_t) b[p + 2] << 16) | ((uint32_t) b[p + 3] << 24)); };
    auto px = [](int w, int h) { return std::to_string(w) + " x " + std::to_string(h) + " px"; };
    if (n >= 4 && b[0] == 0xFF && b[1] == 0xD8)
    {
        size_t p = 2;
        while (p + 9 < n)
        {
            if (b[p] != 0xFF) { p++; continue; }
            uint8_t m = b[p + 1];
            if (m == 0xFF) { p++; continue; }
            if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { p += 2; continue; }
            int len = be16(p + 2);
            if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)
                return px(be16(p + 7), be16(p + 5));
            if (len < 2) break;
            p += 2 + (size_t) len;
        }
    }
    else if (n >= 24 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G')
        return px(be32(16), be32(20));
    else if (n >= 10 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F')
        return px(le16(6), le16(8));
    else if (n >= 26 && b[0] == 'B' && b[1] == 'M')
        return px(le32(18), std::abs(le32(22)));
    return "tamanho desconhecido";
}

/// Decodifica a capa já reduzida (a moldura tem 160 px; 320 cobre telas
/// de alta densidade). null se não houver capa ou se os bytes não forem
/// imagem — página de erro baixada no lugar da capa, por exemplo.
ImageSource JanelaEscolhaFonte::AbrirCapa(const std::vector<uint8_t> &arrBytes)
{
    if (arrBytes.empty()) return nullptr;
    try
    {
        return BitmapSource::DeBytes(arrBytes, 320);
    }
    catch (...) { return nullptr; }
}

} // namespace ARCA::Vistas

// ARCA no xOS - Vistas/JanelaConteudoFaixas (de Vistas\JanelaConteudoFaixas.cs).
#include "JanelaConteudoFaixas.h"

#include "Base/Arquivo.h"
#include "Base/Texto.h"
#include "Interop/EditorFlac.h"

namespace ARCA::Vistas {

using Modelo::ConteudoFaixa;
using Modelo::TClasseMidia;

const std::array<const char *, 3> JanelaConteudoFaixas::arrRotulos = {"Música", "Audiolivro", "Fala"};

JanelaConteudoFaixas::JanelaConteudoFaixas(const std::vector<std::string> &lstArquivos, const std::string &strSugestao)
{
    Title = "Conteúdo das faixas";
    Width = 760;
    Height = 600;
    MinWidth = 560;
    MinHeight = 360;
    WindowStartupLocation = Vistas::WindowStartupLocation::CenterOwner;
    ShowInTaskbar = false;
    Background = Recurso("Canvas");

    _txtAviso = std::make_shared<TextBlock>();
    _txtAviso->Foreground = Recurso("Alarme");
    _txtAviso->FontSize = 12;
    _txtAviso->VerticalAlignment = Vistas::VerticalAlignment::Center;
    _txtAviso->TextWrapping = Vistas::TextWrapping::Wrap;

    auto objRaiz = std::make_shared<DockPanel>();
    objRaiz->Children.Add(MontarCabecalho(strSugestao));
    objRaiz->Children.Add(MontarRodape());

    auto objCorpo = std::make_shared<DockPanel>();
    objCorpo->Margin = Thickness(14);
    UIElementPtr objTodas = MontarTodas();
    DockPanel::SetDock(objTodas, Dock::Top);
    objCorpo->Children.Add(objTodas);
    auto objMoldura = std::make_shared<Border>();
    objMoldura->Background = Recurso("FundoLista");
    objMoldura->BorderBrush = Recurso("Borda");
    objMoldura->BorderThickness = Thickness(1);
    auto objRolagem = std::make_shared<ScrollViewer>();
    objRolagem->VerticalScrollBarVisibility = ScrollBarVisibility::Auto;
    objRolagem->Content = MontarFaixas(lstArquivos);
    objMoldura->SetChild(objRolagem);
    objCorpo->Children.Add(objMoldura);
    objRaiz->Children.Add(objCorpo);
    Content = objRaiz;
}

// ===============================================================
UIElementPtr JanelaConteudoFaixas::MontarCabecalho(const std::string &strSugestao)
{
    auto objPilha = std::make_shared<StackPanel>();
    auto objPergunta = std::make_shared<TextBlock>("O que é cada faixa deste disco?");
    objPergunta->FontSize = 15;
    objPergunta->Foreground = Recurso("Texto");
    objPilha->Children.Add(objPergunta);
    // LINUX: "continua WMA Lossless" no original; o acervo no xOS é FLAC.
    auto objExplica = std::make_shared<TextBlock>(
        "Audiolivro e fala (entrevista, humor) saem de Música e aparecem em Audiolivro, "
        "com o mesmo álbum. O áudio não muda: continua FLAC, bit a bit.");
    objExplica->Style = Tema::Estilo("Fraco");
    objExplica->TextWrapping = Vistas::TextWrapping::Wrap;
    objExplica->Margin = Thickness(0, 3, 0, 0);
    objPilha->Children.Add(objExplica);
    if (!strSugestao.empty())
    {
        auto objSugestao = std::make_shared<TextBlock>(strSugestao);
        objSugestao->Foreground = Recurso("Acento");
        objSugestao->TextWrapping = Vistas::TextWrapping::Wrap;
        objSugestao->Margin = Thickness(0, 6, 0, 0);
        objPilha->Children.Add(objSugestao);
    }
    auto objBorda = std::make_shared<Border>();
    objBorda->Background = Recurso("CabecalhoColuna");
    objBorda->BorderBrush = Recurso("Borda");
    objBorda->BorderThickness = Thickness(0, 0, 0, 1);
    objBorda->Padding = Thickness(14, 10, 14, 10);
    objBorda->SetChild(objPilha);
    DockPanel::SetDock(objBorda, Dock::Top);
    return objBorda;
}

/// Um clique para o disco inteiro — o caso comum de um CD só de fala.
UIElementPtr JanelaConteudoFaixas::MontarTodas()
{
    auto objLinha = std::make_shared<StackPanel>();
    objLinha->Orientation = Vistas::Orientation::Horizontal;
    objLinha->Margin = Thickness(0, 0, 0, 10);
    auto objRotulo = std::make_shared<TextBlock>("Todas as faixas:");
    objRotulo->Style = Tema::Estilo("Rotulo");
    objRotulo->VerticalAlignment = Vistas::VerticalAlignment::Center;
    objRotulo->Margin = Thickness(0, 0, 10, 0);
    objLinha->Children.Add(objRotulo);
    for (size_t i = 0; i < arrRotulos.size(); i++)
    {
        size_t intOpcao = i;
        auto objBotao = std::make_shared<Button>();
        objBotao->Content = arrRotulos[i];
        objBotao->Padding = Thickness(14, 4, 14, 4);
        objBotao->Margin = Thickness(0, 0, 6, 0);
        objBotao->Click += [this, intOpcao](RoutedEventArgs &) {
            for (LinhaConteudo &objL : _lstLinhas) objL.arrOpcoes[intOpcao]->IsChecked = true;
        };
        objLinha->Children.Add(objBotao);
    }
    return objLinha;
}

/// Uma linha por faixa: arquivo, título gravado e as três opções, já na classe que o arquivo tem.
UIElementPtr JanelaConteudoFaixas::MontarFaixas(const std::vector<std::string> &lstArquivos)
{
    auto objGrade = std::make_shared<Grid>();
    objGrade->Margin = Thickness(8, 4, 8, 4);
    objGrade->ColumnDefinitions.push_back(ColumnDefinition());
    for (size_t i = 0; i < arrRotulos.size(); i++)
        objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));

    for (size_t intLinha = 0; intLinha < lstArquivos.size(); intLinha++)
    {
        const std::string &strCaminho = lstArquivos[intLinha];
        std::string strTitulo;                         // "" = null
        ConteudoFaixa eAtual = ConteudoFaixa::Musica;
        {
            Interop::EditorFlac objEditor;
            if (objEditor.Abrir(strCaminho))
            {
                std::vector<Interop::AtributoFlac> lstAtributos = objEditor.LerTudo();
                eAtual = TClasseMidia::Ler(lstAtributos);
                for (const Interop::AtributoFlac &a : lstAtributos)
                    if (Base::IgualSemCaixa(a.Nome, "Title"))
                    {
                        if (!a.Valor.empty()) strTitulo = a.Valor;
                        break;
                    }
            }
        }

        objGrade->RowDefinitions.push_back(RowDefinition(GridLength::Auto()));
        if (intLinha % 2 == 1)
        {
            auto objFundo = std::make_shared<Border>();
            objFundo->Background = Recurso("LinhaAlternada");
            Grid::SetRow(objFundo, (int) intLinha);
            Grid::SetColumnSpan(objFundo, 1 + (int) arrRotulos.size());
            objGrade->Children.Add(objFundo);
        }

        auto objNome = std::make_shared<StackPanel>();
        objNome->Margin = Thickness(4, 5, 12, 5);
        auto objTitulo = std::make_shared<TextBlock>(!strTitulo.empty() ? strTitulo : Base::NomeSemExtensao(strCaminho));
        objTitulo->Foreground = Recurso("Texto");
        objTitulo->TextTrimming = Vistas::TextTrimming::CharacterEllipsis;
        objNome->Children.Add(objTitulo);
        auto objArquivo = std::make_shared<TextBlock>(Base::NomeDoArquivo(strCaminho));
        objArquivo->Style = Tema::Estilo("Fraco");
        objArquivo->FontSize = 10;
        objArquivo->TextTrimming = Vistas::TextTrimming::CharacterEllipsis;
        objNome->Children.Add(objArquivo);
        Grid::SetRow(objNome, (int) intLinha);
        objGrade->Children.Add(objNome);

        LinhaConteudo objL;
        objL.strCaminho = strCaminho;
        objL.eOriginal = eAtual;
        std::string strGrupo = "faixa" + std::to_string(intLinha);
        for (size_t i = 0; i < arrRotulos.size(); i++)
        {
            auto objOpcao = std::make_shared<RadioButton>();
            objOpcao->Content = arrRotulos[i];
            objOpcao->GroupName = strGrupo;
            objOpcao->IsChecked = (size_t) eAtual == i;
            objOpcao->VerticalAlignment = Vistas::VerticalAlignment::Center;
            objOpcao->Margin = Thickness(0, 0, 14, 0);
            Grid::SetRow(objOpcao, (int) intLinha);
            Grid::SetColumn(objOpcao, 1 + (int) i);
            objGrade->Children.Add(objOpcao);
            objL.arrOpcoes[i] = objOpcao;
        }
        _lstLinhas.push_back(objL);
    }
    return objGrade;
}

UIElementPtr JanelaConteudoFaixas::MontarRodape()
{
    auto objGrade = std::make_shared<Grid>();
    objGrade->ColumnDefinitions.push_back(ColumnDefinition());
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));

    objGrade->Children.Add(_txtAviso);
    auto objCancelar = std::make_shared<Button>();
    objCancelar->Content = "Cancelar";
    objCancelar->IsCancel = true;
    objCancelar->Padding = Thickness(18, 6, 18, 6);
    objCancelar->Margin = Thickness(12, 0, 0, 0);
    objCancelar->Click += [this](RoutedEventArgs &) { Close(); };
    auto objAplicar = std::make_shared<Button>();
    objAplicar->Content = "Aplicar às faixas";
    objAplicar->Padding = Thickness(18, 6, 18, 6);
    objAplicar->Margin = Thickness(8, 0, 0, 0);
    objAplicar->Click += [this](RoutedEventArgs &e) { AoAplicar(e); };
    Grid::SetColumn(objCancelar, 1);
    Grid::SetColumn(objAplicar, 2);
    objGrade->Children.Add(objCancelar);
    objGrade->Children.Add(objAplicar);

    auto objBorda = std::make_shared<Border>();
    objBorda->Background = Recurso("BarraFerramentas");
    objBorda->BorderBrush = Recurso("Borda");
    objBorda->BorderThickness = Thickness(0, 1, 0, 0);
    objBorda->Padding = Thickness(14, 8, 14, 8);
    objBorda->SetChild(objGrade);
    DockPanel::SetDock(objBorda, Dock::Bottom);
    return objBorda;
}

void JanelaConteudoFaixas::AoAplicar(RoutedEventArgs &e)
{
    (void) e;
    TAlteradas dicAlteradas;
    for (const LinhaConteudo &objL : _lstLinhas)
        if (objL.Escolhido() != objL.eOriginal)
        {
            // dicAlteradas[objL.strCaminho] = objL.Escolhido (OrdinalIgnoreCase)
            bool blnTinha = false;
            for (auto &par : dicAlteradas)
                if (Base::IgualSemCaixa(par.first, objL.strCaminho)) { par.second = objL.Escolhido(); blnTinha = true; }
            if (!blnTinha) dicAlteradas.emplace_back(objL.strCaminho, objL.Escolhido());
        }

    if (dicAlteradas.empty())
    {
        _txtAviso->Text = "Nenhuma faixa mudou. Cancelar fecha sem gravar.";
        return;
    }
    _alteradas = dicAlteradas;
    Close();
}

Brush JanelaConteudoFaixas::Recurso(const std::string &strChave)
{
    return Tema::Recurso(strChave);
}

} // namespace ARCA::Vistas

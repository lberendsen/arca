// ARCA no xOS - Vistas/JanelaEditorFaixas (de Vistas\JanelaEditorFaixas.cs). Ver JanelaEditorFaixas.h.
#include "JanelaEditorFaixas.h"

#include "Dialogos.h"

#include "../Base/Texto.h"
#include "../Modelo/Formato.h"

#include <regex>

namespace ARCA::Vistas {

using Modelo::Formato;
using Modelo::TCandidatoIdentificacao;

JanelaEditorFaixas::JanelaEditorFaixas(const std::vector<int> &lstDuracoesDisco, const std::string &strArtista,
                                       const std::string &strAlbum, const std::vector<std::string> &lstTitulosAtuais,
                                       const std::vector<std::string> &lstCompositoresAtuais,
                                       const std::vector<uint8_t> &arrCapaAtual)
{
    _pnlEncarte = std::make_shared<StackPanel>();
    _pnlEncarte->Orientation = Orientation::Horizontal;

    Title = "Catalogar à mão";
    Width = 980;
    Height = 820;
    MinWidth = 760;
    MinHeight = 520;
    WindowStartupLocation = Vistas::WindowStartupLocation::CenterOwner;
    ShowInTaskbar = false;
    Background = Tema::Recurso("Canvas");

    static const std::regex objProvisorio(R"(^Faixa\s*\d+$)", std::regex::icase);
    for (size_t i = 0; i < lstDuracoesDisco.size(); i++)
    {
        // null = vetor vazio ou mais curto (o original testa lstTitulosAtuais != null && i < Count)
        bool blnTemTitulo = i < lstTitulosAtuais.size();
        std::string strTitulo = blnTemTitulo ? lstTitulosAtuais[i] : "";
        // "Faixa 07" é o nome provisório da extração, não um título.
        if (blnTemTitulo && std::regex_match(Base::Trim(strTitulo), objProvisorio)) strTitulo = "";
        LinhaFaixaManual objL;
        objL.Numero = (int) i + 1;
        objL.Duracao = lstDuracoesDisco[i] > 0 ? Formato::Tempo(lstDuracoesDisco[i]) : "?";
        objL.Titulo = strTitulo;
        objL.Interprete = "";
        objL.Compositor = i < lstCompositoresAtuais.size() ? lstCompositoresAtuais[i] : "";
        _lstLinhas.push_back(objL);
    }

    _txtArtista = std::make_shared<TextBox>();
    _txtArtista->Text = strArtista;
    _txtAlbum = std::make_shared<TextBox>();
    _txtAlbum->Text = strAlbum;
    _txtAno = std::make_shared<TextBox>();
    _txtAno->Width = 70;
    _txtAno->HorizontalAlignment = HorizontalAlignment::Left;
    _txtAno->MaxLength = 4;
    _txtGenero = std::make_shared<TextBox>();
    _objMolduraCapa = std::make_shared<Border>();
    _objMolduraCapa->Width = 150;
    _objMolduraCapa->Height = 150;
    _objMolduraCapa->Background = Tema::Recurso("PainelLateral");
    _objMolduraCapa->BorderBrush = Tema::Recurso("Borda");
    _objMolduraCapa->BorderThickness = Thickness(1);
    _objMolduraCapa->HorizontalAlignment = HorizontalAlignment::Left;
    _txtAviso = std::make_shared<TextBlock>();
    _txtAviso->Foreground = Tema::Recurso("Alarme");
    _txtAviso->FontSize = 12;
    _txtAviso->VerticalAlignment = VerticalAlignment::Center;
    _txtAviso->TextWrapping = TextWrapping::Wrap;
    _objGrade = MontarGrade();

    auto objRaiz = std::make_shared<DockPanel>();
    objRaiz->Children.Add(MontarCabecalho(lstDuracoesDisco));
    objRaiz->Children.Add(MontarRodape());

    auto objCorpo = std::make_shared<DockPanel>();
    objCorpo->Margin = Thickness(14);
    UIElementPtr objAlbum = MontarDadosAlbum();
    DockPanel::SetDock(objAlbum, Dock::Top);
    objCorpo->Children.Add(objAlbum);
    UIElementPtr objBarra = MontarBarraFaixas();
    DockPanel::SetDock(objBarra, Dock::Top);
    objCorpo->Children.Add(objBarra);
    auto objMolduraGrade = std::make_shared<Border>();
    objMolduraGrade->Background = Tema::Recurso("FundoLista");
    objMolduraGrade->BorderBrush = Tema::Recurso("Borda");
    objMolduraGrade->BorderThickness = Thickness(1);
    objMolduraGrade->SetChild(_objGrade);
    objCorpo->Children.Add(objMolduraGrade);
    objRaiz->Children.Add(objCorpo);
    Content = objRaiz;

    DefinirCapa(arrCapaAtual);
    _strEstadoInicial = Instantaneo();
    Closing += [this](CancelEventArgs &e) { AoFechar(e); };
}

// ===============================================================
UIElementPtr JanelaEditorFaixas::MontarCabecalho(const std::vector<int> &lstDuracoesDisco)
{
    int intTotal = 0;
    for (int intD : lstDuracoesDisco)
        if (intD > 0) intTotal += intD;

    auto objPilha = std::make_shared<StackPanel>();
    auto objTitulo = std::make_shared<TextBlock>("Catalogar este disco a partir do encarte");
    objTitulo->FontSize = 15;
    objTitulo->Foreground = Tema::Recurso("Texto");
    objPilha->Children.Add(objTitulo);
    auto objResumo = std::make_shared<TextBlock>(
        std::to_string(lstDuracoesDisco.size()) + " faixas · " + Formato::Tempo(intTotal) +
        ". Número e duração vêm do próprio disco — use-os para conferir o encarte.");
    objResumo->Style = Tema::Estilo("Fraco");
    objResumo->Margin = Thickness(0, 3, 0, 0);
    objPilha->Children.Add(objResumo);
    auto objBorda = std::make_shared<Border>();
    objBorda->Background = Tema::Recurso("CabecalhoColuna");
    objBorda->BorderBrush = Tema::Recurso("Borda");
    objBorda->BorderThickness = Thickness(0, 0, 0, 1);
    objBorda->Padding = Thickness(14, 10, 14, 10);
    objBorda->SetChild(objPilha);
    DockPanel::SetDock(objBorda, Dock::Top);
    return objBorda;
}

/// <summary>Capa e botões à esquerda; artista, álbum, ano e gênero à direita.</summary>
UIElementPtr JanelaEditorFaixas::MontarDadosAlbum()
{
    auto objGrade = std::make_shared<Grid>();
    objGrade->Margin = Thickness(0, 0, 0, 12);
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength(170)));
    objGrade->ColumnDefinitions.push_back(ColumnDefinition());

    auto objCapa = std::make_shared<StackPanel>();
    objCapa->Width = 150;
    objCapa->HorizontalAlignment = HorizontalAlignment::Left;
    objCapa->Children.Add(_objMolduraCapa);
    auto objDigitalizar = std::make_shared<Button>();
    objDigitalizar->Content = "Digitalizar capa…";
    objDigitalizar->Padding = Thickness(0, 4, 0, 4);
    objDigitalizar->Margin = Thickness(0, 6, 0, 0);
    objDigitalizar->ToolTip = "Abre o scanner. Depois da digitalização, você recorta a capa.";
    objDigitalizar->Click += [this](RoutedEventArgs &) { AoDigitalizarCapa(); };
    auto objLinhaBotoes = std::make_shared<Grid>();
    objLinhaBotoes->Margin = Thickness(0, 4, 0, 0);
    objLinhaBotoes->ColumnDefinitions.push_back(ColumnDefinition());
    objLinhaBotoes->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));
    auto objEscolher = std::make_shared<Button>();
    objEscolher->Content = "De arquivo…";
    objEscolher->Padding = Thickness(0, 3, 0, 3);
    objEscolher->Click += [this](RoutedEventArgs &) { AoEscolherCapa(); };
    auto objRemover = std::make_shared<Button>();
    objRemover->Content = "Tirar";
    objRemover->Padding = Thickness(8, 3, 8, 3);
    objRemover->Margin = Thickness(4, 0, 0, 0);
    objRemover->Click += [this](RoutedEventArgs &) { DefinirCapa({}); _arrCapaPlena.clear(); };
    Grid::SetColumn(objRemover, 1);
    objLinhaBotoes->Children.Add(objEscolher);
    objLinhaBotoes->Children.Add(objRemover);
    objCapa->Children.Add(objDigitalizar);
    objCapa->Children.Add(objLinhaBotoes);
    objGrade->Children.Add(objCapa);

    auto objCampos = std::make_shared<Grid>();
    objCampos->ColumnDefinitions.push_back(ColumnDefinition(GridLength(110)));
    objCampos->ColumnDefinitions.push_back(ColumnDefinition());
    AdicionarCampo(objCampos, 0, "Artista do álbum", _txtArtista);
    AdicionarCampo(objCampos, 1, "Álbum", _txtAlbum);
    AdicionarCampo(objCampos, 2, "Ano", _txtAno);
    AdicionarCampo(objCampos, 3, "Gênero", _txtGenero);

    // Encarte: abaixo dos campos, na mesma coluna.
    auto objCabEncarte = std::make_shared<DockPanel>();
    objCabEncarte->Margin = Thickness(0, 6, 0, 6);
    auto objPagArquivo = std::make_shared<Button>();
    objPagArquivo->Content = "De arquivo…";
    objPagArquivo->Padding = Thickness(10, 3, 10, 3);
    objPagArquivo->Margin = Thickness(6, 0, 0, 0);
    objPagArquivo->Click += [this](RoutedEventArgs &) { AoPaginaDeArquivo(); };
    auto objPagScanner = std::make_shared<Button>();
    objPagScanner->Content = "Digitalizar página…";
    objPagScanner->Padding = Thickness(10, 3, 10, 3);
    objPagScanner->ToolTip = "Uma página por vez, na ordem do livreto. Cada uma passa pelo corte.";
    objPagScanner->Click += [this](RoutedEventArgs &) { AoDigitalizarPagina(); };
    DockPanel::SetDock(objPagArquivo, Dock::Right);
    DockPanel::SetDock(objPagScanner, Dock::Right);
    objCabEncarte->Children.Add(objPagArquivo);
    objCabEncarte->Children.Add(objPagScanner);
    _txtEncarteTitulo = std::make_shared<TextBlock>();
    _txtEncarteTitulo->Style = Tema::Estilo("Rotulo");
    _txtEncarteTitulo->VerticalAlignment = VerticalAlignment::Center;
    objCabEncarte->Children.Add(_txtEncarteTitulo);

    auto objDireita = std::make_shared<StackPanel>();
    objDireita->Children.Add(objCampos);
    objDireita->Children.Add(objCabEncarte);
    auto objRolagem = std::make_shared<ScrollViewer>();
    objRolagem->HorizontalScrollBarVisibility = ScrollBarVisibility::Auto;
    objRolagem->VerticalScrollBarVisibility = ScrollBarVisibility::Disabled;
    objRolagem->Height = 150;
    objRolagem->Content = _pnlEncarte;
    objDireita->Children.Add(objRolagem);
    AtualizarEncarte();
    Grid::SetColumn(objDireita, 1);
    objGrade->Children.Add(objDireita);
    return objGrade;
}

void JanelaEditorFaixas::AdicionarCampo(const std::shared_ptr<Grid> &objCampos, int intLinha, const std::string &strRotulo,
                                        const std::shared_ptr<TextBox> &objCaixa)
{
    objCampos->RowDefinitions.push_back(RowDefinition(GridLength::Auto()));
    auto objRotulo = std::make_shared<TextBlock>(strRotulo);
    objRotulo->Style = Tema::Estilo("Fraco");
    objRotulo->VerticalAlignment = VerticalAlignment::Center;
    objRotulo->Margin = Thickness(0, 0, 8, 8);
    objCaixa->Margin = Thickness(0, 0, 0, 8);
    Grid::SetRow(objRotulo, intLinha);
    Grid::SetRow(objCaixa, intLinha);
    Grid::SetColumn(objCaixa, 1);
    objCampos->Children.Add(objRotulo);
    objCampos->Children.Add(objCaixa);
}

UIElementPtr JanelaEditorFaixas::MontarBarraFaixas()
{
    auto objBarra = std::make_shared<DockPanel>();
    objBarra->Margin = Thickness(0, 0, 0, 6);
    auto objColar = std::make_shared<Button>();
    objColar->Content = "Colar títulos";
    objColar->Padding = Thickness(10, 3, 10, 3);
    objColar->ToolTip = "Cola uma lista de títulos, uma faixa por linha (de um bloco de notas, por exemplo).\n"
                        "Números e traços no começo da linha (\"01 - \", \"1.\") são ignorados.";
    objColar->Click += [this](RoutedEventArgs &) { AoColarTitulos(); };
    DockPanel::SetDock(objColar, Dock::Right);
    objBarra->Children.Add(objColar);
    auto objRotulo = std::make_shared<TextBlock>("FAIXAS — título obrigatório; intérprete só se for diferente do artista do álbum");
    objRotulo->Style = Tema::Estilo("Rotulo");
    objRotulo->VerticalAlignment = VerticalAlignment::Center;
    objBarra->Children.Add(objRotulo);
    return objBarra;
}

std::shared_ptr<DataGrid> JanelaEditorFaixas::MontarGrade()
{
    auto objGrade = std::make_shared<DataGrid>();
    objGrade->AutoGenerateColumns = false;
    objGrade->CanUserAddRows = false;
    objGrade->CanUserDeleteRows = false;
    objGrade->CanUserReorderColumns = false;
    objGrade->CanUserSortColumns = false;
    objGrade->HeadersVisibility = DataGridHeadersVisibility::Column;
    objGrade->SelectionUnit = DataGridSelectionUnit::Cell;
    objGrade->RowHeight = 24;
    objGrade->Background = Brushes::Transparent;
    objGrade->BorderThickness = Thickness(0);
    objGrade->GridLinesVisibility = DataGridGridLinesVisibility::Horizontal;
    objGrade->HorizontalGridLinesBrush = Tema::Recurso("Borda");
    objGrade->ItemsSource = FonteDeItens::De(&_lstLinhas);
    objGrade->Columns.push_back(Coluna("#", &LinhaFaixaManual::Numero, DataGridLength(36), true));
    objGrade->Columns.push_back(Coluna("Duração", &LinhaFaixaManual::Duracao, DataGridLength(64), true));
    objGrade->Columns.push_back(Coluna("Título", &LinhaFaixaManual::Titulo, DataGridLength(2.2, DataGridLengthUnitType::Star), false));
    objGrade->Columns.push_back(Coluna("Intérprete", &LinhaFaixaManual::Interprete, DataGridLength(1, DataGridLengthUnitType::Star), false));
    objGrade->Columns.push_back(Coluna("Compositor", &LinhaFaixaManual::Compositor, DataGridLength(1.3, DataGridLengthUnitType::Star), false));
    // (o original atribui ItemsSource antes das colunas; aqui as colunas pedem a grade relida)
    objGrade->Refresh();
    return objGrade;
}

// new Binding("Campo") { UpdateSourceTrigger = PropertyChanged }: o campo vira um ponteiro para membro
std::shared_ptr<DataGridTextColumn> JanelaEditorFaixas::Coluna(const std::string &strCabecalho,
                                                               std::string LinhaFaixaManual::*ptrCampo,
                                                               DataGridLength objLargura, bool blnSoLeitura)
{
    auto objColuna = DataGridTextColumn::Nova<LinhaFaixaManual>(
        strCabecalho, objLargura, [ptrCampo](LinhaFaixaManual &l) { return l.*ptrCampo; },
        [ptrCampo](LinhaFaixaManual &l, const std::string &v) { l.*ptrCampo = v; });
    objColuna->IsReadOnly = blnSoLeitura;
    // Sem isto o cursor de edição sai preto sobre o fundo escuro.
    objColuna->EditingElementStyle = Tema::Estilo("EdicaoCelula");
    return objColuna;
}

std::shared_ptr<DataGridTextColumn> JanelaEditorFaixas::Coluna(const std::string &strCabecalho, int LinhaFaixaManual::*ptrCampo,
                                                               DataGridLength objLargura, bool blnSoLeitura)
{
    auto objColuna = DataGridTextColumn::Nova<LinhaFaixaManual>(
        strCabecalho, objLargura, [ptrCampo](LinhaFaixaManual &l) { return std::to_string(l.*ptrCampo); });
    objColuna->IsReadOnly = blnSoLeitura;
    objColuna->EditingElementStyle = Tema::Estilo("EdicaoCelula");
    return objColuna;
}

UIElementPtr JanelaEditorFaixas::MontarRodape()
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
    // ARCA-ORIGINAL: com dados digitados, responder "Não" à pergunta de descartar faz a pergunta voltar
    // uma vez: o Click chama Close() (Closing pergunta e cancela) e, por ser IsCancel, o Button do WPF
    // ainda executa o Window.DialogCancelCommand (DialogResult = false), que fecha de novo e pergunta de
    // novo. A camada repete o WPF; visto no X virtual em 08/11/2026 (captura 17-descartar).
    objCancelar->Click += [this](RoutedEventArgs &) { Close(); };
    auto objAplicar = std::make_shared<Button>();
    objAplicar->Content = "Aplicar ao disco";
    objAplicar->Padding = Thickness(18, 6, 18, 6);
    objAplicar->Margin = Thickness(8, 0, 0, 0);
    objAplicar->Click += [this](RoutedEventArgs &) { AoAplicar(); };
    Grid::SetColumn(objCancelar, 1);
    Grid::SetColumn(objAplicar, 2);
    objGrade->Children.Add(objCancelar);
    objGrade->Children.Add(objAplicar);

    auto objBorda = std::make_shared<Border>();
    objBorda->Background = Tema::Recurso("BarraFerramentas");
    objBorda->BorderBrush = Tema::Recurso("Borda");
    objBorda->BorderThickness = Thickness(0, 1, 0, 0);
    objBorda->Padding = Thickness(14, 8, 14, 8);
    objBorda->SetChild(objGrade);
    DockPanel::SetDock(objBorda, Dock::Bottom);
    return objBorda;
}

// ===============================================================
/// <summary>
/// Uma faixa por linha, na ordem. Linhas vazias são puladas; número,
/// ponto, traço e parêntese no começo ("01 - ", "1.", "(3)") saem.
/// Linhas a mais que faixas são avisadas, não descartadas em silêncio.
/// </summary>
void JanelaEditorFaixas::AoColarTitulos()
{
    std::string strTexto = Clipboard::ContainsText() ? Clipboard::GetText() : "";
    std::vector<std::string> lstTitulos;
    // Split(new[] { "\r\n", "\n", "\r" }, StringSplitOptions.None)
    std::vector<std::string> lstLinhas;
    std::string strLinha;
    for (size_t i = 0; i < strTexto.size(); i++)
    {
        char c = strTexto[i];
        if (c == '\r' || c == '\n')
        {
            lstLinhas.push_back(strLinha);
            strLinha.clear();
            if (c == '\r' && i + 1 < strTexto.size() && strTexto[i + 1] == '\n') i++;
        }
        else
            strLinha += c;
    }
    lstLinhas.push_back(strLinha);
    // LINUX: o std::regex anda em bytes, e o "–" (travessao meio, 3 bytes em UTF-8) dentro de [ ] viraria
    // tres bytes soltos; ele vai numa alternativa, que casa o mesmo que a classe [-–.:)] do original
    static const std::regex objPrefixo(R"(^\s*[\(\[]?\d{1,3}[\)\]]?\s*(?:-|–|\.|:|\))?\s*)");
    for (const std::string &strL : lstLinhas)
    {
        std::string strLimpa = Base::Trim(std::regex_replace(strL, objPrefixo, "", std::regex_constants::format_first_only));
        if (!strLimpa.empty()) lstTitulos.push_back(strLimpa);
    }
    if (lstTitulos.empty())
    {
        _txtAviso->Text = "A área de transferência não tem texto. Copie a lista de títulos (uma faixa por linha) e tente de novo.";
        return;
    }

    _objGrade->CommitEdit();
    for (size_t i = 0; i < _lstLinhas.size() && i < lstTitulos.size(); i++) _lstLinhas[i].Titulo = lstTitulos[i];
    _objGrade->Refresh();

    _txtAviso->Text = lstTitulos.size() == _lstLinhas.size() ? ""
        : "Colei " + std::to_string(lstTitulos.size()) + " títulos para " + std::to_string(_lstLinhas.size()) + " faixas — confira " +
          (lstTitulos.size() > _lstLinhas.size() ? "as linhas que sobraram" : "as faixas que ficaram sem título") + ".";
}

void JanelaEditorFaixas::AoEscolherCapa()
{
    std::string strErro;
    std::shared_ptr<BitmapSource> objImagem = ImagensDoEncarte::DeArquivo(this, "Escolher a capa", strErro);
    _txtAviso->Text = strErro;
    if (objImagem) CortarCapa(objImagem);
}

void JanelaEditorFaixas::AoDigitalizarCapa()
{
    std::string strErro;
    std::shared_ptr<BitmapSource> objImagem = ImagensDoEncarte::DoScanner(strErro);
    _txtAviso->Text = strErro;
    if (objImagem) CortarCapa(objImagem);
}

void JanelaEditorFaixas::AoDigitalizarPagina()
{
    std::string strErro;
    std::shared_ptr<BitmapSource> objImagem = ImagensDoEncarte::DoScanner(strErro);
    _txtAviso->Text = strErro;
    if (objImagem) CortarPagina(objImagem);
}

void JanelaEditorFaixas::AoPaginaDeArquivo()
{
    std::string strErro;
    std::shared_ptr<BitmapSource> objImagem = ImagensDoEncarte::DeArquivo(this, "Escolher uma página do encarte", strErro);
    _txtAviso->Text = strErro;
    if (objImagem) CortarPagina(objImagem);
}

/// <summary>
/// Capa: corte quadrado, e duas versões — a reduzida a 1000 px para
/// embutir nas faixas, e a plena para Artwork\capa.jpg, que é a que se
/// preserva.
/// </summary>
void JanelaEditorFaixas::CortarCapa(const std::shared_ptr<BitmapSource> &objImagem)
{
    std::shared_ptr<BitmapSource> objCortada = ImagensDoEncarte::Cortar(this, objImagem, "Cortar a capa", true);
    if (!objCortada) return;

    std::vector<uint8_t> arrReduzida = ImagensDoEncarte::CodificarReduzida(objCortada);
    std::vector<uint8_t> arrPlena = ImagensDoEncarte::CodificarPlena(objCortada);
    if (arrReduzida.empty() || arrPlena.empty())
    {
        _txtAviso->Text = "Não consegui preparar a capa.";
        return;
    }
    _txtAviso->Text = "";
    DefinirCapa(arrReduzida);
    _arrCapaPlena = arrPlena;
}

/// <summary>Página do encarte: corte livre, guardada só em resolução plena.</summary>
void JanelaEditorFaixas::CortarPagina(const std::shared_ptr<BitmapSource> &objImagem)
{
    std::shared_ptr<BitmapSource> objCortada = ImagensDoEncarte::Cortar(
        this, objImagem, "Cortar a página " + std::to_string(_lstEncarte.size() + 1) + " do encarte", false);
    if (!objCortada) return;

    std::vector<uint8_t> arrPlena = ImagensDoEncarte::CodificarPlena(objCortada);
    if (arrPlena.empty())
    {
        _txtAviso->Text = "Não consegui preparar a página.";
        return;
    }
    _txtAviso->Text = "";
    _lstEncarte.push_back(TPaginaPendente(arrPlena));
    AtualizarEncarte();
}

/// <summary>Redesenha a tira de miniaturas do encarte, cada uma com seus botões (girar, dividir, tirar).</summary>
void JanelaEditorFaixas::AtualizarEncarte()
{
    _pnlEncarte->Children.Clear();
    _txtEncarteTitulo->Text = _lstEncarte.empty() ? "ENCARTE"
        : "ENCARTE — " + std::to_string(_lstEncarte.size()) + " página" + (_lstEncarte.size() == 1 ? "" : "s");
    if (_lstEncarte.empty())
    {
        auto objNenhuma = std::make_shared<TextBlock>(
            "Nenhuma página. As páginas vão para a pasta Artwork do álbum, e EM EXECUÇÃO as mostra no painel do encarte.");
        objNenhuma->Style = Tema::Estilo("Fraco");
        objNenhuma->VerticalAlignment = VerticalAlignment::Center;
        objNenhuma->TextWrapping = TextWrapping::Wrap;
        objNenhuma->MaxWidth = 520;
        _pnlEncarte->Children.Add(objNenhuma);
        return;
    }
    for (size_t i = 0; i < _lstEncarte.size(); i++)
    {
        int intIndice = (int) i;
        auto objPagina = std::make_shared<StackPanel>();
        objPagina->Width = 84;
        objPagina->Margin = Thickness(0, 0, 8, 0);
        auto objMiniatura = std::make_shared<Border>();
        objMiniatura->Width = 84;
        objMiniatura->Height = 84;
        objMiniatura->Background = Tema::Recurso("PainelLateral");
        objMiniatura->BorderBrush = Tema::Recurso("Borda");
        objMiniatura->BorderThickness = Thickness(1);
        objMiniatura->ToolTip = "Página " + std::to_string(i + 1);
        std::shared_ptr<BitmapSource> objImg = _lstEncarte[i].Miniatura(168);
        if (objImg)
        {
            auto objImagem = std::make_shared<Image>();
            objImagem->Source = objImg;
            objImagem->Stretch = Stretch::Uniform;
            objMiniatura->SetChild(objImagem);
        }
        objPagina->Children.Add(objMiniatura);
        objPagina->Children.Add(ImagensDoEncarte::AcoesDaPagina(
            [this, intIndice](RoutedEventArgs &) { _lstEncarte[(size_t) intIndice].Girar(-90); AtualizarEncarte(); },
            [this, intIndice](RoutedEventArgs &) { _lstEncarte[(size_t) intIndice].Girar(90); AtualizarEncarte(); },
            [this, intIndice](RoutedEventArgs &) { DividirPagina(intIndice); },
            [this, intIndice](RoutedEventArgs &) { _lstEncarte.erase(_lstEncarte.begin() + intIndice); AtualizarEncarte(); }));
        _pnlEncarte->Children.Add(objPagina);
    }
}

void JanelaEditorFaixas::DividirPagina(int intIndice)
{
    std::string strErro;
    bool blnDividiu = ImagensDoEncarte::DividirPagina(
        this, _lstEncarte, intIndice, "Dividir a página " + std::to_string(intIndice + 1) + " do encarte em duas", strErro);
    _txtAviso->Text = strErro;
    if (blnDividiu) AtualizarEncarte();
}

void JanelaEditorFaixas::DefinirCapa(const std::vector<uint8_t> &arrBytes)
{
    _arrCapa = arrBytes;
    std::shared_ptr<BitmapSource> objImg = ImagensDoEncarte::Miniatura(_arrCapa, 300);
    if (objImg)
    {
        auto objImagem = std::make_shared<Image>();
        objImagem->Source = objImg;
        objImagem->Stretch = Stretch::UniformToFill;
        _objMolduraCapa->SetChild(objImagem);
    }
    else
    {
        auto objSem = std::make_shared<TextBlock>("sem capa");
        objSem->Style = Tema::Estilo("Fraco");
        objSem->HorizontalAlignment = HorizontalAlignment::Center;
        objSem->VerticalAlignment = VerticalAlignment::Center;
        _objMolduraCapa->SetChild(objSem);
    }
}

// ===============================================================
void JanelaEditorFaixas::AoAplicar()
{
    _objGrade->CommitEdit();

    std::string strArtista = Base::Trim(_txtArtista->Text());
    std::string strAlbum = Base::Trim(_txtAlbum->Text());
    std::string strAno = Base::Trim(_txtAno->Text());

    std::vector<int> lstSemTitulo;
    for (const LinhaFaixaManual &objL : _lstLinhas)
        if (Base::VazioOuBranco(objL.Titulo)) lstSemTitulo.push_back(objL.Numero);

    if (strArtista.empty() || strAlbum.empty())
    {
        _txtAviso->Text = "Preencha o artista do álbum e o álbum.";
        return;
    }
    static const std::regex objAno(R"(^\d{4}$)");
    if (!strAno.empty() && !std::regex_match(strAno, objAno))
    {
        _txtAviso->Text = "O ano tem de ter quatro dígitos (ou ficar vazio).";
        return;
    }
    // Todas as faixas com título: um disco meio catalogado entraria no
    // acervo com "Faixa 07" no meio dos nomes certos.
    if (!lstSemTitulo.empty())
    {
        std::vector<std::string> lstNumeros;
        for (int n : lstSemTitulo) lstNumeros.push_back(std::to_string(n));
        _txtAviso->Text = "Falta o título da(s) faixa(s) " + Base::Junta(lstNumeros, ", ") + ".";
        return;
    }

    // O giro pendente de cada página é aplicado aqui, uma vez só.
    std::vector<std::vector<uint8_t>> lstEncarte;
    if (!_lstEncarte.empty())
    {
        for (const TPaginaPendente &objPagina : _lstEncarte)
        {
            std::vector<uint8_t> arrPagina = objPagina.JpegFinal();
            if (arrPagina.empty())
            {
                _txtAviso->Text = "Não consegui preparar a página " + std::to_string(lstEncarte.size() + 1) + " do encarte.";
                return;
            }
            lstEncarte.push_back(std::move(arrPagina));
        }
    }

    auto objC = std::make_shared<TCandidatoIdentificacao>();
    objC->strFonte = "encarte (digitação)";
    objC->strArtista = strArtista;
    objC->strAlbum = strAlbum;
    objC->strAno = strAno;                                  // vazio = null
    objC->strGenero = Base::Trim(_txtGenero->Text());       // vazio = null
    objC->arrCapa = _arrCapa;
    objC->arrCapaPlena = _arrCapaPlena;
    objC->lstEncarte = lstEncarte;
    bool blnTemCompositor = false;
    std::vector<std::string> lstCompositores;
    for (const LinhaFaixaManual &objL : _lstLinhas)
    {
        objC->lstTitulos.push_back(Base::Trim(objL.Titulo));
        // Intérprete vazio = o artista do álbum (o gravador já faz isso).
        objC->lstArtistasFaixas.push_back(Base::Trim(objL.Interprete));
        std::string strComp = Base::Trim(objL.Compositor);
        if (!strComp.empty()) blnTemCompositor = true;
        lstCompositores.push_back(strComp);
        objC->lstDuracoes.push_back(-1);
    }
    if (blnTemCompositor) objC->lstCompositores = lstCompositores;
    else objC->lstCompositores = std::nullopt;

    Resultado = objC;
    _blnAplicado = true;
    Close();
}

/// <summary>Digitar 20 títulos e perder num clique errado não pode acontecer.</summary>
void JanelaEditorFaixas::AoFechar(CancelEventArgs &e)
{
    if (_blnAplicado) return;
    _objGrade->CommitEdit();
    if (Instantaneo() == _strEstadoInicial) return;
    MessageBoxResult objResp = MessageBox::Show(this, "Há dados digitados que ainda não foram aplicados. Descartar?",
                                                "Catalogar à mão", MessageBoxButton::YesNo, MessageBoxImage::Question,
                                                MessageBoxResult::No);
    if (objResp != MessageBoxResult::Yes) e.Cancel = true;
}

/// <summary>Tudo o que é editável numa string só, para saber se algo mudou.</summary>
std::string JanelaEditorFaixas::Instantaneo() const
{
    std::string objSb;
    objSb.append(_txtArtista->Text()).append(1, '\x01').append(_txtAlbum->Text()).append(1, '\x01')
         .append(_txtAno->Text()).append(1, '\x01').append(_txtGenero->Text()).append(1, '\x01')
         .append(std::to_string(_arrCapa.size())).append(1, '\x01').append(std::to_string(_lstEncarte.size()));
    // Girar ou dividir uma página também é mudança a não perder.
    for (const TPaginaPendente &objPagina : _lstEncarte)
        objSb.append(1, '\x03').append(std::to_string(objPagina.arrJpeg.size())).append(1, '/').append(std::to_string(objPagina.intGiro()));
    for (const LinhaFaixaManual &objL : _lstLinhas)
        objSb.append(1, '\x02').append(objL.Titulo).append(1, '\x01').append(objL.Interprete).append(1, '\x01').append(objL.Compositor);
    return objSb;
}

} // namespace ARCA::Vistas

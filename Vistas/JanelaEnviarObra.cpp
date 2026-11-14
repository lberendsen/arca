// ARCA no xOS - Vistas/JanelaEnviarObra (de Vistas\JanelaEnviarObra.cs).
#include "JanelaEnviarObra.h"

#include "Base/Texto.h"
#include "ImagensDoEncarte.h"
#include "Modelo/Biblioteca.h"

namespace ARCA::Vistas {

using Modelo::TObra;

JanelaEnviarObra::JanelaEnviarObra(Modelo::TBancoObras &objBanco, const std::string &strOQue,
                                   const std::string &strAutorSugerido, const std::vector<uint8_t> &arrCapaDoAlbum)
    : _objBanco(objBanco), _arrCapaDoAlbum(arrCapaDoAlbum)
{
    _lstObras = std::make_shared<ListBox>();
    _txtBusca = std::make_shared<TextBox>();
    _txtTitulo = std::make_shared<TextBox>();
    _txtAutor = std::make_shared<TextBox>();

    Title = "Enviar para uma obra";
    Width = 900;
    Height = 600;
    MinWidth = 720;
    MinHeight = 440;
    WindowStartupLocation = Vistas::WindowStartupLocation::CenterOwner;
    ShowInTaskbar = false;
    Background = Recurso("Canvas");

    _txtAutor->Text = strAutorSugerido;
    _objMolduraCapa = std::make_shared<Border>();
    _objMolduraCapa->Width = 150;
    _objMolduraCapa->Height = 150;
    _objMolduraCapa->Background = Recurso("PainelLateral");
    _objMolduraCapa->BorderBrush = Recurso("Borda");
    _objMolduraCapa->BorderThickness = Thickness(1);
    _objMolduraCapa->HorizontalAlignment = Vistas::HorizontalAlignment::Left;
    _txtAviso = std::make_shared<TextBlock>();
    _txtAviso->Foreground = Recurso("Alarme");
    _txtAviso->FontSize = 12;
    _txtAviso->TextWrapping = Vistas::TextWrapping::Wrap;
    _txtAviso->VerticalAlignment = Vistas::VerticalAlignment::Center;

    auto objRaiz = std::make_shared<DockPanel>();
    objRaiz->Children.Add(MontarCabecalho(strOQue));
    objRaiz->Children.Add(MontarRodape());

    auto objColunas = std::make_shared<Grid>();
    objColunas->Margin = Thickness(12);
    objColunas->ColumnDefinitions.push_back(ColumnDefinition());
    objColunas->ColumnDefinitions.push_back(ColumnDefinition(GridLength(14)));
    objColunas->ColumnDefinitions.push_back(ColumnDefinition(GridLength(340)));
    UIElementPtr objExistentes = MontarExistentes();
    UIElementPtr objNova = MontarNova();
    Grid::SetColumn(objNova, 2);
    objColunas->Children.Add(objExistentes);
    objColunas->Children.Add(objNova);
    objRaiz->Children.Add(objColunas);
    Content = objRaiz;

    MostrarCapaNova();
    CarregarObras();
}

// ===============================================================
UIElementPtr JanelaEnviarObra::MontarCabecalho(const std::string &strOQue)
{
    auto objPilha = std::make_shared<StackPanel>();
    auto t1 = std::make_shared<TextBlock>("Enviar " + strOQue + " para uma obra");
    t1->FontSize = 15;
    t1->Foreground = Recurso("Texto");
    t1->TextTrimming = Vistas::TextTrimming::CharacterEllipsis;
    objPilha->Children.Add(t1);
    auto t2 = std::make_shared<TextBlock>("A obra só agrupa: nada é gravado nos arquivos, e um álbum pode estar em mais de uma obra.");
    t2->Style = Tema::Estilo("Fraco");
    t2->Margin = Thickness(0, 3, 0, 0);
    objPilha->Children.Add(t2);
    auto objBorda = std::make_shared<Border>();
    objBorda->Background = Recurso("CabecalhoColuna");
    objBorda->BorderBrush = Recurso("Borda");
    objBorda->BorderThickness = Thickness(0, 0, 0, 1);
    objBorda->Padding = Thickness(14, 10, 14, 10);
    objBorda->SetChild(objPilha);
    DockPanel::SetDock(objBorda, Dock::Top);
    return objBorda;
}

UIElementPtr JanelaEnviarObra::MontarExistentes()
{
    auto objPainel = std::make_shared<DockPanel>();
    auto objRotulo = std::make_shared<TextBlock>("OBRAS EXISTENTES");
    objRotulo->Style = Tema::Estilo("Rotulo");
    objRotulo->Margin = Thickness(0, 0, 0, 6);
    DockPanel::SetDock(objRotulo, Dock::Top);
    objPainel->Children.Add(objRotulo);

    auto objBusca = std::make_shared<DockPanel>();
    objBusca->Margin = Thickness(0, 0, 0, 6);
    auto objLupa = std::make_shared<TextBlock>("Buscar ");
    objLupa->Style = Tema::Estilo("Fraco");
    objLupa->VerticalAlignment = Vistas::VerticalAlignment::Center;
    objBusca->Children.Add(objLupa);
    objBusca->Children.Add(_txtBusca);
    _txtBusca->TextChanged += [this](TextChangedEventArgs &) { Filtrar(); };
    DockPanel::SetDock(objBusca, Dock::Top);
    objPainel->Children.Add(objBusca);

    auto objEnviar = std::make_shared<Button>();
    objEnviar->Content = "Enviar para a obra selecionada";
    objEnviar->Padding = Thickness(14, 6, 14, 6);
    objEnviar->Margin = Thickness(0, 8, 0, 0);
    objEnviar->HorizontalAlignment = Vistas::HorizontalAlignment::Right;
    objEnviar->Click += [this](RoutedEventArgs &) { AoEnviarExistente(); };
    DockPanel::SetDock(objEnviar, Dock::Bottom);
    objPainel->Children.Add(objEnviar);

    _lstObras->Background = Recurso("FundoLista");
    _lstObras->BorderBrush = Recurso("Borda");
    _lstObras->MouseDoubleClick += [this](MouseButtonEventArgs &) { AoEnviarExistente(); };
    _lstObras->HorizontalScrollBarVisibility = ScrollBarVisibility::Disabled;
    objPainel->Children.Add(_lstObras);
    return objPainel;
}

UIElementPtr JanelaEnviarObra::MontarNova()
{
    auto objPilha = std::make_shared<StackPanel>();
    auto rotulo = [](const std::string &s, const char *estilo, Thickness m) {
        auto t = std::make_shared<TextBlock>(s);
        t->Style = Tema::Estilo(estilo);
        t->Margin = m;
        return t;
    };
    objPilha->Children.Add(rotulo("NOVA OBRA", "Rotulo", Thickness(0, 0, 0, 6)));
    objPilha->Children.Add(rotulo("Título", "Fraco", Thickness(0, 0, 0, 3)));
    objPilha->Children.Add(_txtTitulo);
    objPilha->Children.Add(rotulo("Autor", "Fraco", Thickness(0, 8, 0, 3)));
    objPilha->Children.Add(_txtAutor);
    objPilha->Children.Add(rotulo("Capa da obra", "Fraco", Thickness(0, 8, 0, 3)));

    auto objCapa = std::make_shared<Grid>();
    objCapa->ColumnDefinitions.push_back(ColumnDefinition(GridLength(160)));
    objCapa->ColumnDefinitions.push_back(ColumnDefinition());
    objCapa->Children.Add(_objMolduraCapa);
    auto objBotoes = std::make_shared<StackPanel>();
    auto botao = [](const std::string &s, double topo) {
        auto b = std::make_shared<Button>();
        b->Content = s;
        b->Padding = Thickness(0, 4, 0, 4);
        if (topo > 0) b->Margin = Thickness(0, topo, 0, 0);
        return b;
    };
    auto objArquivo = botao("De arquivo…", 0);
    objArquivo->Click += [this](RoutedEventArgs &) {
        std::string strErro;
        std::shared_ptr<BitmapSource> objImg = ImagensDoEncarte::DeArquivo(this, "Escolher a capa da obra", strErro);
        _txtAviso->Text = strErro;
        if (objImg) CortarCapa(objImg);
    };
    auto objScanner = botao("Digitalizar…", 6);
    objScanner->Click += [this](RoutedEventArgs &) {
        std::string strErro;
        std::shared_ptr<BitmapSource> objImg = ImagensDoEncarte::DoScanner(strErro);
        _txtAviso->Text = strErro;
        if (objImg) CortarCapa(objImg);
    };
    auto objDoAlbum = botao("Usar a capa do álbum", 6);
    objDoAlbum->IsEnabled = !_arrCapaDoAlbum.empty();
    objDoAlbum->Click += [this](RoutedEventArgs &) {
        _arrCapaNova = _arrCapaDoAlbum;
        MostrarCapaNova();
    };
    auto objTirar = botao("Sem capa", 6);
    objTirar->Click += [this](RoutedEventArgs &) {
        _arrCapaNova.clear();
        MostrarCapaNova();
    };
    objBotoes->Children.Add(objArquivo);
    objBotoes->Children.Add(objScanner);
    objBotoes->Children.Add(objDoAlbum);
    objBotoes->Children.Add(objTirar);
    Grid::SetColumn(objBotoes, 1);
    objCapa->Children.Add(objBotoes);
    objPilha->Children.Add(objCapa);

    auto objCriar = std::make_shared<Button>();
    objCriar->Content = "Criar a obra e enviar";
    objCriar->Padding = Thickness(14, 6, 14, 6);
    objCriar->Margin = Thickness(0, 14, 0, 0);
    objCriar->HorizontalAlignment = Vistas::HorizontalAlignment::Right;
    objCriar->Click += [this](RoutedEventArgs &) { AoCriarEEnviar(); };
    objPilha->Children.Add(objCriar);

    auto objBorda = std::make_shared<Border>();
    objBorda->Background = Recurso("PainelLateral");
    objBorda->BorderBrush = Recurso("Borda");
    objBorda->BorderThickness = Thickness(1);
    objBorda->Padding = Thickness(12);
    objBorda->SetChild(objPilha);
    return objBorda;
}

UIElementPtr JanelaEnviarObra::MontarRodape()
{
    auto objGrade = std::make_shared<Grid>();
    objGrade->ColumnDefinitions.push_back(ColumnDefinition());
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));
    objGrade->Children.Add(_txtAviso);
    auto objCancelar = std::make_shared<Button>();
    objCancelar->Content = "Cancelar";
    objCancelar->IsCancel = true;
    objCancelar->Padding = Thickness(18, 6, 18, 6);
    objCancelar->Margin = Thickness(12, 0, 0, 0);
    objCancelar->Click += [this](RoutedEventArgs &) { Close(); };
    Grid::SetColumn(objCancelar, 1);
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
void JanelaEnviarObra::CarregarObras()
{
    try { _lstTodas = _objBanco.ListarObras(); }
    catch (const std::exception &ex)
    {
        _txtAviso->Text = std::string("Não consegui ler o banco de obras: ") + ex.what();
        _lstTodas.clear();
    }
    Filtrar();
    if (!_lstObras->Items.empty()) _lstObras->SelectedIndex = 0;
}

void JanelaEnviarObra::Filtrar()
{
    std::string strTrecho = Base::Trim(_txtBusca->Text());
    _lstObras->Items.clear();
    for (const TObra &o : _lstTodas)
    {
        if (!strTrecho.empty() && !Modelo::ContemSemCaixaNemAcento(o.ToString(), strTrecho)) continue;
        _lstObras->Items.push_back(LinhaDaObra(o));
    }
    _lstObras->Refresh();
}

std::shared_ptr<ListBoxItem> JanelaEnviarObra::LinhaDaObra(const TObra &o)
{
    auto g = std::make_shared<Grid>();
    g->Margin = Thickness(2, 3, 2, 3);
    g->ColumnDefinitions.push_back(ColumnDefinition(GridLength(52)));
    g->ColumnDefinitions.push_back(ColumnDefinition());
    auto objMiniatura = std::make_shared<Border>();
    objMiniatura->Width = 44;
    objMiniatura->Height = 44;
    objMiniatura->Background = Recurso("PainelLateral");
    objMiniatura->HorizontalAlignment = Vistas::HorizontalAlignment::Left;
    std::shared_ptr<BitmapSource> objImg = ImagensDoEncarte::Miniatura(o.arrCapa, 88);
    if (objImg)
    {
        auto i = std::make_shared<Image>();
        i->Source = objImg;
        i->Stretch = Vistas::Stretch::UniformToFill;
        objMiniatura->SetChild(i);
    }
    g->Children.Add(objMiniatura);
    auto objTextos = std::make_shared<StackPanel>();
    objTextos->VerticalAlignment = Vistas::VerticalAlignment::Center;
    auto t1 = std::make_shared<TextBlock>(o.strTitulo);
    t1->Foreground = Recurso("Texto");
    t1->TextTrimming = Vistas::TextTrimming::CharacterEllipsis;
    objTextos->Children.Add(t1);
    auto t2 = std::make_shared<TextBlock>((o.strAutor.empty() ? "" : o.strAutor + " · ") +
                                          (o.intQtdPartes == 1 ? std::string("1 parte") : std::to_string(o.intQtdPartes) + " partes"));
    t2->Style = Tema::Estilo("Fraco");
    t2->FontSize = 11;
    objTextos->Children.Add(t2);
    Grid::SetColumn(objTextos, 1);
    g->Children.Add(objTextos);
    auto objItem = std::make_shared<ListBoxItem>();
    objItem->Content = g;
    objItem->Tag = o;
    return objItem;
}

void JanelaEnviarObra::CortarCapa(const std::shared_ptr<BitmapSource> &objImagem)
{
    std::shared_ptr<BitmapSource> objCortada = ImagensDoEncarte::Cortar(this, objImagem, "Cortar a capa da obra", true);
    if (!objCortada) return;
    std::vector<uint8_t> arrJpeg = ImagensDoEncarte::CodificarPlena(objCortada);
    if (arrJpeg.empty()) { _txtAviso->Text = "Não consegui preparar a capa."; return; }
    _arrCapaNova = arrJpeg;
    MostrarCapaNova();
}

void JanelaEnviarObra::MostrarCapaNova()
{
    std::shared_ptr<BitmapSource> objImg = ImagensDoEncarte::Miniatura(_arrCapaNova, 300);
    if (objImg)
    {
        auto i = std::make_shared<Image>();
        i->Source = objImg;
        i->Stretch = Vistas::Stretch::Uniform;
        _objMolduraCapa->SetChild(i);
    }
    else
    {
        auto t = std::make_shared<TextBlock>("sem capa");
        t->Style = Tema::Estilo("Fraco");
        t->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
        t->VerticalAlignment = Vistas::VerticalAlignment::Center;
        _objMolduraCapa->SetChild(t);
    }
}

void JanelaEnviarObra::AoEnviarExistente()
{
    auto *objItem = dynamic_cast<ListBoxItem *>(static_cast<UIElement *>(_lstObras->ItemDaVista(_lstObras->SelectedIndex())));
    const TObra *o = objItem ? std::any_cast<TObra>(&objItem->Tag) : nullptr;
    if (o == nullptr) { _txtAviso->Text = "Escolha uma obra da lista, ou crie uma nova à direita."; return; }
    _intIdObra = o->intId;
    _strTituloObra = o->strTitulo;
    Close();
}

void JanelaEnviarObra::AoCriarEEnviar()
{
    std::string strTitulo = Base::Trim(_txtTitulo->Text());
    if (strTitulo.empty())
    {
        _txtAviso->Text = "A obra nova precisa de um título.";
        _txtTitulo->Focus();
        return;
    }
    try
    {
        _intIdObra = _objBanco.CriarObra(strTitulo, Base::Trim(_txtAutor->Text()), _arrCapaNova);
        _strTituloObra = strTitulo;
        Close();
    }
    catch (const std::exception &ex)
    {
        _txtAviso->Text = std::string("Não consegui criar a obra: ") + ex.what();
    }
}

} // namespace ARCA::Vistas

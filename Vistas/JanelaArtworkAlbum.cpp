// ARCA no xOS - Vistas/JanelaArtworkAlbum (de Vistas\JanelaArtworkAlbum.cs).
#include "JanelaArtworkAlbum.h"

#include "Base/Arquivo.h"
#include "Base/Texto.h"
#include "Dialogos.h"
#include "Modelo/Aquisicao.h"

#include <algorithm>
#include <cstdio>

namespace ARCA::Vistas {

using Modelo::Integracao;

namespace {
std::string DoisDigitos(int n)
{
    char b[16];
    snprintf(b, sizeof b, "%02d", n);
    return b;
}
} // namespace

// =====================================================================================
// TImagemNoAlbum
// =====================================================================================
void JanelaArtworkAlbum::TImagemNoAlbum::Substituir(const std::vector<uint8_t> &arrNovo)
{
    arrConteudo = arrNovo;
    intGiro = 0;
    m_objMiniatura = nullptr;
}

std::shared_ptr<BitmapSource> JanelaArtworkAlbum::TImagemNoAlbum::Imagem() const
{
    std::shared_ptr<BitmapSource> objImagem = !arrConteudo.empty() ? ImagensDoEncarte::Abrir(arrConteudo) : ImagensDoEncarte::Abrir(strArquivo);
    return ImagensDoEncarte::Girar(objImagem, intGiro);
}

std::shared_ptr<BitmapSource> JanelaArtworkAlbum::TImagemNoAlbum::Miniatura(int intLargura)
{
    if (!m_objMiniatura)
        m_objMiniatura = !arrConteudo.empty() ? ImagensDoEncarte::Miniatura(arrConteudo, intLargura)
                                              : ImagensDoEncarte::Miniatura(strArquivo, intLargura);
    return ImagensDoEncarte::Girar(m_objMiniatura, intGiro);
}

std::vector<uint8_t> JanelaArtworkAlbum::TImagemNoAlbum::ConteudoFinal() const
{
    if (intGiro == 0) return arrConteudo;
    std::vector<uint8_t> arrFinal = ImagensDoEncarte::CodificarComo(Imagem(), Base::Extensao(strArquivo));
    if (arrFinal.empty()) throw std::runtime_error("não consegui preparar " + Base::NomeDoArquivo(strArquivo) + ".");
    return arrFinal;
}

// =====================================================================================
JanelaArtworkAlbum::JanelaArtworkAlbum(const std::string &strPastaAlbum, const std::string &strArtista,
                                       const std::string &strAlbum, const std::string &strRaizAcervo)
    : _strPastaAlbum(strPastaAlbum), _strArtista(strArtista), _strAlbum(strAlbum), _strRaizAcervo(strRaizAcervo)
{
    _pnlExistentes = std::make_shared<WrapPanel>();
    _pnlNovos = std::make_shared<WrapPanel>();

    Title = "Capa e encarte do álbum";
    Width = 900;
    Height = 680;
    MinWidth = 640;
    MinHeight = 480;
    WindowStartupLocation = Vistas::WindowStartupLocation::CenterOwner;
    ShowInTaskbar = false;
    Background = Recurso("Canvas");

    _txtExistentes = std::make_shared<TextBlock>();
    _txtExistentes->Style = Tema::Estilo("Rotulo");
    _txtExistentes->Margin = Thickness(0, 0, 0, 8);
    _txtNovos = std::make_shared<TextBlock>();
    _txtNovos->Style = Tema::Estilo("Rotulo");
    _txtNovos->VerticalAlignment = Vistas::VerticalAlignment::Center;
    _txtAviso = std::make_shared<TextBlock>();
    _txtAviso->Foreground = Recurso("Alarme");
    _txtAviso->FontSize = 12;
    _txtAviso->VerticalAlignment = Vistas::VerticalAlignment::Center;
    _txtAviso->TextWrapping = Vistas::TextWrapping::Wrap;
    _btnGravar = std::make_shared<Button>();
    _btnGravar->Content = "Gravar no álbum";
    _btnGravar->Padding = Thickness(18, 6, 18, 6);
    _btnGravar->Margin = Thickness(8, 0, 0, 0);
    _btnGravar->IsEnabled = false;
    _btnGravar->Click += [this](RoutedEventArgs &) { AoGravar(); };

    auto objRaiz = std::make_shared<DockPanel>();
    objRaiz->Children.Add(MontarCabecalho());
    objRaiz->Children.Add(MontarRodape());

    auto objCorpo = std::make_shared<StackPanel>();
    objCorpo->Margin = Thickness(14);
    objCorpo->Children.Add(_txtExistentes);
    objCorpo->Children.Add(_pnlExistentes);
    auto objLinha = std::make_shared<Border>();
    objLinha->Height = 1;
    objLinha->Background = Recurso("Borda");
    objLinha->Margin = Thickness(0, 14, 0, 12);
    objCorpo->Children.Add(objLinha);
    objCorpo->Children.Add(MontarBarraNovos());
    objCorpo->Children.Add(_pnlNovos);
    auto objRolagem = std::make_shared<ScrollViewer>();
    objRolagem->VerticalScrollBarVisibility = ScrollBarVisibility::Auto;
    objRolagem->Content = objCorpo;
    objRaiz->Children.Add(objRolagem);
    Content = objRaiz;

    CarregarExistentes();
    AtualizarNovos();
    Closing += [this](CancelEventArgs &e) { AoFechar(e); };
}

// ===============================================================
UIElementPtr JanelaArtworkAlbum::MontarCabecalho()
{
    auto objPilha = std::make_shared<StackPanel>();
    auto t1 = std::make_shared<TextBlock>(_strArtista + " — " + _strAlbum);
    t1->FontSize = 15;
    t1->Foreground = Recurso("Texto");
    t1->TextTrimming = Vistas::TextTrimming::CharacterEllipsis;
    objPilha->Children.Add(t1);
    auto t2 = std::make_shared<TextBlock>(PastaArtwork());
    t2->Style = Tema::Estilo("Fraco");
    t2->Margin = Thickness(0, 3, 0, 0);
    t2->TextTrimming = Vistas::TextTrimming::CharacterEllipsis;
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

UIElementPtr JanelaArtworkAlbum::MontarBarraNovos()
{
    auto objBarra = std::make_shared<DockPanel>();
    objBarra->Margin = Thickness(0, 0, 0, 8);
    auto objBotoes = std::make_shared<StackPanel>();
    objBotoes->Orientation = Vistas::Orientation::Horizontal;
    objBotoes->Children.Add(Botao("Digitalizar capa…", [this] { AoDigitalizarCapa(); }));
    objBotoes->Children.Add(Botao("Capa de arquivo…", [this] { AoCapaDeArquivo(); }));
    auto objVao = std::make_shared<Border>();
    objVao->Width = 14;
    objBotoes->Children.Add(objVao);
    objBotoes->Children.Add(Botao("Digitalizar página…", [this] { AoDigitalizarPagina(); }));
    objBotoes->Children.Add(Botao("Página de arquivo…", [this] { AoPaginaDeArquivo(); }));
    DockPanel::SetDock(objBotoes, Dock::Right);
    objBarra->Children.Add(objBotoes);
    objBarra->Children.Add(_txtNovos);
    return objBarra;
}

std::shared_ptr<Button> JanelaArtworkAlbum::Botao(const std::string &strTexto, std::function<void()> objAcao)
{
    auto objB = std::make_shared<Button>();
    objB->Content = strTexto;
    objB->Padding = Thickness(10, 3, 10, 3);
    objB->Margin = Thickness(6, 0, 0, 0);
    objB->Click += [objAcao](RoutedEventArgs &) { objAcao(); };
    return objB;
}

UIElementPtr JanelaArtworkAlbum::MontarRodape()
{
    auto objGrade = std::make_shared<Grid>();
    objGrade->ColumnDefinitions.push_back(ColumnDefinition());
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));
    objGrade->Children.Add(_txtAviso);
    auto objFechar = std::make_shared<Button>();
    objFechar->Content = "Fechar";
    objFechar->IsCancel = true;
    objFechar->Padding = Thickness(18, 6, 18, 6);
    objFechar->Margin = Thickness(12, 0, 0, 0);
    objFechar->Click += [this](RoutedEventArgs &) { Close(); };
    Grid::SetColumn(objFechar, 1);
    Grid::SetColumn(_btnGravar, 2);
    objGrade->Children.Add(objFechar);
    objGrade->Children.Add(_btnGravar);
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
/// <summary>
/// A subpasta Artwork do álbum, respeitando a grafia que já existir
/// ("artwork" criada à mão continua sendo usada). Não cria nada.
/// </summary>
std::string JanelaArtworkAlbum::PastaArtwork() const
{
    try
    {
        for (const std::string &strSub : Base::Pastas(_strPastaAlbum))
            if (Base::IgualSemCaixa(Base::NomeDoArquivo(strSub), Integracao::PastaArtwork)) return strSub;
    }
    catch (...) {}
    return Base::Combina(_strPastaAlbum, Integracao::PastaArtwork);
}

std::vector<std::string> JanelaArtworkAlbum::ImagensExistentes() const
{
    std::vector<std::string> lstImagens;
    std::string strPasta = PastaArtwork();
    if (!Base::PastaExiste(strPasta)) return lstImagens;
    for (const std::string &a : Base::Arquivos(strPasta))
    {
        std::string ext = Base::Minusculas(Base::Extensao(a));
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp" || ext == ".tif" || ext == ".tiff") lstImagens.push_back(a);
    }
    std::sort(lstImagens.begin(), lstImagens.end(),
              [](const std::string &a, const std::string &b) { return Base::Minusculas(a) < Base::Minusculas(b); });
    return lstImagens;
}

/// <summary>Relê a pasta Artwork e descarta o que estava pendente nas imagens do álbum.</summary>
void JanelaArtworkAlbum::CarregarExistentes()
{
    _lstNoAlbum.clear();
    for (const std::string &a : ImagensExistentes())
    {
        TImagemNoAlbum i;
        i.strArquivo = a;
        i.blnExiste = true;
        _lstNoAlbum.push_back(i);
    }
    AtualizarExistentes();
}

void JanelaArtworkAlbum::AtualizarExistentes()
{
    _pnlExistentes->Children.Clear();
    int intNoDisco = (int) std::count_if(_lstNoAlbum.begin(), _lstNoAlbum.end(), [](const TImagemNoAlbum &i) { return i.blnExiste; });
    int intAlteradas = QtdAlteradas();
    _txtExistentes->Text = _lstNoAlbum.empty() ? "NO ÁLBUM — a pasta Artwork ainda não tem imagens"
        : "NO ÁLBUM — " + std::to_string(intNoDisco) + " imagem" + (intNoDisco == 1 ? "" : "ns") + " em Artwork" +
              (intAlteradas == 0 ? "" : " · " + std::to_string(intAlteradas) + " alteraç" + (intAlteradas == 1 ? "ão" : "ões") + " por gravar");
    for (size_t i = 0; i < _lstNoAlbum.size(); i++)
    {
        int intIndice = (int) i;
        TImagemNoAlbum &objItem = _lstNoAlbum[i];
        UIElementPtr objAcoes = ImagensDoEncarte::AcoesDaPagina(
            [this, intIndice](RoutedEventArgs &) { _lstNoAlbum[(size_t) intIndice].Girar(-90); AtualizarExistentes(); },
            [this, intIndice](RoutedEventArgs &) { _lstNoAlbum[(size_t) intIndice].Girar(90); AtualizarExistentes(); },
            [this, intIndice](RoutedEventArgs &) { DividirNoAlbum(intIndice); },
            [this, intIndice](RoutedEventArgs &) { TirarNoAlbum(intIndice); },
            objItem.blnTirar ? "Repor" : "Tirar");
        std::shared_ptr<StackPanel> objCartao = Cartao(objItem.Miniatura(192), LegendaNoAlbum(objItem), objAcoes);
        if (objItem.blnTirar) objCartao->Children[0]->Opacity = 0.35;
        _pnlExistentes->Children.Add(objCartao);
    }
    AtualizarBotaoGravar();
}

std::string JanelaArtworkAlbum::LegendaNoAlbum(const TImagemNoAlbum &objItem)
{
    std::string strNome = Base::NomeDoArquivo(objItem.strArquivo);
    if (objItem.blnTirar) return strNome + " (sai do álbum; vira backup)";
    if (!objItem.blnExiste) return strNome + " (nova, da divisão)";
    if (objItem.Alterada()) return strNome + " (alterada; a atual vira backup)";
    return strNome;
}

int JanelaArtworkAlbum::QtdAlteradas() const
{
    return (int) std::count_if(_lstNoAlbum.begin(), _lstNoAlbum.end(), [](const TImagemNoAlbum &i) { return i.Alterada(); });
}

void JanelaArtworkAlbum::AtualizarBotaoGravar()
{
    _btnGravar->IsEnabled = QtdAlteradas() > 0 || !_arrCapaNova.empty() || !_lstPaginasNovas.empty();
}

/// <summary>Imagem do disco: marca para sair (ou desmarca). Metade ainda não gravada: some da lista.</summary>
void JanelaArtworkAlbum::TirarNoAlbum(int intIndice)
{
    TImagemNoAlbum &objItem = _lstNoAlbum[(size_t) intIndice];
    if (objItem.blnExiste) objItem.blnTirar = !objItem.blnTirar;
    else _lstNoAlbum.erase(_lstNoAlbum.begin() + intIndice);
    AtualizarExistentes();
}

/// <summary>
/// A metade esquerda fica no lugar (e com o nome) da imagem; a direita
/// vem logo depois, com o nome seguido de uma letra — encarte_03b.jpg
/// —, que na ordem alfabética cai entre a 03 e a 04 sem renomear as
/// outras páginas do acervo. Ambas no formato da original.
/// </summary>
void JanelaArtworkAlbum::DividirNoAlbum(int intIndice)
{
    std::string strErro;
    std::vector<uint8_t> arrEsquerda, arrDireita;
    std::string strArquivo = _lstNoAlbum[(size_t) intIndice].strArquivo;
    bool blnDividiu = ImagensDoEncarte::PedirDivisao(this, _lstNoAlbum[(size_t) intIndice].Imagem(),
                                                     "Dividir " + Base::NomeDoArquivo(strArquivo) + " em duas", Base::Extensao(strArquivo),
                                                     arrEsquerda, arrDireita, strErro);
    _txtAviso->Text = strErro;
    if (!blnDividiu) return;

    TImagemNoAlbum &objItem = _lstNoAlbum[(size_t) intIndice];
    objItem.Substituir(arrEsquerda);
    objItem.blnTirar = false;
    TImagemNoAlbum objDireita;
    objDireita.strArquivo = NomeDaMetadeDireita(objItem.strArquivo);
    objDireita.blnExiste = false;
    objDireita.arrConteudo = arrDireita;
    _lstNoAlbum.insert(_lstNoAlbum.begin() + intIndice + 1, objDireita);
    AtualizarExistentes();
}

/// <summary>O nome com a primeira letra livre (b, c, …), no disco e entre as pendentes.</summary>
std::string JanelaArtworkAlbum::NomeDaMetadeDireita(const std::string &strArquivo) const
{
    std::string strPasta = Base::PastaDe(strArquivo);
    std::string strBase = Base::NomeSemExtensao(strArquivo);
    std::string strExt = Base::Extensao(strArquivo);
    for (int n = 0;; n++)
    {
        std::string strSufixo = n < 25 ? std::string(1, (char) ('b' + n)) : "_" + std::to_string(n);
        std::string strCandidato = Base::Combina(strPasta, strBase + strSufixo + strExt);
        if (!Base::ArquivoExiste(strCandidato) &&
            std::none_of(_lstNoAlbum.begin(), _lstNoAlbum.end(),
                         [&](const TImagemNoAlbum &i) { return Base::IgualSemCaixa(i.strArquivo, strCandidato); }))
            return strCandidato;
    }
}

void JanelaArtworkAlbum::AtualizarNovos()
{
    _pnlNovos->Children.Clear();
    int intQtd = (int) _lstPaginasNovas.size() + (!_arrCapaNova.empty() ? 1 : 0);
    _txtNovos->Text = intQtd == 0 ? "A ACRESCENTAR" : "A ACRESCENTAR — " + std::to_string(intQtd) + " imagem" + (intQtd == 1 ? "" : "ns");
    AtualizarBotaoGravar();

    if (intQtd == 0)
    {
        auto t = std::make_shared<TextBlock>(
            "Digitalize a capa e as páginas do encarte, na ordem do livreto. Nada vai para o álbum até \"Gravar no álbum\".");
        t->Style = Tema::Estilo("Fraco");
        t->TextWrapping = Vistas::TextWrapping::Wrap;
        t->MaxWidth = 600;
        _pnlNovos->Children.Add(t);
        return;
    }
    if (!_arrCapaNova.empty())
    {
        bool blnSubstitui = Base::ArquivoExiste(Base::Combina(PastaArtwork(), "capa.jpg"));
        auto objTirar = std::make_shared<Button>();
        objTirar->Content = "Tirar";
        objTirar->FontSize = 10;
        objTirar->Padding = Thickness(0, 1, 0, 1);
        objTirar->Margin = Thickness(0, 3, 0, 0);
        objTirar->Click += [this](RoutedEventArgs &) {
            _arrCapaNova.clear();
            AtualizarNovos();
        };
        _pnlNovos->Children.Add(Cartao(ImagensDoEncarte::Miniatura(_arrCapaNova, 192),
                                       blnSubstitui ? "capa.jpg (substitui; a atual vira backup)" : "capa.jpg", objTirar));
    }
    int intProxima = ProximaPagina();
    for (size_t i = 0; i < _lstPaginasNovas.size(); i++)
    {
        int intIndice = (int) i;
        std::string strNome = "encarte_" + DoisDigitos(intProxima + intIndice) + ".jpg";
        UIElementPtr objAcoes = ImagensDoEncarte::AcoesDaPagina(
            [this, intIndice](RoutedEventArgs &) { _lstPaginasNovas[(size_t) intIndice].Girar(-90); AtualizarNovos(); },
            [this, intIndice](RoutedEventArgs &) { _lstPaginasNovas[(size_t) intIndice].Girar(90); AtualizarNovos(); },
            [this, intIndice, strNome](RoutedEventArgs &) { DividirPagina(intIndice, strNome); },
            [this, intIndice](RoutedEventArgs &) { _lstPaginasNovas.erase(_lstPaginasNovas.begin() + intIndice); AtualizarNovos(); });
        _pnlNovos->Children.Add(Cartao(_lstPaginasNovas[i].Miniatura(192), strNome, objAcoes));
    }
}

void JanelaArtworkAlbum::DividirPagina(int intIndice, const std::string &strNome)
{
    std::string strErro;
    bool blnDividiu = ImagensDoEncarte::DividirPagina(this, _lstPaginasNovas, intIndice, "Dividir " + strNome + " em duas páginas", strErro);
    _txtAviso->Text = strErro;
    if (blnDividiu) AtualizarNovos();
}

/// <summary>Miniatura de 96 px com legenda e os botões de ação embaixo.</summary>
std::shared_ptr<StackPanel> JanelaArtworkAlbum::Cartao(const std::shared_ptr<BitmapSource> &objImg, const std::string &strLegenda,
                                                       UIElementPtr objAcoes)
{
    auto objCartao = std::make_shared<StackPanel>();
    objCartao->Width = 110;
    objCartao->Margin = Thickness(0, 0, 10, 10);
    auto objMoldura = std::make_shared<Border>();
    objMoldura->Width = 96;
    objMoldura->Height = 96;
    objMoldura->Background = Recurso("PainelLateral");
    objMoldura->BorderBrush = Recurso("Borda");
    objMoldura->BorderThickness = Thickness(1);
    objMoldura->HorizontalAlignment = Vistas::HorizontalAlignment::Left;
    if (objImg)
    {
        auto i = std::make_shared<Image>();
        i->Source = objImg;
        i->Stretch = Vistas::Stretch::Uniform;
        objMoldura->SetChild(i);
    }
    objCartao->Children.Add(objMoldura);
    auto t = std::make_shared<TextBlock>(strLegenda);
    t->Style = Tema::Estilo("Fraco");
    t->FontSize = 10;
    t->TextWrapping = Vistas::TextWrapping::Wrap;
    t->Margin = Thickness(0, 3, 0, 0);
    objCartao->Children.Add(t);
    if (objAcoes)
    {
        // Mesma largura da miniatura, para os botões ficarem embaixo dela.
        auto objLimite = std::make_shared<Border>();
        objLimite->Width = 96;
        objLimite->HorizontalAlignment = Vistas::HorizontalAlignment::Left;
        objLimite->SetChild(objAcoes);
        objCartao->Children.Add(objLimite);
    }
    return objCartao;
}

/// <summary>Número da próxima página: continua depois do maior encarte_NN existente, para nunca sobrescrever.</summary>
int JanelaArtworkAlbum::ProximaPagina() const
{
    int intMaior = 0;
    for (const std::string &a : ImagensExistentes())
    {
        // Regex ^encarte_(\d+)$, IgnoreCase
        std::string strNome = Base::NomeSemExtensao(a);
        if (!Base::ComecaCom(strNome, "encarte_", true)) continue;
        std::string strNum = strNome.substr(8);
        if (strNum.empty() || !std::all_of(strNum.begin(), strNum.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
        int intN;
        if (Base::TentaInt(strNum, intN) && intN > intMaior) intMaior = intN;
    }
    return intMaior + 1;
}

// ===============================================================
void JanelaArtworkAlbum::AoDigitalizarCapa()
{
    std::string strErro;
    std::shared_ptr<BitmapSource> objImagem = ImagensDoEncarte::DoScanner(strErro);
    _txtAviso->Text = strErro;
    if (objImagem) UsarCapa(objImagem);
}

void JanelaArtworkAlbum::AoCapaDeArquivo()
{
    std::string strErro;
    std::shared_ptr<BitmapSource> objImagem = ImagensDoEncarte::DeArquivo(this, "Escolher a capa", strErro);
    _txtAviso->Text = strErro;
    if (objImagem) UsarCapa(objImagem);
}

void JanelaArtworkAlbum::AoDigitalizarPagina()
{
    std::string strErro;
    std::shared_ptr<BitmapSource> objImagem = ImagensDoEncarte::DoScanner(strErro);
    _txtAviso->Text = strErro;
    if (objImagem) UsarPagina(objImagem);
}

void JanelaArtworkAlbum::AoPaginaDeArquivo()
{
    std::string strErro;
    std::shared_ptr<BitmapSource> objImagem = ImagensDoEncarte::DeArquivo(this, "Escolher uma página do encarte", strErro);
    _txtAviso->Text = strErro;
    if (objImagem) UsarPagina(objImagem);
}

void JanelaArtworkAlbum::UsarCapa(const std::shared_ptr<BitmapSource> &objImagem)
{
    std::shared_ptr<BitmapSource> objCortada = ImagensDoEncarte::Cortar(this, objImagem, "Cortar a capa", true);
    if (!objCortada) return;
    _arrCapaNova = ImagensDoEncarte::CodificarPlena(objCortada);
    if (_arrCapaNova.empty()) _txtAviso->Text = "Não consegui preparar a capa.";
    AtualizarNovos();
}

void JanelaArtworkAlbum::UsarPagina(const std::shared_ptr<BitmapSource> &objImagem)
{
    std::shared_ptr<BitmapSource> objCortada = ImagensDoEncarte::Cortar(
        this, objImagem, "Cortar a página encarte_" + DoisDigitos(ProximaPagina() + (int) _lstPaginasNovas.size()), false);
    if (!objCortada) return;
    std::vector<uint8_t> arrPlena = ImagensDoEncarte::CodificarPlena(objCortada);
    if (arrPlena.empty()) { _txtAviso->Text = "Não consegui preparar a página."; return; }
    _lstPaginasNovas.emplace_back(arrPlena);
    AtualizarNovos();
}

// ===============================================================
void JanelaArtworkAlbum::AoGravar()
{
    std::string strPasta = PastaArtwork();
    std::vector<std::string> lstGravados;      // "a acrescentar"
    std::vector<std::string> lstDivisoes;      // metades novas, vindas do álbum
    std::vector<std::string> lstAlteradas;     // regravadas no lugar (giro, divisão)
    std::vector<std::string> lstRetiradas;     // os backups do que saiu
    bool blnFalhou = false;
    try
    {
        // Primeiro o que já está no álbum. Cada item gravado é limpo na
        // hora: se algo falhar no meio, o que ficou pendente continua
        // na tela para tentar de novo, e o que foi feito não se repete.
        for (size_t i = 0; i < _lstNoAlbum.size();)
        {
            TImagemNoAlbum &objItem = _lstNoAlbum[i];
            if (objItem.blnTirar)
            {
                lstRetiradas.push_back(Integracao::RetirarDoAcervo(objItem.strArquivo));
                _lstNoAlbum.erase(_lstNoAlbum.begin() + (long) i);
                continue;
            }
            std::vector<uint8_t> arrConteudo = objItem.ConteudoFinal();
            if (!arrConteudo.empty())
            {
                Integracao::GravarNoAcervo(arrConteudo, objItem.strArquivo);
                (objItem.blnExiste ? lstAlteradas : lstDivisoes).push_back(objItem.strArquivo);
                objItem.blnExiste = true;
                objItem.Substituir({});
            }
            i++;
        }

        if (!_arrCapaNova.empty())
        {
            std::string strCapa = Base::Combina(strPasta, "capa.jpg");
            Integracao::GravarNoAcervo(_arrCapaNova, strCapa);
            lstGravados.push_back(strCapa);
        }
        int intProxima = ProximaPagina();
        for (size_t i = 0; i < _lstPaginasNovas.size(); i++)
        {
            std::string strPagina = Base::Combina(strPasta, "encarte_" + DoisDigitos(intProxima + (int) i) + ".jpg");
            std::vector<uint8_t> arrPagina = _lstPaginasNovas[i].JpegFinal();
            if (arrPagina.empty())
                throw std::runtime_error("não consegui preparar a página " + Base::NomeDoArquivo(strPagina) + ".");
            Integracao::GravarNoAcervo(arrPagina, strPagina);
            lstGravados.push_back(strPagina);
        }
    }
    catch (const std::exception &ex)
    {
        blnFalhou = true;
        size_t intFeitas = lstGravados.size() + lstDivisoes.size() + lstAlteradas.size() + lstRetiradas.size();
        _txtAviso->Text = std::string("Falhou ao gravar: ") + ex.what() +
                          (intFeitas > 0 ? " (" + std::to_string(intFeitas) + " imagem(ns) já gravada(s) ou retirada(s) antes da falha.)" : "");
    }

    if (lstGravados.size() + lstDivisoes.size() + lstAlteradas.size() + lstRetiradas.size() > 0)
    {
        _blnGravou = true;
        bool blnLedger = true;
        std::vector<std::string> lstAcrescentadas = lstDivisoes;
        lstAcrescentadas.insert(lstAcrescentadas.end(), lstGravados.begin(), lstGravados.end());
        if (!lstAcrescentadas.empty())
            blnLedger &= Integracao::RegistrarEncarteNoLedger(_strRaizAcervo, _strArtista, _strAlbum, _strPastaAlbum, lstAcrescentadas);
        if (!lstAlteradas.empty())
            blnLedger &= Integracao::RegistrarEncarteNoLedger(_strRaizAcervo, _strArtista, _strAlbum, _strPastaAlbum, lstAlteradas, "encarte alterado");
        if (!lstRetiradas.empty())
            blnLedger &= Integracao::RegistrarEncarteNoLedger(_strRaizAcervo, _strArtista, _strAlbum, _strPastaAlbum, lstRetiradas, "encarte retirado");
        // Tira da área "a acrescentar" só o que foi de fato gravado.
        if (!_arrCapaNova.empty() &&
            std::any_of(lstGravados.begin(), lstGravados.end(), [](const std::string &g) { return Base::IgualSemCaixa(Base::NomeDoArquivo(g), "capa.jpg"); }))
            _arrCapaNova.clear();
        size_t intPaginasGravadas = (size_t) std::count_if(lstGravados.begin(), lstGravados.end(),
                                                           [](const std::string &g) { return Base::ComecaCom(Base::NomeDoArquivo(g), "encarte_", true); });
        _lstPaginasNovas.erase(_lstPaginasNovas.begin(), _lstPaginasNovas.begin() + (long) std::min(intPaginasGravadas, _lstPaginasNovas.size()));
        if (_txtAviso->Text.empty())
            _txtAviso->Text = blnLedger ? "" : "Gravado, mas não consegui escrever no ARCA_Ledger.jsonl.";
    }
    // Sem falha, a lista volta a ser o retrato da pasta; com falha,
    // fica como está, com o que não foi feito ainda pendente.
    if (blnFalhou) AtualizarExistentes();
    else CarregarExistentes();
    AtualizarNovos();
}

void JanelaArtworkAlbum::AoFechar(CancelEventArgs &e)
{
    if (_arrCapaNova.empty() && _lstPaginasNovas.empty() && QtdAlteradas() == 0) return;
    MessageBoxResult objResp = MessageBox::Show(this, "Há imagens e alterações que ainda não foram gravadas no álbum. Descartar?",
                                                "Capa e encarte", MessageBoxButton::YesNo, MessageBoxImage::Question, MessageBoxResult::No);
    if (objResp != MessageBoxResult::Yes) e.Cancel = true;
}

} // namespace ARCA::Vistas

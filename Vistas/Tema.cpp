// ARCA no xOS - Vistas/Tema (de Tema.xaml).
//
// ===================================================================
// PALETA ESCURA — a chapa do EM EXECUÇÃO no programa inteiro.
//
// Até 2026-10 os módulos AQUISIÇÃO e BIBLIOTECA eram claros (papel
// sépia, #EFE8DB) e só o EM EXECUÇÃO era escuro. O papel claro cansava
// a vista em sessão longa; hoje tudo usa a chapa (#17120B). As chaves
// continuam as mesmas — só os valores mudaram —, então quem pede
// "Canvas" ou "Texto" recebe o tom escuro sem saber.
//
// As cinco regras que decidem ONDE cada cor vai, que é o que cansa ou
// não cansa a vista:
//
// 1. O VALOR faz a hierarquia; o MATIZ faz o clima.
//    Tudo aqui é praticamente o mesmo matiz (~35°). O que separa um
//    painel do outro é claridade, nunca cor. Teste: em tons de cinza a
//    tela tem de continuar legível.
//
// 2. SATURAÇÃO INVERSAMENTE PROPORCIONAL À ÁREA.
//    As áreas grandes (Canvas, FundoLista) são quase pretas, com croma
//    baixíssimo. O croma alto fica só em coisas pequenas.
//
// 3. UM ÚNICO ACENTO, USADO COM AVAREZA.
//    É a regra herdada do WMP12 — lá o azul era caro. Aqui o âmbar é
//    caro: botão de reproduzir, faixa em execução, rótulo de seção.
//    Nada mais. É a raridade que faz destacar.
//
// 4. NADA DE BRANCO SOBRE PRETO.
//    No escuro o contraste tem de CAIR, não subir: o texto é creme
//    (#E3D2B0) sobre a chapa (#17120B), não branco sobre preto. O
//    branco puro em traço fino sobre o vazio ofusca e cansa.
//
// 5. UM ALARME FORA DA FAMÍLIA.
//    Correlação negativa, integração que falhou: essas precisam furar
//    a paleta para serem vistas. Tijolo (#E0563A) contrasta sem brigar
//    com o sépia.
// ===================================================================
#include "Tema.h"

#include <cstdio>
#include <map>
#include <mutex>

namespace ARCA::Vistas {

Brush Brush::Linear(Point inicio, Point fim, std::vector<GradientStop> paradas)
{
    Brush b;
    b._nulo = false;
    b._inicio = inicio;
    b._fim = fim;
    b._paradas = std::move(paradas);
    if (!b._paradas.empty()) b._cor = b._paradas.front().Color;
    return b;
}

bool Brush::operator==(const Brush &o) const
{
    if (_nulo != o._nulo) return false;
    if (_nulo) return true;
    if (_cor != o._cor || Opacity != o.Opacity || _paradas.size() != o._paradas.size()) return false;
    for (size_t i = 0; i < _paradas.size(); i++)
        if (_paradas[i].Color != o._paradas[i].Color || _paradas[i].Offset != o._paradas[i].Offset) return false;
    return true;
}

namespace Tema {

namespace {

Brush C(const char *hex) { return Brush(Color::De(hex)); }

struct Dicionario
{
    std::map<std::string, Brush> pinceis;
    std::map<std::string, Style> estilos;      // x:Key
    std::map<std::string, Style> implicitos;   // TargetType
    std::map<Gabarito, GabaritoBorda> modelos;
};

Dicionario Monta()
{
    Dicionario d;
    auto &p = d.pinceis;

    // ===== Áreas grandes, croma baixo =================================
    p["Canvas"]           = C("#FF17120B");
    p["FundoLista"]       = C("#FF120E08");
    p["PainelLateral"]    = C("#FF1C160D");
    p["BarraTrilha"]      = C("#FF211A10");
    p["BarraFerramentas"] = C("#FF1C160D");
    p["CabecalhoColuna"]  = C("#FF1A140C");
    p["LinhaCabecalho"]   = C("#FF2A2115");
    p["AbaTarefa"]        = C("#FF241C10");
    p["Borda"]            = C("#FF3A2E1C");

    // Fundo alternado das listas: a diferença tem de ser quase subliminar.
    // Faixa zebrada de contraste alto é das coisas que mais cansam em
    // tabela longa.
    p["LinhaAlternada"]   = C("#FF15110A");

    // ===== Tinta ======================================================
    p["Texto"]            = C("#FFE3D2B0");
    p["TextoFraco"]       = C("#FF8A7654");

    // O acento. Chamava-se AzulTexto/AzulTocando; agora é o ocre, e os
    // nomes deixaram de mentir.
    p["Acento"]           = C("#FFE8A24A");
    p["AcentoVivo"]       = C("#FFFFB454");
    p["Alarme"]           = C("#FFE0563A");

    p["ProgressoTocado"]   = C("#FF8A7654");
    p["ProgressoRestante"] = C("#FF3A2E1C");

    p["Rodape"] = Brush::Linear(Point(0, 0), Point(0, 1), {{Color::De("#FF1E170E"), 0},
                                                           {Color::De("#FF17120B"), 1}});
    p["RodapeBorda"] = C("#FF4A3A22");

    // O orbe de reproduzir. Único lugar da tela com croma alto: é
    // pequeno, é redondo, e é o que a vista tem de achar primeiro.
    p["OrbePlay"] = Brush::Linear(Point(0, 0), Point(0, 1), {{Color::De("#FFE6BE7A"), 0},
                                                             {Color::De("#FFC9902F"), 0.45},
                                                             {Color::De("#FF8A5613"), 0.55},
                                                             {Color::De("#FF5E3A0C"), 1}});

    // ===== Chapa: o sépia escuro do EM EXECUÇÃO ======================
    // Não é o claro invertido. Num módulo escuro o contraste tem de CAIR,
    // não subir: creme sobre marrom escuríssimo, nunca branco sobre preto.
    // O fundo é #17120B — quente e quase preto, mas não preto: um pouco de
    // luz no fundo impede o efeito de brilho ofuscante dos traços finos dos
    // instrumentos sobre o vazio.
    //
    // E o âmbar dos instrumentos não é concessão ao sépia: monitores de
    // fósforo âmbar existiram e eram vendidos justamente como os menos
    // cansativos. A referência é essa. =================================
    p["Chapa"]            = C("#FF17120B");
    p["ChapaFunda"]       = C("#FF120E08");
    p["ChapaBorda"]       = C("#FF3A2E1C");
    p["ChapaTexto"]       = C("#FFE3D2B0");
    p["ChapaRotulo"]      = C("#FF8A7654");
    p["ChapaFraco"]       = C("#FF6A583C");
    p["Fosforo"]          = C("#FFFFB454");

    // ===================================================================
    // O QUE O WINDOWS PINTARIA DE CLARO: as cores do sistema redefinidas.
    // (No WPF a chave é SystemColors.XxxBrushKey; aqui o nome dela.)
    // ===================================================================
    p["SystemColors.WindowBrushKey"]        = Brush(SistemaJanela);
    p["SystemColors.WindowTextBrushKey"]    = Brush(SistemaTextoJanela);
    p["SystemColors.ControlBrushKey"]       = Brush(SistemaControle);
    p["SystemColors.ControlTextBrushKey"]   = Brush(SistemaTextoControle);
    p["SystemColors.ControlLightBrushKey"]  = Brush(SistemaControleClaro);
    p["SystemColors.ControlDarkBrushKey"]   = Brush(SistemaControleEscuro);
    p["SystemColors.GrayTextBrushKey"]      = Brush(SistemaTextoCinza);
    p["SystemColors.HighlightBrushKey"]     = Brush(SistemaRealce);
    p["SystemColors.HighlightTextBrushKey"] = Brush(SistemaTextoRealce);
    p["SystemColors.MenuBrushKey"]          = Brush(SistemaMenu);
    p["SystemColors.MenuTextBrushKey"]      = Brush(SistemaTextoMenu);
    p["SystemColors.InfoBrushKey"]          = Brush(SistemaDica);
    p["SystemColors.InfoTextBrushKey"]      = Brush(SistemaTextoDica);

    // ===== Estilos com chave ==========================================
    auto &e = d.estilos;
    auto estilo = [&](const char *chave, const char *tipo) -> Style & {
        Style &s = e[chave];
        s.Chave = chave;
        s.TargetType = tipo;
        return s;
    };

    // ===== Texto ======================================================
    {
        Style &s = estilo("Titulo", "TextBlock");
        s.FontFamily = Fonte;
        s.Foreground = p["Texto"];
        s.FontSize = 15;
        s.FontWeight = FontWeight::SemiBold;
    }
    {
        Style &s = estilo("Fraco", "TextBlock");
        s.FontFamily = Fonte;
        s.Foreground = p["TextoFraco"];
        s.FontSize = 12;
        s.TextWrapping = TextWrapping::Wrap;
    }
    {
        Style &s = estilo("Rotulo", "TextBlock");
        s.FontFamily = Fonte;
        s.Foreground = p["Acento"];
        s.FontSize = 11;
        s.FontWeight = FontWeight::SemiBold;
        s.Margin = Thickness(0, 0, 0, 6);
    }

    // ===== Abas de módulo ============================================
    // Não são abas de conveniência: trocam o contexto inteiro. Por isso são
    // maiores que as abas de tarefa do WMP12, mas usam o mesmo vocabulário
    // visual. =========================================================
    {
        Style &s = estilo("AbaModulo", "RadioButton");
        s.FontFamily = Fonte;
        s.Foreground = p["TextoFraco"];
        s.FontSize = 12;
        s.Margin = Thickness(0, 0, 2, 0);
        s.Template = Gabarito::AbaModulo;
        GabaritoBorda &g = d.modelos[Gabarito::AbaModulo];
        g.Fundo = p["AbaTarefa"];
        g.Borda = p["Borda"];
        g.Espessura = Thickness(1, 1, 1, 0);
        g.Cantos = CornerRadius(3, 3, 0, 0);
        g.Recuo = Thickness(22, 7);
        g.FundoSobre = C("#FF2C2214");
        g.FundoMarcado = p["BarraTrilha"];
        g.BordaMarcada = p["RodapeBorda"];
        g.TextoMarcado = p["Acento"];
        g.PesoMarcado = FontWeight::SemiBold;
    }

    // ===== Botões de transporte =======================================
    // Parar, anterior e próximo não podem ter moldura: o rodapé muda de tom
    // conforme o módulo, e um botão de fundo claro sobre o rodapé escuro
    // viraria um bloco brilhante disputando a atenção com o orbe. Aqui são
    // só o glifo, cuja cor o AplicarTomRodape troca junto com o rodapé. O
    // realce da passagem do mouse é ocre translúcido — funciona nos dois
    // tons sem precisar de duas versões. ==============================
    {
        Style &s = estilo("BotaoTransporte", "Button");
        s.FontFamily = Fonte;
        s.Foreground = p["TextoFraco"];
        s.Template = Gabarito::BotaoTransporte;
        GabaritoBorda &g = d.modelos[Gabarito::BotaoTransporte];
        g.Fundo = Brushes::Transparent;
        g.Cantos = CornerRadius(3);
        g.RecuoDoControle = true;
        g.FundoSobre = C("#33B4761C");
        g.FundoApertado = C("#55B4761C");
        g.OpacidadeDesligado = 0.4;
    }

    // ===== Aba interna da BIBLIOTECA ==================================
    // Os tipos de mídia (Tudo, Música, Vídeos, Obras). Mesmo desenho da aba
    // interna do EM EXECUÇÃO: só o texto e um traço embaixo, que acende em
    // âmbar na aba ativa. É um item de ListBox, não um RadioButton, porque
    // a seleção da categoria já era de lista. ==========================
    {
        Style &s = estilo("AbaInternaLista", "ListBoxItem");
        s.FontFamily = Fonte;
        s.Foreground = p["ChapaRotulo"];
        s.FontSize = 11;
        s.FontWeight = FontWeight::SemiBold;
        s.Margin = Thickness(0, 0, 4, 0);
        s.Template = Gabarito::AbaInternaLista;
        GabaritoBorda &g = d.modelos[Gabarito::AbaInternaLista];
        g.Fundo = Brushes::Transparent;
        g.Borda = p["ChapaBorda"];
        g.Espessura = Thickness(0, 0, 0, 2);
        g.Recuo = Thickness(14, 6);
        g.FundoSobre = C("#FF241C10");
        g.BordaMarcada = p["Fosforo"];
        g.TextoMarcado = p["Fosforo"];
        g.ConteudoV = VerticalAlignment::Stretch;
    }

    // Aba interna do EM EXECUÇÃO (ExecucaoView.xaml, UserControl.Resources):
    // escolhe qual visualizador aparece. Versão escura do vocabulário de
    // abas do resto do app — reservada, com o ocre marcando a ativa.
    // Fica aqui porque o desenho é o mesmo da AbaInternaLista; a ExecucaoView
    // a pede por Tema::Estilo("AbaInterna").
    {
        Style &s = estilo("AbaInterna", "RadioButton");
        s.Foreground = C("#FF8A7654");
        s.FontSize = 11;
        s.FontWeight = FontWeight::SemiBold;
        s.Margin = Thickness(0, 0, 4, 0);
        s.Template = Gabarito::AbaInterna;
        GabaritoBorda &g = d.modelos[Gabarito::AbaInterna];
        g.Fundo = Brushes::Transparent;
        g.Borda = C("#FF3A2E1C");
        g.Espessura = Thickness(0, 0, 0, 2);
        g.Recuo = Thickness(12, 6);
        g.FundoSobre = C("#FF241C10");
        g.BordaMarcada = C("#FFFFB454");
        g.TextoMarcado = C("#FFFFB454");
        g.ConteudoV = VerticalAlignment::Stretch;
    }

    // ===== Lista de álbuns da BIBLIOTECA ==============================
    // Discreta como a lista de faixas: seleção por valor, e o âmbar só no
    // texto do álbum escolhido. O <todos> vai em itálico — não é
    // um álbum, é a ausência de filtro. (O itálico é do DataTrigger
    // Binding Todos: quem monta o item põe FontStyle = Italic.) ========
    {
        Style &s = estilo("ItemAlbum", "ListBoxItem");
        s.FontFamily = Fonte;
        s.Foreground = p["Texto"];
        s.FontSize = 12;
        s.Template = Gabarito::ItemAlbum;
        GabaritoBorda &g = d.modelos[Gabarito::ItemAlbum];
        g.Fundo = Brushes::Transparent;
        g.Recuo = Thickness(12, 4, 8, 4);
        g.FundoSobre = C("#FF241C10");
        g.FundoMarcado = C("#FF2E2415");
        g.TextoMarcado = p["Acento"];
        g.Conteudo = HorizontalAlignment::Stretch;
        g.ConteudoV = VerticalAlignment::Stretch;
    }

    // ===== Lista da biblioteca ========================================
    // Seleção e passagem do mouse: separadas por valor, não por cor. É uma
    // lista de milhares de linhas que se percorre com o olho — tudo aqui é
    // deliberadamente discreto. =======================================
    {
        Style &s = estilo("LinhaBiblioteca", "ListViewItem");
        s.FontFamily = Fonte;
        s.Foreground = p["Texto"];
        s.FontSize = 12;
        s.Template = Gabarito::LinhaBiblioteca;
        GabaritoBorda &g = d.modelos[Gabarito::LinhaBiblioteca];
        g.Fundo = Brushes::Transparent;
        g.Recuo = Thickness(0, 4);
        g.FundoSobre = Brush(LinhaSobre);
        g.FundoMarcado = Brush(LinhaSelecionada);
        g.Conteudo = HorizontalAlignment::Stretch;
        g.ConteudoV = VerticalAlignment::Stretch;
    }

    // Caixa de edição dentro da célula: a grade não usa o estilo de
    // TextBox acima, e o cursor padrão sairia preto no fundo escuro.
    {
        Style &s = estilo("EdicaoCelula", "TextBox");
        s.BorderThickness = Thickness(0);
        s.Padding = Thickness(0);
        s.Background = Brushes::Transparent;
        s.Foreground = p["Texto"];
        s.CaretBrush = p["Fosforo"];
        s.SelectionBrush = p["AcentoVivo"];
    }

    // ===== Estilos implícitos (por tipo) ==============================
    auto &i = d.implicitos;
    auto implicito = [&](const char *tipo) -> Style & {
        Style &s = i[tipo];
        s.TargetType = tipo;
        return s;
    };

    // ===== Botão ======================================================
    {
        Style &s = implicito("Button");
        s.FontFamily = Fonte;
        s.Foreground = p["Texto"];
        s.FontSize = 12;
        s.Padding = Thickness(12, 5);
        s.Template = Gabarito::Button;
        GabaritoBorda &g = d.modelos[Gabarito::Button];
        g.Fundo = p["AbaTarefa"];
        g.Borda = p["Borda"];
        g.Espessura = Thickness(1);
        g.Cantos = CornerRadius(3);
        g.RecuoDoControle = true;
        g.FundoSobre = C("#FF2C2214");
        g.BordaSobre = p["RodapeBorda"];
        g.FundoApertado = C("#FF3A2E1C");
        g.TextoDesligado = C("#FF5A4A32");
    }

    // O gabarito do BtnTocar (MainWindow.xaml): dois Ellipse e o conteúdo.
    // O desenho fica em Controles.cpp; aqui só o que é dado.
    {
        GabaritoBorda &g = d.modelos[Gabarito::OrbePlay];
        g.Fundo = p["OrbePlay"];
        g.Borda = C("#FF6B4310");
        g.Espessura = Thickness(1);
    }

    // ===== Caixa de texto =============================================
    // Mais funda que o painel em volta: o campo editável se reconhece por
    // ser um "buraco" na chapa. O cursor é âmbar — o padrão do Windows é
    // preto e sumiria no fundo escuro. ==================================
    {
        Style &s = implicito("TextBox");
        s.FontFamily = Fonte;
        s.Background = Brush(CaixaFundo);
        s.CaretBrush = p["Fosforo"];
        s.SelectionBrush = p["AcentoVivo"];
        s.Foreground = p["Texto"];
        s.BorderBrush = p["Borda"];
        s.BorderThickness = Thickness(1);
        s.Padding = Thickness(5, 3);
        s.FontSize = 12;
    }

    // ===== Caixa de marcar e botão de opção ===========================
    // A caixa de marcar ganhou desenho próprio: a do Aero é um quadrado
    // branco, e mesmo pequeno acendia na tela escura. O botão de opção só
    // troca o texto — os que o ARCA usa já têm estilo de aba.
    {
        Style &s = implicito("CheckBox");
        s.FontFamily = Fonte;
        s.Foreground = p["Texto"];
        s.Template = Gabarito::CheckBox;
        GabaritoBorda &g = d.modelos[Gabarito::CheckBox];
        g.Fundo = Brush(CaixaFundo);
        g.Borda = p["RodapeBorda"];
        g.Espessura = Thickness(1);
        g.Cantos = CornerRadius(2);
        g.BordaSobre = p["ChapaRotulo"];
        g.TextoMarcado = p["Fosforo"];          // a cor da marca (Path Stroke)
        g.OpacidadeDesligado = 0.4;
    }
    {
        Style &s = implicito("RadioButton");
        s.FontFamily = Fonte;
        s.Foreground = p["Texto"];
        s.Template = Gabarito::RadioButton;
        d.modelos[Gabarito::RadioButton].TextoDesligado = Brush(SistemaTextoCinza);
    }

    // ===== Dica (ToolTip) =============================================
    {
        Style &s = implicito("ToolTip");
        s.FontFamily = Fonte;
        s.FontSize = 12;
        s.Foreground = p["Texto"];
        s.Background = p["PainelLateral"];
        s.BorderBrush = p["Borda"];
        s.BorderThickness = Thickness(1);
        s.Padding = DicaRecuo;
    }

    // ===== Barra de rolagem ===========================================
    // Fina e sem setas: o polegar é a única coisa que a vista precisa achar,
    // e ele só clareia sob o mouse. A barra cinza do Aero, de 17 pixels,
    // era a maior área clara que sobraria na tela. =====================
    {
        Style &s = implicito("ScrollBar");
        s.Background = p["ChapaFunda"];
    }

    // ===== Lista suspensa =============================================
    {
        Style &s = implicito("ComboBoxItem");
        s.FontFamily = Fonte;
        s.Foreground = p["Texto"];
        s.Template = Gabarito::ComboBoxItem;
        GabaritoBorda &g = d.modelos[Gabarito::ComboBoxItem];
        g.Fundo = Brushes::Transparent;
        g.Recuo = ComboItemRecuo;
        g.FundoSobre = Brush(ComboItemRealce);   // IsHighlighted
        g.TextoMarcado = p["Acento"];            // IsSelected
        g.Conteudo = HorizontalAlignment::Stretch;
    }
    {
        Style &s = implicito("ComboBox");
        s.FontFamily = Fonte;
        s.FontSize = 12;
        s.Foreground = p["Texto"];
        s.Padding = ComboRecuo;
        s.Background = p["AbaTarefa"];
        s.BorderBrush = p["Borda"];
    }

    // ===== Grade (DataGrid) ===========================================
    // Cabeçalho e célula próprios: o Aero desenha o cabeçalho em degradê
    // claro e a célula selecionada em azul, ambos fixos no modelo. A linha
    // em si não tem realce de passagem do mouse — a grade calcula o fundo
    // da linha (alternado ou não) e passaria por cima de qualquer gatilho.
    {
        Style &s = implicito("DataGrid");
        s.FontFamily = Fonte;
        s.FontSize = 12;
        s.Foreground = p["Texto"];
        s.Background = p["FundoLista"];
        s.BorderBrush = p["Borda"];
    }
    {
        Style &s = implicito("DataGridColumnHeader");
        s.FontFamily = Fonte;
        s.FontSize = 12;
        s.Foreground = p["TextoFraco"];
        s.Background = p["CabecalhoColuna"];
        s.BorderBrush = p["LinhaCabecalho"];
        s.Padding = CabecalhoGradeRecuo;
    }
    {
        Style &s = implicito("GridViewColumnHeader");
        s.FontFamily = Fonte;
        s.Background = p["CabecalhoColuna"];
        s.Foreground = p["Texto"];
        s.FontSize = 12;
        s.FontWeight = FontWeight::Normal;
        s.Padding = CabecalhoListaRecuo;
        s.BorderBrush = p["LinhaCabecalho"];
        s.BorderThickness = Thickness(0, 0, 1, 1);
    }

    // ===== Deslizante ================================================
    // Sem croma, como no WMP12 a barra era cinza: aqui é marrom dessaturado.
    // O ocre fica reservado ao orbe e à faixa em execução. =============
    {
        Style &s = implicito("Slider");
        s.Background = p["ProgressoRestante"];
        s.Foreground = p["ProgressoTocado"];
    }

    // O ListBoxItem e o ToggleButton do Aero, com as cores de sistema do Tema
    {
        GabaritoBorda &g = d.modelos[Gabarito::ListBoxItem];
        g.Fundo = Brushes::Transparent;
        g.Recuo = Thickness(2, 0, 0, 0);
        g.FundoMarcado = Brush(SistemaRealce);
        g.TextoMarcado = Brush(SistemaTextoRealce);
        g.Conteudo = HorizontalAlignment::Stretch;
        g.ConteudoV = VerticalAlignment::Stretch;
    }
    {
        GabaritoBorda &g = d.modelos[Gabarito::ToggleButton];
        g = d.modelos[Gabarito::Button];
        g.FundoMarcado = C("#FF3A2E1C");
        g.BordaMarcada = p["RodapeBorda"];
    }
    d.modelos[Gabarito::Nenhum] = GabaritoBorda();
    return d;
}

std::mutex trava;

Dicionario &Dic()
{
    static Dicionario d = Monta();
    return d;
}

} // namespace

Brush Recurso(const std::string &chave)
{
    std::lock_guard<std::mutex> g(trava);
    auto &p = Dic().pinceis;
    auto it = p.find(chave);
    if (it != p.end()) return it->second;
    fprintf(stderr, "xos-arca: Tema: o recurso \"%s\" nao existe\n", chave.c_str());
    return Brush();
}

bool TemRecurso(const std::string &chave)
{
    std::lock_guard<std::mutex> g(trava);
    return Dic().pinceis.count(chave) > 0;
}

const Style *Estilo(const std::string &chave)
{
    std::lock_guard<std::mutex> g(trava);
    auto &e = Dic().estilos;
    auto it = e.find(chave);
    if (it != e.end()) return &it->second;
    fprintf(stderr, "xos-arca: Tema: o estilo \"%s\" nao existe\n", chave.c_str());
    return nullptr;
}

const Style *EstiloPadrao(const std::string &tipo)
{
    std::lock_guard<std::mutex> g(trava);
    auto &i = Dic().implicitos;
    auto it = i.find(tipo);
    return it != i.end() ? &it->second : nullptr;
}

void RegistraEstilo(const Style &estilo)
{
    std::lock_guard<std::mutex> g(trava);
    // std::map nao move os nos: os ponteiros ja entregues continuam valendo
    Dic().estilos[estilo.Chave] = estilo;
}

void RegistraRecurso(const std::string &chave, const Brush &pincel)
{
    std::lock_guard<std::mutex> g(trava);
    Dic().pinceis[chave] = pincel;
}

const GabaritoBorda &Modelo(Gabarito g)
{
    std::lock_guard<std::mutex> t(trava);
    auto &m = Dic().modelos;
    auto it = m.find(g);
    return it != m.end() ? it->second : m[Gabarito::Nenhum];
}

} // namespace Tema
} // namespace ARCA::Vistas

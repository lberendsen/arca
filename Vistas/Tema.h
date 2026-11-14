// ARCA no xOS - Vistas/Tema (de Tema.xaml). A paleta escura e ambar ("Tudo escuro", 03/10/2026) e os
// estilos e gabaritos de controle do ARCA, com as MESMAS chaves do ResourceDictionary original.
//
//   (Brush)FindResource("Canvas")     ->  Tema::Recurso("Canvas")
//   (Style)FindResource("Fraco")      ->  Tema::Estilo("Fraco")
//   {StaticResource Fonte}            ->  Tema::Fonte  ("Fonte"; o Segoe UI e o Noto Sans aqui)
//
// Um Style e uma lista de "setters" opcionais: o que ele nao define fica com o valor herdado ou com
// o do controle. Valor posto direto no elemento vence o do estilo, como no WPF, nao importa a ordem
// em que as duas coisas foram escritas. Os gabaritos (ControlTemplate) sao dados: fundo, borda,
// cantos, recuo e o que cada gatilho (mouse por cima, apertado, marcado, desligado) troca. Quem os
// desenha e Controles.cpp.
#pragma once

#include "Wpf.h"

#include <optional>
#include <string>

namespace ARCA::Vistas {

// Os ControlTemplate do Tema.xaml (e os dois que a MainWindow.xaml e a ExecucaoView.xaml definem
// no lugar). Nenhum = o controle desenha so o conteudo.
enum class Gabarito
{
    Nenhum,
    Button,             // <Style TargetType="Button">
    BotaoTransporte,    // x:Key="BotaoTransporte"
    AbaModulo,          // x:Key="AbaModulo" (RadioButton)
    AbaInternaLista,    // x:Key="AbaInternaLista" (ListBoxItem)
    AbaInterna,         // ExecucaoView.xaml, UserControl.Resources, x:Key="AbaInterna" (RadioButton)
    ItemAlbum,          // x:Key="ItemAlbum" (ListBoxItem)
    LinhaBiblioteca,    // x:Key="LinhaBiblioteca" (ListViewItem)
    ListBoxItem,        // o ListBoxItem do Aero (sem estilo no Tema): selecao na cor de sistema
    ComboBoxItem,       // <Style TargetType="ComboBoxItem">
    OrbePlay,           // o Button.Template do BtnTocar (MainWindow.xaml)
    CheckBox,           // <Style TargetType="CheckBox">
    RadioButton,        // o do Aero: o Tema so troca o texto
    ToggleButton,       // o do Aero, com as cores de sistema do Tema
};

// Um ControlTemplate do tipo "Border com gatilhos". Pincel vazio (Brush()) = o gatilho nao mexe
// nessa propriedade. Os gatilhos valem na ordem do XAML: mouse por cima, apertado, marcado.
struct GabaritoBorda
{
    Brush Fundo, Borda;
    Thickness Espessura;
    ARCA::Vistas::CornerRadius Cantos;
    Thickness Recuo;                       // Padding do Border do gabarito (quando fixo no gabarito)
    bool RecuoDoControle = false;          // Padding="{TemplateBinding Padding}"
    Brush FundoSobre, BordaSobre;          // IsMouseOver
    Brush FundoApertado;                   // IsPressed
    Brush FundoMarcado, BordaMarcada;      // IsChecked / IsSelected
    Brush TextoMarcado;                    // Setter Property="Foreground" no gatilho de marcado
    std::optional<ARCA::Vistas::FontWeight> PesoMarcado;
    Brush TextoDesligado;                  // IsEnabled=False -> Foreground
    double OpacidadeDesligado = 1;         // IsEnabled=False -> Opacity
    ARCA::Vistas::HorizontalAlignment Conteudo = ARCA::Vistas::HorizontalAlignment::Center;
    ARCA::Vistas::VerticalAlignment ConteudoV = ARCA::Vistas::VerticalAlignment::Center;
};

struct Style
{
    std::string Chave;        // x:Key ("" no estilo implicito)
    std::string TargetType;
    std::optional<std::string> FontFamily;
    std::optional<Brush> Foreground, Background, BorderBrush, CaretBrush, SelectionBrush;
    std::optional<double> FontSize;
    std::optional<ARCA::Vistas::FontWeight> FontWeight;
    std::optional<ARCA::Vistas::FontStyle> FontStyle;
    std::optional<Thickness> Margin, Padding, BorderThickness;
    std::optional<ARCA::Vistas::TextWrapping> TextWrapping;
    std::optional<ARCA::Vistas::Gabarito> Template;
};

namespace Tema {

// as familias de fonte do Tema (FontFamily="{StaticResource Fonte}")
inline const std::string Fonte = "Fonte";            // Segoe UI      -> Noto Sans
inline const std::string FonteMono = "FonteMono";    // Consolas      -> Noto Sans Mono

// FindResource para pincel. Chave que nao existe: avisa no stderr e devolve o pincel vazio (o WPF
// lancaria ResourceReferenceKeyNotFoundException).
Brush Recurso(const std::string &chave);
bool TemRecurso(const std::string &chave);
// FindResource para estilo; nullptr se nao existe (com aviso)
const Style *Estilo(const std::string &chave);
// o estilo implicito de um tipo (<Style TargetType="Button"> sem x:Key); nullptr se nao ha
const Style *EstiloPadrao(const std::string &tipo);
// os UserControl.Resources de uma vista (a ExecucaoView define RotuloInstrumento, Leitura...):
// a vista registra os dela no construtor e passa a pedi-los por Tema::Estilo como os outros
void RegistraEstilo(const Style &estilo);
void RegistraRecurso(const std::string &chave, const Brush &pincel);

// o desenho de cada gabarito
const GabaritoBorda &Modelo(Gabarito g);

// ---- valores fixos dos gabaritos implicitos (os numeros do Tema.xaml) ----
// TextBox
inline const Color CaixaFundo = Color::De("#FF0E0B06");
// Slider
constexpr double SliderAltura = 16, SliderTrilho = 3, SliderPolegar = 10;
inline const Color SliderPolegarFundo = Color::De("#FFE3D2B0");
inline const Color SliderPolegarBorda = Color::De("#FF8C7A5E");
// ScrollBar
constexpr double BarraLargura = 11, BarraPolegarMinimo = 24, BarraPolegarMargem = 2, BarraPolegarCanto = 3;
inline const Color BarraPolegar = Color::De("#FF3A2E1C");
inline const Color BarraPolegarSobre = Color::De("#FF6A583C");
inline const Color BarraPolegarArrasto = Color::De("#FF8A7654");
// CheckBox
constexpr double CaixaMarcarLado = 13;
// ComboBox
inline const Thickness ComboRecuo = Thickness(7, 4, 26, 4);
inline const Thickness ComboItemRecuo = Thickness(7, 4);
inline const Color ComboItemRealce = Color::De("#FF2C2214");
constexpr double ComboAlturaMaxima = 360;      // MaxDropDownHeight padrao (1/3 da tela de 1080)
// DataGrid
inline const Thickness CabecalhoGradeRecuo = Thickness(6, 5);
inline const Color CabecalhoGradeSobre = Color::De("#FF241C10");
inline const Color CelulaSelecionada = Color::De("#FF33281A");
inline const Color CelulaEditando = Color::De("#FF0E0B06");
constexpr double GarraColuna = 6;
// GridViewColumnHeader (ListView)
inline const Thickness CabecalhoListaRecuo = Thickness(8, 5);
// LinhaBiblioteca (ListViewItem)
inline const Color LinhaSobre = Color::De("#FF1F180F");
inline const Color LinhaSelecionada = Color::De("#FF33281A");
// ToolTip
inline const Thickness DicaRecuo = Thickness(8, 5);
constexpr double DicaLarguraMaxima = 480;
// as cores de sistema redefinidas (SystemColors.*BrushKey)
inline const Color SistemaJanela = Color::De("#FF120E08");
inline const Color SistemaTextoJanela = Color::De("#FFE3D2B0");
inline const Color SistemaControle = Color::De("#FF1C160D");
inline const Color SistemaTextoControle = Color::De("#FFE3D2B0");
inline const Color SistemaControleClaro = Color::De("#FF2A2115");
inline const Color SistemaControleEscuro = Color::De("#FF3A2E1C");
inline const Color SistemaTextoCinza = Color::De("#FF5A4A32");
inline const Color SistemaRealce = Color::De("#FF33281A");
inline const Color SistemaTextoRealce = Color::De("#FFE3D2B0");
inline const Color SistemaMenu = Color::De("#FF1C160D");
inline const Color SistemaTextoMenu = Color::De("#FFE3D2B0");
inline const Color SistemaDica = Color::De("#FF1C160D");
inline const Color SistemaTextoDica = Color::De("#FFE3D2B0");

} // namespace Tema

} // namespace ARCA::Vistas

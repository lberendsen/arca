// ARCA no xOS - Vistas/JanelaDivisaoImagem (de Vistas\JanelaDivisaoImagem.cs). Ver JanelaDivisaoImagem.h.
#include "JanelaDivisaoImagem.h"

#include <algorithm>
#include <cmath>

namespace ARCA::Vistas {

JanelaDivisaoImagem::JanelaDivisaoImagem(std::shared_ptr<BitmapSource> objImagem, const std::string &strTitulo)
{
    m_objImagem = objImagem;
    m_intLinhaX = objImagem->PixelWidth / 2;

    Title = strTitulo.empty() ? "Dividir imagem" : strTitulo;
    Width = 900;
    Height = 760;
    MinWidth = 600;
    MinHeight = 500;
    WindowStartupLocation = Vistas::WindowStartupLocation::CenterOwner;
    ShowInTaskbar = false;
    Background = Tema::Recurso("Canvas");

    m_objVista = std::make_shared<Image>();
    // LINUX: a vista e uma copia de no maximo 2048 px (o BitmapScalingMode.HighQuality do WPF; ver o
    // mesmo comentario em JanelaCorteImagem::DefinirImagem). A divisao e feita na imagem plena.
    int intMaior = std::max(objImagem->PixelWidth, objImagem->PixelHeight);
    m_objVista->Source = intMaior > 2048 ? objImagem->Escalada(2048.0 / intMaior) : objImagem;
    m_objVista->Stretch = Stretch::Fill;
    m_objLinha = std::make_shared<Line>();
    m_objLinha->Stroke = Tema::Recurso("AcentoVivo");
    m_objLinha->StrokeThickness = 2;
    m_objLinha->IsHitTestVisible = false;
    m_objPalco = std::make_shared<Canvas>();
    m_objPalco->Background = Brushes::Transparent;
    m_objPalco->ClipToBounds = true;
    m_objPalco->Cursor = Cursors::SizeWE;
    m_objPalco->Children.Add(m_objVista);
    m_objPalco->Children.Add(m_objLinha);

    m_txtTamanho = std::make_shared<TextBlock>();
    m_txtTamanho->Style = Tema::Estilo("Fraco");
    m_txtTamanho->VerticalAlignment = VerticalAlignment::Center;

    auto objAreaPalco = std::make_shared<Border>();
    objAreaPalco->Background = Tema::Recurso("ChapaFunda");
    objAreaPalco->BorderBrush = Tema::Recurso("Borda");
    objAreaPalco->BorderThickness = Thickness(1);
    objAreaPalco->Margin = Thickness(14);
    objAreaPalco->SetChild(m_objPalco);
    objAreaPalco->SizeChanged += [this](SizeChangedEventArgs &) { Diagramar(); };

    auto objRaiz = std::make_shared<DockPanel>();
    objRaiz->Children.Add(MontarBarra());
    objRaiz->Children.Add(MontarRodape());
    objRaiz->Children.Add(objAreaPalco);
    Content = objRaiz;

    m_objPalco->MouseLeftButtonDown += [this](MouseButtonEventArgs &e) { AoApertar(e); };
    m_objPalco->MouseMove += [this](MouseEventArgs &e) { AoMover(e); };
    m_objPalco->MouseLeftButtonUp += [this](MouseButtonEventArgs &) { Soltar(); };
    m_objPalco->LostMouseCapture += [this](RoutedEventArgs &) { m_blnArrastando = false; };
    PreviewKeyDown += [this](KeyEventArgs &e) { AoTeclar(e); };
}

// ===============================================================
UIElementPtr JanelaDivisaoImagem::MontarBarra()
{
    auto objBarra = std::make_shared<StackPanel>();
    objBarra->Orientation = Orientation::Horizontal;
    auto objMeio = std::make_shared<Button>();
    objMeio->Content = "Voltar ao meio";
    objMeio->Padding = Thickness(10, 3, 10, 3);
    objMeio->Margin = Thickness(0, 0, 14, 0);
    objMeio->Click += [this](RoutedEventArgs &) { MoverLinha(m_objImagem->PixelWidth / 2); };
    objBarra->Children.Add(objMeio);
    objBarra->Children.Add(m_txtTamanho);

    auto objBorda = std::make_shared<Border>();
    objBorda->Background = Tema::Recurso("BarraFerramentas");
    objBorda->BorderBrush = Tema::Recurso("Borda");
    objBorda->BorderThickness = Thickness(0, 0, 0, 1);
    objBorda->Padding = Thickness(14, 8, 14, 8);
    objBorda->SetChild(objBarra);
    DockPanel::SetDock(objBorda, Dock::Top);
    return objBorda;
}

UIElementPtr JanelaDivisaoImagem::MontarRodape()
{
    auto objGrade = std::make_shared<Grid>();
    objGrade->ColumnDefinitions.push_back(ColumnDefinition());
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));
    objGrade->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));
    auto objAjuda = std::make_shared<TextBlock>("Clique ou arraste para pôr a linha na dobra. As setas movem 1 px; com Shift, 10 px.");
    objAjuda->Style = Tema::Estilo("Fraco");
    objAjuda->VerticalAlignment = VerticalAlignment::Center;
    objAjuda->TextWrapping = TextWrapping::Wrap;
    objGrade->Children.Add(objAjuda);
    auto objCancelar = std::make_shared<Button>();
    objCancelar->Content = "Cancelar";
    objCancelar->IsCancel = true;
    objCancelar->Padding = Thickness(18, 6, 18, 6);
    objCancelar->Margin = Thickness(12, 0, 0, 0);
    objCancelar->Click += [this](RoutedEventArgs &) { Esquerda = nullptr; Direita = nullptr; Close(); };
    auto objDividir = std::make_shared<Button>();
    objDividir->Content = "Dividir em duas";
    objDividir->IsDefault = true;
    objDividir->Padding = Thickness(18, 6, 18, 6);
    objDividir->Margin = Thickness(8, 0, 0, 0);
    objDividir->Click += [this](RoutedEventArgs &) { AoDividir(); };
    Grid::SetColumn(objCancelar, 1);
    Grid::SetColumn(objDividir, 2);
    objGrade->Children.Add(objCancelar);
    objGrade->Children.Add(objDividir);

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
void JanelaDivisaoImagem::AoDividir()
{
    int intAltura = m_objImagem->PixelHeight;
    // dois CroppedBitmap
    Esquerda = m_objImagem->Cortada(Int32Rect(0, 0, m_intLinhaX, intAltura));
    Direita = m_objImagem->Cortada(Int32Rect(m_intLinhaX, 0, m_objImagem->PixelWidth - m_intLinhaX, intAltura));
    Close();
}

/// <summary>Leva a linha a uma coluna da imagem, sem deixar nenhum lado menor que o mínimo.</summary>
void JanelaDivisaoImagem::MoverLinha(int intX)
{
    m_intLinhaX = std::max(intLadoMinimo, std::min(m_objImagem->PixelWidth - intLadoMinimo, intX));
    Desenhar();
}

/// <summary>Ajusta a imagem ao espaço disponível, sem distorcer, e redesenha a linha.</summary>
void JanelaDivisaoImagem::Diagramar()
{
    double dblLargura = m_objPalco->ActualWidth, dblAltura = m_objPalco->ActualHeight;
    if (dblLargura <= 0 || dblAltura <= 0) return;
    m_dblEscala = std::min(dblLargura / m_objImagem->PixelWidth, dblAltura / m_objImagem->PixelHeight);
    m_objVista->Width = m_objImagem->PixelWidth * m_dblEscala;
    m_objVista->Height = m_objImagem->PixelHeight * m_dblEscala;
    Canvas::SetLeft(m_objVista, (dblLargura - m_objVista->Width) / 2);
    Canvas::SetTop(m_objVista, (dblAltura - m_objVista->Height) / 2);
    Desenhar();
}

void JanelaDivisaoImagem::Desenhar()
{
    double dblEsquerda = Canvas::GetLeft(m_objVista), dblTopo = Canvas::GetTop(m_objVista);
    if (!std::isnan(dblEsquerda))
    {
        double dblX = dblEsquerda + m_intLinhaX * m_dblEscala;
        m_objLinha->X1 = dblX;
        m_objLinha->X2 = dblX;
        m_objLinha->Y1 = dblTopo;
        m_objLinha->Y2 = dblTopo + m_objVista->Height;
    }
    m_txtTamanho->Text = "Esquerda: " + std::to_string(m_intLinhaX) + " x " + std::to_string(m_objImagem->PixelHeight) +
                         " px    Direita: " + std::to_string(m_objImagem->PixelWidth - m_intLinhaX) + " x " +
                         std::to_string(m_objImagem->PixelHeight) + " px";
    InvalidateVisual();
}

int JanelaDivisaoImagem::ColunaDaImagem(Point objTela) const
{
    return (int) std::nearbyint((objTela.X - Canvas::GetLeft(m_objVista)) / m_dblEscala);
}

// ===============================================================
void JanelaDivisaoImagem::AoApertar(MouseButtonEventArgs &e)
{
    m_blnArrastando = true;
    m_objPalco->CaptureMouse();
    MoverLinha(ColunaDaImagem(e.GetPosition(m_objPalco.get())));
}

void JanelaDivisaoImagem::AoMover(MouseEventArgs &e)
{
    if (!m_blnArrastando) return;
    if (e.LeftButton != MouseButtonState::Pressed) { Soltar(); return; }
    MoverLinha(ColunaDaImagem(e.GetPosition(m_objPalco.get())));
}

void JanelaDivisaoImagem::Soltar()
{
    m_blnArrastando = false;
    if (m_objPalco->IsMouseCaptured()) m_objPalco->ReleaseMouseCapture();
}

void JanelaDivisaoImagem::AoTeclar(KeyEventArgs &e)
{
    if (e.Key != Key::Left && e.Key != Key::Right) return;
    int intPasso = Tem(Keyboard::Modifiers(), ModifierKeys::Shift) ? 10 : 1;
    MoverLinha(m_intLinhaX + (e.Key == Key::Left ? -intPasso : intPasso));
    e.Handled = true;
}

} // namespace ARCA::Vistas

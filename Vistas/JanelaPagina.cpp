// ARCA no xOS - Vistas/JanelaPagina (de Vistas\JanelaPagina.cs).
#include "JanelaPagina.h"

#include "Base/Arquivo.h"
#include "Modelo/Formato.h"

#include <algorithm>
#include <cmath>

namespace ARCA::Vistas {

JanelaPagina::JanelaPagina(const std::vector<std::string> &arquivos, int inicial) : _arquivos(arquivos)
{
    _indice = std::max(0, std::min(inicial, (int) arquivos.size() - 1));

    Title = "Encarte";
    WindowStyle = Vistas::WindowStyle::None;
    ResizeMode = Vistas::ResizeMode::NoResize;
    WindowState = Vistas::WindowState::Maximized;
    Background = SolidColorBrush(Color::FromRgb(0x14, 0x12, 0x10));
    ShowInTaskbar = false;

    _img = std::make_shared<Image>();
    _img->Stretch = Vistas::Stretch::None;
    _img->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    _img->VerticalAlignment = Vistas::VerticalAlignment::Center;
    // LINUX: RenderOptions.SetBitmapScalingMode(HighQuality): o NanoVG reduz com filtro linear

    _area = std::make_shared<ScrollViewer>();
    _area->HorizontalScrollBarVisibility = ScrollBarVisibility::Auto;
    _area->VerticalScrollBarVisibility = ScrollBarVisibility::Auto;
    _area->Background = SolidColorBrush(Color::FromRgb(0x14, 0x12, 0x10));
    _area->Content = _img;

    _legenda = std::make_shared<TextBlock>();
    _legenda->Foreground = SolidColorBrush(Color::FromRgb(0x8A, 0x76, 0x54));
    _legenda->FontSize = 12;
    _legenda->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    _legenda->VerticalAlignment = Vistas::VerticalAlignment::Bottom;
    _legenda->Margin = Thickness(0, 0, 0, 14);
    _legenda->IsHitTestVisible = false;

    auto raiz = std::make_shared<Grid>();
    raiz->Children.Add(_area);
    raiz->Children.Add(_legenda);
    Content = raiz;

    PreviewKeyDown += [this](KeyEventArgs &e) { AoTeclar(e); };
    _area->PreviewMouseWheel += [this](MouseWheelEventArgs &e) { AoGirarRoda(e); };

    // Fecha com um clique. A verificação de deslocamento existe para
    // que arrastar — agora ou quando houver arrasto para deslocar a
    // imagem — não seja confundido com clique.
    _area->PreviewMouseLeftButtonDown += [this](MouseButtonEventArgs &e) { _ondeDesceu = e.GetPosition(this); };
    _area->PreviewMouseLeftButtonUp += [this](MouseButtonEventArgs &e) {
        // Clique na barra de rolagem é rolagem, não "fechar".
        // LINUX: a barra nao e um elemento: e a faixa do ScrollViewer fora da area visivel
        Point q = e.GetPosition(_area.get());
        if (q.X >= _area->ViewportWidth() || q.Y >= _area->ViewportHeight()) return;
        Point p = e.GetPosition(this);
        if (std::abs(p.X - _ondeDesceu.X) < 4 && std::abs(p.Y - _ondeDesceu.Y) < 4)
            Close();
    };

    Loaded += [this](RoutedEventArgs &) { Mostrar(); };
}

void JanelaPagina::AoTeclar(KeyEventArgs &e)
{
    switch (e.Key)
    {
    case Key::Escape: Close(); e.Handled = true; break;
    case Key::Right:
    case Key::PageDown:
    case Key::Space: Ir(+1); e.Handled = true; break;
    case Key::Left:
    case Key::PageUp: Ir(-1); e.Handled = true; break;
    // Pelo mesmo caminho das setas: sem zerar a largura decodificada,
    // a imagem não era recarregada e continuava a página anterior.
    case Key::Home: Ir(-_indice); e.Handled = true; break;
    case Key::End: Ir((int) _arquivos.size() - 1 - _indice); e.Handled = true; break;
    case Key::Add:
    case Key::OemPlus: Zoom(1.25); e.Handled = true; break;
    case Key::Subtract:
    case Key::OemMinus: Zoom(1 / 1.25); e.Handled = true; break;
    case Key::D0: _zoom = 1.0; _larguraDecodificada = 0; Mostrar(); e.Handled = true; break;
    default: break;
    }
}

/// <summary>
/// Ampliar ancorado no ponto sob o cursor.
///
/// Sem isto, mudar o tamanho da imagem faz o ScrollViewer manter o
/// canto superior esquerdo parado, e o que se está olhando escorrega na
/// diagonal. O conserto é guardar em que fração do conteúdo o cursor
/// está, ampliar, e reposicionar a rolagem para que a mesma fração
/// continue embaixo dele.
/// </summary>
void JanelaPagina::AoGirarRoda(MouseWheelEventArgs &e)
{
    e.Handled = true;

    Point p = e.GetPosition(_area.get());
    double fx = (_area->HorizontalOffset() + p.X) / std::max(1.0, _area->ExtentWidth());
    double fy = (_area->VerticalOffset() + p.Y) / std::max(1.0, _area->ExtentHeight());

    if (!Zoom(e.Delta > 0 ? 1.25 : 1 / 1.25)) return;

    _area->UpdateLayout();   // o extent só muda depois de medir
    _area->ScrollToHorizontalOffset(fx * _area->ExtentWidth() - p.X);
    _area->ScrollToVerticalOffset(fy * _area->ExtentHeight() - p.Y);
}

void JanelaPagina::Ir(int passo)
{
    int novo = _indice + passo;
    if (novo < 0 || novo >= (int) _arquivos.size()) return;
    _indice = novo;
    _zoom = 1.0;
    _larguraDecodificada = 0;      // a próxima página começa ajustada
    Mostrar();
}

bool JanelaPagina::Zoom(double fator)
{
    double novo = std::max(1.0, std::min(8.0, _zoom * fator));
    if (std::abs(novo - _zoom) < 0.001) return false;
    _zoom = novo;
    Mostrar();
    return true;
}

void JanelaPagina::Mostrar()
{
    if (_arquivos.empty()) return;
    double disponivel = std::max(200.0, ActualWidth);
    int alvo = (int) std::min((double) Teto, std::ceil(disponivel * _zoom));

    // Só redecodifica para ganhar resolução. Diminuir o zoom reaproveita
    // a imagem maior, que a placa reduz de graça.
    if (!_img->Source || alvo > _larguraDecodificada)
    {
        std::shared_ptr<BitmapSource> bmp = Abrir(_arquivos[(size_t) _indice], alvo);
        if (!bmp) return;
        _img->Source = bmp;
        _larguraDecodificada = bmp->PixelWidth;
    }

    const BitmapSource &fonte = *_img->Source;

    // Ajustado: cabe inteiro na tela. Com zoom, cresce a partir daí.
    double escalaAjuste = std::min(ActualWidth / fonte.PixelWidth, ActualHeight / fonte.PixelHeight);
    double escala = escalaAjuste * _zoom;
    _img->Width = fonte.PixelWidth * escala;
    _img->Height = fonte.PixelHeight * escala;

    _legenda->Text = Base::NomeSemExtensao(_arquivos[(size_t) _indice]) + "   ·   " + std::to_string(_indice + 1) + " de " +
                     std::to_string(_arquivos.size()) +
                     (_zoom > 1.001 ? "   ·   " + Modelo::Formato::Num(_zoom * 100, 0) + "%" : "") +
                     "   ·   setas para folhear, roda para ampliar, clique para fechar";
}

/// <summary>
/// (PainelEncarte.Abrir) Decodifica na largura pedida, lendo o arquivo inteiro para a memória.
/// </summary>
// O PainelEncarte e do EM EXECUCAO (outra frente); o corpo e o mesmo, aqui para nao depender dela.
std::shared_ptr<BitmapSource> JanelaPagina::Abrir(const std::string &arquivo, int larguraAlvo)
{
    try { return BitmapSource::DeArquivo(arquivo, larguraAlvo); }
    catch (...) { return nullptr; }
}

} // namespace ARCA::Vistas

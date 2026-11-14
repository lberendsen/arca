// ARCA no xOS - Vistas/Wpf: os tipos simples do WPF (System.Windows e System.Windows.Media) que o
// Tema e os controles usam: pontos, tamanhos, espessuras, cores, pinceis e as enumeracoes.
//
// Os nomes sao os do WPF, de proposito: uma vista portada escreve `Margin = Thickness(0, 4, 0, 0)` e
// `Visibility = Visibility::Collapsed` como o original escrevia, trocando so o "." por "::".
// Os valores de enumeracao se escrevem sempre com o nome do tipo (Visibility::Collapsed): dentro de
// uma classe derivada de UIElement o nome do tipo tambem e o de uma propriedade, e o C++ so aceita
// o tipo antes de "::". Para declarar uma variavel desses tipos dentro de uma vista, use `auto` ou
// `Vistas::Visibility`.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace ARCA::Vistas {

constexpr double Infinito = std::numeric_limits<double>::infinity();
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();   // double.NaN: Width/Height "Auto"

// ---- geometria (System.Windows) ----
struct Point
{
    double X = 0, Y = 0;
    Point() = default;
    Point(double x, double y) : X(x), Y(y) {}
};

struct Size
{
    double Width = 0, Height = 0;
    Size() = default;
    Size(double w, double h) : Width(w), Height(h) {}
};

struct Rect
{
    double X = 0, Y = 0, Width = 0, Height = 0;
    Rect() = default;
    Rect(double x, double y, double w, double h) : X(x), Y(y), Width(w), Height(h) {}
    double Left() const { return X; }
    double Top() const { return Y; }
    double Right() const { return X + Width; }
    double Bottom() const { return Y + Height; }
    bool Contains(Point p) const { return p.X >= X && p.Y >= Y && p.X < X + Width && p.Y < Y + Height; }
    bool IsEmpty() const { return Width <= 0 || Height <= 0; }
    Rect Intersecao(const Rect &o) const
    {
        double l = std::max(X, o.X), t = std::max(Y, o.Y);
        double r = std::min(Right(), o.Right()), b = std::min(Bottom(), o.Bottom());
        return Rect(l, t, std::max(0.0, r - l), std::max(0.0, b - t));
    }
};

struct Int32Rect
{
    int X = 0, Y = 0, Width = 0, Height = 0;
    Int32Rect() = default;
    Int32Rect(int x, int y, int w, int h) : X(x), Y(y), Width(w), Height(h) {}
};

struct Thickness
{
    double Left = 0, Top = 0, Right = 0, Bottom = 0;
    Thickness() = default;
    Thickness(double u) : Left(u), Top(u), Right(u), Bottom(u) {}
    Thickness(double h, double v) : Left(h), Top(v), Right(h), Bottom(v) {}   // a forma "12,5" do XAML
    Thickness(double l, double t, double r, double b) : Left(l), Top(t), Right(r), Bottom(b) {}
    double Horizontal() const { return Left + Right; }
    double Vertical() const { return Top + Bottom; }
};

struct CornerRadius
{
    double TopLeft = 0, TopRight = 0, BottomRight = 0, BottomLeft = 0;
    CornerRadius() = default;
    CornerRadius(double u) : TopLeft(u), TopRight(u), BottomRight(u), BottomLeft(u) {}
    CornerRadius(double tl, double tr, double br, double bl) : TopLeft(tl), TopRight(tr), BottomRight(br), BottomLeft(bl) {}
    bool Zero() const { return TopLeft == 0 && TopRight == 0 && BottomRight == 0 && BottomLeft == 0; }
};

// ---- cor e pincel (System.Windows.Media) ----
struct Color
{
    uint8_t A = 0, R = 0, G = 0, B = 0;
    static Color FromRgb(uint8_t r, uint8_t g, uint8_t b) { return Color{255, r, g, b}; }
    static Color FromArgb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) { return Color{a, r, g, b}; }
    // "#AARRGGBB" ou "#RRGGBB", como no XAML
    static constexpr Color De(const char *hex)
    {
        uint32_t v = 0;
        int n = 0;
        for (const char *p = hex; *p; p++)
        {
            char c = *p;
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                  : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) continue;     // o "#"
            v = (v << 4) | (uint32_t) d;
            n++;
        }
        if (n <= 6) v |= 0xFF000000u;
        return Color{uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    }
    bool operator==(const Color &o) const { return A == o.A && R == o.R && G == o.G && B == o.B; }
    bool operator!=(const Color &o) const { return !(*this == o); }
};

struct GradientStop
{
    ARCA::Vistas::Color Color;
    double Offset = 0;
};

// Brush: solido ou degrade linear (com quantas paradas quiser). O Brush vazio e o "null" do WPF:
// nao pinta e nao recebe clique; Brushes::Transparent pinta nada mas recebe clique.
class Brush
{
public:
    Brush() = default;                                    // null
    Brush(ARCA::Vistas::Color c) : _nulo(false), _cor(c) {}
    static Brush Linear(Point inicio, Point fim, std::vector<GradientStop> paradas);

    bool Nulo() const { return _nulo; }
    bool Solido() const { return _paradas.empty(); }
    ARCA::Vistas::Color Cor() const { return _cor; }       // a cor (ou a primeira parada do degrade)
    const std::vector<GradientStop> &Paradas() const { return _paradas; }
    Point StartPoint() const { return _inicio; }
    Point EndPoint() const { return _fim; }
    double Opacity = 1;
    bool operator==(const Brush &o) const;
    bool operator!=(const Brush &o) const { return !(*this == o); }

private:
    bool _nulo = true;
    ARCA::Vistas::Color _cor;
    std::vector<GradientStop> _paradas;
    Point _inicio{0, 0}, _fim{0, 1};
};

// new SolidColorBrush(Color.FromRgb(...))  ->  SolidColorBrush(Color::FromRgb(...))
inline Brush SolidColorBrush(Color c) { return Brush(c); }

namespace Brushes {
inline const Brush Transparent = Brush(Color::FromArgb(0, 255, 255, 255));
inline const Brush Black = Brush(Color::FromRgb(0, 0, 0));
inline const Brush White = Brush(Color::FromRgb(255, 255, 255));
} // namespace Brushes

// ---- enumeracoes ----
enum class Visibility { Visible, Hidden, Collapsed };
enum class HorizontalAlignment { Stretch, Left, Center, Right };
enum class VerticalAlignment { Stretch, Top, Center, Bottom };
enum class Orientation { Horizontal, Vertical };
enum class Dock { Left, Top, Right, Bottom };
enum class TextWrapping { NoWrap, Wrap, WrapWithOverflow };
enum class TextTrimming { None, CharacterEllipsis, WordEllipsis };
enum class TextAlignment { Left, Right, Center, Justify };
enum class Stretch { None, Fill, Uniform, UniformToFill };
enum class ScrollBarVisibility { Disabled, Auto, Hidden, Visible };
enum class SelectionMode { Single, Multiple, Extended };
enum class FontWeight { Normal, SemiBold, Bold };
enum class FontStyle { Normal, Italic };
enum class GridUnitType { Auto, Pixel, Star };

// FontWeights.SemiBold, FontStyles.Italic: como no WPF
namespace FontWeights {
constexpr FontWeight Normal = FontWeight::Normal, SemiBold = FontWeight::SemiBold, Bold = FontWeight::Bold;
}
namespace FontStyles {
constexpr FontStyle Normal = FontStyle::Normal, Italic = FontStyle::Italic;
}

// Cursors.Hand etc. (o nome X de cada um fica em Janela.cpp)
enum class Cursor { Herda, Arrow, Hand, IBeam, Wait, SizeWE, SizeNS, SizeAll, Cross, SizeNWSE, SizeNESW, None };
namespace Cursors {
constexpr Cursor Arrow = Cursor::Arrow, Hand = Cursor::Hand, IBeam = Cursor::IBeam, Wait = Cursor::Wait,
                 SizeWE = Cursor::SizeWE, SizeNS = Cursor::SizeNS, SizeAll = Cursor::SizeAll,
                 Cross = Cursor::Cross, SizeNWSE = Cursor::SizeNWSE, SizeNESW = Cursor::SizeNESW,
                 None = Cursor::None;
}

// GridLength / DataGridLength: 52, Auto, 2.4*
struct GridLength
{
    double Value = 1;
    ARCA::Vistas::GridUnitType GridUnitType = ARCA::Vistas::GridUnitType::Star;
    GridLength() = default;
    GridLength(double px) : Value(px), GridUnitType(GridUnitType::Pixel) {}
    GridLength(double v, ARCA::Vistas::GridUnitType t) : Value(v), GridUnitType(t) {}
    static GridLength Auto() { return GridLength(1, GridUnitType::Auto); }
    static GridLength Star(double v = 1) { return GridLength(v, GridUnitType::Star); }
    bool IsAuto() const { return GridUnitType == GridUnitType::Auto; }
    bool IsStar() const { return GridUnitType == GridUnitType::Star; }
    bool IsAbsolute() const { return GridUnitType == GridUnitType::Pixel; }
};
using DataGridLength = GridLength;
using DataGridLengthUnitType = GridUnitType;

} // namespace ARCA::Vistas

// ARCA no xOS - Modelo/PonderacaoK (de Modelo\PonderacaoK.cs).
#pragma once

#include <memory>

namespace ARCA::Modelo {

/// <summary>
/// Ponderação K da ITU-R BS.1770 / EBU R128: um shelf agudo (~1682 Hz, +4 dB,
/// modelo da cabeça) seguido de um passa-alta (~38 Hz). É o filtro que torna
/// a energia medida próxima do volume percebido.
///
/// Vive à parte porque duas frentes precisam dele: a loudness AO VIVO
/// (<see cref="AnalisadorAudio"/>) e a de ARQUIVO (<see cref="OndaCache"/>).
/// Um só lugar para os coeficientes evita que uma correção conserte só metade.
///
/// Os coeficientes saem das fórmulas do RBJ Audio EQ Cookbook com os
/// parâmetros da norma, recalculados para a taxa real — a tabela fixa da
/// BS.1770 é de 48 kHz e erraria a 44,1.
/// </summary>
struct PonderacaoK
{
    class Filtro
    {
    public:
        Filtro(double b0, double b1, double b2, double a1, double a2) : b0(b0), b1(b1), b2(b2), a1(a1), a2(a2) {}

        double P(double x)
        {
            double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }

    private:
        double b0, b1, b2, a1, a2;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    };

    // o "null" do C# (o filtro ainda não montado): std::unique_ptr vazio
    static std::unique_ptr<Filtro> Shelf(double fs);
    static std::unique_ptr<Filtro> PassaAlta(double fs);

    /// <summary>Loudness (LUFS) de uma energia K-ponderada somada nos canais.</summary>
    static double Lufs(double z);
};

} // namespace ARCA::Modelo

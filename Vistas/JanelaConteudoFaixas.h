// ARCA no xOS - Vistas/JanelaConteudoFaixas (de Vistas\JanelaConteudoFaixas.cs).
#pragma once

#include "Janela.h"
#include "Modelo/ClasseMidia.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ARCA::Vistas {

/// O que cada faixa da área de trabalho é: música, audiolivro ou fala
/// (entrevista, humor). Grava a classe de mídia do ASF — ver
/// Modelo\ClasseMidia.cs. (LINUX: no FLAC, os campos WM_MEDIACLASS*.)
///
/// É por faixa porque um disco mistura: as entrevistas do Thriller são
/// fala dentro de um álbum de música. Não depende da identificação — é uma
/// marca nos arquivos, que vale venha o resto de onde vier.
///
/// Nada é gravado aqui: a janela devolve só as faixas que mudaram.
class JanelaConteudoFaixas : public Window
{
public:
    /// lstArquivos: os FLAC (WMA no original) da área de trabalho, na ordem das faixas.
    /// strSugestao: "" = null.
    JanelaConteudoFaixas(const std::vector<std::string> &lstArquivos, const std::string &strSugestao);

    // O Dictionary<string, ConteudoFaixa> (OrdinalIgnoreCase) do original.
    // LINUX: um vetor de pares, na ordem em que as faixas entraram, que é a ordem em que o
    // Dictionary do .NET as devolve ao percorrer; os caminhos vêm de uma lista sem repetição.
    using TAlteradas = std::vector<std::pair<std::string, Modelo::ConteudoFaixa>>;

    /// As faixas cuja escolha difere do que o arquivo tem, ou nullopt (null) se a pessoa cancelou.
    const std::optional<TAlteradas> &Alteradas() const { return _alteradas; }

private:
    struct LinhaConteudo
    {
        std::string strCaminho;
        Modelo::ConteudoFaixa eOriginal = Modelo::ConteudoFaixa::Musica;
        std::array<std::shared_ptr<RadioButton>, 3> arrOpcoes;

        Modelo::ConteudoFaixa Escolhido() const
        {
            for (size_t i = 0; i < arrOpcoes.size(); i++)
                if (arrOpcoes[i]->IsChecked() == true) return (Modelo::ConteudoFaixa) i;
            return eOriginal;
        }
    };

    static const std::array<const char *, 3> arrRotulos;

    std::vector<LinhaConteudo> _lstLinhas;
    std::shared_ptr<TextBlock> _txtAviso;
    std::optional<TAlteradas> _alteradas;

    UIElementPtr MontarCabecalho(const std::string &strSugestao);
    UIElementPtr MontarTodas();
    UIElementPtr MontarFaixas(const std::vector<std::string> &lstArquivos);
    UIElementPtr MontarRodape();
    void AoAplicar(RoutedEventArgs &e);
    static Brush Recurso(const std::string &strChave);
};

} // namespace ARCA::Vistas

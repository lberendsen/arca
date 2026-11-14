// ARCA no xOS - Modelo/OrdemFaixas (de Modelo\OrdemFaixas.cs).
#include "OrdemFaixas.h"

#include "Base/Arquivo.h"
#include "Base/Texto.h"

#include <algorithm>
#include <climits>
#include <set>

namespace ARCA::Modelo {

using namespace ARCA::Base;
using ARCA::Interop::AtributoFlac;
using ARCA::Interop::EditorFlac;

int TOrdemFaixas::NumeroDaFaixa(const std::vector<AtributoFlac> &lstAtributos)
{
    int intAntigo = -1;
    for (const AtributoFlac &a : lstAtributos)
    {
        if (IgualSemCaixa(a.Nome, "WM/TrackNumber"))
        {
            int intN = Numero(a.Valor);
            if (intN > 0) return intN;
        }
        // Formato antigo, base zero.
        else if (IgualSemCaixa(a.Nome, "WM/Track"))
        {
            int intN = Numero(a.Valor);
            if (intN >= 0) intAntigo = intN + 1;
        }
    }
    return intAntigo;
}

int TOrdemFaixas::NumeroDaFaixa(const std::string &strArquivo)
{
    EditorFlac objEditor;
    return objEditor.Abrir(strArquivo) ? NumeroDaFaixa(objEditor.LerTudo()) : -1;
}

std::vector<std::string> TOrdemFaixas::Ordenar(const std::vector<std::string> &arquivos,
                                                const std::map<std::string, int> *dicNumeros)
{
    std::vector<std::string> lstArquivos(arquivos);
    // LINUX: o original indexava por caminho sem distinguir maiúsculas (Windows); no Linux o
    // caminho distingue, e o dicionário é pelo caminho exato.
    std::map<std::string, int> dicN;
    for (const std::string &f : lstArquivos)
    {
        int intN;
        auto it = dicNumeros ? dicNumeros->find(f) : std::map<std::string, int>::const_iterator();
        if (dicNumeros == nullptr || it == dicNumeros->end()) intN = NumeroDaFaixa(f);
        else intN = it->second;
        dicN[f] = intN;
    }
    std::stable_sort(lstArquivos.begin(), lstArquivos.end(), [&dicN](const std::string &a, const std::string &b)
    {
        int intA = dicN[a] > 0 ? dicN[a] : INT_MAX;
        int intB = dicN[b] > 0 ? dicN[b] : INT_MAX;
        if (intA != intB) return intA < intB;
        // StringComparer.OrdinalIgnoreCase.Compare(Path.GetFileName(a), Path.GetFileName(b))
        return Maiusculas(NomeDoArquivo(a)) < Maiusculas(NomeDoArquivo(b));
    });
    return lstArquivos;
}

bool TOrdemFaixas::NumeracaoCompleta(const std::vector<std::string> &arquivos, const std::map<std::string, int> &dicNumeros)
{
    std::set<int> setVistos;
    for (const std::string &f : arquivos)
    {
        auto it = dicNumeros.find(f);
        if (it == dicNumeros.end() || it->second <= 0 || !setVistos.insert(it->second).second) return false;
    }
    return true;
}

int TOrdemFaixas::Numero(const std::string &strValor)
{
    // "3", "03", "3/12"
    if (strValor.empty()) return -1;
    size_t intFim = 0;
    std::string s = Trim(strValor);
    while (intFim < s.size() && s[intFim] >= '0' && s[intFim] <= '9') intFim++;
    int intN;
    return intFim > 0 && TentaInt(s.substr(0, intFim), intN) ? intN : -1;
}

} // namespace ARCA::Modelo

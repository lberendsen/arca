// ARCA no xOS - Modelo/OrdemFaixas (de Modelo\OrdemFaixas.cs).
#pragma once

#include "Interop/EditorFlac.h"

#include <map>
#include <string>
#include <vector>

namespace ARCA::Modelo {

/// A ordem das faixas de um álbum do acervo vem da tag, não do nome do
/// arquivo. O nome é da pessoa e pode ser qualquer um; o número da faixa
/// (WM/TrackNumber) é o que diz a posição no disco.
///
/// Antes o Conferir álbum ordenava pelo nome, e isso foi usado para travar
/// o "01 - " na renomeação (04/10/2026) — o defeito era a ordenação.
class TOrdemFaixas
{
public:
    /// O número da faixa nos atributos, ou -1 se não houver.
    static int NumeroDaFaixa(const std::vector<Interop::AtributoFlac> &lstAtributos);

    /// O número da faixa lido do arquivo, ou -1.
    static int NumeroDaFaixa(const std::string &strArquivo);

    /// Os arquivos na ordem do disco: pelo número da faixa; os sem número
    /// vão para o fim, pelo nome. dicNumeros evita reabrir arquivos já
    /// lidos (pode ser nullptr).
    static std::vector<std::string> Ordenar(const std::vector<std::string> &arquivos,
                                            const std::map<std::string, int> *dicNumeros);

    /// Os números se repetem ou faltam? Então a ordem não é confiável — quem
    /// confere bit-perfect precisa dizer isso em vez de comparar às cegas.
    static bool NumeracaoCompleta(const std::vector<std::string> &arquivos, const std::map<std::string, int> &dicNumeros);

private:
    static int Numero(const std::string &strValor);
};

} // namespace ARCA::Modelo

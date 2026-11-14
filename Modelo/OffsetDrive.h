// ARCA no xOS - Modelo/OffsetDrive (de Modelo\OffsetDrive.cs).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ARCA::Modelo {

/// O deslocamento de leitura (read offset) de cada modelo de drive.
///
/// Todo drive entrega o audio deslocado por um numero fixo de amostras; sem corrigir, a faixa sai
/// "igual, mas fora do lugar" - as fronteiras entre faixas ficam algumas amostras adiantadas ou
/// atrasadas. A correcao e sempre a mesma para o mesmo modelo.
///
/// De onde vem, nesta ordem:
/// 1. offsets.txt em %LOCALAPPDATA%\ARCA (LINUX: Base::PastaCache(), /var/cache/arca) - o que
///    este ARCA ja confirmou com o AccurateRip, ou o que a pessoa escreveu ali;
/// 2. a tabela publica do AccurateRip (DriveOffsets.bin), baixada uma vez e guardada na mesma pasta.
class OffsetDrive
{
public:
    static std::string Pasta();

    /// "PLEXTOR |BD-R   PX-B900A " -> "PLEXTOR|BD-RPX-B900A": sem espacos, maiusculas.
    static std::string Chave(const std::string &strFabricante, const std::string &strModelo);

    /// O offset do drive, ou nullopt (null) se nao se sabe. strOrigem diz de onde veio ("" = null).
    static std::optional<int> Obter(const std::string &strFabricante, const std::string &strModelo, std::string &strOrigem);

    /// Guarda o offset confirmado (com o AccurateRip) para este modelo. Vale mais que a tabela: e
    /// deste drive, medido aqui. Lanca std::runtime_error se nao conseguir gravar.
    static void Registrar(const std::string &strFabricante, const std::string &strModelo, int intOffset, const std::string &strOrigem);

    /// Deslocamentos em que albuns do acervo ja conferiram, do mais comum ao menos. Sao bons
    /// candidatos para os proximos: o acervo antigo foi ripado por poucos drives, sem correcao.
    static std::vector<int> OffsetsDoAcervo();

    static void RegistrarOffsetDoAcervo(int intOffset);

private:
    static constexpr const char *UrlTabela = "http://www.accuraterip.com/accuraterip/DriveOffsets.bin";
    static constexpr int TamanhoRegistro = 69;   // offset (int16) + nome (32) + 35 bytes que nao se usam

    static std::string Sem(const std::string &s);
    /// chave -> (offset, origem), na ordem em que as chaves entraram (o Dictionary do original)
    using TLocal = std::vector<std::pair<std::string, std::pair<int, std::string>>>;
    static TLocal LerLocal();
    /// A tabela do AccurateRip: guardada, ou baixada na primeira vez (e a cada 6 meses). Vazia = null.
    static std::vector<uint8_t> Tabela();
};

} // namespace ARCA::Modelo

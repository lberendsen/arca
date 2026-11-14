// ARCA no xOS - Modelo/ConferenciaBitPerfect (de Modelo\ConferenciaBitPerfect.cs).
#pragma once

#include "Modelo/ProcessadorExtracaoAudio.h"

#include <functional>
#include <string>
#include <vector>

namespace ARCA::Interop {
class SptiImageExtractor;
}

namespace ARCA::Modelo {

class TRelatorioExtracao;

/// A meta do ARCA é bit-perfect: cada faixa no acervo igual, amostra por
/// amostra, à do CD. São três elos, e cada um tem a sua prova:
///
/// 1. **Ler o disco.** O drive pode entregar um trecho estimado sem
///    acusar erro. Prova: o AccurateRip — a assinatura da faixa bate com a
///    de quem ripou o mesmo disco. Fora da base, a melhor evidência local:
///    ler de novo e comparar setor a setor.
/// 2. **Pôr cada amostra no lugar.** Todo drive lê deslocado por um número
///    fixo de amostras (o offset). Correção: o offset do modelo, aplicado
///    ao cortar as faixas.
/// 3. **Codificar.** FLAC (WMA Lossless no original) é sem perda por definição; aqui é
///    conferido: o FLAC decodificado tem de ter o mesmo SHA-256 do áudio
///    que entrou no codificador.
///
/// Preparar roda entre a leitura e o corte (decide o offset); Concluir,
/// depois do corte (registra a conferência dos FLAC, grava etiquetas e o
/// relatório).
class ConferenciaBitPerfect
{
public:
    /// Offset do drive, AccurateRip sobre a imagem crua e releitura das
    /// faixas que ele não confirmar. Devolve a correção de offset a
    /// aplicar no corte. Preenche objRel.
    /// LINUX: chrDrive (a letra da unidade) vira strDrive, o que o SptiImageExtractor portado
    /// recebe: "/dev/sr0" (ou qualquer coisa sem "/" no começo, que vira Base::DispositivoCd()).
    static int Preparar(Interop::SptiImageExtractor &objExtrator, const std::string &strDrive, const std::string &strPasta,
                        TRelatorioExtracao &objRel,
                        const std::function<void(const std::string &)> &relatar,
                        const std::function<void(const std::string &, const std::string &)> &fase,
                        const std::function<void(const std::string &, double, const std::string &)> &progresso);

    /// Depois do corte: registra a conferência de cada FLAC (WMA no original), grava nas
    /// faixas o índice do CD (WM/MCDI — permite conferir de novo anos
    /// depois, sem o disco), o drive e o veredito (ARCA/RipDrive,
    /// ARCA/Verification; no FLAC, MCDI, ARCA_RIPDRIVE e ARCA_VERIFICATION), e salva o relatório.
    static void Concluir(const std::string &strPasta, TRelatorioExtracao &objRel, const std::vector<TConferenciaWma> &lstWma,
                         const std::function<void(const std::string &)> &relatar);
};

} // namespace ARCA::Modelo

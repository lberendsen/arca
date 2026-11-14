// ARCA no xOS - Modelo/FontesDisco (de Modelo\FontesDisco.cs). TFonteBin e TFonteWma.
#pragma once

#include "Modelo/AccurateRip.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ARCA::Interop {
class LeitorAudio;
}

namespace ARCA::Modelo {

/// O AudioCD.bin da extração como disco: setores de 2352 bytes desde o
/// LBA 0, exatamente como o drive os entregou (sem correção de offset).
class TFonteBin : public IFonteDisco
{
public:
    explicit TFonteBin(const std::string &strArquivo);
    ~TFonteBin() override;            // Dispose()
    TFonteBin(const TFonteBin &) = delete;
    TFonteBin &operator=(const TFonteBin &) = delete;

    int LbaInicial() const override { return 0; }
    int64_t TotalAmostras() const override { return m_lngTotalAmostras; }

    void Reiniciar() override;
    int Ler(uint32_t *arrDestino, int intMax) override;

    void Dispose();

private:
    std::string m_strArquivo;
    FILE *m_objArq = nullptr;
    int64_t m_lngTotalAmostras = 0;
};

/// As faixas FLAC (WMA no original) de um álbum, decodificadas em sequência como se fossem o
/// disco: a posição 0 é o início da primeira faixa. Só serve para áudio de
/// CD (16 bits, estéreo, 44,1 kHz) — outro formato não tem assinatura no
/// AccurateRip, e a abertura recusa.
///
/// A conferência só faz sentido se cada arquivo tiver exatamente o
/// tamanho que o índice do CD dá à faixa: é o que Validar confere antes.
///
/// LINUX: o nome fica TFonteWma, o do original, para a JanelaConferenciaAlbum (e quem mais
/// chamar) ser portada sem mudança. Os arquivos são lidos pelo LeitorAudio, que no xOS decodifica
/// FLAC pela libFLAC e o resto (MP3, os WMA antigos que ainda existam) pelo FFmpeg: a fonte é a
/// de qualquer álbum já codificado, e no acervo do xOS isso quer dizer FLAC.
class TFonteWma : public IFonteDisco
{
public:
    TFonteWma(const std::vector<std::string> &lstArquivos, int intLbaInicial);
    ~TFonteWma() override;            // Dispose()
    TFonteWma(const TFonteWma &) = delete;
    TFonteWma &operator=(const TFonteWma &) = delete;

    int LbaInicial() const override { return m_intLbaInicial; }
    int64_t TotalAmostras() const override { return m_lngTotalAmostras; }

    /// Algo que não impede a conferência mas deve ser dito, ou "" (null).
    const std::string &Observacao() const { return m_strObservacao; }

    /// Decodifica cada arquivo uma vez para contar as amostras e confere
    /// com o índice. "" (null) se tudo bate; senão, o porquê.
    /// Lança Base::OperationCanceledException.
    std::string Validar(const TTocAudio &objToc, const std::function<bool()> &cancelou);

    void Reiniciar() override;
    int Ler(uint32_t *arrDestino, int intMax) override;

    void Dispose();

private:
    std::vector<std::string> m_lstArquivos;
    int m_intAtual = 0;
    std::unique_ptr<Interop::LeitorAudio> m_objLeitor;
    std::vector<uint8_t> m_arrResto;
    size_t m_intRestoPos = 0;
    int m_intLbaInicial = 0;
    int64_t m_lngTotalAmostras = 0;
    std::string m_strObservacao;

    /// Vazio = null (fim de todos os arquivos).
    std::vector<uint8_t> ProximoBloco();
    void Fechar();
};

} // namespace ARCA::Modelo

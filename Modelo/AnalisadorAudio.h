// ARCA no xOS - Modelo/AnalisadorAudio (de Modelo\AnalisadorAudio.cs).
#pragma once

#include "Interop/LeitorAudio.h"
#include "Modelo/PonderacaoK.h"

#include <memory>
#include <string>
#include <vector>

namespace ARCA::Modelo {

/// <summary>
/// Mantém uma janela das amostras mais recentes, alinhada com o que o
/// player está tocando, e calcula as medidas que os instrumentos desenham.
///
/// Decodifica na própria thread da interface, de propósito: a 64x o tempo
/// real, cada quadro de tela precisa de menos de um milissegundo de
/// decodificação. Uma thread separada traria sincronização e travas para
/// resolver um problema que não existe.
///
/// A sincronia é por acompanhamento, não por acoplamento: a cada batida
/// comparamos onde nossa leitura está com onde o player está, e puxamos
/// blocos até alcançar. Se a distância ficar grande — alguém arrastou a
/// barra de posição —, reposicionamos.
/// </summary>
class AnalisadorAudio
{
public:
    /// <summary>Quadros guardados. 4096 a 44,1 kHz são 93 ms — o bastante
    /// para o goniômetro e para uma janela de FFT de 2048.</summary>
    static constexpr int Janela = 4096;

    AnalisadorAudio();
    ~AnalisadorAudio();                // Dispose()

    bool Pronto() const { return _leitor.Aberto(); }
    int Taxa() const { return _leitor.Taxa(); }
    int Canais() const { return _leitor.Canais(); }
    const std::string &Falha() const { return _leitor.Falha(); }    // "" = null

    double PicoEsq = 0, PicoDir = 0, RmsEsq = 0, RmsDir = 0;
    /// <summary>+1 = idêntico nos dois canais; 0 = sem relação; -1 = em oposição de fase.</summary>
    double Correlacao = 0;
    /// <summary>RMS do componente central (L+R)/2.</summary>
    double RmsMid = 0;
    /// <summary>RMS do componente lateral (L-R)/2.</summary>
    double RmsSide = 0;
    /// <summary>Distribuição de energia entre canais: -1 tudo à esquerda, +1 tudo à direita.</summary>
    double Balanco = 0;
    /// <summary>Houve amostra no teto (ou quase) nesta janela? Um por canal.</summary>
    bool ClipouEsq = false, ClipouDir = false;

    // --- Loudness (ITU-R BS.1770 / EBU R128) -------------------------
    // A loudness não sai da janela de 93 ms: precisa de 400 ms (momentâneo),
    // 3 s (curto prazo) e o programa inteiro (integrado). Por isso mora aqui,
    // onde passa todo o fluxo decodificado, e não no painel.
    /// <summary>LUFS momentâneo (janela de 400 ms).</summary>
    double LufsM;
    /// <summary>LUFS de curto prazo (janela de 3 s).</summary>
    double LufsS;
    /// <summary>LUFS integrado, com gate absoluto (-70) e relativo (-10), desde o início/reinício.</summary>
    double LufsI;

    bool Abrir(const std::string &caminho);
    void Fechar();

    /// <summary>
    /// Chamada a cada quadro, com a posição em que o player está.
    /// </summary>
    void Acompanhar(double posicaoDoPlayer);

    /// <summary>
    /// Copia a janela em ordem cronológica, do mais antigo ao mais recente.
    ///
    /// Em duas cópias de bloco, não num laço com resto de divisão: eram
    /// 8.192 divisões inteiras por quadro, e divisão é das operações mais
    /// caras que existem.
    /// </summary>
    void Copiar(std::vector<float> &destinoEsq, std::vector<float> &destinoDir) const;

    /// <summary>Reinicia a medida de loudness: refaz os filtros para a taxa
    /// atual e zera os acumuladores. Chamado ao abrir e ao saltar.</summary>
    void ReiniciarLoudness();

    static double Db(double v);

private:
    Interop::LeitorAudio _leitor;
    std::vector<float> _esq, _dir;
    int _escrita = 0;

    // -0,013 dBFS: uma amostra encostada no teto conta como saturação.
    static constexpr double LimiarClip = 0.9985;

    std::unique_ptr<PonderacaoK::Filtro> _shE, _hpE, _shD, _hpD;   // K-weighting por canal
    double _accE = 0, _accD = 0;             // soma de quadrados K-ponderados no sub-bloco
    int _accN = 0, _amostras100 = 0;         // sub-bloco de 100 ms
    static constexpr int MaxSub = 30;        // 3 s em sub-blocos de 100 ms
    double _subE[MaxSub] = {};
    double _subD[MaxSub] = {};
    int _subPos = 0, _subCount = 0;
    // Integrado por histograma (0,1 LU de −70 a +30 LUFS): cada bloco de
    // gate entra uma vez, e o integrado sai em custo fixo. Antes, a lista
    // de todos os blocos era percorrida duas vezes a cada 100 ms — depois
    // de horas tocando, engasgava. Erro: no máximo 0,05 LU no limiar.
    static constexpr double HistMin = -70.0, HistPasso = 0.1;
    static constexpr int HistBins = 1000;
    std::vector<double> _histEnergia;
    std::vector<int> _histN;
    // Mono: um canal só entra na soma (BS.1770). Antes o canal único valia
    // como esquerdo e direito, e o loudness saía +3,01 dB.
    bool _blnMono = false;

    void Zerar();
    void Empilhar(const std::vector<std::vector<float>> &bloco);
    void Medir();
    void ProcessarLoudness(const std::vector<std::vector<float>> &bloco);
    void FinalizarSub();
    double MediaUltimos(int quantos, const double *anel) const;
    /// <summary>Bloco de gate no histograma; abaixo de −70 LUFS (gate absoluto) nem entra.</summary>
    void AcrescentarAoGate(double z);
    void RecalcularIntegrado();
    static double Lufs(double z);
};

} // namespace ARCA::Modelo

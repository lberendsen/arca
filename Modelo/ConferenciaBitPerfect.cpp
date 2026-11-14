// ARCA no xOS - Modelo/ConferenciaBitPerfect (de Modelo\ConferenciaBitPerfect.cs).
#include "ConferenciaBitPerfect.h"

#include "Base/Arquivo.h"
#include "Base/Texto.h"
#include "Interop/EditorFlac.h"
#include "Interop/SptiImageExtractor.h"
#include "Modelo/AccurateRip.h"
#include "Modelo/FontesDisco.h"
#include "Modelo/OffsetDrive.h"
#include "Modelo/RelatorioExtracao.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace ARCA::Modelo {

using namespace ARCA::Base;

namespace {

// string.Format("{0:+0;-0;0}")
std::string ComSinal(int n)
{
    if (n == 0) return "0";
    return (n > 0 ? "+" : "-") + std::to_string(n > 0 ? n : -n);
}

// string.Format("{0:D2}")
std::string D2(int n)
{
    char b[32];
    snprintf(b, sizeof b, "%02d", n);
    return b;
}

} // namespace

int ConferenciaBitPerfect::Preparar(Interop::SptiImageExtractor &objExtrator, const std::string &strDrive, const std::string &strPasta,
                                    TRelatorioExtracao &objRel,
                                    const std::function<void(const std::string &)> &relatar,
                                    const std::function<void(const std::string &, const std::string &)> &fase,
                                    const std::function<void(const std::string &, double, const std::string &)> &progresso)
{
    std::string strCue = Combina(strPasta, "AudioCD.cue"), strBin = Combina(strPasta, "AudioCD.bin");
    std::shared_ptr<TTocAudio> objToc = ArquivoExiste(strBin) ? TTocAudio::DeCue(strCue, Tamanho(strBin)) : nullptr;
    if (objToc == nullptr) { relatar("Conferência bit-perfect: não consegui montar o índice do disco a partir do CUE."); return 0; }

    // ---- offset do drive ----
    objRel.strDrive = Trim(Trim(objExtrator.Fabricante()) + " " + Trim(objExtrator.Modelo()));
    std::string strOrigem;
    std::optional<int> intOffsetDrive = OffsetDrive::Obter(objExtrator.Fabricante(), objExtrator.Modelo(), strOrigem);
    relatar(intOffsetDrive.has_value()
        ? "Offset do drive " + *objRel.strDrive + ": " + ComSinal(*intOffsetDrive) + " amostras (" + strOrigem + ")."
        : "Offset do drive " + *objRel.strDrive + ": desconhecido — vou tentar deduzir pelo AccurateRip.");

    // ---- AccurateRip sobre a leitura crua ----
    if (fase) fase("CONFERINDO NO ACCURATERIP…", "Assinatura de cada faixa contra a base pública");
    TResultadoAccurateRip objAr;
    {
        TFonteBin objFonte(strBin);
        objAr = AccurateRip::Conferir(objFonte, *objToc, intOffsetDrive.has_value() ? std::vector<int>{ *intOffsetDrive } : std::vector<int>{},
                                      nullptr);
    }

    if (!objAr.strFalha.empty())
    {
        objRel.strAccurateRip = "não consultado: " + objAr.strFalha;
        relatar("AccurateRip: " + objAr.strFalha);
    }
    else if (!objAr.blnNaBase)
    {
        objRel.strAccurateRip = "fora da base";
        relatar("AccurateRip: este disco não está na base. As faixas serão lidas de novo para comparação.");
        for (const TFaixaAccurateRip &f : objAr.lstFaixas) objRel.Verificacao(f.intNumero).intConfiancaAr = 0;
    }
    else
    {
        objRel.strAccurateRip = "na base";
        objRel.intOffsetAccurateRip = objAr.intOffset;
        for (const TFaixaAccurateRip &f : objAr.lstFaixas)
        {
            TVerificacaoFaixa &v = objRel.Verificacao(f.intNumero);
            v.intConfiancaAr = f.intConfianca;
            v.intTotalAr = f.intTotalNaBase;
            v.strVersaoAr = f.strVersao;
        }
        relatar("AccurateRip: " + std::to_string(objAr.QtdConferem()) + " de " + std::to_string(objAr.lstFaixas.size()) +
                " faixa(s) conferem (deslocamento " + ComSinal(objAr.intOffset) + ").");
    }

    // ---- decidir a correção ----
    int intAplicar = 0;
    if (intOffsetDrive.has_value())
    {
        intAplicar = *intOffsetDrive;
        objRel.strOrigemOffset = strOrigem;
        // Tabela confirmada por um disco inteiro: passa a ser do drive, medido aqui.
        if (objAr.TodasConferem() && objAr.intOffset == *intOffsetDrive && strOrigem == "tabela do AccurateRip")
        {
            try { OffsetDrive::Registrar(objExtrator.Fabricante(), objExtrator.Modelo(), intAplicar, "confirmado com o AccurateRip em " + Agora("%d/%m/%Y")); }
            catch (...) {}
        }
        if (objAr.QtdConferem() > 0 && objAr.intOffset != intAplicar)
            relatar("A base confere no deslocamento " + ComSinal(objAr.intOffset) +
                    ": é outra prensagem do mesmo disco. A leitura está certa; o corte usa o offset do drive.");
    }
    else if (objAr.QtdConferem() > (int) objAr.lstFaixas.size() / 2)
    {
        // Sem offset conhecido, o que a base mostra é o melhor que há
        // — vale para este disco, e não fica gravado (pode ser
        // prensagem diferente, e não o drive).
        intAplicar = objAr.intOffset;
        objRel.strOrigemOffset = "deduzido do AccurateRip neste disco";
        relatar("Offset deduzido do AccurateRip: " + ComSinal(intAplicar) + " amostras.");
    }
    else
        relatar("ATENÇÃO: offset do drive desconhecido. As faixas sairão sem correção, deslocadas por algumas amostras.");
    objRel.intOffsetAplicado = intAplicar;

    // ---- releitura do que a base não confirma ----
    std::vector<std::array<int, 3>> lstReler;
    int intFolga = std::abs(intAplicar) / TTocAudio::AmostrasPorSetor + 1;   // o corte corrigido pega setores vizinhos
    for (int t = 0; t < objToc->QtdFaixas(); t++)
    {
        int intNum = objToc->intPrimeiraFaixa + t;
        TVerificacaoFaixa &v = objRel.Verificacao(intNum);
        if (v.intConfiancaAr > 0) continue;
        lstReler.push_back({ intNum, std::max(0, objToc->lstInicios[t] - intFolga), objToc->FimDaFaixa(t) + intFolga });
    }
    // Faixa 00 (áudio antes da faixa 1): o AccurateRip não a cobre; com som,
    // vai sempre para a releitura.
    if (objToc->intDadosAntes < 0 && objToc->lstInicios[0] >= ProcessadorExtracaoAudio::SetoresMinimosFaixaOculta)
    {
        double dblPico;
        if (ProcessadorExtracaoAudio::TrechoTemSom(strBin, (int64_t) intAplicar * 4, (int64_t) objToc->lstInicios[0] * 2352, dblPico))
        {
            objRel.Verificacao(0);
            lstReler.insert(lstReler.begin(), { 0, 0, objToc->lstInicios[0] + intFolga });
        }
    }
    if (!lstReler.empty())
    {
        relatar("Relendo " + std::to_string(lstReler.size()) + " faixa(s) que o AccurateRip não confirmou, para comparar com a primeira leitura...");
        if (fase) fase("RELENDO FAIXAS…", "Segunda leitura para comparar setor a setor");
        std::string strFalha;
        std::optional<std::vector<Interop::SptiImageExtractor::TReleitura>> lstRes = objExtrator.Reler(strDrive, strBin, lstReler,
            [&progresso](int intFaixa, double dblPct) {
                if (progresso) progresso("RELENDO FAIXAS", dblPct, "Faixa " + D2(intFaixa));
            }, nullptr, strFalha);
        if (!lstRes.has_value()) relatar("Releitura: " + strFalha);
        else
            for (const Interop::SptiImageExtractor::TReleitura &r : *lstRes)
            {
                TVerificacaoFaixa &v = objRel.Verificacao(r.intFaixa);
                v.intReleituraDiferentes = r.intSetoresDiferentes;
                v.intReleituraIlegiveis = r.intSetoresIlegiveis;
                relatar(r.Identica()
                    ? "  Faixa " + D2(r.intFaixa) + ": segunda leitura idêntica."
                    : "  ⚠ Faixa " + D2(r.intFaixa) + ": segunda leitura DIFERENTE (" + std::to_string(r.intSetoresDiferentes) +
                          " setor(es) diferente(s), " + std::to_string(r.intSetoresIlegiveis) + " ilegível(is)).");
            }
    }

    for (const auto &kv : objRel.dicMascaradosPorFaixa)
        objRel.Verificacao(kv.first).intMascarados = kv.second;
    return intAplicar;
}

void ConferenciaBitPerfect::Concluir(const std::string &strPasta, TRelatorioExtracao &objRel, const std::vector<TConferenciaWma> &lstWma,
                                     const std::function<void(const std::string &)> &relatar)
{
    for (const TConferenciaWma &c : lstWma)
    {
        TVerificacaoFaixa &v = objRel.Verificacao(c.intFaixa);
        v.blnWmaIdentico = c.Confere();
        v.strFalhaWma = c.strFalha;
    }

    std::string strBin = Combina(strPasta, "AudioCD.bin");
    std::shared_ptr<TTocAudio> objToc = ArquivoExiste(strBin) ? TTocAudio::DeCue(Combina(strPasta, "AudioCD.cue"), Tamanho(strBin)) : nullptr;
    std::string strDrive = !objRel.strDrive.has_value() || objRel.strDrive->empty() ? ""
        : *objRel.strDrive + (!objRel.strOrigemOffset.empty() ? " (offset " + ComSinal(objRel.intOffsetAplicado) + ")" : " (offset desconhecido)");

    for (const TConferenciaWma &c : lstWma)
    {
        if (c.strArquivoWma.empty() || !ArquivoExiste(c.strArquivoWma)) continue;
        try
        {
            Interop::EditorFlac e;
            if (!e.Abrir(c.strArquivoWma)) continue;
            // LINUX: no FLAC, ARCA_VERIFICATION, ARCA_RIPDRIVE e MCDI (o EditorFlac traduz os nomes).
            e.DefinirTexto("ARCA/Verification", objRel.Verificacao(c.intFaixa).ParaEtiqueta());
            if (!strDrive.empty()) e.DefinirTexto("ARCA/RipDrive", strDrive);
            if (objToc != nullptr) e.DefinirMcdi(objToc->ParaMcdi());
            // ARCA-ORIGINAL: o retorno do Salvar não é olhado; se a gravação falhar sem exceção, a
            // faixa fica sem a verificação e ninguém é avisado.
            e.Salvar();
        }
        catch (const std::exception &ex)
        {
            relatar("Aviso: não gravei a verificação na faixa " + std::to_string(c.intFaixa) + ": " + ex.what());
        }
    }

    try { objRel.Gravar(strPasta); }
    catch (const std::exception &ex) { relatar(std::string("Aviso: não consegui gravar ") + TRelatorioExtracao::NomeArquivo + ": " + ex.what()); }
    relatar("BIT-PERFECT: " + objRel.ResumoBitPerfect() + ".");
}

} // namespace ARCA::Modelo

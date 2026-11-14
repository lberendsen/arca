// ARCA no xOS - Modelo/RelatorioExtracao (de Modelo\RelatorioExtracao.cs).
#include "RelatorioExtracao.h"

#include "Base/Arquivo.h"
#include "Base/Texto.h"

#include <cstdio>
#include <vector>

namespace ARCA::Modelo {

using namespace ARCA::Base;

namespace {

// string.Format("{0:F2}") na cultura pt-BR: virgula decimal, sem separador de milhar
std::string F(double v, int casas)
{
    char b[64];
    snprintf(b, sizeof b, "%.*f", casas, v);
    for (char *p = b; *p; p++)
        if (*p == '.') *p = ',';
    return b;
}

} // namespace

// ===============================================================
//  TRelatorioExtracao
// ===============================================================

TVerificacaoFaixa &TRelatorioExtracao::Verificacao(int intFaixa)
{
    auto it = dicVerificacao.find(intFaixa);
    if (it == dicVerificacao.end())
    {
        TVerificacaoFaixa v;
        v.intFaixa = intFaixa;
        it = dicVerificacao.emplace(intFaixa, v).first;
    }
    return it->second;
}

int TRelatorioExtracao::QtdComprovadas() const
{
    int n = 0;
    for (auto &kv : dicVerificacao) if (kv.second.Nivel() == NivelBitPerfect::Comprovada) n++;
    return n;
}

std::string TRelatorioExtracao::ResumoBitPerfect() const
{
    int n = (int) dicVerificacao.size();
    if (n == 0) return "conferência bit-perfect não feita";
    int c = 0, r = 0, x = 0;
    std::vector<std::string> lstProblemas;
    for (auto &kv : dicVerificacao)
    {
        const TVerificacaoFaixa &v = kv.second;
        if (v.Nivel() == NivelBitPerfect::Comprovada) c++;
        else if (v.Nivel() == NivelBitPerfect::LidaDuasVezes) r++;
        else { x++; lstProblemas.push_back("faixa " + std::to_string(v.intFaixa) + " (" + v.Motivo() + ")"); }
    }
    std::string s = std::to_string(c) + " de " + std::to_string(n) + " faixa(s) comprovada(s) pelo AccurateRip";
    if (r > 0) s += "; " + std::to_string(r) + " lida(s) duas vezes, idêntica(s) — boa, mas sem prova externa";
    if (x > 0) s += "; " + std::to_string(x) + " NÃO comprovada(s): " + Junta(lstProblemas, ", ");
    return s;
}

std::string TRelatorioExtracao::ResumoPorFaixa() const
{
    std::vector<std::string> lstPartes;
    for (auto &kv : dicMascaradosPorFaixa)
        lstPartes.push_back("faixa " + std::to_string(kv.first) + " (" + std::to_string(kv.second) + " setor" +
                            (kv.second == 1 ? "" : "es") + ", " + F((double) kv.second / SetoresPorSegundo, 2) + " s)");
    return !lstPartes.empty() ? Junta(lstPartes, ", ") : "faixa não identificada";
}

void TRelatorioExtracao::Gravar(const std::string &strPasta) const
{
    std::string sb;
    sb += "setoresMascarados=" + std::to_string(intSetoresMascarados) + "\r\n";
    sb += "setoresRecuperados=" + std::to_string(intSetoresRecuperados) + "\r\n";
    for (auto &kv : dicMascaradosPorFaixa)
        sb += "faixa=" + std::to_string(kv.first) + ";mascarados=" + std::to_string(kv.second) + "\r\n";
    if (strDrive) sb += "drive=" + *strDrive + "\r\n";
    sb += "offset=" + std::to_string(intOffsetAplicado) + ";origem=" + strOrigemOffset + "\r\n";
    if (strAccurateRip)
        sb += "accuraterip=" + Substitui(*strAccurateRip, ";", ",") + ";offset=" + std::to_string(intOffsetAccurateRip) + "\r\n";
    for (auto &kv : dicVerificacao) sb += kv.second.ParaLinha() + "\r\n";
    GravaTexto(Combina(strPasta, NomeArquivo), sb);       // UTF-8 sem BOM
}

std::shared_ptr<TRelatorioExtracao> TRelatorioExtracao::Ler(const std::string &strPasta)
{
    std::string strArquivo = Combina(strPasta, NomeArquivo);
    if (!ArquivoExiste(strArquivo)) return nullptr;
    try
    {
        auto objR = std::make_shared<TRelatorioExtracao>();
        for (const std::string &strLinha : LeLinhas(strArquivo))
        {
            std::string strL = Trim(strLinha);
            if (ComecaCom(strL, "setoresMascarados=")) objR->intSetoresMascarados = Inteiro(strL.substr(18));
            else if (ComecaCom(strL, "setoresRecuperados=")) objR->intSetoresRecuperados = Inteiro(strL.substr(19));
            else if (ComecaCom(strL, "drive=")) objR->strDrive = strL.substr(6);
            else if (ComecaCom(strL, "offset="))
            {
                std::vector<std::string> arrP = Divide(strL.substr(7), ";");
                objR->intOffsetAplicado = Inteiro(arrP[0]);
                std::string strO = arrP.size() > 1 && ComecaCom(arrP[1], "origem=") ? arrP[1].substr(7) : "";
                objR->strOrigemOffset = strO;             // "" = null
            }
            else if (ComecaCom(strL, "accuraterip="))
            {
                std::vector<std::string> arrP = Divide(strL.substr(12), ";");
                objR->strAccurateRip = arrP[0];
                if (arrP.size() > 1 && ComecaCom(arrP[1], "offset=")) objR->intOffsetAccurateRip = Inteiro(arrP[1].substr(7));
            }
            else if (ComecaCom(strL, "verif="))
            {
                std::unique_ptr<TVerificacaoFaixa> v = TVerificacaoFaixa::DeLinha(strL);
                if (v) objR->dicVerificacao[v->intFaixa] = *v;
            }
            else if (ComecaCom(strL, "faixa="))
            {
                std::vector<std::string> arrPartes = Divide(strL, ";");
                if (arrPartes.size() == 2 && ComecaCom(arrPartes[1], "mascarados="))
                    objR->dicMascaradosPorFaixa[Inteiro(arrPartes[0].substr(6))] = Inteiro(arrPartes[1].substr(11));
            }
        }
        return objR;
    }
    catch (...) { return nullptr; }
}

void TRelatorioExtracao::Apagar(const std::string &strPasta)
{
    std::error_code e;
    fs::remove(fs::u8path(Combina(strPasta, NomeArquivo)), e);
}

int TRelatorioExtracao::Inteiro(const std::string &s)
{
    int n;
    return TentaInt(Trim(s), n) ? n : 0;
}

// ===============================================================
//  TVerificacaoFaixa
// ===============================================================

NivelBitPerfect TVerificacaoFaixa::Nivel() const
{
    if (blnWmaIdentico != true || intMascarados > 0) return NivelBitPerfect::NaoComprovada;
    if (intConfiancaAr > 0) return NivelBitPerfect::Comprovada;
    if (intReleituraDiferentes == 0 && intReleituraIlegiveis == 0) return NivelBitPerfect::LidaDuasVezes;
    return NivelBitPerfect::NaoComprovada;
}

// LINUX: nos textos para a tela e para a etiqueta, "WMA" vira "FLAC" (o formato do acervo no
// xOS, PORTE.md secao 3); as chaves do arquivo (wma=, wmafalha=) ficam as do original.
std::string TVerificacaoFaixa::Motivo() const
{
    if (intMascarados > 0) return std::to_string(intMascarados) + " setor(es) ilegível(is)";
    if (blnWmaIdentico == false) return "FLAC diferente do lido: " + strFalhaWma;
    if (!blnWmaIdentico.has_value()) return "FLAC não conferido";
    if (intReleituraDiferentes > 0) return "a releitura deu " + std::to_string(intReleituraDiferentes) + " setor(es) diferente(s)";
    if (intReleituraIlegiveis > 0) return "a releitura não leu " + std::to_string(intReleituraIlegiveis) + " setor(es)";
    if (intConfiancaAr == 0 && intTotalAr > 0) return "não confere com o AccurateRip";
    return "sem conferência";
}

std::string TVerificacaoFaixa::ParaEtiqueta() const
{
    std::vector<std::string> lst;
    if (intConfiancaAr > 0) lst.push_back("AccurateRip " + strVersaoAr + " (confiança " + std::to_string(intConfiancaAr) + ")");
    else if (intConfiancaAr == 0 && intTotalAr > 0) lst.push_back("AccurateRip: não confere");
    else if (intConfiancaAr == 0) lst.push_back("AccurateRip: disco fora da base");
    if (intReleituraDiferentes == 0 && intReleituraIlegiveis == 0) lst.push_back("lida duas vezes, idêntica");
    else if (intReleituraDiferentes > 0 || intReleituraIlegiveis > 0) lst.push_back("releitura diferente");
    if (intMascarados > 0) lst.push_back(std::to_string(intMascarados) + " setor(es) ilegível(is)");
    if (blnWmaIdentico == true) lst.push_back("FLAC idêntico ao lido");
    else if (blnWmaIdentico == false) lst.push_back("FLAC diferente do lido");
    std::string strNivel = Nivel() == NivelBitPerfect::Comprovada ? "BIT-PERFECT COMPROVADO"
                         : Nivel() == NivelBitPerfect::LidaDuasVezes ? "provavelmente bit-perfect"
                         : "NÃO comprovado";
    return strNivel + " — " + Junta(lst, "; ");
}

std::string TVerificacaoFaixa::ParaLinha() const
{
    return "verif=" + std::to_string(intFaixa) + ";ar=" + std::to_string(intConfiancaAr) + ";artotal=" + std::to_string(intTotalAr) +
           ";arversao=" + strVersaoAr + ";releitura=" + std::to_string(intReleituraDiferentes) + ";ilegiveis=" +
           std::to_string(intReleituraIlegiveis) + ";mascarados=" + std::to_string(intMascarados) + ";wma=" +
           (!blnWmaIdentico.has_value() ? "" : (*blnWmaIdentico ? "ok" : "diferente")) + ";wmafalha=" + Substitui(strFalhaWma, ";", ",");
}

std::unique_ptr<TVerificacaoFaixa> TVerificacaoFaixa::DeLinha(const std::string &strLinha)
{
    auto v = std::make_unique<TVerificacaoFaixa>();
    bool blnTem = false;
    for (const std::string &strParte : Divide(strLinha, ";"))
    {
        size_t i = strParte.find('=');
        if (i == std::string::npos) continue;
        std::string k = strParte.substr(0, i), x = strParte.substr(i + 1);
        if (k == "verif") { v->intFaixa = TRelatorioExtracao::Inteiro(x); blnTem = true; }
        else if (k == "ar") v->intConfiancaAr = TRelatorioExtracao::Inteiro(x);
        else if (k == "artotal") v->intTotalAr = TRelatorioExtracao::Inteiro(x);
        else if (k == "arversao") v->strVersaoAr = x;
        else if (k == "releitura") v->intReleituraDiferentes = TRelatorioExtracao::Inteiro(x);
        else if (k == "ilegiveis") v->intReleituraIlegiveis = TRelatorioExtracao::Inteiro(x);
        else if (k == "mascarados") v->intMascarados = TRelatorioExtracao::Inteiro(x);
        else if (k == "wma")
        {
            if (x == "ok") v->blnWmaIdentico = true;
            else if (x == "diferente") v->blnWmaIdentico = false;
            else v->blnWmaIdentico.reset();
        }
        else if (k == "wmafalha") v->strFalhaWma = x;
    }
    if (blnTem) return v;
    return nullptr;
}

} // namespace ARCA::Modelo

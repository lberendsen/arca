// ARCA no xOS - Modelo/AccurateRip (de Modelo\AccurateRip.cs).
#include "AccurateRip.h"
#include "ExternalMetadataClient.h"

#include "Base/Arquivo.h"
#include "Base/Excecoes.h"
#include "Base/Texto.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace ARCA::Modelo {

using namespace ARCA::Base;

namespace {
bool TentaHex(const std::string &s, int &v)
{
    std::string t = Trim(s);
    if (t.empty()) return false;
    char *fim = nullptr;
    long x = strtol(t.c_str(), &fim, 16);
    if (!fim || *fim) return false;
    v = (int) x;
    return true;
}

std::string HexMaiusculo(int v)
{
    char b[16];
    snprintf(b, sizeof b, "%X", (unsigned) v);
    return b;
}

uint32_t LeU32(const std::vector<uint8_t> &a, size_t p)     // BitConverter.ToUInt32 (little-endian)
{
    return (uint32_t) a[p] | ((uint32_t) a[p + 1] << 8) | ((uint32_t) a[p + 2] << 16) | ((uint32_t) a[p + 3] << 24);
}
} // namespace

// ===============================================================
//  TTocAudio
// ===============================================================

std::shared_ptr<TTocAudio> TTocAudio::DeMcdi(const std::string &strMcdi, int intFaixasDeAudio)
{
    if (strMcdi.empty()) return nullptr;
    std::string t = Trim(strMcdi);
    while (!t.empty() && t.back() == '\0') t.pop_back();          // TrimEnd('\0')
    std::vector<std::string> arr = Divide(t, "+");
    int n;
    if (arr.size() < 3 || !TentaHex(arr[0], n)) return nullptr;
    if ((int) arr.size() < n + 2) return nullptr;
    std::vector<int> arrEnd(n + 1);
    for (int i = 0; i <= n; i++)
        if (!TentaHex(arr[i + 1], arrEnd[i])) return nullptr;

    auto objToc = std::make_shared<TTocAudio>();
    int intAudio = intFaixasDeAudio > 0 && intFaixasDeAudio < n ? intFaixasDeAudio : n;
    for (int i = 0; i < intAudio; i++) objToc->lstInicios.push_back(arrEnd[i] - 150);
    if (intAudio < n)
    {
        objToc->intInicioDados = arrEnd[intAudio] - 150;
        objToc->intLeadOutFisico = arrEnd[n] - 150;
        objToc->intLeadOut = objToc->intInicioDados - GapSessaoCdExtra;
    }
    else objToc->intLeadOut = arrEnd[n] - 150;
    return objToc;
}

std::shared_ptr<TTocAudio> TTocAudio::DeCue(const std::string &strCue, int64_t lngBytesBin)
{
    if (!ArquivoExiste(strCue)) return nullptr;
    auto objToc = std::make_shared<TTocAudio>();
    std::string strTipo;
    int intNumero = 0, intLeadOut1 = -1;
    bool blnPrimeira = true;
    std::vector<int> lstDados;
    bool blnExato = false;
    for (const std::string &strLinha : LeLinhas(strCue))
    {
        std::string t = Trim(strLinha);
        std::vector<std::string> arr = Divide(t, " ", true);
        if (arr.size() >= 3 && IgualSemCaixa(arr[0], "TRACK"))
        {
            if (!TentaInt(arr[1], intNumero)) intNumero = 0;
            strTipo = Maiusculas(arr[2]);
        }
        else if (arr.size() >= 3 && IgualSemCaixa(arr[0], "INDEX") && arr[1] == "01")
        {
            int intLba = Msf(arr[2]);
            if (intLba < 0) continue;
            if (strTipo == "AUDIO")
            {
                if (blnPrimeira) { objToc->intPrimeiraFaixa = intNumero; blnPrimeira = false; }
                objToc->lstInicios.push_back(intLba);
            }
            // Dados antes de qualquer audio: CD mixed-mode (faixa 1 de dados).
            // Nao e sessao 2 e nao mexe no fim do audio.
            else if (objToc->lstInicios.empty()) objToc->intDadosAntes = intLba;
            else lstDados.push_back(intLba);
        }
        else if (arr.size() >= 3 && arr[0] == "REM" && IgualSemCaixa(arr[1], "LEAD-OUT"))
            intLeadOut1 = Msf(arr[2]);
        else if (arr.size() >= 3 && arr[0] == "REM" && IgualSemCaixa(arr[1], "LEAD-OUT-SESSION2"))
            objToc->intLeadOutFisico = Msf(arr[2]);
        else if (arr.size() >= 2 && arr[0] == "REM" && IgualSemCaixa(arr[1], "ARCA-LEADOUT-EXATO"))
            blnExato = true;
    }
    if (objToc->lstInicios.empty()) return nullptr;
    // Mesma janela do extrator: ate 11.600 setores (faixa real comeca a 11.700 ou mais).
    if (lstDados.size() >= 2 && lstDados[1] - lstDados[0] >= 11000 && lstDados[1] - lstDados[0] <= 11600)
    {
        // Protecao CDS-300: a "faixa fantasma" fica no lead-out da
        // sessao 1 - e ali que o audio acaba; os dados vem depois.
        objToc->intLeadOut = lstDados[0];
        objToc->intInicioDados = lstDados[1];
    }
    else if (lstDados.size() >= 1)
    {
        // CD Extra: o audio acaba no lead-out da sessao 1, que fica
        // 11.400 setores antes da faixa de dados - nao no inicio dela.
        objToc->intInicioDados = lstDados[0];
        objToc->intLeadOut = objToc->intInicioDados - GapSessaoCdExtra;
    }
    else
        objToc->intLeadOut = intLeadOut1 > 0 ? intLeadOut1 : (int) (lngBytesBin / 2352);
    // O drive disse onde a sessao de audio acaba (indice completo):
    // vale mais que qualquer estimativa acima.
    if (blnExato && intLeadOut1 > 0 && intLeadOut1 > objToc->lstInicios.back())
        objToc->intLeadOut = intLeadOut1;
    if (objToc->intInicioDados >= 0 && objToc->intLeadOutFisico < 0) objToc->intLeadOutFisico = (int) (lngBytesBin / 2352);
    return objToc;
}

int TTocAudio::Msf(const std::string &s)
{
    std::vector<std::string> p = Divide(s, ":");
    int m, sg, f;
    if (p.size() != 3 || !TentaInt(p[0], m) || !TentaInt(p[1], sg) || !TentaInt(p[2], f)) return -1;
    return (m * 60 + sg) * 75 + f;
}

std::string TTocAudio::ParaMcdi() const
{
    std::vector<int> lst(lstInicios);
    if (intDadosAntes >= 0) lst.insert(lst.begin(), intDadosAntes);   // mixed-mode: a faixa 1 e de dados
    int intFim = intLeadOut;
    if (intInicioDados >= 0) { lst.push_back(intInicioDados); intFim = intLeadOutFisico; }
    std::string sb = HexMaiusculo((int) lst.size());
    for (int intLba : lst) sb += "+" + HexMaiusculo(intLba + 150);
    sb += "+" + HexMaiusculo(intFim + 150);
    return sb;
}

// ===============================================================
//  Identificacao do disco e consulta a base
// ===============================================================

void AccurateRip::CalcularIds(const TTocAudio &objToc, uint32_t &id1, uint32_t &id2, uint32_t &idFreedb)
{
    id1 = 0; id2 = 0;
    int n = objToc.QtdFaixas();
    for (int i = 0; i < n; i++)
    {
        uint32_t uintLba = (uint32_t) objToc.lstInicios[i];
        id1 += uintLba;
        id2 += (uintLba > 0 ? uintLba : 1) * (uint32_t) (i + 1);
    }
    id1 += (uint32_t) objToc.intLeadOut;
    id2 += (uint32_t) objToc.intLeadOut * (uint32_t) (n + 1);

    // freedb: conta tambem a faixa de dados do CD Extra e o lead-out fisico.
    std::vector<int> lstTodas(objToc.lstInicios);
    if (objToc.intDadosAntes >= 0) lstTodas.insert(lstTodas.begin(), objToc.intDadosAntes);
    int intFim = objToc.intLeadOut;
    if (objToc.intInicioDados >= 0) { lstTodas.push_back(objToc.intInicioDados); intFim = objToc.intLeadOutFisico; }
    int intSoma = 0;
    for (int intLba : lstTodas)
    {
        int intSeg = (intLba + 150) / 75;
        while (intSeg > 0) { intSoma += intSeg % 10; intSeg /= 10; }
    }
    int intDuracao = (intFim + 150) / 75 - (lstTodas[0] + 150) / 75;
    idFreedb = ((uint32_t) (intSoma % 255) << 24) | ((uint32_t) intDuracao << 8) | (uint32_t) lstTodas.size();
}

std::string AccurateRip::Url(const TTocAudio &objToc)
{
    uint32_t id1, id2, idF;
    CalcularIds(objToc, id1, id2, idF);
    char b[256];
    snprintf(b, sizeof b, "http://www.accuraterip.com/accuraterip/%x/%x/%x/dBAR-%03d-%08x-%08x-%08x.bin",
             id1 & 0xF, (id1 >> 4) & 0xF, (id1 >> 8) & 0xF, objToc.QtdFaixas(), id1, id2, idF);
    return b;
}

std::optional<std::vector<AccurateRip::TPrensagem>> AccurateRip::Consultar(const TTocAudio &objToc, std::string &strUrl,
                                                                          std::string &strFalha)
{
    strFalha = "";
    strUrl = Url(objToc);
    std::vector<uint8_t> arrDados;
    try
    {
        NetworkConfiguration::CreateProxy();
        RespostaHttp resp = NetworkConfiguration::Responder(strUrl, {}, 20000, "ARCA");
        arrDados.assign(resp.corpo.begin(), resp.corpo.end());
    }
    catch (const WebException &ex)
    {
        if (NetworkConfiguration::StatusHttp(ex) == 404) return std::vector<TPrensagem>();   // disco fora da base
        strFalha = std::string("não consegui consultar o AccurateRip: ") + ex.what();
        return std::nullopt;
    }
    catch (const std::exception &ex)
    {
        strFalha = std::string("não consegui consultar o AccurateRip: ") + ex.what();
        return std::nullopt;
    }
    return Interpretar(arrDados, objToc, strFalha);
}

std::vector<AccurateRip::TPrensagem> AccurateRip::Interpretar(const std::vector<uint8_t> &arr, const TTocAudio &objToc,
                                                              std::string &strFalha)
{
    strFalha = "";
    uint32_t id1, id2, idF;
    CalcularIds(objToc, id1, id2, idF);
    std::vector<TPrensagem> lst;
    size_t p = 0;
    while (p + 13 <= arr.size())
    {
        int n = arr[p];
        uint32_t c1 = LeU32(arr, p + 1), c2 = LeU32(arr, p + 5), c3 = LeU32(arr, p + 9);
        p += 13;
        if (p + (size_t) n * 9 > arr.size()) break;
        // A resposta e do disco pedido; um bloco de outro disco nao vale.
        bool blnEste = n == objToc.QtdFaixas() && c1 == id1 && c2 == id2 && c3 == idF;
        TPrensagem objP;
        objP.arrConfianca.resize(n);
        objP.arrCrc.resize(n);
        for (int i = 0; i < n; i++)
        {
            objP.arrConfianca[i] = arr[p];
            objP.arrCrc[i] = LeU32(arr, p + 1);
            p += 9;
        }
        if (blnEste) lst.push_back(objP);
    }
    if (lst.empty() && !arr.empty()) strFalha = "a resposta do AccurateRip não é deste disco";
    return lst;
}

// ===============================================================
//  Assinaturas
// ===============================================================

void AccurateRip::Calcular(IFonteDisco &objFonte, const TTocAudio &objToc, const std::vector<int> &arrOffsets,
                           std::vector<std::vector<uint32_t>> &arrV1, std::vector<std::vector<uint32_t>> &arrV2,
                           const std::function<bool()> &cancelou)
{
    int n = objToc.QtdFaixas(), m = (int) arrOffsets.size();
    arrV1.assign(m, std::vector<uint32_t>(n, 0));
    arrV2.assign(m, std::vector<uint32_t>(n, 0));
    std::vector<int64_t> arrIni(n), arrLen(n), arrDe(n), arrAte(n);
    for (int t = 0; t < n; t++)
    {
        arrIni[t] = (int64_t) (objToc.lstInicios[t] - objFonte.LbaInicial()) * TTocAudio::AmostrasPorSetor;
        arrLen[t] = objToc.AmostrasDaFaixa(t);
        arrDe[t] = t == 0 ? AmostrasPontas : 1;
        arrAte[t] = t == n - 1 ? arrLen[t] - AmostrasPontas : arrLen[t];
    }
    std::vector<int> arrFaixaAtual(m, 0);

    objFonte.Reiniciar();
    std::vector<uint32_t> buf(65536);
    int64_t pos = 0;
    int lidos;
    while ((lidos = objFonte.Ler(buf.data(), (int) buf.size())) > 0)
    {
        for (int o = 0; o < m; o++)
        {
            int k = arrOffsets[o];
            int t = arrFaixaAtual[o];
            for (int j = 0; j < lidos; j++)
            {
                int64_t q = pos + j - k;                       // posicao no disco "corrigido"
                while (t < n && q >= arrIni[t] + arrLen[t]) t++;
                if (t >= n) break;
                if (q < arrIni[t]) continue;
                int64_t mul = q - arrIni[t] + 1;
                if (mul < arrDe[t] || mul > arrAte[t]) continue;
                uint32_t s = buf[j];
                arrV1[o][t] += s * (uint32_t) mul;
                uint64_t prod = (uint64_t) s * (uint64_t) (uint32_t) mul;
                arrV2[o][t] += (uint32_t) prod + (uint32_t) (prod >> 32);
            }
            arrFaixaAtual[o] = t;
        }
        pos += lidos;
        if (cancelou && cancelou()) throw OperationCanceledException();
    }
}

std::vector<int> AccurateRip::ProcurarOffsets(IFonteDisco &objFonte, const TTocAudio &objToc,
                                              const std::vector<TPrensagem> *lstBase,
                                              const std::function<bool()> &cancelou)
{
    std::vector<int> lstAchados;
    int n = objToc.QtdFaixas();
    if (n < 3 || lstBase == nullptr || lstBase->empty()) return lstAchados;

    // A faixa do meio mais curta: menos memoria, e sem as pontas cortadas.
    int intSonda = 1;
    for (int t = 2; t < n - 1; t++)
        if (objToc.AmostrasDaFaixa(t) < objToc.AmostrasDaFaixa(intSonda)) intSonda = t;
    int64_t lngIni = (int64_t) (objToc.lstInicios[intSonda] - objFonte.LbaInicial()) * TTocAudio::AmostrasPorSetor;
    int intN = (int) objToc.AmostrasDaFaixa(intSonda);
    int64_t lngJanIni = lngIni - AlcanceBusca;
    std::vector<uint32_t> s(intN + 2 * AlcanceBusca + 1, 0);   // fora do disco fica zero

    objFonte.Reiniciar();
    std::vector<uint32_t> buf(65536);
    int64_t pos = 0;
    int lidos;
    while ((lidos = objFonte.Ler(buf.data(), (int) buf.size())) > 0)
    {
        int64_t a = std::max(pos, lngJanIni), b = std::min(pos + lidos, lngJanIni + (int64_t) s.size());
        for (int64_t q = a; q < b; q++) s[q - lngJanIni] = buf[q - pos];
        pos += lidos;
        if (pos >= lngJanIni + (int64_t) s.size()) break;
        if (cancelou && cancelou()) throw OperationCanceledException();
    }

    std::set<uint32_t> setBase;
    for (const TPrensagem &objP : *lstBase) setBase.insert(objP.arrCrc[intSonda]);

    // k = -AlcanceBusca: a faixa comeca na posicao 0 da janela.
    uint32_t v1 = 0, w = 0;
    for (int i = 0; i < intN; i++) { v1 += s[i] * (uint32_t) (i + 1); w += s[i]; }
    for (int k = -AlcanceBusca;; k++)
    {
        if (setBase.count(v1)) lstAchados.push_back(k);
        if (k == AlcanceBusca) break;
        int j = k + AlcanceBusca;                    // inicio da janela no deslocamento k
        uint32_t sSai = s[j], sEntra = s[j + intN];
        v1 = v1 - w + (uint32_t) intN * sEntra;
        w = w - sSai + sEntra;
    }
    // List.Sort nao e estavel, mas so muda a ordem entre k e -k de mesmo modulo
    std::stable_sort(lstAchados.begin(), lstAchados.end(), [](int x, int y) { return std::abs(x) < std::abs(y); });
    return lstAchados;
}

TResultadoAccurateRip AccurateRip::Conferir(IFonteDisco &objFonte, const TTocAudio &objToc,
                                            const std::vector<int> &arrConhecidos,
                                            const std::function<bool()> &cancelou)
{
    TResultadoAccurateRip r;
    std::string strUrl, strFalha;
    std::optional<std::vector<TPrensagem>> lstBase = Consultar(objToc, strUrl, strFalha);
    r.strUrl = strUrl;
    if (!lstBase) { r.strFalha = strFalha; return r; }
    r.blnNaBase = !lstBase->empty();

    std::vector<int> lstPrimeira{0};
    for (int k : arrConhecidos)
        if (std::find(lstPrimeira.begin(), lstPrimeira.end(), k) == lstPrimeira.end()) lstPrimeira.push_back(k);

    std::vector<std::vector<uint32_t>> v1, v2;
    std::vector<int> arrK = lstPrimeira;
    Calcular(objFonte, objToc, arrK, v1, v2, cancelou);
    r = Melhor(r, objToc, *lstBase, v1, v2, arrK, strUrl);
    if (!r.blnNaBase || r.TodasConferem()) return r;

    std::vector<int> lstK = ProcurarOffsets(objFonte, objToc, &*lstBase, cancelou);
    lstK.erase(std::remove_if(lstK.begin(), lstK.end(),
                              [&](int k) { return std::find(lstPrimeira.begin(), lstPrimeira.end(), k) != lstPrimeira.end(); }),
               lstK.end());
    if (lstK.empty()) return r;
    arrK.assign(lstK.begin(), lstK.begin() + std::min<size_t>(3, lstK.size()));
    Calcular(objFonte, objToc, arrK, v1, v2, cancelou);
    return Melhor(r, objToc, *lstBase, v1, v2, arrK, strUrl);
}

TResultadoAccurateRip AccurateRip::Melhor(const TResultadoAccurateRip &rAtual, const TTocAudio &objToc,
                                          const std::vector<TPrensagem> &lstBase,
                                          const std::vector<std::vector<uint32_t>> &v1,
                                          const std::vector<std::vector<uint32_t>> &v2, const std::vector<int> &arrK,
                                          const std::string &strUrl)
{
    // Entre os deslocamentos calculados, o que mais confere (empate: o primeiro, mais proximo de zero).
    TResultadoAccurateRip r = rAtual;
    for (int o = 0; o < (int) arrK.size(); o++)
    {
        TResultadoAccurateRip rk;
        rk.blnNaBase = !lstBase.empty();
        rk.strUrl = strUrl;
        Preencher(rk, objToc, lstBase, v1, v2, o, arrK[o]);
        if (r.lstFaixas.empty() || rk.QtdConferem() > r.QtdConferem()) r = rk;
    }
    return r;
}

void AccurateRip::Preencher(TResultadoAccurateRip &r, const TTocAudio &objToc, const std::vector<TPrensagem> &lstBase,
                            const std::vector<std::vector<uint32_t>> &v1, const std::vector<std::vector<uint32_t>> &v2,
                            int o, int k)
{
    r.intOffset = k;
    r.lstFaixas.clear();
    for (int t = 0; t < objToc.QtdFaixas(); t++)
    {
        TFaixaAccurateRip f;
        f.intNumero = objToc.intPrimeiraFaixa + t;
        f.uintV1 = v1[o][t];
        f.uintV2 = v2[o][t];
        for (const TPrensagem &objP : lstBase)
        {
            f.intTotalNaBase += objP.arrConfianca[t];
            if (objP.arrCrc[t] == f.uintV2) { f.intConfianca += objP.arrConfianca[t]; f.strVersao = "v2"; }
            else if (objP.arrCrc[t] == f.uintV1) { f.intConfianca += objP.arrConfianca[t]; if (f.strVersao.empty()) f.strVersao = "v1"; }
        }
        r.lstFaixas.push_back(f);
    }
}

} // namespace ARCA::Modelo

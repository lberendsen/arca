// ARCA no xOS - Modelo/RenomeacaoFaixa (de Modelo\RenomeacaoFaixa.cs).
#include "RenomeacaoFaixa.h"

#include "Base/Acervo.h"
#include "Base/Arquivo.h"
#include "Base/Texto.h"
#include "Modelo/Aquisicao.h"
#include "Modelo/BancoObras.h"

#include <algorithm>
#include <filesystem>

namespace ARCA::Modelo {

using namespace ARCA::Base;

std::function<std::string(const std::string &)> TRenomeacaoFaixa::CaminhoSidecarOnda;
std::function<std::string(const std::string &)> TRenomeacaoFaixa::CaminhoSidecarEspectrograma;

namespace {
const char *const s_arrExtensoesPlaylist[] = {".wpl", ".m3u", ".m3u8", ".asx", ".zpl"};

// LINUX: o File.Move do .NET (o rename do Linux sobrescreveria o destino; o Validar ja recusa)
void Mover(const std::string &de, const std::string &para)
{
    std::error_code ec;
    if (Existe(para) && !IgualSemCaixa(de, para)) throw std::runtime_error("o destino já existe");
    fs::rename(de, para, ec);
    if (ec) throw std::runtime_error(ec.message());
}

// latim-1 <-> UTF-8, para regravar a playlist na codificação em que veio
std::string ParaLatin1(const std::string &s)
{
    std::string r;
    for (uint32_t cp : CodePoints(s)) r += cp < 256 ? (char) cp : '?';
    return r;
}
} // namespace

std::string TRenomeacaoFaixa::Validar(const std::string &strCaminhoAtual, const std::string &strNovoSemExtensao)
{
    if (strNovoSemExtensao.empty() || Trim(strNovoSemExtensao).empty())
        return "O nome do arquivo não pode ficar vazio.";
    if (strNovoSemExtensao != Trim(strNovoSemExtensao))
        return "O nome não pode começar nem terminar com espaço.";
    if (TerminaCom(strNovoSemExtensao, "."))
        return "O nome não pode terminar com ponto.";
    if (strNovoSemExtensao.find_first_of(CaracteresInvalidosEmNome()) != std::string::npos)
        return "O nome tem caractere que o Windows não aceita (\\ / : * ? \" < > |).";

    std::string strNovoCaminho = Combina(PastaDe(strCaminhoAtual), strNovoSemExtensao + Extensao(strCaminhoAtual));
    // LINUX: o caminho do xOS ("/mnt/DADOS/Músicas/...") e 8 caracteres mais longo que o da
    // Windows 7 ("D:\Músicas\..."); o limite continua o do Windows, contado aqui (fica mais justo)
    if (Caracteres(strNovoCaminho) >= 260)
        return "O caminho completo ficaria longo demais para o Windows (260 caracteres).";
    bool blnSoMaiusculas = IgualSemCaixa(strNovoCaminho, strCaminhoAtual);
    if (!blnSoMaiusculas && Existe(strNovoCaminho))
        return "Já existe outro arquivo com esse nome na pasta do álbum.";
    return "";
}

TResultadoRenomeacao TRenomeacaoFaixa::Renomear(const std::string &strCaminhoAtual, const std::string &strNovoSemExtensao,
                                                const std::string &strRaizAcervo, const std::string &strArtista,
                                                const std::string &strAlbum)
{
    TResultadoRenomeacao objR;
    objR.strFalha = Validar(strCaminhoAtual, strNovoSemExtensao);
    if (!objR.strFalha.empty()) return objR;

    std::string strPasta = PastaDe(strCaminhoAtual);
    std::string strNovo = Combina(strPasta, strNovoSemExtensao + Extensao(strCaminhoAtual));
    if (strNovo == strCaminhoAtual)
    {
        objR.strFalha = "O nome não mudou.";
        return objR;
    }

    // 1. O arquivo. Só maiúsculas/minúsculas: o NTFS acha que é o mesmo
    // nome, então passa por um nome temporário.
    try
    {
        GravacaoNoAcervo objGravacao;   // LINUX: o acervo so de leitura abre durante a troca
        if (IgualSemCaixa(strNovo, strCaminhoAtual))
        {
            std::string strTemp = strCaminhoAtual + ".renomeando";
            Mover(strCaminhoAtual, strTemp);
            try { Mover(strTemp, strNovo); }
            catch (...) { Mover(strTemp, strCaminhoAtual); throw; }
        }
        else Mover(strCaminhoAtual, strNovo);
    }
    catch (const std::exception &ex)
    {
        // LINUX: "O Windows não deixou renomear" no original
        objR.strFalha = std::string("O sistema não deixou renomear: ") + ex.what();
        return objR;
    }
    objR.blnOk = true;
    objR.strCaminhoNovo = strNovo;
    objR.lstFeito.push_back("arquivo");

    // 2. Caches: o conteúdo continua valendo (tamanho e data do
    // arquivo não mudam com a renomeação); só a chave muda.
    int intCaches = 0;
    if (CaminhoSidecarOnda) intCaches += MoverSidecar(CaminhoSidecarOnda(strCaminhoAtual), CaminhoSidecarOnda(strNovo), objR);
    if (CaminhoSidecarEspectrograma)
        intCaches += MoverSidecar(CaminhoSidecarEspectrograma(strCaminhoAtual), CaminhoSidecarEspectrograma(strNovo), objR);
    if (intCaches > 0) objR.lstFeito.push_back(intCaches == 1 ? "1 cache" : std::to_string(intCaches) + " caches");

    // 3. Playlists.
    int intPlaylists = AtualizarPlaylists(strCaminhoAtual, strNovo, objR);
    if (intPlaylists > 0) objR.lstFeito.push_back(intPlaylists == 1 ? "1 playlist" : std::to_string(intPlaylists) + " playlists");

    // 4. Obras: a faixa enviada sozinha para uma obra aponta pelo
    // caminho. (Álbum aponta pela pasta, que não muda aqui.)
    if (!strRaizAcervo.empty())
    {
        try
        {
            TBancoObras objBanco(strRaizAcervo);
            if (objBanco.Existe() && objBanco.AtualizarCaminhoDaFaixa(strCaminhoAtual, strNovo) > 0)
                objR.lstFeito.push_back("obras");
        }
        catch (const std::exception &ex)
        {
            objR.lstAvisos.push_back(std::string("o banco de obras não foi atualizado (") + ex.what() +
                                     ") — a faixa aparecerá como não encontrada na obra");
        }
    }

    // 5. Ledger.
    if (!strRaizAcervo.empty() && PastaExiste(strRaizAcervo))
    {
        if (Integracao::RegistrarRenomeacaoNoLedger(strRaizAcervo, strArtista, strAlbum, strPasta,
                                                    NomeDoArquivo(strCaminhoAtual), strNovo))
            objR.lstFeito.push_back("ledger");
        else
            objR.lstAvisos.push_back("não consegui registrar no ARCA_Ledger.jsonl — anote a renomeação à mão");
    }
    return objR;
}

int TRenomeacaoFaixa::MoverSidecar(const std::string &strDe, const std::string &strPara, TResultadoRenomeacao &objR)
{
    try
    {
        if (strDe.empty() || !ArquivoExiste(strDe)) return 0;
        if (ArquivoExiste(strPara)) fs::remove(strPara);
        Mover(strDe, strPara);
        return 1;
    }
    catch (const std::exception &ex)
    {
        objR.lstAvisos.push_back(std::string("um cache não foi movido (") + ex.what() + ") — será refeito ao abrir a faixa");
        return 0;
    }
}

/// <summary>
/// Troca o caminho em cada playlist de Minhas Músicas\Playlists, na
/// forma absoluta e na relativa à playlist (o .wpl do WMP costuma
/// gravar "..\Artista\Álbum\faixa.wma"). Mantém a codificação do
/// arquivo e só o regrava se algo mudou.
/// </summary>
int TRenomeacaoFaixa::AtualizarPlaylists(const std::string &strAntigo, const std::string &strNovo, TResultadoRenomeacao &objR)
{
    // LINUX: Minhas Músicas é a raiz do acervo
    std::string strPastaPlaylists = Combina(PastaAcervo(), "Playlists");
    if (!PastaExiste(strPastaPlaylists)) return 0;

    int intAtualizadas = 0;
    std::vector<std::string> arrArquivos;
    try
    {
        for (const auto &e : fs::recursive_directory_iterator(strPastaPlaylists, fs::directory_options::skip_permission_denied))
            if (e.is_regular_file()) arrArquivos.push_back(e.path().string());
    }
    catch (...) { return 0; }
    for (const std::string &strPlaylist : arrArquivos)
    {
        std::string ext = Minusculas(Extensao(strPlaylist));
        if (std::find_if(std::begin(s_arrExtensoesPlaylist), std::end(s_arrExtensoesPlaylist),
                         [&](const char *x) { return ext == x; }) == std::end(s_arrExtensoesPlaylist))
            continue;
        try
        {
            // LINUX: o StreamReader(Encoding.Default, detectar BOM): UTF-8 (com ou sem BOM) ou, se
            // nao for UTF-8 valido, o latim-1 da Windows 7. UTF-16 nao e tratado.
            std::vector<uint8_t> arrBytes = LeBytes(strPlaylist);
            std::string strBruto(arrBytes.begin(), arrBytes.end());
            bool blnBom = ComecaCom(strBruto, "\xEF\xBB\xBF");
            if (blnBom) strBruto = strBruto.substr(3);
            std::string strTexto = Utf8Valido(strBruto);
            bool blnLatin1 = strTexto != strBruto;

            std::string strNovoTexto = strTexto;
            std::string strDirPlaylist = PastaDe(strPlaylist);
            const std::string arrPares[2][2] = {{strAntigo, strNovo},
                                                {Relativo(strDirPlaylist, strAntigo), Relativo(strDirPlaylist, strNovo)}};
            for (const auto &arrPar : arrPares)
            {
                if (arrPar[0].empty()) continue;
                // Em XML (.wpl, .asx) o & do caminho vem como &amp;.
                strNovoTexto = Trocar(strNovoTexto, arrPar[0], arrPar[1]);
                strNovoTexto = Trocar(strNovoTexto, Substitui(arrPar[0], "&", "&amp;"), Substitui(arrPar[1], "&", "&amp;"));
            }
            if (strNovoTexto == strTexto) continue;

            std::string strSaida = (blnBom ? "\xEF\xBB\xBF" : "") + (blnLatin1 ? ParaLatin1(strNovoTexto) : strNovoTexto);
            GravacaoNoAcervo objGravacao;
            GravaTexto(strPlaylist, strSaida);
            intAtualizadas++;
        }
        catch (const std::exception &ex)
        {
            objR.lstAvisos.push_back("a playlist " + NomeDoArquivo(strPlaylist) + " não foi atualizada (" + ex.what() + ")");
        }
    }
    return intAtualizadas;
}

/// <summary>Troca sem diferenciar maiúsculas — o Windows também não diferencia caminhos.</summary>
std::string TRenomeacaoFaixa::Trocar(const std::string &strTexto, const std::string &strDe, const std::string &strPara)
{
    if (strDe.empty() || Indice(strTexto, strDe, true) < 0) return strTexto;
    std::string r;
    size_t i = 0;
    while (true)
    {
        int64_t p = Indice(strTexto, strDe, true, i);
        if (p < 0) break;
        r += strTexto.substr(i, (size_t) p - i);
        r += strPara;
        i = (size_t) p + strDe.size();
    }
    r += strTexto.substr(i);
    return r;
}

/// <summary>O caminho do arquivo relativo à pasta da playlist ("..\Artista\Álbum\faixa.wma"), ou null.</summary>
// LINUX: com "/" (o separador do sistema); "" = null
std::string TRenomeacaoFaixa::Relativo(const std::string &strPasta, const std::string &strArquivo)
{
    try
    {
        std::string r = fs::path(strArquivo).lexically_relative(fs::path(strPasta)).string();
        return r.empty() || r == "." ? "" : r;
    }
    catch (...) { return ""; }
}

} // namespace ARCA::Modelo

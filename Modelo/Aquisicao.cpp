// ARCA no xOS - Modelo/Aquisicao (de Modelo\Aquisicao.cs).
#include "Aquisicao.h"

#include "Base/Acervo.h"
#include "Base/Arquivo.h"
#include "Base/Excecoes.h"
#include "Base/Hash.h"
#include "Base/Texto.h"
#include "Interop/EditorFlac.h"
#include "Modelo/RelatorioExtracao.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fnmatch.h>
#include <regex>
#include <unistd.h>

namespace ARCA::Modelo {

using namespace ARCA::Base;

namespace {

// Array.Sort(arquivos, StringComparer.OrdinalIgnoreCase)
bool MenorSemCaixa(const std::string &a, const std::string &b)
{
    return Maiusculas(a) < Maiusculas(b);
}

void ApagarArquivo(const std::string &c)
{
    // File.Delete: arquivo que não existe não é erro; o resto lança
    fs::remove(fs::u8path(c));
}

// Directory.Delete(pasta, false): só pasta vazia; senão lança
void ApagarPastaVazia(const std::string &c)
{
    fs::path p = fs::u8path(c);
    if (!fs::is_directory(p)) throw IOException("A pasta " + c + " não existe.");
    if (!fs::is_empty(p)) throw IOException("A pasta " + c + " não está vazia.");
    fs::remove(p);
}

// File.Move(origem, destino): o destino não pode existir
void Mover(const std::string &strOrigem, const std::string &strDestino)
{
    if (Existe(strDestino)) throw IOException("Não é possível criar um arquivo já existente: " + strDestino);
    fs::rename(fs::u8path(strOrigem), fs::u8path(strDestino));
}

// File.Replace(origem, destino, backup): o destino vira o backup e a origem toma o lugar dele.
// LINUX: o File.Replace do Windows faz as duas trocas de nome numa operação só. Aqui são dois
// rename; se o segundo falhar, o backup volta a ser o arquivo, e a exceção sobe como lá.
void Substituir(const std::string &strOrigem, const std::string &strDestino, const std::string &strBackup)
{
    fs::rename(fs::u8path(strDestino), fs::u8path(strBackup));
    try
    {
        fs::rename(fs::u8path(strOrigem), fs::u8path(strDestino));
    }
    catch (...)
    {
        try { fs::rename(fs::u8path(strBackup), fs::u8path(strDestino)); } catch (...) {}
        throw;
    }
}

// using (orig = FileStream(Open)) using (dest = FileStream(CreateNew)) { orig.CopyTo(dest); dest.Flush(true); }
void CopiarAteODisco(const std::string &strOrigem, const std::string &strParcial)
{
    FILE *orig = fopen(strOrigem.c_str(), "rb");
    if (!orig) throw IOException("Não foi possível abrir " + strOrigem + ": " + strerror(errno));
    FILE *dest = fopen(strParcial.c_str(), "wbx");     // "x": CreateNew, falha se já existir
    if (!dest)
    {
        int e = errno;
        fclose(orig);
        throw IOException("Não foi possível criar " + strParcial + ": " + strerror(e));
    }
    std::vector<uint8_t> buf(1 << 20);
    bool blnOk = true;
    int intErro = 0;
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), orig)) > 0)
        if (fwrite(buf.data(), 1, n, dest) != n) { blnOk = false; intErro = errno; break; }
    if (blnOk && ferror(orig)) { blnOk = false; intErro = errno; }
    fclose(orig);
    // até o disco, não só até o cache
    if (blnOk && (fflush(dest) != 0 || fsync(fileno(dest)) != 0)) { blnOk = false; intErro = errno; }
    if (fclose(dest) != 0 && blnOk) { blnOk = false; intErro = errno; }
    if (!blnOk) throw IOException("Erro ao gravar " + strParcial + ": " + strerror(intErro));
    SincronizaArquivo(strParcial);     // e a pasta, para o nome também estar no disco
}

// Directory.GetFiles(pasta, padrao): o padrão do Windows não distingue maiúsculas
std::vector<std::string> ArquivosQueCasam(const std::string &pasta, const std::string &padrao)
{
    std::vector<std::string> r;
    std::string strPadrao = Minusculas(padrao);
    for (const std::string &a : Arquivos(pasta))
        if (fnmatch(strPadrao.c_str(), Minusculas(NomeDoArquivo(a)).c_str(), 0) == 0) r.push_back(a);
    return r;
}

} // namespace

const std::vector<std::string> Integracao::SubprodutosDaExtracao = {
    "AudioCD.bin", "AudioCD.cue", "cover.jpg", TRelatorioExtracao::NomeArquivo, "aquisicao_detalhada.log"
};

std::vector<ItemManifesto> Integracao::CongelarManifesto(const std::string &pastaTrabalho,
                                                         const std::string &padrao, Relato relatar)
{
    std::vector<ItemManifesto> manifesto;
    if (!PastaExiste(pastaTrabalho)) return manifesto;

    std::vector<std::string> arquivosList;
    // LINUX: o padrão das faixas era "*.wma"; no xOS são "*.flac". Os dois levam ao mesmo caminho,
    // para o chamador portado poder passar qualquer um.
    if (!padrao.empty() && padrao != "*.wma" && padrao != "*.flac")
    {
        std::vector<std::string> lst = ArquivosQueCasam(pastaTrabalho, padrao);
        arquivosList.insert(arquivosList.end(), lst.begin(), lst.end());
    }
    else
    {
        // Inclui arquivos de áudio FLAC (WMA no original) e quaisquer arquivos de vídeo e mídia extraídos (Enhanced CD)
        // LINUX: ".flac" no lugar de ".wma".
        static const std::vector<std::string> arrExtensoesValidas = { ".flac", ".mpg", ".mpeg", ".avi", ".mov", ".wmv", ".mp4", ".mkv" };
        for (const std::string &a : Arquivos(pastaTrabalho))
        {
            std::string ext = Minusculas(Extensao(a));
            if (std::find(arrExtensoesValidas.begin(), arrExtensoesValidas.end(), ext) != arrExtensoesValidas.end())
            {
                arquivosList.push_back(a);
            }
        }
    }

    std::vector<std::string> arquivos = arquivosList;
    std::sort(arquivos.begin(), arquivos.end(), MenorSemCaixa);

    for (const std::string &a : arquivos)
    {
        if (relatar) relatar("Conferindo " + NomeDoArquivo(a) + "…");
        ItemManifesto it;
        it.Nome = NomeDoArquivo(a);
        it.Origem = a;
        it.Tamanho = Tamanho(a);
        it.Sha256 = Hash(a);
        manifesto.push_back(it);
    }

    // Capa em resolução plena e páginas do encarte, digitalizadas na
    // AQUISIÇÃO. Entram no manifesto como "Artwork\nome.jpg": passam
    // pelas mesmas quatro passagens que as faixas e chegam à subpasta
    // Artwork do álbum, que é onde EM EXECUÇÃO procura o encarte.
    // LINUX: o separador é "/" ("Artwork/nome.jpg"), que é o que o Path.Combine dá aqui; o
    // EhArtwork reconhece os dois.
    std::string strArtwork = Combina(pastaTrabalho, PastaArtwork);
    if (PastaExiste(strArtwork))
    {
        std::vector<std::string> lstImagens;
        for (const std::string &a : Arquivos(strArtwork))
        {
            std::string ext = Minusculas(Extensao(a));
            if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".tif" || ext == ".tiff") lstImagens.push_back(a);
        }
        std::sort(lstImagens.begin(), lstImagens.end(), MenorSemCaixa);
        for (const std::string &a : lstImagens)
        {
            if (relatar) relatar("Conferindo " + std::string(PastaArtwork) + "/" + NomeDoArquivo(a) + "…");
            ItemManifesto it;
            it.Nome = Combina(PastaArtwork, NomeDoArquivo(a));
            it.Origem = a;
            it.Tamanho = Tamanho(a);
            it.Sha256 = Hash(a);
            manifesto.push_back(it);
        }
    }
    return manifesto;
}

bool Integracao::EhArtwork(const std::string &strNome)
{
    // LINUX: "Artwork/" (o separador daqui) ou "Artwork\" (o do Windows, que o ledger antigo tem).
    return ComecaCom(strNome, std::string(PastaArtwork) + "/", true) || ComecaCom(strNome, std::string(PastaArtwork) + "\\", true);
}

ResultadoIntegracao Integracao::Integrar(const std::vector<ItemManifesto> &manifesto,
                                         const std::string &pastaTrabalho,
                                         const std::string &raizDoAcervo,
                                         const std::string &artista, const std::string &album,
                                         const std::string &idDoDisco,
                                         Relato relatar,
                                         ProgressoIntegracao progresso)
{
    ResultadoIntegracao r;
    if (manifesto.empty())
    {
        r.Erro = "Não há manifesto — nada foi extraído nesta sessão.";
        return r;
    }

    try
    {
        // LINUX: o DADOS fica em leitura e escrita do começo ao fim da integração, e volta a só
        // leitura ao sair daqui (dando certo ou não). Não abrir é falha da integração, antes de
        // qualquer coisa ser tocada.
        GravacaoNoAcervo objGravacao;

        std::string destino = Combina(Combina(raizDoAcervo, Limpar(artista, "Desconhecido")),
                                      Limpar(album, "Álbum desconhecido"));
        r.PastaDestino = destino;
        // ARCA-ORIGINAL: a pasta do álbum é criada antes da passagem 1; se ela recusar (origem
        // mudou), fica no acervo uma pasta vazia.
        if (!PastaExiste(destino)) CriaPasta(destino);

        // ---- 1/4 — as origens ainda são as que foram aprovadas? ----
        Relatar(relatar, "1/4 — conferindo a integridade das origens…");
        if (progresso) progresso(15.0, "Passo 1/4: Conferindo integridade das origens…");
        for (const ItemManifesto &it : manifesto)
        {
            if (!ArquivoExiste(it.Origem))
                throw InvalidOperationException("O arquivo " + it.Nome + " sumiu da origem.");
            if (Tamanho(it.Origem) != it.Tamanho)
                throw InvalidOperationException("O tamanho de " + it.Nome + " mudou desde a extração.");
            if (Hash(it.Origem) != it.Sha256)
                throw InvalidOperationException("O conteúdo de " + it.Nome + " mudou desde a extração.");
        }

        // ---- 2/4 — copiar ao lado do destino e conferir a cópia ----
        Relatar(relatar, "2/4 — copiando e conferindo…");
        int intIndexCopia = 0;
        for (const ItemManifesto &it : manifesto)
        {
            intIndexCopia++;
            double dblPctCopia = 25.0 + ((double) intIndexCopia / manifesto.size() * 40.0);
            if (progresso)
            {
                progresso(dblPctCopia, "Passo 2/4: Copiando " + it.Nome + " (" + std::to_string(intIndexCopia) + "/" +
                                           std::to_string(manifesto.size()) + ")");
            }

            std::string final = Combina(destino, it.Nome);
            std::string parcial = final + ".partial";
            // Itens do encarte moram numa subpasta (Artwork\…).
            std::string strPastaFinal = PastaDe(final);
            if (!PastaExiste(strPastaFinal)) CriaPasta(strPastaFinal);
            if (ArquivoExiste(parcial)) ApagarArquivo(parcial);

            Relatar(relatar, "   " + it.Nome);
            CopiarAteODisco(it.Origem, parcial);

            if (Tamanho(parcial) != it.Tamanho)
                throw IOException("A cópia de " + it.Nome + " saiu com tamanho diferente.");
            if (Hash(parcial) != it.Sha256)
                throw IOException("A cópia de " + it.Nome + " não confere com a origem.");
        }

        // ---- 3/4 — promover. Só aqui algo existente é tocado. ----
        Relatar(relatar, "3/4 — promovendo ao acervo…");
        int intIndexPromo = 0;
        for (const ItemManifesto &it : manifesto)
        {
            intIndexPromo++;
            double dblPctPromo = 65.0 + ((double) intIndexPromo / manifesto.size() * 25.0);
            if (progresso)
            {
                progresso(dblPctPromo, "Passo 3/4: Promovendo " + it.Nome + " (" + std::to_string(intIndexPromo) + "/" +
                                           std::to_string(manifesto.size()) + ")");
            }

            std::string final = Combina(destino, it.Nome);
            std::string parcial = final + ".partial";

            if (ArquivoExiste(final))
            {
                if (Hash(final) == it.Sha256)
                {
                    ApagarArquivo(parcial);          // já existe idêntico
                }
                else
                {
                    // Backup datado e único: um nome fixo (".backup") era
                    // apagado na substituição seguinte, e o original se perdia.
                    Substituir(parcial, final, NomeDeBackup(final));
                }
            }
            else
            {
                Mover(parcial, final);
            }

            if (Hash(final) != it.Sha256)
                throw IOException("O arquivo promovido " + it.Nome + " não confere.");

            r.Integrados.push_back(final);
        }

        // ---- 4/4 — registro e limpeza ----
        Relatar(relatar, "4/4 — registrando e limpando…");
        if (progresso) progresso(92.0, "Passo 4/4: Gravando ledger e limpando quarentena…");
        // Sempre: o ledger é a procedência do acervo. Sem DiscID a entrada
        // perde um campo, mas o que entrou, onde e com que hash continua
        // valendo. Pular a entrada inteira foi o que deixou de fora dois
        // discos em 02/10/2026.
        std::string strIdLedger = (idDoDisco.empty() || idDoDisco == "UnknownDisc") ? "" : idDoDisco;
        // Se a extração mascarou setores, a procedência tem de dizer.
        std::shared_ptr<TRelatorioExtracao> objRelatorio = TRelatorioExtracao::Ler(pastaTrabalho);
        if (!EscreverLedger(raizDoAcervo, artista, album, destino, strIdLedger, manifesto, "", objRelatorio.get()))
            Relatar(relatar, "ATENÇÃO: não consegui gravar o ARCA_Ledger.jsonl. A integração foi feita; registre a procedência à mão.");

        for (const ItemManifesto &it : manifesto)
            if (ArquivoExiste(it.Origem)) ApagarArquivo(it.Origem);
        // A subpasta Artwork da área de trabalho só sai se ficou vazia.
        try { ApagarPastaVazia(Combina(pastaTrabalho, PastaArtwork)); } catch (...) {}

        // cover.jpg vai para o álbum pelo mesmo caminho conferido do
        // encarte: se já houver um diferente, ele vira backup em vez de
        // ser sobrescrito.
        std::string capaOrigem = Combina(pastaTrabalho, "cover.jpg");
        if (ArquivoExiste(capaOrigem))
        {
            try { GravarNoAcervo(LeBytes(capaOrigem), Combina(destino, "cover.jpg")); }
            catch (const std::exception &exCapa)
            {
                Relatar(relatar, std::string("ATENÇÃO: cover.jpg não foi para o álbum (") + exCapa.what() + "); ficou na área de trabalho.");
            }
        }

        // Sai da área de trabalho só o que se sabe o que é: o que foi
        // para o acervo (apagado acima) e os subprodutos da extração.
        // Qualquer outra coisa — um FLAC (WMA no original) que não entrou no manifesto, um
        // arquivo posto à mão — fica, e é listado. Apagar "tudo" foi o
        // que fazia sumir material que nunca chegou ao acervo.
        for (const std::string &strSub : SubprodutosDaExtracao)
        {
            std::string p = Combina(pastaTrabalho, strSub);
            if (strSub == "cover.jpg" && !ArquivoExiste(Combina(destino, "cover.jpg"))) continue;   // não foi: fica
            try { if (ArquivoExiste(p)) ApagarArquivo(p); } catch (...) {}
        }
        try
        {
            for (const std::string &strWav : Arquivos(pastaTrabalho, ".wav"))
                try { ApagarArquivo(strWav); } catch (...) {}
        }
        catch (...) {}

        try
        {
            if (PastaExiste(pastaTrabalho))
            {
                for (auto &d : fs::recursive_directory_iterator(fs::u8path(pastaTrabalho)))
                {
                    if (!d.is_regular_file()) continue;    // GetFiles: só arquivos
                    // strResto.Substring(pastaTrabalho.Length).TrimStart('\\') (LINUX: '/')
                    std::string strResto = d.path().u8string().substr(pastaTrabalho.size());
                    size_t p = strResto.find_first_not_of('/');
                    r.NaoIntegrados.push_back(p == std::string::npos ? "" : strResto.substr(p));
                }
                // LINUX: o NTFS entregava os nomes em ordem alfabética; o ext4 não tem ordem.
                std::sort(r.NaoIntegrados.begin(), r.NaoIntegrados.end());
                if (r.NaoIntegrados.empty())
                {
                    try { ApagarPastaVazia(Combina(pastaTrabalho, PastaArtwork)); } catch (...) {}
                    ApagarPastaVazia(pastaTrabalho);
                }
                else
                    Relatar(relatar, std::to_string(r.NaoIntegrados.size()) +
                                         " arquivo(s) não faziam parte do que foi aprovado e ficaram na área de trabalho: " +
                                         Junta(r.NaoIntegrados, ", "));
            }
        }
        catch (...) {}

        r.Arquivos = (int) manifesto.size();
        r.Ok = true;
        if (progresso) progresso(100.0, "Integrado ao acervo com sucesso!");
        return r;
    }
    catch (const std::exception &ex)
    {
        r.Erro = ex.what();
        return r;
    }
}

void Integracao::GravarNoAcervo(const std::vector<uint8_t> &arrDados, const std::string &strFinal)
{
    // LINUX: o DADOS em leitura e escrita enquanto se grava (aninha com o da integração).
    GravacaoNoAcervo objGravacao;

    std::string strPasta = PastaDe(strFinal);
    if (!PastaExiste(strPasta)) CriaPasta(strPasta);
    std::string strEsperado = Sha256Hex(arrDados);

    if (ArquivoExiste(strFinal) && Hash(strFinal) == strEsperado) return;   // já é este

    std::string strParcial = strFinal + ".partial";
    if (ArquivoExiste(strParcial)) ApagarArquivo(strParcial);
    {
        // FileMode.CreateNew + Write + Flush(true)
        FILE *dest = fopen(strParcial.c_str(), "wbx");
        if (!dest) throw IOException("Não foi possível criar " + strParcial + ": " + strerror(errno));
        bool blnOk = arrDados.empty() || fwrite(arrDados.data(), 1, arrDados.size(), dest) == arrDados.size();
        int intErro = blnOk ? 0 : errno;
        if (blnOk && (fflush(dest) != 0 || fsync(fileno(dest)) != 0)) { blnOk = false; intErro = errno; }
        if (fclose(dest) != 0 && blnOk) { blnOk = false; intErro = errno; }
        if (!blnOk) throw IOException("Erro ao gravar " + strParcial + ": " + strerror(intErro));
        SincronizaArquivo(strParcial);
    }
    if (Hash(strParcial) != strEsperado)
    {
        ApagarArquivo(strParcial);
        throw IOException("A gravação de " + NomeDoArquivo(strFinal) + " não confere.");
    }

    if (ArquivoExiste(strFinal))
        Substituir(strParcial, strFinal, NomeDeBackup(strFinal));
    else
        Mover(strParcial, strFinal);

    if (Hash(strFinal) != strEsperado)
        throw IOException("O arquivo " + NomeDoArquivo(strFinal) + " não confere depois de gravado.");
}

std::string Integracao::RetirarDoAcervo(const std::string &strArquivo)
{
    // LINUX: o DADOS em leitura e escrita enquanto se troca o nome.
    GravacaoNoAcervo objGravacao;
    std::string strBackup = NomeDeBackup(strArquivo);
    Mover(strArquivo, strBackup);
    return strBackup;
}

/// nome.backup-aaaaMMddHHmmss, com -2, -3… se já existir: duas
/// substituições do mesmo arquivo no mesmo segundo (girar a capa e
/// gravar outra por cima) não podem apagar o primeiro backup.
std::string Integracao::NomeDeBackup(const std::string &strArquivo)
{
    std::string strBase = strArquivo + ".backup-" + AgoraCarimbo();
    std::string strBackup = strBase;
    for (int i = 2; ArquivoExiste(strBackup); i++) strBackup = strBase + "-" + std::to_string(i);
    return strBackup;
}

bool Integracao::RegistrarEncarteNoLedger(const std::string &raiz, const std::string &artista, const std::string &album,
                                          const std::string &pastaAlbum, const std::vector<std::string> &arquivos,
                                          const std::string &evento)
{
    std::vector<ItemManifesto> lstItens;
    for (const std::string &a : arquivos)
    {
        // LINUX: o separador é "/".
        std::string strRelativo = ComecaCom(a, pastaAlbum, true)
            ? Trim(a.substr(pastaAlbum.size()), "/") : NomeDoArquivo(a);
        ItemManifesto it;
        it.Nome = strRelativo;
        it.Origem = a;
        it.Tamanho = Tamanho(a);
        it.Sha256 = Hash(a);
        lstItens.push_back(it);
    }
    return EscreverLedger(raiz, artista, album, pastaAlbum, "", lstItens, evento);
}

bool Integracao::RegistrarRenomeacaoNoLedger(const std::string &raiz, const std::string &artista, const std::string &album,
                                             const std::string &pastaAlbum, const std::string &strNomeAntigo,
                                             const std::string &strCaminhoNovo)
{
    ItemManifesto it;
    it.Nome = NomeDoArquivo(strCaminhoNovo);
    it.Origem = strCaminhoNovo;
    it.Tamanho = Tamanho(strCaminhoNovo);
    it.Sha256 = Hash(strCaminhoNovo);
    std::vector<ItemManifesto> lstItens = { it };
    return EscreverLedger(raiz, artista, album, pastaAlbum, "", lstItens, "arquivo renomeado; antes: " + strNomeAntigo);
}

bool Integracao::EscreverLedger(const std::string &raiz, const std::string &artista, const std::string &album,
                                const std::string &destino, const std::string &idDoDisco,
                                const std::vector<ItemManifesto> &manifesto)
{
    return EscreverLedger(raiz, artista, album, destino, idDoDisco, manifesto, "");
}

/// Registro append-only de tudo que entrou no acervo: o que era o
/// `ARCAwm_Ledger.jsonl`. Uma linha por aquisição, e nunca se reescreve
/// — é o histórico de proveniência.
bool Integracao::EscreverLedger(const std::string &raiz, const std::string &artista, const std::string &album,
                                const std::string &destino, const std::string &idDoDisco,
                                const std::vector<ItemManifesto> &manifesto, const std::string &strEvento,
                                const TRelatorioExtracao *objRelatorio)
{
    try
    {
        std::string sb;
        sb.append("{\"quando\":\"").append(AgoraIso());
        if (!strEvento.empty())
            sb.append("\",\"evento\":\"").append(Escapar(strEvento));
        sb.append("\",\"idDoDisco\":\"").append(Escapar(idDoDisco))
          .append("\",\"artista\":\"").append(Escapar(artista))
          .append("\",\"album\":\"").append(Escapar(album))
          .append("\",\"destino\":\"").append(Escapar(destino)).append("\"");
        if (objRelatorio != nullptr)
        {
            sb.append(",\"extracao\":{\"exata\":").append(objRelatorio->Exata() ? "true" : "false")
              .append(",\"setoresMascarados\":").append(std::to_string(objRelatorio->intSetoresMascarados))
              .append(",\"setoresRecuperados\":").append(std::to_string(objRelatorio->intSetoresRecuperados))
              .append(",\"mascaradosPorFaixa\":{");
            bool blnPrimeira = true;
            for (const auto &kv : objRelatorio->dicMascaradosPorFaixa)
            {
                if (!blnPrimeira) sb.append(",");
                sb.append("\"").append(std::to_string(kv.first)).append("\":").append(std::to_string(kv.second));
                blnPrimeira = false;
            }
            sb.append("}");
            // Bit-perfect: com que drive e offset, o que o AccurateRip
            // disse, e o veredito de cada faixa.
            if (!objRelatorio->dicVerificacao.empty())
            {
                sb.append(",\"drive\":\"").append(Escapar(objRelatorio->strDrive.value_or("")))
                  .append("\",\"offsetAplicado\":").append(std::to_string(objRelatorio->intOffsetAplicado))
                  .append(",\"origemOffset\":\"").append(Escapar(!objRelatorio->strOrigemOffset.empty() ? objRelatorio->strOrigemOffset : "desconhecido"))
                  .append("\",\"accurateRip\":\"").append(Escapar(objRelatorio->strAccurateRip.value_or("")))
                  .append("\",\"bitPerfect\":\"").append(Escapar(objRelatorio->ResumoBitPerfect()))
                  .append("\",\"verificacao\":{");
                blnPrimeira = true;
                for (const auto &kv : objRelatorio->dicVerificacao)
                {
                    const TVerificacaoFaixa &v = kv.second;
                    if (!blnPrimeira) sb.append(",");
                    sb.append("\"").append(std::to_string(v.intFaixa)).append("\":\"").append(Escapar(v.ParaEtiqueta())).append("\"");
                    blnPrimeira = false;
                }
                sb.append("}");
            }
            sb.append("}");
        }
        sb.append(",\"faixas\":[");

        for (size_t i = 0; i < manifesto.size(); i++)
        {
            if (i > 0) sb.append(",");
            sb.append("{\"nome\":\"").append(Escapar(manifesto[i].Nome))
              .append("\",\"bytes\":").append(std::to_string(manifesto[i].Tamanho))
              .append(",\"sha256\":\"").append(manifesto[i].Sha256).append("\"}");
        }
        sb.append("]}");

        // LINUX: o DADOS em leitura e escrita enquanto se acrescenta a linha (aninha com o da
        // integração). O arquivo é Combina(raiz, "ARCA_Ledger.jsonl"), que é o Base::ArquivoLedger()
        // quando raiz é o Base::PastaAcervo(), como em todos os chamadores. A linha termina em
        // \r\n, o Environment.NewLine do Windows, como as linhas que o ARCA já gravou no ledger.
        GravacaoNoAcervo objGravacao;
        AcrescentaTexto(Combina(raiz, "ARCA_Ledger.jsonl"), sb + "\r\n");
        return true;
    }
    catch (...) { return false; }   // o ledger é registro, não pode derrubar a integração — mas a falha é avisada
}

// ARCA-ORIGINAL: só escapa \ e "; um caractere de controle (quebra de linha, tabulação) num nome
// sairia cru e deixaria a linha do ledger fora do JSON.
std::string Integracao::Escapar(const std::string &s)
{
    return Substitui(Substitui(s, "\\", "\\\\"), "\"", "\\\"");
}

std::string Integracao::Hash(const std::string &arquivo)
{
    return Sha256HexArquivo(arquivo);
}

// DECISAO-LORENZO (08/11/2026). ARCA-ORIGINAL: as caixas Artista e Album so davam o nome da pasta;
// as etiquetas ficavam com o que a identificacao gravou, e o album entrava na BIBLIOTECA com outro
// nome (Glenn Miller, 08/11: pasta "Mitos do Jazz - Glen Miller", etiquetas "MITOS DO JAZZ - VOL. 1").
std::string Integracao::GravarArtistaEAlbum(std::vector<ItemManifesto> &manifesto, const std::string &artista,
                                            const std::string &album, int &intRegravadas)
{
    intRegravadas = 0;
    for (ItemManifesto &item : manifesto)
    {
        if (!TerminaCom(item.Origem, ".flac", true)) continue;
        Interop::EditorFlac ed;
        if (!ed.Abrir(item.Origem))
            return "não consegui abrir " + item.Nome + " para gravar artista e álbum: " + ed.Falha();
        std::string strAlbumAtual, strArtistaAtual;
        for (const Interop::AtributoFlac &a : ed.LerTudo())
        {
            if (a.Nome == "WM/AlbumTitle") strAlbumAtual = a.Valor;
            else if (a.Nome == "WM/AlbumArtist") strArtistaAtual = a.Valor;
        }
        bool blnMudou = false;
        if (!album.empty() && strAlbumAtual != album) { ed.DefinirTexto("WM/AlbumTitle", album); blnMudou = true; }
        if (!artista.empty() && strArtistaAtual != artista) { ed.DefinirTexto("WM/AlbumArtist", artista); blnMudou = true; }
        if (!blnMudou) continue;
        if (!ed.Salvar())
            return "não consegui gravar artista e álbum em " + item.Nome + ": " + ed.Falha();
        ed.Fechar();
        item.Tamanho = Tamanho(item.Origem);
        item.Sha256 = Hash(item.Origem);
        intRegravadas++;
    }
    return "";
}

std::string Integracao::Limpar(const std::string &nomeDado, const std::string &padrao)
{
    if (nomeDado.empty()) return padrao;
    // Path.GetInvalidFileNameChars(): ASCII, então se tira byte a byte sem cortar UTF-8
    std::string nome;
    const std::string &inv = CaracteresInvalidosEmNome();
    for (char c : nomeDado)
        if (inv.find(c) == std::string::npos) nome += c;
    // nome.Trim().TrimEnd('.', ' ')
    nome = Trim(nome);
    while (!nome.empty() && (nome.back() == '.' || nome.back() == ' ')) nome.pop_back();
    if (nome.empty()) return padrao;
    std::string strBase = Maiusculas(Trim(Divide(nome, ".")[0]));
    static const std::regex reDispositivo(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$)");
    if (std::regex_match(strBase, reDispositivo)) nome = "_" + nome;
    return nome;
}

} // namespace ARCA::Modelo

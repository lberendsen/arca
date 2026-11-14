// ARCA no xOS - Interop/ColecaoMidia (LINUX: no lugar da biblioteca do WMP; ver ColecaoMidia.h).
#include "ColecaoMidia.h"

#include "Base/Arquivo.h"
#include "Base/Despacho.h"
#include "Base/Json.h"
#include "Base/Texto.h"
#include "Interop/EditorFlac.h"
#include "Modelo/ClasseMidia.h"
#include "Modelo/Formato.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

#include <sqlite3.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <thread>

namespace ARCA::Interop {

using namespace ARCA::Base;

namespace {

// As extensoes que a biblioteca do WMP inscrevia das pastas Musicas e Videos. As de video sao as
// do s_arrExtensoesVideo da MainWindow original.
const std::vector<std::string> &ExtensoesAudio()
{
    static const std::vector<std::string> v = {".flac", ".wma", ".mp3", ".m4a", ".aac", ".ogg", ".wav"};
    return v;
}
const std::vector<std::string> &ExtensoesVideo()
{
    static const std::vector<std::string> v = {".wmv", ".avi", ".mpg", ".mpeg", ".mp4", ".m4v", ".mov", ".mkv",
                                               ".dvr-ms", ".wtv", ".ts", ".m2ts", ".vob"};
    return v;
}

// Os atributos que o indice guarda de cada arquivo: os que o ARCA consulta, mostra na grade ou
// procura. O resto do arquivo o painel da direita le direto dele (EditorFlac), como no original.
const std::vector<std::string> &AtributosGuardados()
{
    static const std::vector<std::string> v = {
        "Title", "Author", "WM/AlbumArtist", "WM/AlbumTitle", "WM/TrackNumber", "WM/PartOfSet", "WM/Year",
        "WM/Genre", "WM/Composer", "WM/Publisher", "WM/MediaClassPrimaryID", "WM/MediaClassSecondaryID"};
    return v;
}

// O que so o banco do WMP guardava (contagem, nota, ultima reproducao): sobrevive a releitura.
bool EhDadoDoPlayer(const std::string &strAtributo)
{
    return ComecaCom(strAtributo, "User", true);
}

std::string Chave(const std::string &strAtributo)
{
    return Minusculas(strAtributo);
}

// pastas de sistema do NTFS que a varredura pula
bool PastaPulada(const std::string &strNome)
{
    return strNome == "$RECYCLE.BIN" || strNome == "System Volume Information" || (!strNome.empty() && strNome[0] == '.');
}

bool Estado(const std::string &strCaminho, int64_t &lngTamanho, int64_t &lngAlterado)
{
    struct stat st;
    if (stat(strCaminho.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    lngTamanho = (int64_t) st.st_size;
    lngAlterado = (int64_t) st.st_mtime;
    return true;
}

// O MediaType decidido na inscricao.
void DecidirTipo(MidiaWmp &m)
{
    std::string strTipo = ColecaoMidia::TipoPelaExtensao(m.Caminho);
    if (strTipo == "audio")
    {
        std::string strClasse = EditorFlac::NormalizarGuid(m.getItemInfo(Modelo::TClasseMidia::AtributoPrimario));
        if (strClasse == Modelo::TClasseMidia::guidPrimariaAudioNaoMusica) strTipo = "other";
    }
    m.Info[Chave("MediaType")] = strTipo;
}

// As etiquetas pelo FFmpeg (MP3, WMA, video): os nomes que ele usa para o mapa do WMP.
void LerPeloFfmpeg(MidiaWmp &m, bool blnVideo)
{
    static const std::map<std::string, std::string> mapa = {
        {"title", "Title"}, {"artist", "Author"}, {"author", "Author"}, {"album_artist", "WM/AlbumArtist"},
        {"album artist", "WM/AlbumArtist"}, {"album", "WM/AlbumTitle"}, {"track", "WM/TrackNumber"},
        {"date", "WM/Year"}, {"year", "WM/Year"}, {"genre", "WM/Genre"}, {"composer", "WM/Composer"},
        {"publisher", "WM/Publisher"}, {"disc", "WM/PartOfSet"}};
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, m.Caminho.c_str(), nullptr, nullptr) != 0) throw std::runtime_error("o FFmpeg não abriu o arquivo");
    // video (MPEG-PS e afins) so tem duracao depois de olhar os fluxos
    if (blnVideo || fmt->duration <= 0) avformat_find_stream_info(fmt, nullptr);
    if (fmt->duration > 0) m.Segundos = fmt->duration / (double) AV_TIME_BASE;
    const AVDictionaryEntry *e = nullptr;
    while ((e = av_dict_get(fmt->metadata, "", e, AV_DICT_IGNORE_SUFFIX)) != nullptr)
    {
        std::string strNome = e->key, strValor = Utf8Valido(e->value ? e->value : "");
        std::string strAtributo;
        auto it = mapa.find(Minusculas(strNome));
        if (it != mapa.end()) strAtributo = it->second;
        else if (ComecaCom(strNome, "WM/", true)) strAtributo = strNome;   // o ASF entrega os nomes do WMP
        if (strAtributo.empty() || strValor.empty()) continue;
        if (!m.Info.count(Chave(strAtributo))) m.Info[Chave(strAtributo)] = strValor;
    }
    avformat_close_input(&fmt);
}

} // namespace

// =====================================================================================
// MidiaWmp
// =====================================================================================
std::string MidiaWmp::getItemInfo(const std::string &strAtributo) const
{
    if (IgualSemCaixa(strAtributo, "SourceURL")) return Caminho;
    if (IgualSemCaixa(strAtributo, "Duration")) return Segundos > 0 ? Modelo::Formato::Num(Segundos, 3) : "";
    auto it = Info.find(Chave(strAtributo));
    return it == Info.end() ? "" : it->second;
}

void MidiaWmp::setItemInfo(const std::string &strAtributo, const std::string &strValor)
{
    if (IgualSemCaixa(strAtributo, "SourceURL") || IgualSemCaixa(strAtributo, "MediaType")) return;   // so leitura, como no WMP
    Info[Chave(strAtributo)] = strValor;
}

std::string MidiaWmp::name() const
{
    std::string strTitulo = getItemInfo("Title");
    return !strTitulo.empty() ? strTitulo : NomeSemExtensao(Caminho);
}

std::string MidiaWmp::durationString() const
{
    return Segundos > 0 ? Modelo::Formato::Tempo(Segundos) : "";
}

// =====================================================================================
// ConsultaWmp
// =====================================================================================
void ConsultaWmp::addCondition(const std::string &strAtributo, const std::string &strOperador, const std::string &strValor)
{
    _grupos.back().push_back(Condicao{strAtributo, strOperador, strValor});
}

void ConsultaWmp::beginNextGroup()
{
    _grupos.emplace_back();
}

bool ConsultaWmp::Casa(const MidiaWmp &m) const
{
    for (const auto &g : _grupos)
    {
        if (g.empty()) continue;
        bool blnTodas = true;
        for (const Condicao &c : g)
        {
            std::string v = m.getItemInfo(c.Atributo);
            bool blnCasa = IgualSemCaixa(c.Operador, "Contains") ? (!v.empty() && Contem(v, c.Valor, true))
                                                                 : IgualSemCaixa(v, c.Valor);
            if (!blnCasa) { blnTodas = false; break; }
        }
        if (blnTodas) return true;
    }
    return false;
}

// =====================================================================================
// ColecaoMidia
// =====================================================================================
ColecaoMidia &ColecaoMidia::Instancia()
{
    static ColecaoMidia c;
    return c;
}

std::vector<std::string> ColecaoMidia::Raizes()
{
    std::vector<std::string> v;
    v.push_back(PastaAcervo());
    const char *strVideos = getenv("ARCA_VIDEOS");
    std::string strPasta = strVideos ? strVideos : "/mnt/DADOS/Vídeos";   // ARCA_VIDEOS="" desliga
    if (!strPasta.empty()) v.push_back(strPasta);
    return v;
}

std::string ColecaoMidia::TipoPelaExtensao(const std::string &strCaminho)
{
    std::string ext = Minusculas(Extensao(strCaminho));
    if (std::find(ExtensoesAudio().begin(), ExtensoesAudio().end(), ext) != ExtensoesAudio().end()) return "audio";
    if (std::find(ExtensoesVideo().begin(), ExtensoesVideo().end(), ext) != ExtensoesVideo().end()) return "video";
    return "";
}

MidiaWmpPtr ColecaoMidia::LerArquivo(const std::string &strCaminho, std::string &strFalha)
{
    strFalha.clear();
    std::string strTipo = TipoPelaExtensao(strCaminho);
    if (strTipo.empty()) { strFalha = "não é um arquivo de mídia"; return nullptr; }
    auto m = std::make_shared<MidiaWmp>();
    m->Caminho = strCaminho;
    if (!Estado(strCaminho, m->Tamanho, m->Alterado)) { strFalha = "o arquivo não existe"; return nullptr; }
    try
    {
        if (Minusculas(Extensao(strCaminho)) == ".flac")
        {
            EditorFlac objEditor;
            if (!objEditor.Abrir(strCaminho)) { strFalha = objEditor.Falha(); return nullptr; }
            const std::vector<std::string> &lstGuardados = AtributosGuardados();
            for (const AtributoFlac &a : objEditor.LerTudo())
            {
                if (a.Nome == "Duration")
                {
                    double dbl100ns = 0;
                    if (TentaDouble(a.Valor, dbl100ns)) m->Segundos = dbl100ns / 1e7;
                    continue;
                }
                if (a.Binario() || a.Valor.empty()) continue;
                bool blnGuardar = false;
                for (const std::string &g : lstGuardados)
                    if (IgualSemCaixa(g, a.Nome)) { blnGuardar = true; break; }
                if (blnGuardar && !m->Info.count(Chave(a.Nome))) m->Info[Chave(a.Nome)] = a.Valor;
            }
        }
        else
            LerPeloFfmpeg(*m, strTipo == "video");
    }
    catch (const std::exception &ex)
    {
        strFalha = ex.what();
        return nullptr;
    }
    DecidirTipo(*m);
    return m;
}

void ColecaoMidia::Ordenar()
{
    std::sort(_lstItens.begin(), _lstItens.end(), [](const MidiaWmpPtr &a, const MidiaWmpPtr &b) { return a->Caminho < b->Caminho; });
}

MidiaWmpPtr ColecaoMidia::Achar(const std::string &strCaminho) const
{
    auto it = std::lower_bound(_lstItens.begin(), _lstItens.end(), strCaminho,
                               [](const MidiaWmpPtr &a, const std::string &c) { return a->Caminho < c; });
    return it != _lstItens.end() && (*it)->Caminho == strCaminho ? *it : nullptr;
}

ListaWmpPtr ColecaoMidia::getAll() const
{
    return std::make_shared<ListaWmp>(_lstItens);
}

ListaWmpPtr ColecaoMidia::getByAttribute(const std::string &strAtributo, const std::string &strValor) const
{
    auto l = std::make_shared<ListaWmp>();
    // GUID compara como GUID (o WMP guarda "{01CD0F29-...}")
    std::string strGuid = EditorFlac::NormalizarGuid(strValor);
    for (const MidiaWmpPtr &m : _lstItens)
    {
        std::string v = m->getItemInfo(strAtributo);
        bool blnCasa = !strGuid.empty() ? EditorFlac::NormalizarGuid(v) == strGuid : IgualSemCaixa(v, strValor);
        if (blnCasa) l->push_back(m);
    }
    return l;
}

std::shared_ptr<std::vector<std::string>> ColecaoMidia::getAttributeStringCollection(const std::string &strAtributo,
                                                                                     const std::string &strTipo) const
{
    if (strTipo != "audio" && strTipo != "video" && strTipo != "other") return nullptr;
    auto l = std::make_shared<std::vector<std::string>>();
    std::set<std::string> setVistos;
    for (const MidiaWmpPtr &m : _lstItens)
    {
        if (m->getItemInfo("MediaType") != strTipo) continue;
        std::string v = m->getItemInfo(strAtributo);
        if (setVistos.insert(v).second) l->push_back(v);
    }
    return l;
}

ListaWmpPtr ColecaoMidia::getPlaylistByQuery(const ConsultaWmp &objConsulta, const std::string &strTipo,
                                             const std::string &strOrdem, bool blnCrescente) const
{
    auto l = std::make_shared<ListaWmp>();
    for (const MidiaWmpPtr &m : _lstItens)
        if (m->getItemInfo("MediaType") == strTipo && objConsulta.Casa(*m)) l->push_back(m);
    bool blnNumero = IgualSemCaixa(strOrdem, "WM/TrackNumber");
    std::stable_sort(l->begin(), l->end(), [&](const MidiaWmpPtr &a, const MidiaWmpPtr &b) {
        std::string x = a->getItemInfo(strOrdem), y = b->getItemInfo(strOrdem);
        int r;
        if (blnNumero)
        {
            int nx = 0, ny = 0;
            // "3/12" vale 3
            TentaInt(Divide(x, "/")[0], nx);
            TentaInt(Divide(y, "/")[0], ny);
            r = nx < ny ? -1 : (nx > ny ? 1 : 0);
        }
        else
        {
            std::string px = Minusculas(SemAcentos(x)), py = Minusculas(SemAcentos(y));
            r = px.compare(py);
        }
        return blnCrescente ? r < 0 : r > 0;
    });
    return l;
}

MidiaWmpPtr ColecaoMidia::add(const std::string &strCaminho)
{
    std::string strFalha;
    MidiaWmpPtr m = LerArquivo(strCaminho, strFalha);
    if (!m) return nullptr;
    MidiaWmpPtr objAntigo = Achar(strCaminho);
    if (objAntigo)
    {
        // Ja inscrito (uma aprovacao que substituiu o album, por exemplo): rele no mesmo item,
        // guardando o que so o player sabia, e o devolve, como o add do WMP devolvia o existente.
        for (auto &par : objAntigo->Info)
            if (EhDadoDoPlayer(par.first) && !m->Info.count(par.first)) m->Info[par.first] = par.second;
        *objAntigo = *m;
        GravarIndice({objAntigo}, {});
        return objAntigo;
    }
    _lstItens.push_back(m);
    Ordenar();
    GravarIndice({m}, {});
    return m;
}

void ColecaoMidia::remove(const MidiaWmpPtr &m, bool blnApagarArquivo)
{
    (void) blnApagarArquivo;   // nunca apaga
    if (!m) return;
    auto it = std::find(_lstItens.begin(), _lstItens.end(), m);
    if (it == _lstItens.end()) return;
    std::string strCaminho = m->Caminho;
    _lstItens.erase(it);
    GravarIndice({}, {strCaminho});
}

void ColecaoMidia::Guardar(const MidiaWmpPtr &m)
{
    if (m) GravarIndice({m}, {});
}

// ---------------------------------------------------------------------------------------
// O indice guardado
// ---------------------------------------------------------------------------------------
std::string ColecaoMidia::ArquivoIndice()
{
    return Combina(PastaCache(), "colecao.sqlite");
}

namespace {
sqlite3 *AbrirIndice(const std::string &strArquivo, bool blnCriar)
{
    sqlite3 *db = nullptr;
    int flags = SQLITE_OPEN_READWRITE | (blnCriar ? SQLITE_OPEN_CREATE : 0);
    if (sqlite3_open_v2(strArquivo.c_str(), &db, flags, nullptr) != SQLITE_OK)
    {
        if (db) sqlite3_close(db);
        return nullptr;
    }
    sqlite3_busy_timeout(db, 2000);
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS Midia (Caminho TEXT PRIMARY KEY, Tamanho INTEGER NOT NULL, "
                     "Alterado INTEGER NOT NULL, Segundos REAL NOT NULL, Info TEXT NOT NULL)",
                 nullptr, nullptr, nullptr);
    return db;
}
} // namespace

std::vector<MidiaWmpPtr> ColecaoMidia::LerIndice() const
{
    std::vector<MidiaWmpPtr> v;
    if (!ArquivoExiste(ArquivoIndice())) return v;
    sqlite3 *db = AbrirIndice(ArquivoIndice(), false);
    if (!db) return v;
    std::vector<std::string> lstRaizes = Raizes();
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT Caminho, Tamanho, Alterado, Segundos, Info FROM Midia", -1, &st, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(st) == SQLITE_ROW)
        {
            auto m = std::make_shared<MidiaWmp>();
            m->Caminho = (const char *) sqlite3_column_text(st, 0);
            // so o que esta nas pastas acompanhadas agora (um teste com outro acervo nao mistura)
            bool blnDentro = false;
            for (const std::string &r : lstRaizes)
                if (ComecaCom(m->Caminho, r + "/")) blnDentro = true;
            if (!blnDentro) continue;
            m->Tamanho = sqlite3_column_int64(st, 1);
            m->Alterado = sqlite3_column_int64(st, 2);
            m->Segundos = sqlite3_column_double(st, 3);
            Json j = JsonDe((const char *) sqlite3_column_text(st, 4));
            if (j.is_object())
                for (auto it = j.begin(); it != j.end(); ++it)
                    if (it->is_string()) m->Info[it.key()] = it->get<std::string>();
            v.push_back(m);
        }
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return v;
}

void ColecaoMidia::GravarIndice(const std::vector<MidiaWmpPtr> &lstMudados, const std::vector<std::string> &lstSairam) const
{
    if (lstMudados.empty() && lstSairam.empty()) return;
    try { CriaPasta(PastaCache()); }
    catch (...) { return; }
    sqlite3 *db = AbrirIndice(ArquivoIndice(), true);
    if (!db) return;
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO Midia (Caminho, Tamanho, Alterado, Segundos, Info) VALUES (?, ?, ?, ?, ?)",
                           -1, &st, nullptr) == SQLITE_OK)
    {
        for (const MidiaWmpPtr &m : lstMudados)
        {
            Json j = Json::object();
            for (auto &par : m->Info) j[par.first] = par.second;
            std::string strInfo = j.dump(-1, ' ', false, Json::error_handler_t::replace);
            sqlite3_bind_text(st, 1, m->Caminho.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 2, m->Tamanho);
            sqlite3_bind_int64(st, 3, m->Alterado);
            sqlite3_bind_double(st, 4, m->Segundos);
            sqlite3_bind_text(st, 5, strInfo.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_reset(st);
        }
        sqlite3_finalize(st);
    }
    if (sqlite3_prepare_v2(db, "DELETE FROM Midia WHERE Caminho = ?", -1, &st, nullptr) == SQLITE_OK)
    {
        for (const std::string &c : lstSairam)
        {
            sqlite3_bind_text(st, 1, c.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_reset(st);
        }
        sqlite3_finalize(st);
    }
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    sqlite3_close(db);
}

// ---------------------------------------------------------------------------------------
// A conferencia das pastas (o monitor de pastas do WMP)
// ---------------------------------------------------------------------------------------
void ColecaoMidia::Iniciar(std::function<void(int intMudancas)> aoAtualizar)
{
    if (_blnIniciada) return;
    _blnIniciada = true;
    _lstItens = LerIndice();
    Ordenar();

    // o que a varredura precisa saber do indice: tamanho e data de cada caminho
    auto dicConhecidos = std::make_shared<std::map<std::string, std::pair<int64_t, int64_t>>>();
    for (const MidiaWmpPtr &m : _lstItens) (*dicConhecidos)[m->Caminho] = {m->Tamanho, m->Alterado};
    std::vector<std::string> lstRaizes = Raizes();
    _blnVarrendo = true;

    std::thread([this, dicConhecidos, lstRaizes, aoAtualizar] {
        auto lstNovos = std::make_shared<std::vector<MidiaWmpPtr>>();     // entraram ou mudaram
        auto setVistos = std::make_shared<std::set<std::string>>();
        for (const std::string &strRaiz : lstRaizes)
        {
            std::error_code ec;
            if (!fs::is_directory(strRaiz, ec)) continue;
            fs::recursive_directory_iterator it(strRaiz, fs::directory_options::skip_permission_denied, ec), fim;
            while (!ec && it != fim)
            {
                const fs::directory_entry &e = *it;
                std::string strNome = e.path().filename().string();
                std::error_code ec2;
                if (e.is_directory(ec2))
                {
                    // nao segue atalhos/juncoes nem pastas de sistema; e nao desce sem fim
                    if (PastaPulada(strNome) || e.is_symlink(ec2) || it.depth() >= 8) it.disable_recursion_pending();
                }
                else if (!TipoPelaExtensao(strNome).empty() && !TerminaCom(strNome, ".partial", true))
                {
                    std::string strCaminho = e.path().string();
                    int64_t lngTamanho = 0, lngAlterado = 0;
                    if (Estado(strCaminho, lngTamanho, lngAlterado))
                    {
                        setVistos->insert(strCaminho);
                        auto k = dicConhecidos->find(strCaminho);
                        if (k == dicConhecidos->end() || k->second.first != lngTamanho || k->second.second != lngAlterado)
                        {
                            std::string strFalha;
                            MidiaWmpPtr m = LerArquivo(strCaminho, strFalha);
                            if (m) lstNovos->push_back(m);
                        }
                    }
                }
                it.increment(ec);
            }
        }
        Despacha([this, lstNovos, setVistos, aoAtualizar] {
            std::vector<std::string> lstSairam;
            std::vector<MidiaWmpPtr> lstFicam;
            for (const MidiaWmpPtr &m : _lstItens)
            {
                if (!setVistos->count(m->Caminho)) { lstSairam.push_back(m->Caminho); continue; }
                lstFicam.push_back(m);
            }
            _lstItens = lstFicam;
            std::vector<MidiaWmpPtr> lstEntram;
            for (const MidiaWmpPtr &m : *lstNovos)
            {
                MidiaWmpPtr objAntigo = Achar(m->Caminho);
                if (objAntigo)
                {
                    // o que so o player guardava continua
                    for (auto &par : objAntigo->Info)
                        if (EhDadoDoPlayer(par.first) && !m->Info.count(par.first)) m->Info[par.first] = par.second;
                    *objAntigo = *m;     // o mesmo objeto: quem ja o tem ve os dados novos
                }
                else
                    lstEntram.push_back(m);
            }
            _lstItens.insert(_lstItens.end(), lstEntram.begin(), lstEntram.end());
            Ordenar();
            GravarIndice(*lstNovos, lstSairam);
            _blnVarrendo = false;
            if (aoAtualizar) aoAtualizar((int) (lstNovos->size() + lstSairam.size()));
        });
    }).detach();
}

} // namespace ARCA::Interop

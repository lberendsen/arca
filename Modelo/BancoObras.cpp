// ARCA no xOS - Modelo/BancoObras (de Modelo\BancoObras.cs). LINUX: SQLite no lugar do SQL CE.
#include "BancoObras.h"

#include "Base/Acervo.h"
#include "Base/Arquivo.h"
#include "Base/Hash.h"
#include "Base/Json.h"
#include "Base/Texto.h"

#include <sqlite3.h>

#include <map>
#include <memory>
#include <stdexcept>

namespace ARCA::Modelo {

using namespace ARCA::Base;

namespace {

// os parametros dos comandos (o AddWithValue do original)
struct Valor
{
    enum Tipo { Nulo, Inteiro, Texto, Bytes } eTipo = Nulo;
    int64_t lng = 0;
    std::string str;
    const std::vector<uint8_t> *arr = nullptr;
    Valor() = default;
    Valor(int v) : eTipo(Inteiro), lng(v) {}
    Valor(int64_t v) : eTipo(Inteiro), lng(v) {}
    Valor(const std::string &s) : eTipo(Texto), str(s) {}
    Valor(const char *s) : eTipo(Texto), str(s) {}
    static Valor DeBytes(const std::vector<uint8_t> &a)
    {
        Valor v;
        // A capa vai como IMAGE explícito: pelo AddWithValue, bytes acima de 8 KB seriam truncados como VARBINARY.
        // LINUX: no SQLite e um BLOB, sem limite; vazio = DBNull
        if (!a.empty()) { v.eTipo = Bytes; v.arr = &a; }
        return v;
    }
    static Valor TextoOuNulo(const std::string &s)
    {
        return s.empty() ? Valor() : Valor(s);
    }
};

// DateTime.Now como o banco guarda
std::string Agora()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    char ms[8];
    snprintf(ms, sizeof ms, ".%03ld", ts.tv_nsec / 1000000);
    return Base::Agora("%Y-%m-%d %H:%M:%S") + ms;
}

std::string Erro(sqlite3 *db)
{
    return db ? sqlite3_errmsg(db) : "o banco não abriu";
}

} // namespace

// Uma operação por conexão (o using (SqlCeConnection objCon = Abrir()) do original). Quem grava
// abre o acervo em leitura e escrita antes e o fecha depois (LINUX).
class TBancoObras::Conexao
{
public:
    Conexao(TBancoObras &objBanco, bool blnGravar)
    {
        bool blnCriar = !ArquivoExiste(objBanco.m_strArquivo);
        if (blnGravar || blnCriar) m_objGravacao = std::make_unique<GravacaoNoAcervo>();
        int flags = blnGravar || blnCriar ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY;
        if (sqlite3_open_v2(objBanco.m_strArquivo.c_str(), &db, flags, nullptr) != SQLITE_OK)
        {
            std::string strErro = Erro(db);
            sqlite3_close(db);
            db = nullptr;
            throw std::runtime_error("não consegui abrir " + objBanco.m_strArquivo + ": " + strErro);
        }
        sqlite3_busy_timeout(db, 5000);
        // Excluir a obra leva as partes junto: no SQLite a chave estrangeira so vale com isto ligado.
        Executar("PRAGMA foreign_keys = ON");
        if (blnCriar) objBanco.Criar(db);
    }
    ~Conexao()
    {
        if (db) sqlite3_close(db);
    }
    Conexao(const Conexao &) = delete;
    Conexao &operator=(const Conexao &) = delete;

    sqlite3 *db = nullptr;

    sqlite3_stmt *Comando(const std::string &strSql, const std::vector<Valor> &arrParametros = {})
    {
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(db, strSql.c_str(), -1, &st, nullptr) != SQLITE_OK)
            throw std::runtime_error(Erro(db));
        for (size_t i = 0; i < arrParametros.size(); i++)
        {
            const Valor &v = arrParametros[i];
            int k = (int) i + 1;
            switch (v.eTipo)
            {
            case Valor::Nulo: sqlite3_bind_null(st, k); break;
            case Valor::Inteiro: sqlite3_bind_int64(st, k, v.lng); break;
            case Valor::Texto: sqlite3_bind_text(st, k, v.str.c_str(), -1, SQLITE_TRANSIENT); break;
            case Valor::Bytes: sqlite3_bind_blob(st, k, v.arr->data(), (int) v.arr->size(), SQLITE_TRANSIENT); break;
            }
        }
        return st;
    }

    int Executar(const std::string &strSql, const std::vector<Valor> &arrParametros = {})
    {
        sqlite3_stmt *st = Comando(strSql, arrParametros);
        int r = sqlite3_step(st);
        sqlite3_finalize(st);
        if (r != SQLITE_DONE && r != SQLITE_ROW) throw std::runtime_error(Erro(db));
        return sqlite3_changes(db);
    }

    int64_t Escalar(const std::string &strSql, const std::vector<Valor> &arrParametros, bool &blnNulo)
    {
        sqlite3_stmt *st = Comando(strSql, arrParametros);
        int64_t v = 0;
        blnNulo = true;
        int r = sqlite3_step(st);
        if (r == SQLITE_ROW && sqlite3_column_type(st, 0) != SQLITE_NULL)
        {
            v = sqlite3_column_int64(st, 0);
            blnNulo = false;
        }
        sqlite3_finalize(st);
        if (r != SQLITE_DONE && r != SQLITE_ROW) throw std::runtime_error(Erro(db));
        return v;
    }

private:
    std::unique_ptr<GravacaoNoAcervo> m_objGravacao;
};

TBancoObras::TBancoObras(const std::string &strRaizAcervo)
{
    m_strArquivo = Combina(strRaizAcervo, NomeArquivo);
}

bool TBancoObras::Existe() const
{
    return ArquivoExiste(m_strArquivo);
}

void TBancoObras::Criar(sqlite3 *db)
{
    // Um comando por instrução: o SQL CE não aceita lote.
    // LINUX: os tipos do SQL CE na forma do SQLite (IDENTITY -> INTEGER PRIMARY KEY AUTOINCREMENT,
    // NVARCHAR -> TEXT, IMAGE -> BLOB, DATETIME -> TEXT); a chave estrangeira vai na criacao da
    // tabela, porque o SQLite nao tem ALTER TABLE ... ADD CONSTRAINT.
    const char *arrComandos[] = {
        "CREATE TABLE Obra (Id INTEGER PRIMARY KEY AUTOINCREMENT, Titulo TEXT NOT NULL, "
        "Autor TEXT NOT NULL, Capa BLOB NULL, Criada TEXT NOT NULL, Alterada TEXT NOT NULL)",
        // Excluir a obra leva as partes junto — só as linhas do banco, nunca arquivos.
        "CREATE TABLE ParteObra (Id INTEGER PRIMARY KEY AUTOINCREMENT, IdObra INTEGER NOT NULL "
        "CONSTRAINT FK_ParteObra_Obra REFERENCES Obra(Id) ON DELETE CASCADE, Tipo INTEGER NOT NULL, "
        "Ordem INTEGER NOT NULL, Caminho TEXT NOT NULL, Album TEXT NULL, Mcdi TEXT NULL, "
        "Faixa INTEGER NULL, Incluida TEXT NOT NULL)",
        "CREATE INDEX IX_ParteObra_Obra ON ParteObra (IdObra)",
        "CREATE INDEX IX_ParteObra_Caminho ON ParteObra (Caminho)",
        "CREATE TABLE Versao (Numero INTEGER NOT NULL)",
    };
    for (const char *c : arrComandos)
        if (sqlite3_exec(db, c, nullptr, nullptr, nullptr) != SQLITE_OK) throw std::runtime_error(Erro(db));
    std::string strVersao = "INSERT INTO Versao (Numero) VALUES (" + std::to_string(intVersaoAtual) + ")";
    if (sqlite3_exec(db, strVersao.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) throw std::runtime_error(Erro(db));
}

// ===============================================================
TObra TBancoObras::LerObra(void *p)
{
    sqlite3_stmt *r = (sqlite3_stmt *) p;
    TObra o;
    o.intId = sqlite3_column_int(r, 0);
    o.strTitulo = (const char *) sqlite3_column_text(r, 1);
    o.strAutor = (const char *) sqlite3_column_text(r, 2);
    if (sqlite3_column_type(r, 3) != SQLITE_NULL)
    {
        const uint8_t *b = (const uint8_t *) sqlite3_column_blob(r, 3);
        o.arrCapa.assign(b, b + sqlite3_column_bytes(r, 3));
    }
    const unsigned char *d = sqlite3_column_text(r, 4);
    o.dtAlterada = d ? (const char *) d : "";
    return o;
}

std::vector<TObra> TBancoObras::ListarObras()
{
    std::vector<TObra> lstObras;
    Conexao objCon(*this, false);
    {
        sqlite3_stmt *r = objCon.Comando("SELECT Id, Titulo, Autor, Capa, Alterada FROM Obra ORDER BY Alterada DESC");
        while (sqlite3_step(r) == SQLITE_ROW) lstObras.push_back(LerObra(r));
        sqlite3_finalize(r);
    }

    std::map<int, int> dicQtd;
    {
        sqlite3_stmt *r = objCon.Comando("SELECT IdObra, COUNT(*) FROM ParteObra GROUP BY IdObra");
        while (sqlite3_step(r) == SQLITE_ROW) dicQtd[sqlite3_column_int(r, 0)] = sqlite3_column_int(r, 1);
        sqlite3_finalize(r);
    }
    for (TObra &o : lstObras)
    {
        auto it = dicQtd.find(o.intId);
        o.intQtdPartes = it != dicQtd.end() ? it->second : 0;
    }
    return lstObras;
}

TObra TBancoObras::Obter(int intId)
{
    Conexao objCon(*this, false);
    sqlite3_stmt *r = objCon.Comando("SELECT Id, Titulo, Autor, Capa, Alterada FROM Obra WHERE Id = ?", {intId});
    TObra o;
    if (sqlite3_step(r) == SQLITE_ROW) o = LerObra(r);
    sqlite3_finalize(r);
    return o;
}

int TBancoObras::CriarObra(const std::string &strTitulo, const std::string &strAutor, const std::vector<uint8_t> &arrCapa)
{
    Conexao objCon(*this, true);
    std::string dtAgora = Agora();
    objCon.Executar("INSERT INTO Obra (Titulo, Autor, Capa, Criada, Alterada) VALUES (?, ?, ?, ?, ?)",
                    {strTitulo, strAutor, Valor::DeBytes(arrCapa), dtAgora, dtAgora});
    // SELECT @@IDENTITY
    return (int) sqlite3_last_insert_rowid(objCon.db);
}

void TBancoObras::AlterarTituloEAutor(int intId, const std::string &strTitulo, const std::string &strAutor)
{
    Conexao objCon(*this, true);
    objCon.Executar("UPDATE Obra SET Titulo = ?, Autor = ?, Alterada = ? WHERE Id = ?", {strTitulo, strAutor, Agora(), intId});
}

void TBancoObras::AlterarCapa(int intId, const std::vector<uint8_t> &arrCapa)
{
    Conexao objCon(*this, true);
    objCon.Executar("UPDATE Obra SET Capa = ?, Alterada = ? WHERE Id = ?", {Valor::DeBytes(arrCapa), Agora(), intId});
}

void TBancoObras::ExcluirObra(int intId)
{
    Conexao objCon(*this, true);
    objCon.Executar("DELETE FROM Obra WHERE Id = ?", {intId});
}

// ===============================================================
std::vector<TParteObra> TBancoObras::Partes(int intIdObra)
{
    std::vector<TParteObra> lstPartes;
    Conexao objCon(*this, false);
    sqlite3_stmt *r = objCon.Comando(
        "SELECT Id, IdObra, Tipo, Ordem, Caminho, Album, Mcdi, Faixa FROM ParteObra WHERE IdObra = ? ORDER BY Ordem, Id",
        {intIdObra});
    while (sqlite3_step(r) == SQLITE_ROW)
    {
        TParteObra p;
        p.intId = sqlite3_column_int(r, 0);
        p.intIdObra = sqlite3_column_int(r, 1);
        p.eTipo = (TipoParteObra) sqlite3_column_int(r, 2);
        p.intOrdem = sqlite3_column_int(r, 3);
        p.strCaminho = (const char *) sqlite3_column_text(r, 4);
        if (sqlite3_column_type(r, 5) != SQLITE_NULL) p.strAlbum = (const char *) sqlite3_column_text(r, 5);
        if (sqlite3_column_type(r, 6) != SQLITE_NULL) p.strMcdi = (const char *) sqlite3_column_text(r, 6);
        p.intFaixa = sqlite3_column_type(r, 7) == SQLITE_NULL ? -1 : sqlite3_column_int(r, 7);
        lstPartes.push_back(p);
    }
    sqlite3_finalize(r);
    return lstPartes;
}

bool TBancoObras::AcrescentarParte(int intIdObra, const TParteObra &objParte)
{
    Conexao objCon(*this, true);
    objCon.Executar("BEGIN");
    try
    {
        bool blnNulo;
        if (objCon.Escalar("SELECT COUNT(*) FROM ParteObra WHERE IdObra = ? AND Tipo = ? AND Caminho = ?",
                           {intIdObra, (int) objParte.eTipo, objParte.strCaminho}, blnNulo) > 0)
        {
            objCon.Executar("ROLLBACK");
            return false;
        }

        int64_t o = objCon.Escalar("SELECT MAX(Ordem) FROM ParteObra WHERE IdObra = ?", {intIdObra}, blnNulo);
        int intOrdem = (blnNulo ? 0 : (int) o) + 1;

        std::string dtAgora = Agora();
        objCon.Executar("INSERT INTO ParteObra (IdObra, Tipo, Ordem, Caminho, Album, Mcdi, Faixa, Incluida) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                        {intIdObra, (int) objParte.eTipo, intOrdem, objParte.strCaminho, Valor::TextoOuNulo(objParte.strAlbum),
                         Valor::TextoOuNulo(objParte.strMcdi), objParte.intFaixa > 0 ? Valor(objParte.intFaixa) : Valor(), dtAgora});
        // A obra que acabou de receber algo sobe para o topo da lista.
        objCon.Executar("UPDATE Obra SET Alterada = ? WHERE Id = ?", {Agora(), intIdObra});
        objCon.Executar("COMMIT");
        return true;
    }
    catch (...)
    {
        sqlite3_exec(objCon.db, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

void TBancoObras::RetirarParte(int intIdParte)
{
    Conexao objCon(*this, true);
    objCon.Executar("DELETE FROM ParteObra WHERE Id = ?", {intIdParte});
}

void TBancoObras::Mover(int intIdObra, int intIdParte, int intDirecao)
{
    std::vector<TParteObra> lstPartes = Partes(intIdObra);
    int i = -1;
    for (size_t k = 0; k < lstPartes.size(); k++)
        if (lstPartes[k].intId == intIdParte) { i = (int) k; break; }
    int j = i + intDirecao;
    if (i < 0 || j < 0 || j >= (int) lstPartes.size()) return;

    // Ordens renumeradas 1..N antes da troca: duas partes com a mesma
    // ordem (nunca deveria haver) não travam o movimento.
    std::swap(lstPartes[(size_t) i], lstPartes[(size_t) j]);
    Conexao objCon(*this, true);
    objCon.Executar("BEGIN");
    try
    {
        for (size_t k = 0; k < lstPartes.size(); k++)
            objCon.Executar("UPDATE ParteObra SET Ordem = ? WHERE Id = ?", {(int) k + 1, lstPartes[k].intId});
        objCon.Executar("COMMIT");
    }
    catch (...)
    {
        sqlite3_exec(objCon.db, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

// ===============================================================
int TBancoObras::AtualizarCaminhoDaFaixa(const std::string &strAntigo, const std::string &strNovo)
{
    Conexao objCon(*this, true);
    return objCon.Executar("UPDATE ParteObra SET Caminho = ? WHERE Tipo = ? AND Caminho = ?",
                           {strNovo, (int) TipoParteObra::Faixa, strAntigo});
}

int TBancoObras::AtualizarAlbumDaPasta(const std::string &strPasta, const std::string &strAlbum)
{
    Conexao objCon(*this, true);
    return objCon.Executar("UPDATE ParteObra SET Album = ? WHERE Tipo = ? AND Caminho = ?",
                           {strAlbum, (int) TipoParteObra::Album, strPasta});
}

// ===============================================================
// LINUX: a conversao do ARCA_Obras.sdf
std::string TBancoObras::CaminhoDoWindows(const std::string &strCaminhoWindows, const std::string &strRaizAcervo)
{
    // "E:\Músicas\Gabriel Chalita\A ética do rei menino" (ou D:\, ou \\LORENZO-PC\...): tudo o que
    // vem depois da pasta Músicas e relativo a raiz do acervo
    std::string s = Substitui(strCaminhoWindows, "\\", "/");
    int64_t p = Indice(s, "/Músicas/", true);
    if (p < 0)
    {
        if (TerminaCom(s, "/Músicas", true)) return strRaizAcervo;
        return "";
    }
    return Combina(strRaizAcervo, s.substr((size_t) p + std::string("/Músicas/").size()));
}

std::string TBancoObras::ImportarExportacaoDoSdf(const std::string &strJsonl, const std::string &strRaizAcervo)
{
    TBancoObras objBanco(strRaizAcervo);
    if (objBanco.Existe()) throw std::runtime_error("o banco já existe: " + objBanco.Arquivo());
    std::vector<Json> lstObras, lstPartes;
    int intVersao = 0;
    for (const std::string &strLinha : LeLinhas(strJsonl))
    {
        if (Trim(strLinha).empty()) continue;
        Json j = JsonDe(strLinha);
        if (!JsonValido(j) || !j.is_object()) throw std::runtime_error("linha que não é JSON: " + strLinha.substr(0, 80));
        std::string t = JsonTexto(j, "tabela");
        if (t == "Obra") lstObras.push_back(j);
        else if (t == "ParteObra") lstPartes.push_back(j);
        else if (t == "Versao") intVersao = j.value("Numero", 0);
    }
    if (intVersao != intVersaoAtual)
        throw std::runtime_error("versão do banco " + std::to_string(intVersao) + "; esperada " + std::to_string(intVersaoAtual));

    // "2026-10-04T16:05:11.730" -> "2026-10-04 16:05:11.730"
    auto data = [](const Json &j, const char *c) { return Substitui(JsonTexto(j, c), "T", " "); };
    auto nulo = [](const Json &j, const char *c) { return !j.contains(c) || j[c].is_null(); };
    std::vector<std::string> lstSemCaminho;
    Conexao objCon(objBanco, true);
    objCon.Executar("BEGIN");
    try
    {
        for (const Json &o : lstObras)
        {
            std::vector<uint8_t> arrCapa = DeBase64(JsonTexto(o, "Capa"));
            objCon.Executar("INSERT INTO Obra (Id, Titulo, Autor, Capa, Criada, Alterada) VALUES (?, ?, ?, ?, ?, ?)",
                            {o.value("Id", 0), JsonTexto(o, "Titulo"), JsonTexto(o, "Autor"), Valor::DeBytes(arrCapa),
                             data(o, "Criada"), data(o, "Alterada")});
        }
        for (const Json &p : lstPartes)
        {
            std::string strCaminho = CaminhoDoWindows(JsonTexto(p, "Caminho"), strRaizAcervo);
            if (strCaminho.empty())
            {
                // fora do acervo: fica como estava, e a parte aparece como "não encontrada"
                strCaminho = JsonTexto(p, "Caminho");
                lstSemCaminho.push_back(strCaminho);
            }
            objCon.Executar("INSERT INTO ParteObra (Id, IdObra, Tipo, Ordem, Caminho, Album, Mcdi, Faixa, Incluida) "
                            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                            {p.value("Id", 0), p.value("IdObra", 0), p.value("Tipo", 0), p.value("Ordem", 0), strCaminho,
                             nulo(p, "Album") ? Valor() : Valor(JsonTexto(p, "Album")),
                             nulo(p, "Mcdi") ? Valor() : Valor(JsonTexto(p, "Mcdi")),
                             nulo(p, "Faixa") ? Valor() : Valor(p.value("Faixa", 0)), data(p, "Incluida")});
        }
        objCon.Executar("COMMIT");
    }
    catch (...)
    {
        sqlite3_exec(objCon.db, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
    std::string strResumo = std::to_string(lstObras.size()) + (lstObras.size() == 1 ? " obra, " : " obras, ") +
                            std::to_string(lstPartes.size()) + (lstPartes.size() == 1 ? " parte" : " partes");
    if (!lstSemCaminho.empty())
        strResumo += "; fora do acervo (caminho mantido): " + Junta(lstSemCaminho, "; ");
    return strResumo;
}

} // namespace ARCA::Modelo

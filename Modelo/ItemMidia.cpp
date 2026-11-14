// ARCA no xOS - Modelo/ItemMidia (de Modelo\ItemMidia.cs).
#include "ItemMidia.h"

#include "Biblioteca.h"

namespace ARCA::Modelo {

namespace {
std::string Info(const Interop::MidiaWmp &m, const std::string &atributo)
{
    // "" = null (getItemInfo vazio vira null no original)
    return m.getItemInfo(atributo);
}
std::string Ou(const std::string &a, const std::string &b) { return !a.empty() ? a : b; }
} // namespace

ItemMidia::ItemMidia(Biblioteca *dono, int indice, int geracao) : _dono(dono), _indice(indice), _geracao(geracao) {}

void ItemMidia::Carregar() const
{
    if (_carregado) return;
    _carregado = true;

    try
    {
        Interop::MidiaWmpPtr m = _dono->ObterMedia(_indice, _geracao);
        if (!m) { _titulo = "(não consegui ler)"; return; }

        _titulo = Ou(Info(*m, "Title"), Ou(m->name(), "(sem título)"));
        _artista = Ou(Info(*m, "WM/AlbumArtist"), Info(*m, "Author"));
        _album = Info(*m, "WM/AlbumTitle");
        _duracao = m->durationString();
        _tipo = Info(*m, "MediaType");
        _arquivo = m->sourceURL();
        // A duração vem como número. Passá-la por texto e reler como
        // invariante dava sempre 0 em português ("245,1" não é "245.1").
        _segundos = m->duration();
    }
    catch (...)
    {
        if (_titulo.empty()) _titulo = "(erro ao ler)";
    }
}

} // namespace ARCA::Modelo

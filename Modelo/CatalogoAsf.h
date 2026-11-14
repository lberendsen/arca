// ARCA no xOS - Modelo/CatalogoAsf (de Modelo\CatalogoAsf.cs).
// LINUX: o catálogo continua o dos campos ASF, com os mesmos nomes, rótulos e categorias; cada
// campo ganha o nome Vorbis com que ele está gravado no FLAC (Interop/EditorFlac, pelo mapa do
// converte_acervo.py).
#pragma once

#include <string>
#include <vector>

namespace ARCA::Modelo {

class CampoAsf
{
public:
    CampoAsf(const std::string &atributo, const std::string &rotulo, const std::string &categoria, bool editavel);

    const std::string &Atributo() const { return m_strAtributo; }
    const std::string &Rotulo() const { return m_strRotulo; }
    const std::string &Categoria() const { return m_strCategoria; }
    bool Editavel() const { return m_blnEditavel; }
    // LINUX: o nome do campo no Vorbis comment do FLAC ("WM/AlbumTitle" -> "ALBUM"). "" no que não
    // é Vorbis comment: a imagem (bloco PICTURE) e as propriedades do contêiner, que não estão
    // gravadas em lugar nenhum.
    const std::string &Vorbis() const { return m_strVorbis; }

private:
    std::string m_strAtributo, m_strRotulo, m_strCategoria, m_strVorbis;
    bool m_blnEditavel;
};

/// A organização dos campos ASF, tirada do estudo em
/// `Referência\Sheet_2_Campo a campo.csv` — 98 campos inventariados com
/// tipo, se pode ser gravado e para que serve numa biblioteca.
///
/// Duas decisões registradas aqui:
///
/// 1. **Os campos de curadoria levam o prefixo `ARCA/`.** Conferi no código
///    antigo: eles nunca chegaram a ser gravados, só existiam no estudo.
///    Como o nome estava livre, prefixar evita colisão com os `WM/` padrão
///    e deixa claro de onde vieram. Outra ferramenta de tag vai vê-los como
///    atributos customizados desconhecidos, que é o comportamento correto.
///
/// 2. **A categoria do estudo chamava-se "Customizado"**, que descreve o
///    mecanismo. Aqui virou "Curadoria", que descreve o assunto — são
///    procedência, qualidade da extração, onde está o disco físico e notas
///    curatoriais.
class CatalogoAsf
{
public:
    static const std::string Basico;
    static const std::string Musical;
    static const std::string Curadoria;
    static const std::string Imagem;
    static const std::string Tecnico;
    static const std::string Conteiner;
    static const std::string Outros;

    /// Ordem em que as categorias aparecem na tela.
    static const std::vector<std::string> &Ordem();

    /// Explicação que aparece sob o título de algumas categorias. "" = null no original.
    static std::string NotaDe(const std::string &categoria);

    /// Campos de curadoria e bibliográficos que o editor oferece mesmo quando ainda não existem no arquivo.
    static std::vector<const CampoAsf *> Editaveis();

    /// nullptr = null no original.
    static const CampoAsf *Conhecido(const std::string &atributo);

    static std::string CategoriaDe(const std::string &atributo);

    static std::string RotuloDe(const std::string &atributo);

    // LINUX: não havia no original. O atributo ASF catalogado cujo nome Vorbis é este
    // ("ARCA_VERIFICATION" -> "ARCA/Verification"), ou "". É o que deixa o EditorFlac devolver os
    // nomes ASF com a grafia certa (o Vorbis guarda tudo em maiúsculas).
    static std::string AtributoDoVorbis(const std::string &nomeVorbis);
};

} // namespace ARCA::Modelo

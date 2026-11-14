// ARCA no xOS - Interop/EditorFlac (de Interop\EditorAsf.cs).
// LINUX: o original lia e gravava atributos no contêiner ASF pelo Windows Media Format SDK
// (wmvcore.dll). Aqui o arquivo é FLAC: Vorbis comment + bloco PICTURE, pela libFLAC (metadata
// level 2, com padding). A classe muda de nome (EditorAsf -> EditorFlac, AtributoAsf ->
// AtributoFlac, TipoAtributoAsf -> TipoAtributoFlac), mas os métodos públicos são os mesmos e os
// NOMES DE ATRIBUTO continuam os do ASF ("WM/AlbumTitle", "Author", "ARCA/Verification",
// "WM/MCDI", "WM/Picture"...). A tradução ASF <-> Vorbis fica aqui dentro, pelo mapa de
// tools/converte_acervo.py (MAPA, TECNICOS e etiquetas), o mesmo que converteu o acervo em
// 02/11/2026. Assim quem chama o editor é portado sem mudar nada além do nome da classe.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct FLAC__Metadata_Chain;

namespace ARCA::Interop {

enum class TipoAtributoFlac
{
    Dword = 0, Texto = 1, Binario = 2, Booleano = 3,
    Qword = 4, Word = 5, Guid = 6
};

class AtributoFlac
{
public:
    std::string Nome;                 // o nome ASF ("WM/AlbumTitle"); o do arquivo está em NomeVorbis
    TipoAtributoFlac Tipo = TipoAtributoFlac::Texto;
    std::string Valor;
    int Bytes = 0;
    uint16_t Indice = 0;
    uint16_t Idioma = 0;
    // LINUX: não havia no original. O nome com que o campo está gravado no FLAC ("ALBUM"), para a
    // tela poder mostrar os dois; "" no que não é Vorbis comment (capa, propriedades do contêiner).
    std::string NomeVorbis;

    bool Binario() const { return Tipo == TipoAtributoFlac::Binario; }

    std::string TipoTexto() const;
};

/// Leitura e escrita de atributos no FLAC, pela libFLAC.
///
/// Portado do `AsfNativeTagger.cs` do ARCAwm, que já tinha as declarações
/// certas — o que foi acrescentado aqui é o lado da **leitura**: enumerar
/// tudo que existe no arquivo, inclusive os atributos customizados, que é o
/// que o `getItemInfo` do player não alcança.
///
/// Por que não usar a biblioteca do WMP para isso: ela devolve o que o
/// banco dela guardou, não o que está no arquivo. Para um acervo, a
/// diferença importa.
class EditorFlac
{
public:
    EditorFlac() = default;
    ~EditorFlac();                    // Dispose() -> Fechar()
    EditorFlac(const EditorFlac &) = delete;
    EditorFlac &operator=(const EditorFlac &) = delete;

    const std::string &Falha() const { return m_strFalha; }      // "" = null no original
    bool Aberto() const { return m_cadeia != nullptr; }

    bool Abrir(const std::string &arquivo);

    /// Todos os atributos de nível de arquivo, como estão no FLAC, com os nomes ASF.
    std::vector<AtributoFlac> LerTudo();

    /// O conteúdo bruto de um atributo de nível de arquivo, ou vazio se não
    /// houver. Serve ao WM/MCDI, o índice do CD de origem que o WMP grava
    /// como bytes (texto UTF-16 por dentro).
    // LINUX: no FLAC todo atributo é texto; o "bruto" é esse texto em UTF-16LE terminado em zero,
    // que é como o ASF o guardava. Vazio = null no original.
    std::vector<uint8_t> LerBruto(const std::string &strNome);

    /// O WM/MCDI como texto ("A+96+5C1C+…"), ou "" (null no original).
    std::string LerMcdi();

    /// A capa embutida (bloco PICTURE). Vazio = null no original.
    std::vector<uint8_t> LerImagem(std::string &mime, std::string &descricao);

    /// Grava o WM/MCDI como o acervo o guarda (no original: bytes, tipo binário, com o texto
    /// UTF-16 dentro; aqui: o campo MCDI, em texto).
    bool DefinirMcdi(const std::string &strMcdi);

    /// Grava (ou cria) um atributo de texto no nível do arquivo.
    bool DefinirTexto(const std::string &nome, const std::string &valor);

    /// Grava um atributo GUID. É o tipo de WM/MediaClassPrimaryID e SecondaryID.
    // LINUX: o .NET tinha System.Guid; aqui o GUID vai como texto em qualquer das formas que o
    // Guid(string) aceitava ("d1607dbc-e323-...", "{D1607DBC-...}" ou os 32 dígitos), e é gravado
    // como a conversão do acervo gravou: "{D1607DBC-E323-4BE2-86A1-48A42A28441E}".
    bool DefinirGuid(const std::string &strNome, const std::string &guidValor);

    void RemoverAtributo(const std::string &nome);

    /// O tipo da imagem pelos primeiros bytes (a "assinatura" do formato),
    /// e não pelo que quem chama supõe: capa PNG declarada como JPEG não
    /// abre em alguns tocadores.
    static std::string MimeDaImagem(const std::vector<uint8_t> &arr);

    /// Grava a capa frontal oficial (bloco PICTURE, tipo 3). mime "": descobre pelos bytes.
    bool DefinirImagem(const std::vector<uint8_t> &dadosImagem, const std::string &mime = "",
                       const std::string &descricao = "Cover");

    /// Confirma as alterações no arquivo. Sem isto, nada é gravado.
    bool Salvar();

    void Fechar();

    // ---- LINUX: a tradução dos nomes, que no Windows não existia ----
    /// O nome Vorbis de um atributo ASF ("WM/AlbumTitle" -> "ALBUM"), pela regra do
    /// converte_acervo.py. "" para os que não viram Vorbis comment (os TECNICOS; WM/Picture é o
    /// bloco PICTURE). Nome que já é Vorbis (só existe no FLAC: "encoder") fica como está, em
    /// maiúsculas. É o nome que DefinirTexto, RemoverAtributo e o EscritorFlac gravam.
    static std::string NomeVorbis(const std::string &nomeAsf);
    /// O nome ASF de um campo Vorbis ("ALBUM" -> "WM/AlbumTitle"): o mapa ao contrário e, nos
    /// ARCA_ e WM_, o catálogo (Modelo/CatalogoAsf). Campo que só existe no FLAC fica como está.
    static std::string NomeAsf(const std::string &nomeVorbis);
    /// Um GUID na forma do Guid.ToString() do .NET ("d1607dbc-e323-4be2-86a1-48a42a28441e"), ou ""
    /// se o texto não for GUID (o new Guid(string) do original lançaria).
    static std::string NormalizarGuid(const std::string &s);

private:
    FLAC__Metadata_Chain *m_cadeia = nullptr;
    std::string m_strArquivo;
    std::string m_strFalha;
    bool m_blnAlterado = false;
};

} // namespace ARCA::Interop

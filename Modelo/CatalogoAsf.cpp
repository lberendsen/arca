// ARCA no xOS - Modelo/CatalogoAsf (de Modelo\CatalogoAsf.cs).
#include "CatalogoAsf.h"

#include "Base/Texto.h"
#include "Interop/EditorFlac.h"

#include <map>

namespace ARCA::Modelo {

using namespace ARCA::Base;

CampoAsf::CampoAsf(const std::string &atributo, const std::string &rotulo, const std::string &categoria, bool editavel)
    : m_strAtributo(atributo), m_strRotulo(rotulo), m_strCategoria(categoria), m_blnEditavel(editavel)
{
    // LINUX: o nome Vorbis de cada campo. Imagem e contêiner não são Vorbis comment.
    if (categoria != CatalogoAsf::Imagem && categoria != CatalogoAsf::Conteiner)
        m_strVorbis = Interop::EditorFlac::NomeVorbis(atributo);
}

const std::string CatalogoAsf::Basico = "Bibliográfico";
const std::string CatalogoAsf::Musical = "Musical";
const std::string CatalogoAsf::Curadoria = "Curadoria";
const std::string CatalogoAsf::Imagem = "Imagem embutida";
const std::string CatalogoAsf::Tecnico = "Técnico";
const std::string CatalogoAsf::Conteiner = "Contêiner e fluxo";
const std::string CatalogoAsf::Outros = "Outros";

const std::vector<std::string> &CatalogoAsf::Ordem()
{
    static const std::vector<std::string> v = { Basico, Musical, Curadoria, Imagem, Tecnico, Conteiner, Outros };
    return v;
}

std::string CatalogoAsf::NotaDe(const std::string &categoria)
{
    if (categoria == Conteiner)
        // LINUX: o original dizia "Não estão gravados no arquivo: o SDK deduz do contêiner ao ler.
        // Vêm da origem do ASF como formato de transmissão." No FLAC quem deduz é o próprio ARCA,
        // do STREAMINFO; os nomes continuam os do ASF.
        return "Não estão gravados no arquivo: o ARCA deduz do STREAMINFO do FLAC ao ler. "
               "Os nomes vêm do ASF, da origem dele como formato de transmissão.";
    if (categoria == Curadoria)
        // LINUX: no FLAC o prefixo ARCA/ é gravado ARCA_ (converte_acervo.py).
        return "Procedência e anotação. Os campos do ARCA levam o prefixo ARCA/ (ARCA_ no FLAC); "
               "o índice do CD e o BitPerfect já vinham de antes.";
    if (categoria == Outros)
        return "O catálogo não reconhece estes. Vale olhar: ou é campo novo do seu "
               "acervo, ou está gravado no lugar errado.";
    return "";
}

namespace {

// O Dictionary<string, CampoAsf>(StringComparer.OrdinalIgnoreCase) do original. LINUX: guardado
// como vetor (a ordem de inserção, que é a ordem em que o Dictionary do .NET devolve os Values) +
// índice pela chave em minúsculas.
struct TMapa
{
    std::vector<CampoAsf> lstCampos;
    std::map<std::string, size_t> dicIndice;
    std::map<std::string, size_t> dicVorbis;     // LINUX: nome Vorbis -> campo (o primeiro)
};

const TMapa &Mapa()
{
    // LINUX: estático de função, e não de classe, para não depender da ordem de inicialização
    // entre arquivos (o EditorFlac consulta o catálogo).
    static const TMapa m = []
    {
        std::vector<CampoAsf> l;

        // --- Bibliográfico básico (atributos de raiz do ASF, sem prefixo) ---
        l.emplace_back("Title", "Título", CatalogoAsf::Basico, true);
        l.emplace_back("Author", "Artista", CatalogoAsf::Basico, true);
        l.emplace_back("Copyright", "Direitos", CatalogoAsf::Basico, true);
        l.emplace_back("Description", "Descrição", CatalogoAsf::Basico, true);
        l.emplace_back("Rating", "Avaliação", CatalogoAsf::Basico, true);

        // --- Musical avançado ---
        l.emplace_back("WM/AlbumTitle", "Álbum", CatalogoAsf::Musical, true);
        l.emplace_back("WM/AlbumArtist", "Artista do álbum", CatalogoAsf::Musical, true);
        l.emplace_back("WM/Genre", "Gênero", CatalogoAsf::Musical, true);
        l.emplace_back("WM/Year", "Ano", CatalogoAsf::Musical, true);
        l.emplace_back("WM/TrackNumber", "Faixa", CatalogoAsf::Musical, true);
        // WM/Track é o antecessor, com numeração começando em zero. Ainda
        // aparece em arquivos antigos; fica visível, mas não se edita — o
        // campo válido é o de cima.
        l.emplace_back("WM/Track", "Faixa (formato antigo, base zero)", CatalogoAsf::Musical, false);
        l.emplace_back("WM/PartOfSet", "Disco do conjunto", CatalogoAsf::Musical, true);
        l.emplace_back("WM/Composer", "Compositor", CatalogoAsf::Musical, true);
        l.emplace_back("WM/Conductor", "Regente", CatalogoAsf::Musical, true);
        l.emplace_back("WM/Publisher", "Gravadora", CatalogoAsf::Musical, true);
        l.emplace_back("WM/Producer", "Produtor", CatalogoAsf::Musical, true);
        l.emplace_back("WM/EncodedBy", "Codificado por", CatalogoAsf::Musical, true);
        l.emplace_back("WM/Lyrics", "Letra", CatalogoAsf::Musical, true);
        l.emplace_back("WM/Language", "Idioma", CatalogoAsf::Musical, true);
        l.emplace_back("WM/ISRC", "ISRC", CatalogoAsf::Musical, true);
        l.emplace_back("WM/BeatsPerMinute", "Andamento (BPM)", CatalogoAsf::Musical, true);
        l.emplace_back("WM/Mood", "Clima", CatalogoAsf::Musical, true);
        l.emplace_back("WM/OriginalArtist", "Artista original", CatalogoAsf::Musical, true);
        l.emplace_back("WM/OriginalAlbumTitle", "Álbum original", CatalogoAsf::Musical, true);
        l.emplace_back("WM/OriginalReleaseYear", "Ano do lançamento original", CatalogoAsf::Musical, true);
        l.emplace_back("WM/Provider", "Fornecedor", CatalogoAsf::Musical, true);

        // --- Curadoria: procedência e o que o curador anotou ---
        //
        // Nem todos são do ARCA. O WM/MCDI é padrão e guarda o índice do CD
        // de origem — é a impressão digital do disco físico, e é o que se
        // manda a um serviço de identificação. Para um acervo isso é
        // procedência, não detalhe técnico.
        //
        // O BitPerfect já existe nos arquivos do Lorenzo, com valores como
        // "FLAC Import". Foi inventado antes do estudo e nunca entrou nele.
        l.emplace_back("WM/MCDI", "Índice do CD de origem", CatalogoAsf::Curadoria, false);
        l.emplace_back("BitPerfect", "Extração bit-perfect", CatalogoAsf::Curadoria, true);
        l.emplace_back("ARCA/LibraryCatalogID", "Número de catálogo", CatalogoAsf::Curadoria, true);
        l.emplace_back("ARCA/ImportDate", "Data de entrada", CatalogoAsf::Curadoria, true);
        l.emplace_back("ARCA/AcquisitionSource", "Origem da aquisição", CatalogoAsf::Curadoria, true);
        l.emplace_back("ARCA/RipSource", "Mídia de origem", CatalogoAsf::Curadoria, true);
        l.emplace_back("ARCA/RipDrive", "Drive usado na extração", CatalogoAsf::Curadoria, true);
        l.emplace_back("ARCA/RipQualityScore", "Qualidade da extração", CatalogoAsf::Curadoria, true);
        l.emplace_back("ARCA/Verification", "Verificação", CatalogoAsf::Curadoria, true);
        l.emplace_back("ARCA/CollectionShelf", "Prateleira", CatalogoAsf::Curadoria, true);
        l.emplace_back("ARCA/PhysicalMediaLocation", "Onde está o disco físico", CatalogoAsf::Curadoria, true);
        l.emplace_back("ARCA/NotesCuratoriais", "Notas curatoriais", CatalogoAsf::Curadoria, true);

        // --- Imagem embutida ---
        l.emplace_back("WM/Picture", "Imagem", CatalogoAsf::Imagem, false);

        // --- Gravados no lugar errado ---
        //
        // Título, artista, direitos, descrição e avaliação moram no Content
        // Description Object do ASF, endereçados pelo nome SEM prefixo. O
        // que leva "WM/" vai para o Extended Content Description, que é
        // outro lugar. Um tagueador que escreve "WM/Title" produz um
        // arquivo que continua, na prática, sem título.
        //
        // Isso foi encontrado comparando 01-pre-tagger com 02-post-tagger:
        // o tagueador antigo gravou WM/Title, e nenhuma das amostras tem um
        // Title puro. Ficam aqui, visíveis e rotulados, em vez de
        // escondidos entre os técnicos.
        l.emplace_back("WM/Title", "WM/Title — fora do lugar (o certo é Título)", CatalogoAsf::Outros, false);
        l.emplace_back("WM/Author", "WM/Author — fora do lugar (o certo é Artista)", CatalogoAsf::Outros, false);

        // --- Técnico: gravado no arquivo, mas não se edita à mão ---
        for (const char *t : {
                 "WM/AudioCodec", "WM/Codec", "WM/MediaClassPrimaryID",
                 "WM/MediaClassSecondaryID", "WM/EncodingSettings", "WM/ToolName",
                 "WM/WMADRCPeakReference", "WM/WMADRCAverageReference",
                 "WM/WMADRCPeakTarget", "WM/WMADRCAverageTarget",
                 "NSC_Name", "WM/UniqueFileIdentifier", "WM/EncodingTime",
                 // As duas grafias circulam na documentação da Microsoft;
                 // catalogar ambas custa nada e evita cair em "Outros".
                 "WM/MediaPrimaryClassID", "WM/MediaClassPrimaryID",
                 "WM/MediaSecondaryClassID", "WM/MediaClassSecondaryID" })
            l.emplace_back(t, t, CatalogoAsf::Tecnico, false);

        // Medidas de volume que o WMP grava ao ripar, para nivelamento.
        // Não são enfeite: são dados de loudness já prontos no arquivo, e
        // interessam ao histograma do módulo EM EXECUÇÃO.
        l.emplace_back("PeakValue", "Pico de nível", CatalogoAsf::Tecnico, false);
        l.emplace_back("AverageLevel", "Nível médio", CatalogoAsf::Tecnico, false);

        // --- Contêiner: NÃO está gravado. O SDK calcula ao enumerar. ---
        //
        // Estes não constam do estudo em Sheet_2, e com razão: aquele
        // levantamento é de vocabulário de AUTORIA — o que o ASF permite
        // armazenar. Estes aqui são de OBSERVAÇÃO — o que o SDK conclui do
        // arquivo. "HasAudio" e "Seekable" não estão escritos em lugar
        // nenhum; são deduções.
        //
        // A herança é do nome original do formato: Advanced *Streaming*
        // Format. O cabeçalho carrega o que um servidor precisaria negociar
        // com um cliente antes de começar a transmitir.
        // LINUX: no FLAC quem deduz é o EditorFlac, do STREAMINFO (Duration, Bitrate, FileSize,
        // HasAudio, Seekable, IsVBR); os outros simplesmente não aparecem.
        for (const char *t : {
                 "Broadcast", "Seekable", "Stridable",
                 "Can_Skip_Forward", "Can_Skip_Backward", "ASFLeakyBucketPairs",
                 "HasAudio", "HasVideo", "HasImage", "HasScript",
                 "HasAttachedImages", "HasArbitraryDataStream", "HasFileTransferStream",
                 "Bitrate", "CurrentBitrate", "OptimalBitrate", "IsVBR",
                 "Duration", "FileSize", "WM/ASFPacketCount", "WM/ContainerFormat",
                 "Is_Protected", "Is_Trusted", "Signature_Name",
                 "WM/ASFSecurityObjectsSize",
                 "WMFSDKVersion", "WMFSDKNeeded", "MediaFoundationVersion" })
            l.emplace_back(t, t, CatalogoAsf::Conteiner, false);

        TMapa d;
        for (CampoAsf &c : l)
        {
            std::string k = Minusculas(c.Atributo());
            auto it = d.dicIndice.find(k);
            if (it != d.dicIndice.end()) d.lstCampos[it->second] = c;     // d[c.Atributo] = c
            else
            {
                d.dicIndice[k] = d.lstCampos.size();
                d.lstCampos.push_back(c);
            }
        }
        for (size_t i = 0; i < d.lstCampos.size(); i++)
        {
            const std::string &v = d.lstCampos[i].Vorbis();
            if (!v.empty() && !d.dicVorbis.count(v)) d.dicVorbis[v] = i;
        }
        return d;
    }();
    return m;
}

} // namespace

std::vector<const CampoAsf *> CatalogoAsf::Editaveis()
{
    std::vector<const CampoAsf *> r;
    for (const CampoAsf &c : Mapa().lstCampos)
        if (c.Editavel()) r.push_back(&c);
    return r;
}

const CampoAsf *CatalogoAsf::Conhecido(const std::string &atributo)
{
    const TMapa &m = Mapa();
    auto it = m.dicIndice.find(Minusculas(atributo));
    return it != m.dicIndice.end() ? &m.lstCampos[it->second] : nullptr;
}

std::string CatalogoAsf::CategoriaDe(const std::string &atributo)
{
    const CampoAsf *c = Conhecido(atributo);
    if (c != nullptr) return c->Categoria();
    // Desconhecido vai para "Outros", sempre. A tentação é jogar o que
    // começa com WM/ em "Técnico", mas isso enterra novidade: num
    // programa de acervo, o que o catálogo não conhece precisa aparecer.
    return Outros;
}

std::string CatalogoAsf::RotuloDe(const std::string &atributo)
{
    const CampoAsf *c = Conhecido(atributo);
    return c != nullptr ? c->Rotulo() : atributo;
}

std::string CatalogoAsf::AtributoDoVorbis(const std::string &nomeVorbis)
{
    const TMapa &m = Mapa();
    auto it = m.dicVorbis.find(Maiusculas(nomeVorbis));
    return it != m.dicVorbis.end() ? m.lstCampos[it->second].Atributo() : "";
}

} // namespace ARCA::Modelo

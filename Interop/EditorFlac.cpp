// ARCA no xOS - Interop/EditorFlac (de Interop\EditorAsf.cs).
// LINUX: o Windows Media Format SDK (IWMMetadataEditor, IWMHeaderInfo3) dá lugar à libFLAC,
// metadata level 2 (FLAC__Metadata_Chain): lê todos os blocos, altera na memória e grava de uma
// vez no Salvar, usando o padding quando cabe. O áudio (os quadros FLAC) não é tocado; quando a
// libFLAC precisa reescrever o arquivo inteiro (a capa nova não cabe no padding), o Salvar confere
// por SHA-256 que os bytes do áudio saíram iguais.
#include "EditorFlac.h"

#include "Base/Arquivo.h"
#include "Base/Hash.h"
#include "Base/Texto.h"
#include "Modelo/CatalogoAsf.h"
#include "Modelo/Formato.h"

#include <FLAC/format.h>
#include <FLAC/metadata.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <stdexcept>
#include <utility>

namespace ARCA::Interop {

using namespace ARCA::Base;

std::string AtributoFlac::TipoTexto() const
{
    switch (Tipo)
    {
        case TipoAtributoFlac::Texto: return "texto";
        case TipoAtributoFlac::Dword: return "número (32)";
        case TipoAtributoFlac::Qword: return "número (64)";
        case TipoAtributoFlac::Word: return "número (16)";
        case TipoAtributoFlac::Booleano: return "sim/não";
        case TipoAtributoFlac::Guid: return "GUID";
        case TipoAtributoFlac::Binario: return "binário";
        default: return "?";
    }
}

namespace {

// ---- o mapa de tools/converte_acervo.py ----

// Mapa ASF -> Vorbis: o do ARCA (Herança\ARCA-FLAC\Exportacao.cs), mais o que a Discoteca usa.
const std::vector<std::pair<std::string, std::string>> &Mapa()
{
    static const std::vector<std::pair<std::string, std::string>> m = {
        { "Title", "TITLE" }, { "Author", "ARTIST" }, { "Copyright", "COPYRIGHT" }, { "Description", "COMMENT" },
        { "WM/AlbumTitle", "ALBUM" }, { "WM/AlbumArtist", "ALBUMARTIST" }, { "WM/TrackNumber", "TRACKNUMBER" },
        { "WM/PartOfSet", "DISCNUMBER" }, { "WM/Year", "DATE" }, { "WM/Genre", "GENRE" }, { "WM/Composer", "COMPOSER" },
        { "WM/Conductor", "CONDUCTOR" }, { "WM/Publisher", "ORGANIZATION" }, { "WM/ISRC", "ISRC" },
        { "WM/Lyrics", "LYRICS" }, { "WM/Mood", "MOOD" }, { "WM/BeatsPerMinute", "BPM" }, { "WM/EncodedBy", "ENCODED-BY" },
        { "WM/Writer", "LYRICIST" }, { "WM/OriginalReleaseYear", "ORIGINALDATE" },
        { "WM/MCDI", "MCDI" },                                   // o índice do CD: conferir anos depois
        { "WM/MediaClassPrimaryID", "WM_MEDIACLASSPRIMARYID" },  // música / audiolivro / fala (ARCA)
        { "WM/MediaClassSecondaryID", "WM_MEDIACLASSSECONDARYID" },
        { "WM/MediaPrimaryClassID", "WM_MEDIACLASSPRIMARYID" },  // grafia que o WMP antigo usou
        { "PeakValue", "WM_PEAKVALUE" }, { "AverageLevel", "WM_AVERAGELEVEL" },   // nivelamento já medido
        { "WM/WMADRCPeakReference", "WM_DRCPEAKREFERENCE" }, { "WM/WMADRCAverageReference", "WM_DRCAVERAGEREFERENCE" },
        { "Rating", "WM_RATING" }, { "WM/SharedUserRating", "WM_RATING" },
    };
    return m;
}

// derivados do contêiner ou do codificador: não são da obra, ficam para trás
const std::set<std::string> &Tecnicos()
{
    static const std::set<std::string> s = [] {
        std::set<std::string> r;
        for (const char *t : { "IsVBR", "WMFSDKVersion", "WMFSDKNeeded", "ASFLeakyBucketPairs", "DeviceConformanceTemplate",
                               "MediaFoundationVersion", "WM/EncodingTime", "WM/Track", "WM/WMADRCPeakTarget",
                               "WM/WMADRCAverageTarget", "WM/Picture", "Duration", "Bitrate", "Seekable", "Stridable",
                               "Broadcast", "Is_Protected", "Is_Trusted", "Signature_Name", "HasAudio", "HasVideo",
                               "HasImage", "HasScript", "HasArbitraryDataStream", "HasFileTransferStream", "FileSize",
                               "NumberOfFrames", "OptimalBitrate", "CurrentBitrate", "WM/ToolName", "WM/ToolVersion",
                               "WM/UniqueFileIdentifier", "WM/WMContentID", "WM/WMCollectionID", "WM/WMCollectionGroupID",
                               "WM/Provider", "WM/ProviderRating", "WM/ProviderStyle", "WM/MediaIsStereo" })
            r.insert(Minusculas(t));
        return r;
    }();
    return s;
}

const std::string *NoMapa(const std::string &nomeAsf)
{
    for (const auto &p : Mapa())
        if (IgualSemCaixa(p.first, nomeAsf)) return &p.second;
    return nullptr;
}

bool EhTecnico(const std::string &nomeAsf) { return Tecnicos().count(Minusculas(nomeAsf)) > 0; }

bool EhAtributoGuid(const std::string &nomeAsf)
{
    for (const char *g : { "WM/MediaClassPrimaryID", "WM/MediaClassSecondaryID", "WM/MediaPrimaryClassID",
                           "WM/MediaSecondaryClassID" })
        if (IgualSemCaixa(nomeAsf, g)) return true;
    return false;
}

// texto UTF-8 -> UTF-16LE terminado em zero (o string do ASF)
std::vector<uint8_t> ParaUtf16LE(const std::string &s)
{
    std::vector<uint8_t> r;
    for (uint32_t cp : CodePoints(s))
    {
        auto poe = [&r](uint32_t u) { r.push_back((uint8_t) (u & 0xFF)); r.push_back((uint8_t) (u >> 8)); };
        if (cp >= 0x10000)
        {
            cp -= 0x10000;
            poe(0xD800 + (cp >> 10));
            poe(0xDC00 + (cp & 0x3FF));
        }
        else poe(cp);
    }
    r.push_back(0);
    r.push_back(0);
    return r;
}

// nome e valor de uma entrada do Vorbis comment (NOME=valor), sem depender do terminador
bool Separa(const FLAC__StreamMetadata_VorbisComment_Entry &e, std::string &nome, std::string &valor)
{
    const char *p = (const char *) e.entry;
    const char *igual = (const char *) memchr(p, '=', e.length);
    if (!igual) return false;
    nome.assign(p, igual - p);
    valor.assign(igual + 1, p + e.length - (igual + 1));
    return true;
}

/// Largura, altura e profundidade direto do cabeçalho, sem decodificar a imagem. O
/// bloco PICTURE pede essas medidas, e carregar um bitmap inteiro só
/// para saber o tamanho seria caro num álbum com dezenas de faixas.
/// Zero quando não reconhecer — o campo aceita.
// (de Herança\ARCA-FLAC\Exportacao.cs, MedirImagem.) LINUX: acrescentada a profundidade (bits por
// pixel), que o bloco PICTURE também pede: bits por amostra x componentes, do IHDR ou do SOF.
void MedirImagem(const std::vector<uint8_t> &b, uint32_t &largura, uint32_t &altura, uint32_t &profundidade)
{
    largura = 0; altura = 0; profundidade = 0;
    // PNG: IHDR vem logo depois da assinatura de 8 bytes.
    if (b.size() > 26 && b[0] == 0x89 && b[1] == 0x50 && b[2] == 0x4E && b[3] == 0x47)
    {
        largura = ((uint32_t) b[16] << 24) | ((uint32_t) b[17] << 16) | ((uint32_t) b[18] << 8) | b[19];
        altura = ((uint32_t) b[20] << 24) | ((uint32_t) b[21] << 16) | ((uint32_t) b[22] << 8) | b[23];
        int canais = b[25] == 2 ? 3 : b[25] == 4 ? 2 : b[25] == 6 ? 4 : 1;
        profundidade = (uint32_t) b[24] * canais;
        return;
    }

    // JPEG: percorre os marcadores até um SOF.
    if (b.size() > 4 && b[0] == 0xFF && b[1] == 0xD8)
    {
        size_t i = 2;
        while (i + 9 < b.size())
        {
            if (b[i] != 0xFF) { i++; continue; }
            int marca = b[i + 1];
            if (marca == 0xD8 || marca == 0x01 || (marca >= 0xD0 && marca <= 0xD7))
            { i += 2; continue; }
            int tam = (b[i + 2] << 8) | b[i + 3];
            // SOF0..SOF15, menos os marcadores que não são de quadro
            if (marca >= 0xC0 && marca <= 0xCF &&
                marca != 0xC4 && marca != 0xC8 && marca != 0xCC)
            {
                altura = (b[i + 5] << 8) | b[i + 6];
                largura = (b[i + 7] << 8) | b[i + 8];
                profundidade = (uint32_t) b[i + 4] * b[i + 9];
                return;
            }
            if (tam <= 0) return;
            i += 2 + tam;
        }
    }
}

// LINUX: onde começa o áudio (o primeiro quadro) no arquivo: depois de um ID3v2 eventual, do
// "fLaC" e de todos os blocos de metadados. -1 se não for FLAC.
int64_t InicioDoAudio(const std::string &caminho, int64_t &lngTamanho)
{
    lngTamanho = -1;
    FILE *f = fopen(caminho.c_str(), "rb");
    if (!f) return -1;
    int64_t r = -1;
    uint8_t c[10];
    int64_t p = 0;
    if (fread(c, 1, 10, f) == 10)
    {
        if (c[0] == 'I' && c[1] == 'D' && c[2] == '3')
        {
            p = 10 + (((int64_t) (c[6] & 0x7F) << 21) | ((c[7] & 0x7F) << 14) | ((c[8] & 0x7F) << 7) | (c[9] & 0x7F));
            if (c[5] & 0x10) p += 10;
        }
        uint8_t m[4];
        if (fseeko(f, p, SEEK_SET) == 0 && fread(m, 1, 4, f) == 4 && memcmp(m, "fLaC", 4) == 0)
        {
            p += 4;
            while (true)
            {
                uint8_t h[4];
                if (fread(h, 1, 4, f) != 4) { p = -1; break; }
                int64_t n = ((int64_t) h[1] << 16) | (h[2] << 8) | h[3];
                p += 4 + n;
                if (h[0] & 0x80) break;
                if (fseeko(f, p, SEEK_SET) != 0) { p = -1; break; }
            }
            r = p;
        }
    }
    if (fseeko(f, 0, SEEK_END) == 0) lngTamanho = ftello(f);
    fclose(f);
    return r;
}

// SHA-256 de um trecho do arquivo
std::string Sha256Trecho(const std::string &caminho, int64_t inicio, int64_t n)
{
    FILE *f = fopen(caminho.c_str(), "rb");
    if (!f) return "";
    Hasher h(Algoritmo::Sha256);
    std::vector<uint8_t> buf(1 << 20);
    bool ok = fseeko(f, inicio, SEEK_SET) == 0;
    while (ok && n > 0)
    {
        size_t q = fread(buf.data(), 1, (size_t) std::min<int64_t>(n, (int64_t) buf.size()), f);
        if (q == 0) { ok = false; break; }
        h.Acrescenta(buf.data(), q);
        n -= (int64_t) q;
    }
    fclose(f);
    if (!ok) return "";
    return Hex(h.Final());
}

std::string Status(FLAC__Metadata_Chain *c)
{
    return FLAC__Metadata_ChainStatusString[FLAC__metadata_chain_status(c)];
}

} // namespace

// ===============================================================
//  Os nomes
// ===============================================================

std::string EditorFlac::NomeVorbis(const std::string &nomeAsf)
{
    // a função etiquetas() do converte_acervo.py, nome a nome
    if (EhTecnico(nomeAsf)) return "";
    // LINUX: o campo que só existe no FLAC, e que o LerTudo devolveu com o nome Vorbis mesmo
    // ("encoder", "REPLAYGAIN_TRACK_GAIN"), volta com esse nome: a regra da conversão o
    // transformaria em WM_ENCODER, que é outro campo. Nome ASF tem barra ou é misto
    // ("PeakValue", "NSC_Name"); nome Vorbis é todo maiúsculo ou todo minúsculo.
    if (nomeAsf.find('/') == std::string::npos && !NoMapa(nomeAsf) && nomeAsf != "BitPerfect"
        && (nomeAsf == Maiusculas(nomeAsf) || nomeAsf == Minusculas(nomeAsf)))
        return Maiusculas(nomeAsf);
    std::string chave;
    if (const std::string *m = NoMapa(nomeAsf)) chave = *m;
    else if (ComecaCom(nomeAsf, "ARCA/")) chave = "ARCA_" + Substitui(nomeAsf.substr(5), "/", "_");   // convenção do ARCA
    else if (nomeAsf == "BitPerfect") chave = "ARCA_BITPERFECT";
    else chave = "WM_" + Substitui(Substitui(nomeAsf, "WM/", ""), "/", "_");   // desconhecido: preservado
    return Maiusculas(chave);
}

std::string EditorFlac::NomeAsf(const std::string &nomeVorbis)
{
    std::string v = Maiusculas(nomeVorbis);
    for (const auto &p : Mapa())                // o mapa ao contrário; na repetição, a primeira grafia
        if (p.second == v) return p.first;
    if (v == "ARCA_BITPERFECT") return "BitPerfect";
    std::string c = Modelo::CatalogoAsf::AtributoDoVorbis(v);
    if (!c.empty()) return c;
    if (ComecaCom(v, "ARCA_")) return "ARCA/" + v.substr(5);
    if (ComecaCom(v, "WM_")) return "WM/" + v.substr(3);
    return nomeVorbis;                          // só existe no FLAC: fica com o nome do arquivo
}

std::string EditorFlac::NormalizarGuid(const std::string &s)
{
    std::string t = Trim(s), h;
    if (t.size() >= 2 && ((t.front() == '{' && t.back() == '}') || (t.front() == '(' && t.back() == ')')))
        t = t.substr(1, t.size() - 2);
    if (t.size() == 36)
    {
        for (size_t i = 0; i < t.size(); i++)
        {
            bool hifen = i == 8 || i == 13 || i == 18 || i == 23;
            if (hifen != (t[i] == '-')) return "";
            if (!hifen) h += t[i];
        }
    }
    else if (t.size() == 32) h = t;
    else return "";
    for (char &ch : h)
    {
        if (!isxdigit((unsigned char) ch)) return "";
        ch = (char) tolower((unsigned char) ch);
    }
    return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" + h.substr(20);
}

// ===============================================================
//  O editor
// ===============================================================

EditorFlac::~EditorFlac() { Fechar(); }

bool EditorFlac::Abrir(const std::string &arquivo)
{
    Fechar();
    m_strFalha.clear();
    try
    {
        m_cadeia = FLAC__metadata_chain_new();
        if (m_cadeia == nullptr)
        {
            // LINUX: era "WMCreateEditor devolveu 0x..."
            m_strFalha = "FLAC__metadata_chain_new devolveu nulo";
            return false;
        }
        if (!FLAC__metadata_chain_read(m_cadeia, arquivo.c_str()))
        {
            // LINUX: no original, o SDK recusava o arquivo que não era ASF; aqui, o que não é FLAC.
            m_strFalha = "libFLAC: " + Status(m_cadeia);
            Fechar();
            return false;
        }
        m_strArquivo = arquivo;
        return true;
    }
    catch (const std::exception &ex)
    {
        m_strFalha = std::string("exceção: ") + ex.what();
        Fechar();
        return false;
    }
}

std::vector<AtributoFlac> EditorFlac::LerTudo()
{
    std::vector<AtributoFlac> lista;
    if (m_cadeia == nullptr) return lista;

    FLAC__Metadata_Iterator *it = FLAC__metadata_iterator_new();
    if (!it) { m_strFalha = "sem memória para o iterador da libFLAC"; return lista; }
    FLAC__metadata_iterator_init(it, m_cadeia);

    std::vector<AtributoFlac> lstConteiner;
    auto acrescenta = [&lista](AtributoFlac a) { a.Indice = (uint16_t) lista.size(); lista.push_back(std::move(a)); };
    do
    {
        FLAC__StreamMetadata *b = FLAC__metadata_iterator_get_block(it);
        if (b->type == FLAC__METADATA_TYPE_STREAMINFO)
        {
            // LINUX: o SDK do Windows relatava, ao enumerar, propriedades que ele deduzia do
            // contêiner (a categoria "Contêiner e fluxo" do catálogo). No FLAC elas saem do
            // STREAMINFO e do tamanho do arquivo; as que não têm sentido aqui não aparecem.
            const FLAC__StreamMetadata_StreamInfo &si = b->data.stream_info;
            int64_t lngTamanho = Tamanho(m_strArquivo);
            double dblSegundos = si.sample_rate > 0 ? (double) si.total_samples / si.sample_rate : 0;
            auto poe = [&lstConteiner](const char *nome, TipoAtributoFlac tipo, const std::string &valor, int bytes) {
                AtributoFlac a;
                a.Nome = nome; a.Tipo = tipo; a.Valor = valor; a.Bytes = bytes;
                lstConteiner.push_back(a);
            };
            if (dblSegundos > 0)
            {
                // Duration do ASF: em unidades de 100 ns
                poe("Duration", TipoAtributoFlac::Qword, std::to_string((uint64_t) std::llround(dblSegundos * 1e7)), 8);
                if (lngTamanho > 0)
                    poe("Bitrate", TipoAtributoFlac::Dword, std::to_string((uint32_t) std::llround(lngTamanho * 8.0 / dblSegundos)), 4);
            }
            poe("Seekable", TipoAtributoFlac::Booleano, "sim", 4);
            poe("HasAudio", TipoAtributoFlac::Booleano, "sim", 4);
            poe("IsVBR", TipoAtributoFlac::Booleano, "sim", 4);
            if (lngTamanho >= 0) poe("FileSize", TipoAtributoFlac::Qword, std::to_string(lngTamanho), 8);
        }
        else if (b->type == FLAC__METADATA_TYPE_VORBIS_COMMENT)
        {
            const FLAC__StreamMetadata_VorbisComment &vc = b->data.vorbis_comment;
            for (uint32_t i = 0; i < vc.num_comments; i++)
            {
                std::string strNome, strValor;
                AtributoFlac a;
                if (!Separa(vc.comments[i], strNome, strValor))
                {
                    a.Nome = "(atributo " + std::to_string(lista.size()) + ")";
                    a.Tipo = TipoAtributoFlac::Texto;
                    a.Valor = "não consegui ler — comentário sem '='";
                    acrescenta(a);
                    continue;
                }
                a.Nome = NomeAsf(strNome);
                a.NomeVorbis = strNome;
                a.Idioma = 0;
                a.Valor = Utf8Valido(strValor);
                a.Bytes = (int) strValor.size();
                a.Tipo = TipoAtributoFlac::Texto;
                // LINUX: no ASF a classe de mídia era do tipo GUID (16 bytes); no FLAC é texto
                // "{D1607DBC-...}". Volta a ser GUID, com o valor no formato do Guid.ToString().
                if (EhAtributoGuid(a.Nome))
                {
                    std::string g = NormalizarGuid(strValor);
                    if (!g.empty()) { a.Tipo = TipoAtributoFlac::Guid; a.Valor = g; a.Bytes = 16; }
                }
                acrescenta(a);
            }
        }
        else if (b->type == FLAC__METADATA_TYPE_PICTURE)
        {
            AtributoFlac a;
            a.Nome = "WM/Picture";
            a.Tipo = TipoAtributoFlac::Binario;
            a.Bytes = (int) b->data.picture.data_length;
            a.Valor = "[" + Modelo::Formato::Inteiro(b->data.picture.data_length) + " bytes]";
            acrescenta(a);
        }
    } while (FLAC__metadata_iterator_next(it));
    FLAC__metadata_iterator_delete(it);

    for (AtributoFlac &a : lstConteiner) acrescenta(a);
    return lista;
}

std::vector<uint8_t> EditorFlac::LerBruto(const std::string &strNome)
{
    if (m_cadeia == nullptr) return {};
    std::vector<uint8_t> r;
    bool blnAchou = false;
    FLAC__Metadata_Iterator *it = FLAC__metadata_iterator_new();
    if (!it) return {};
    FLAC__metadata_iterator_init(it, m_cadeia);
    do
    {
        FLAC__StreamMetadata *b = FLAC__metadata_iterator_get_block(it);
        if (b->type == FLAC__METADATA_TYPE_VORBIS_COMMENT)
        {
            const FLAC__StreamMetadata_VorbisComment &vc = b->data.vorbis_comment;
            for (uint32_t i = 0; i < vc.num_comments && !blnAchou; i++)
            {
                std::string n, v;
                if (!Separa(vc.comments[i], n, v)) continue;
                if (!IgualSemCaixa(NomeAsf(n), strNome)) continue;
                r = ParaUtf16LE(Utf8Valido(v));
                blnAchou = true;
            }
        }
        else if (b->type == FLAC__METADATA_TYPE_PICTURE && !blnAchou && IgualSemCaixa(strNome, "WM/Picture"))
        {
            // LINUX: no original o bruto do WM/Picture era a estrutura WM_PICTURE, com ponteiros
            // para dentro do próprio buffer (sem uso fora dali). Aqui: os bytes da imagem.
            r.assign(b->data.picture.data, b->data.picture.data + b->data.picture.data_length);
            blnAchou = true;
        }
    } while (!blnAchou && FLAC__metadata_iterator_next(it));
    FLAC__metadata_iterator_delete(it);
    return r;
}

std::string EditorFlac::LerMcdi()
{
    std::vector<uint8_t> arr = LerBruto("WM/MCDI");
    if (arr.size() < 2) return "";
    std::string s = DeUtf16LE(arr.data(), arr.size());
    while (!s.empty() && s.back() == '\0') s.pop_back();      // TrimEnd('\0')
    return s;
}

std::vector<uint8_t> EditorFlac::LerImagem(std::string &mime, std::string &descricao)
{
    mime.clear();
    descricao.clear();
    if (m_cadeia == nullptr) return {};

    std::vector<uint8_t> imagem;
    FLAC__Metadata_Iterator *it = FLAC__metadata_iterator_new();
    if (!it) return {};
    FLAC__metadata_iterator_init(it, m_cadeia);
    do
    {
        FLAC__StreamMetadata *b = FLAC__metadata_iterator_get_block(it);
        if (b->type != FLAC__METADATA_TYPE_PICTURE) continue;
        const FLAC__StreamMetadata_Picture &p = b->data.picture;
        if (p.data_length == 0 || p.data == nullptr) continue;
        // LINUX: no ASF o layout da estrutura WM_PICTURE era ambíguo (TentarLayout com dois
        // alinhamentos); o bloco PICTURE do FLAC não tem essa ambiguidade. Fica o selo:
        // Selo de arquivo de imagem: se não for JPEG nem PNG, o
        // deslocamento provavelmente está errado.
        bool jpeg = p.data_length > 3 && p.data[0] == 0xFF && p.data[1] == 0xD8;
        bool png = p.data_length > 8 && p.data[0] == 0x89 && p.data[1] == 0x50;
        if (!jpeg && !png) continue;
        imagem.assign(p.data, p.data + p.data_length);
        if (p.mime_type) mime = p.mime_type;
        if (p.description) descricao = (const char *) p.description;
        break;
    } while (FLAC__metadata_iterator_next(it));
    FLAC__metadata_iterator_delete(it);
    return imagem;
}

namespace {

// o bloco VORBIS_COMMENT da cadeia; cria um logo depois do STREAMINFO se não houver
FLAC__StreamMetadata *Comentarios(FLAC__Metadata_Chain *cadeia)
{
    FLAC__Metadata_Iterator *it = FLAC__metadata_iterator_new();
    if (!it) return nullptr;
    FLAC__metadata_iterator_init(it, cadeia);
    FLAC__StreamMetadata *r = nullptr;
    do
    {
        if (FLAC__metadata_iterator_get_block_type(it) == FLAC__METADATA_TYPE_VORBIS_COMMENT)
        {
            r = FLAC__metadata_iterator_get_block(it);
            break;
        }
    } while (FLAC__metadata_iterator_next(it));
    if (!r)
    {
        FLAC__metadata_iterator_init(it, cadeia);       // o primeiro é sempre o STREAMINFO
        FLAC__StreamMetadata *novo = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
        if (novo && FLAC__metadata_iterator_insert_block_after(it, novo)) r = novo;
        else if (novo) FLAC__metadata_object_delete(novo);
    }
    FLAC__metadata_iterator_delete(it);
    return r;
}

} // namespace

/// Grava o WM/MCDI como o acervo o guarda.
// LINUX: o original gravava como o WMP grava: bytes (tipo binário) com o texto UTF-16 dentro,
// terminado em zero. A conversão do acervo passou o MCDI a texto (campo MCDI), e é assim que vai.
bool EditorFlac::DefinirMcdi(const std::string &strMcdi)
{
    if (m_cadeia == nullptr || strMcdi.empty()) return false;
    return DefinirTexto("WM/MCDI", strMcdi);     // a falha já sai "Ao gravar 'WM/MCDI': ..."
}

bool EditorFlac::DefinirTexto(const std::string &nome, const std::string &valor)
{
    if (m_cadeia == nullptr) return false;
    std::string strVorbis = EditorFlac::NomeVorbis(nome);
    if (strVorbis.empty())
    {
        // LINUX: no ASF qualquer nome gravava; os técnicos do ASF não existem no FLAC
        // (converte_acervo.py, TECNICOS) e não são inventados aqui.
        m_strFalha = "Ao gravar '" + nome + "': é um campo técnico do ASF, que o FLAC não guarda";
        return false;
    }
    if (!FLAC__format_vorbiscomment_entry_name_is_legal(strVorbis.c_str()))
    {
        m_strFalha = "Ao gravar '" + nome + "': '" + strVorbis + "' não é um nome de campo Vorbis válido";
        return false;
    }
    FLAC__StreamMetadata *vc = Comentarios(m_cadeia);
    if (!vc) { m_strFalha = "Ao gravar '" + nome + "': sem memória"; return false; }

    std::string v = valor;
    // LINUX: o SetAttribute do ASF gravava o texto vazio como atributo vazio. No acervo em FLAC
    // campo vazio não existe (a conversão o deixou de fora); gravar "" tira o campo.
    if (v.empty())
    {
        if (FLAC__metadata_object_vorbiscomment_remove_entries_matching(vc, strVorbis.c_str()) < 0)
        {
            m_strFalha = "Ao gravar '" + nome + "': sem memória";
            return false;
        }
        m_blnAlterado = true;
        return true;
    }

    FLAC__StreamMetadata_VorbisComment_Entry e;
    if (!FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(&e, strVorbis.c_str(), v.c_str()))
    {
        m_strFalha = "Ao gravar '" + nome + "': o valor não é UTF-8 válido";
        return false;
    }
    // O SetAttribute troca o valor do atributo existente. LINUX: no Vorbis o campo pode se repetir;
    // o primeiro é trocado no lugar (a ordem dos campos fica), os outros saem.
    int intPrimeiro = FLAC__metadata_object_vorbiscomment_find_entry_from(vc, 0, strVorbis.c_str());
    bool ok;
    if (intPrimeiro < 0) ok = FLAC__metadata_object_vorbiscomment_append_comment(vc, e, false);
    else
    {
        ok = FLAC__metadata_object_vorbiscomment_set_comment(vc, (uint32_t) intPrimeiro, e, false);
        int i;
        while (ok && (i = FLAC__metadata_object_vorbiscomment_find_entry_from(vc, (uint32_t) intPrimeiro + 1, strVorbis.c_str())) >= 0)
            ok = FLAC__metadata_object_vorbiscomment_delete_comment(vc, (uint32_t) i);
    }
    if (!ok)
    {
        free(e.entry);
        m_strFalha = "Ao gravar '" + nome + "': sem memória";
        return false;
    }
    m_blnAlterado = true;
    return true;
}

/// Grava um atributo GUID (WMT_TYPE_GUID, 16 bytes) no nível do
/// arquivo. É o tipo de WM/MediaClassPrimaryID e SecondaryID.
///
/// Por AddAttribute, não SetAttribute: o SetAttribute grava no Extended
/// Content Description, que não aceita GUID — a sonda de 04/10/2026
/// recebeu "O valor não recai no intervalo esperado". O AddAttribute
/// põe no objeto que aceita (Metadata Library). Remove antes, para não
/// ficar com dois valores.
// LINUX: no FLAC não há tipos: o GUID vai como texto, na forma que a conversão do acervo gravou
// ("{D1607DBC-E323-4BE2-86A1-48A42A28441E}"). Fica o "remove antes".
bool EditorFlac::DefinirGuid(const std::string &strNome, const std::string &guidValor)
{
    if (m_cadeia == nullptr) return false;
    std::string g = NormalizarGuid(guidValor);
    if (g.empty())
    {
        m_strFalha = "Ao gravar '" + strNome + "': '" + guidValor + "' não é um GUID";
        return false;
    }
    RemoverAtributo(strNome);
    return DefinirTexto(strNome, "{" + Maiusculas(g) + "}");
}

void EditorFlac::RemoverAtributo(const std::string &nome)
{
    if (m_cadeia == nullptr) return;
    try
    {
        if (IgualSemCaixa(nome, "WM/Picture"))
        {
            // LINUX: a capa é o bloco PICTURE (todos, como o RemoverAtributo tirava todos os
            // WM/Picture); o lugar vira padding, que o Salvar junta.
            FLAC__Metadata_Iterator *it = FLAC__metadata_iterator_new();
            if (!it) return;
            FLAC__metadata_iterator_init(it, m_cadeia);
            bool blnMais = true;
            while (blnMais)
            {
                if (FLAC__metadata_iterator_get_block_type(it) == FLAC__METADATA_TYPE_PICTURE)
                {
                    FLAC__metadata_iterator_delete_block(it, true);
                    m_blnAlterado = true;
                }
                blnMais = FLAC__metadata_iterator_next(it);
            }
            FLAC__metadata_iterator_delete(it);
            return;
        }
        std::string strVorbis = EditorFlac::NomeVorbis(nome);
        if (strVorbis.empty()) return;
        FLAC__Metadata_Iterator *it = FLAC__metadata_iterator_new();
        if (!it) return;
        FLAC__metadata_iterator_init(it, m_cadeia);
        do
        {
            if (FLAC__metadata_iterator_get_block_type(it) != FLAC__METADATA_TYPE_VORBIS_COMMENT) continue;
            if (FLAC__metadata_object_vorbiscomment_remove_entries_matching(FLAC__metadata_iterator_get_block(it),
                                                                            strVorbis.c_str()) > 0)
                m_blnAlterado = true;
        } while (FLAC__metadata_iterator_next(it));
        FLAC__metadata_iterator_delete(it);
    }
    catch (...) { }
}

std::string EditorFlac::MimeDaImagem(const std::vector<uint8_t> &arr)
{
    if (arr.size() < 8) return "image/jpeg";
    if (arr[0] == 0x89 && arr[1] == 0x50 && arr[2] == 0x4E && arr[3] == 0x47) return "image/png";
    if (arr[0] == 0x47 && arr[1] == 0x49 && arr[2] == 0x46) return "image/gif";
    if (arr[0] == 0x42 && arr[1] == 0x4D) return "image/bmp";
    return "image/jpeg";
}

bool EditorFlac::DefinirImagem(const std::vector<uint8_t> &dadosImagem, const std::string &mimeDado,
                               const std::string &descricao)
{
    if (m_cadeia == nullptr || dadosImagem.empty()) return false;
    std::string mime = mimeDado;
    if (mime.empty()) mime = MimeDaImagem(dadosImagem);

    RemoverAtributo("WM/Picture");

    FLAC__StreamMetadata *pic = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PICTURE);
    FLAC__Metadata_Iterator *it = FLAC__metadata_iterator_new();
    try
    {
        if (!pic || !it) throw std::runtime_error("sem memória");
        pic->data.picture.type = FLAC__STREAM_METADATA_PICTURE_TYPE_FRONT_COVER;   // Front Cover (3)
        std::string d = descricao;
        if (!FLAC__metadata_object_picture_set_mime_type(pic, (char *) mime.c_str(), true)
            || !FLAC__metadata_object_picture_set_description(pic, (FLAC__byte *) d.c_str(), true)
            || !FLAC__metadata_object_picture_set_data(pic, (FLAC__byte *) dadosImagem.data(), (FLAC__uint32) dadosImagem.size(), true))
            throw std::runtime_error("sem memória");
        MedirImagem(dadosImagem, pic->data.picture.width, pic->data.picture.height, pic->data.picture.depth);
        pic->data.picture.colors = 0;
        const char *violacao = nullptr;
        if (!FLAC__metadata_object_picture_is_legal(pic, &violacao))
            throw std::runtime_error(violacao ? violacao : "bloco PICTURE inválido");

        // logo depois do STREAMINFO (e do SEEKTABLE, se houver): onde a conversão do acervo a pôs
        FLAC__metadata_iterator_init(it, m_cadeia);
        while (FLAC__metadata_iterator_next(it))
        {
            if (FLAC__metadata_iterator_get_block_type(it) != FLAC__METADATA_TYPE_SEEKTABLE)
            {
                FLAC__metadata_iterator_prev(it);
                break;
            }
        }
        if (!FLAC__metadata_iterator_insert_block_after(it, pic)) throw std::runtime_error("sem memória");
        pic = nullptr;                       // agora é da cadeia
        FLAC__metadata_iterator_delete(it);
        m_blnAlterado = true;
        return true;
    }
    catch (const std::exception &ex)
    {
        m_strFalha = std::string("Ao gravar WM/Picture: ") + ex.what();
        if (pic) FLAC__metadata_object_delete(pic);
        if (it) FLAC__metadata_iterator_delete(it);
        return false;
    }
}

/// Confirma as alterações no arquivo. Sem isto, nada é gravado.
bool EditorFlac::Salvar()
{
    if (m_cadeia == nullptr) return false;
    // LINUX: o Flush do SDK sem alteração não mudava nada; aqui nem se abre o arquivo para escrita.
    if (!m_blnAlterado) return true;
    try
    {
        FLAC__metadata_chain_sort_padding(m_cadeia);
        bool blnReescreve = FLAC__metadata_chain_check_if_tempfile_needed(m_cadeia, true);

        // LINUX: "ao gravar, o áudio não pode mudar". Antes: onde está o áudio e, se a libFLAC
        // vai reescrever o arquivo inteiro, o SHA-256 dele.
        int64_t lngTamAntes, lngTamDepois;
        int64_t lngIniAntes = InicioDoAudio(m_strArquivo, lngTamAntes);
        if (lngIniAntes < 0) { m_strFalha = "Ao salvar: não achei o começo do áudio no arquivo"; return false; }
        int64_t lngAudio = lngTamAntes - lngIniAntes;
        std::string strShaAntes = blnReescreve ? Sha256Trecho(m_strArquivo, lngIniAntes, lngAudio) : "";
        if (blnReescreve && strShaAntes.empty()) { m_strFalha = "Ao salvar: não consegui ler o áudio para conferir"; return false; }

        if (!FLAC__metadata_chain_write(m_cadeia, true, false))
        {
            m_strFalha = "Ao salvar: libFLAC: " + Status(m_cadeia);
            return false;
        }

        int64_t lngIniDepois = InicioDoAudio(m_strArquivo, lngTamDepois);
        if (lngIniDepois < 0 || lngTamDepois - lngIniDepois != lngAudio)
        {
            m_strFalha = "Ao salvar: o áudio mudou de tamanho na gravação das etiquetas";
            return false;
        }
        if (blnReescreve && Sha256Trecho(m_strArquivo, lngIniDepois, lngAudio) != strShaAntes)
        {
            m_strFalha = "Ao salvar: o áudio mudou na gravação das etiquetas (SHA-256 diferente)";
            return false;
        }
        m_blnAlterado = false;
        return true;
    }
    catch (const std::exception &ex) { m_strFalha = std::string("Ao salvar: ") + ex.what(); return false; }
}

void EditorFlac::Fechar()
{
    if (m_cadeia != nullptr) FLAC__metadata_chain_delete(m_cadeia);
    m_cadeia = nullptr;
    m_strArquivo.clear();
    m_blnAlterado = false;
}

} // namespace ARCA::Interop

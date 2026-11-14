// ARCA no xOS - Interop/ColecaoMidia: a biblioteca do WMP, que no original vinha do controle
// (PlayerWmp.Colecao -> IWMPMediaCollection2, IWMPQuery, IWMPPlaylist, IWMPMedia).
//
// LINUX: nao ha WMP. A Biblioteca do ARCA (Modelo/Biblioteca) fazia todas as consultas pelo
// banco do WMP, que o wmplayer.exe mantinha varrendo as pastas Musicas e Videos. Aqui a mesma
// "colecao" e do proprio ARCA: varre a pasta do acervo (Base::PastaAcervo, "Minhas Musicas") e a
// de videos (ARCA_VIDEOS ou /mnt/DADOS/Vídeos), le as etiquetas de cada arquivo (FLAC pela
// libFLAC/EditorFlac, o resto pelo FFmpeg) e guarda o indice em <PastaCache>/colecao.sqlite.
// Ao abrir, o indice guardado vale na hora; uma linha de execucao confere as pastas (tamanho e
// data de cada arquivo) e rele so o que mudou, como o monitor de pastas do WMP. O arquivo nunca e
// gravado aqui: a colecao so le.
//
// As operacoes tem os nomes e o sentido das do WMP que o ARCA usava (getAll, getByAttribute,
// getAttributeStringCollection, createQuery/addCondition/beginNextGroup, getPlaylistByQuery, add,
// remove, getItemInfo/setItemInfo), para a Biblioteca ser traduzida sem mudar de forma. Os nomes
// de atributo sao os do WMP/ASF ("Title", "Author", "WM/AlbumTitle", "MediaType", "SourceURL").
//
// O MediaType e decidido na inscricao, como o WMP fazia (sonda de 04/10/2026): video pela
// extensao; audio com a classe "audio que nao e musica" (WM/MediaClassPrimaryID) vai para "other";
// o resto e "audio".
//
// Tudo no fio da tela, como o objeto COM do original; so a conferencia das pastas roda a parte, e
// o resultado volta pelo Base::Despacha.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ARCA::Interop {

/// IWMPMedia: um item da colecao.
class MidiaWmp
{
public:
    std::string Caminho;           // sourceURL
    double Segundos = 0;           // duration
    int64_t Tamanho = -1;          // para saber se o arquivo mudou
    int64_t Alterado = 0;          // data de modificacao (segundos desde 1970)
    // os atributos, com os nomes do WMP (sem distinguir caixa, como o getItemInfo)
    std::map<std::string, std::string> Info;

    /// getItemInfo: "" quando o item nao tem o atributo (o WMP devolvia "" ou null).
    std::string getItemInfo(const std::string &strAtributo) const;
    /// setItemInfo: so no indice (o WMP tambem so mexia no banco dele, nunca no arquivo).
    void setItemInfo(const std::string &strAtributo, const std::string &strValor);
    /// name: o titulo, ou o nome do arquivo sem extensao.
    std::string name() const;
    std::string sourceURL() const { return Caminho; }
    double duration() const { return Segundos; }
    std::string durationString() const;
};
using MidiaWmpPtr = std::shared_ptr<MidiaWmp>;

/// IWMPPlaylist: o resultado de uma consulta (count e Item(i)).
using ListaWmp = std::vector<MidiaWmpPtr>;
using ListaWmpPtr = std::shared_ptr<ListaWmp>;

/// IWMPQuery: condicoes; cada beginNextGroup abre um grupo novo. Dentro do grupo vale o E, entre os
/// grupos o OU.
class ConsultaWmp
{
public:
    /// strOperador: "Equals" (sem distinguir caixa, como o WMP) ou "Contains" (trecho, sem caixa).
    void addCondition(const std::string &strAtributo, const std::string &strOperador, const std::string &strValor);
    void beginNextGroup();
    bool Casa(const MidiaWmp &m) const;

private:
    struct Condicao
    {
        std::string Atributo, Operador, Valor;
    };
    std::vector<std::vector<Condicao>> _grupos{1};
};
using ConsultaWmpPtr = std::shared_ptr<ConsultaWmp>;

/// IWMPMediaCollection2.
class ColecaoMidia
{
public:
    /// A colecao do programa (no original: a do controle do WMP, uma por processo).
    static ColecaoMidia &Instancia();

    /// Le o indice guardado e poe a conferencia das pastas para rodar. aoAtualizar (no fio da
    /// tela) e chamado quando a conferencia termina, com quantos itens entraram, mudaram ou sairam.
    void Iniciar(std::function<void(int intMudancas)> aoAtualizar);
    /// A conferencia das pastas ainda esta rodando?
    bool Varrendo() const { return _blnVarrendo; }
    size_t Quantidade() const { return _lstItens.size(); }
    /// As pastas que a colecao acompanha (as "bibliotecas" Musicas e Videos do Windows).
    static std::vector<std::string> Raizes();

    ListaWmpPtr getAll() const;
    ListaWmpPtr getByAttribute(const std::string &strAtributo, const std::string &strValor) const;
    /// Os valores distintos de um atributo nos itens de um tipo de midia. nullptr: tipo desconhecido.
    std::shared_ptr<std::vector<std::string>> getAttributeStringCollection(const std::string &strAtributo,
                                                                           const std::string &strTipo) const;
    ConsultaWmpPtr createQuery() const { return std::make_shared<ConsultaWmp>(); }
    /// Os itens do tipo que casam com a consulta, ordenados pelo atributo strOrdem.
    ListaWmpPtr getPlaylistByQuery(const ConsultaWmp &objConsulta, const std::string &strTipo,
                                   const std::string &strOrdem, bool blnCrescente) const;
    /// Inscreve (ou reinscreve) o arquivo: le as etiquetas dele. nullptr se nao der (arquivo que
    /// nao existe, que nao e midia, ou que nao abre).
    MidiaWmpPtr add(const std::string &strCaminho);
    /// Tira o item da colecao. blnApagarArquivo: no WMP, true apagava o arquivo; o ARCA sempre
    /// passava false, e aqui true nao e aceito (o arquivo nunca e tocado).
    void remove(const MidiaWmpPtr &m, bool blnApagarArquivo);
    /// Guarda no indice o que o setItemInfo mudou num item.
    void Guardar(const MidiaWmpPtr &m);

    /// MediaType de um arquivo pela extensao ("audio", "video"), ou "" se nao for midia.
    static std::string TipoPelaExtensao(const std::string &strCaminho);
    /// Le um arquivo como item novo. nullptr se nao der (strFalha diz por que).
    static MidiaWmpPtr LerArquivo(const std::string &strCaminho, std::string &strFalha);

private:
    ColecaoMidia() = default;
    std::vector<MidiaWmpPtr> _lstItens;          // na ordem dos caminhos
    bool _blnVarrendo = false;
    bool _blnIniciada = false;

    void Ordenar();
    MidiaWmpPtr Achar(const std::string &strCaminho) const;
    static std::string ArquivoIndice();
    std::vector<MidiaWmpPtr> LerIndice() const;
    void GravarIndice(const std::vector<MidiaWmpPtr> &lstMudados, const std::vector<std::string> &lstSairam) const;
};

} // namespace ARCA::Interop

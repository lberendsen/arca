// ARCA no xOS - Modelo/ItemMidia (de Modelo\ItemMidia.cs).
#pragma once

#include <string>

namespace ARCA::Modelo {

class Biblioteca;

/// <summary>
/// Uma linha da biblioteca. Os campos só são lidos do COM quando a linha
/// aparece na tela.
///
/// O motivo é concreto: são 5.154 itens, e ler título, artista, álbum e
/// duração de cada um na abertura daria mais de 20 mil chamadas COM antes
/// da primeira tela. Com a virtualização do WPF, só as ~30 linhas visíveis
/// pagam esse custo.
/// </summary>
// LINUX: o "COM" e a ColecaoMidia do ARCA (Interop/ColecaoMidia), que ja esta na memoria; a carga
// preguicosa continua igual, e a grade so pede as linhas visiveis. Guardado por valor na lista da
// grade (std::vector<ItemMidia>); a identidade do item (a "referencia" do C#) e o par consulta +
// posicao: MesmoItem diz se dois valores sao o mesmo item. O INotifyPropertyChanged nao existe
// aqui: a grade redesenha depois de qualquer evento e le os campos de novo.
class ItemMidia
{
public:
    /// <summary>
    /// O índice só vale dentro da consulta que criou o item (geracao). Se
    /// a lista mudou antes de a linha ser lida, o item fica "não consegui
    /// ler" — nunca com os dados da faixa que agora ocupa a mesma posição.
    /// Depois de lido, o item guarda os próprios campos, inclusive o
    /// caminho: tocar e editar usam o que foi mostrado, sem reconsultar.
    /// </summary>
    ItemMidia(Biblioteca *dono, int indice, int geracao);

    int Indice() const { return _indice; }
    int Numero() const { return _indice + 1; }

    /// <summary>
    /// Duração em segundos. A propriedade Duracao, textual, serve para
    /// mostrar na tela; esta serve para calcular o andamento da exportação.
    /// </summary>
    double Segundos() const { Carregar(); return _segundos; }

    const std::string &Titulo() const { Carregar(); return _titulo; }
    const std::string &Artista() const { Carregar(); return _artista; }
    const std::string &Album() const { Carregar(); return _album; }
    const std::string &Duracao() const { Carregar(); return _duracao; }
    const std::string &Tipo() const { Carregar(); return _tipo; }
    const std::string &Arquivo() const { Carregar(); return _arquivo; }

    // LINUX: a comparacao por referencia do C# (o mesmo objeto da mesma consulta)
    bool MesmoItem(const ItemMidia &o) const { return _dono == o._dono && _indice == o._indice && _geracao == o._geracao; }

private:
    Biblioteca *_dono;
    int _indice;
    int _geracao;      // a consulta de onde o item veio
    mutable bool _carregado = false;

    mutable std::string _titulo, _artista, _album, _duracao, _tipo, _arquivo;
    mutable double _segundos = 0;

    void Carregar() const;
};

} // namespace ARCA::Modelo

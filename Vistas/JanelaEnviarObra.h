// ARCA no xOS - Vistas/JanelaEnviarObra (de Vistas\JanelaEnviarObra.cs).
#pragma once

#include "Janela.h"
#include "Modelo/BancoObras.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ARCA::Vistas {

/// <summary>
/// Para qual obra vai o álbum (ou a faixa): uma das existentes, ou uma
/// nova, criada aqui com título, autor e capa.
///
/// A obra usada por último vem no topo — mandar os três CDs da Ética do
/// Rei Menino são três envios seguidos para a mesma obra.
///
/// O título da obra nova começa vazio: não se deduz "A Ética do Rei
/// Menino" tirando o "vol. 1" do álbum (o título é de quem escreve). O
/// autor sugere o artista do álbum, à vista e editável.
///
/// A janela cria a obra, mas não envia: devolve a obra escolhida em
/// IdObra, e quem chamou acrescenta as partes.
/// </summary>
class JanelaEnviarObra : public Window
{
public:
    /// <param name="strOQue">O que vai ser enviado, para o cabeçalho ("o álbum “…”", "a faixa “…”").</param>
    /// <param name="strAutorSugerido">Artista do álbum, para o autor da obra nova.</param>
    /// <param name="arrCapaDoAlbum">Capa do álbum, para "Usar a capa do álbum"; pode ser null.</param>
    JanelaEnviarObra(Modelo::TBancoObras &objBanco, const std::string &strOQue, const std::string &strAutorSugerido,
                     const std::vector<uint8_t> &arrCapaDoAlbum);

    /// <summary>A obra escolhida (existente ou criada aqui), ou 0 se a pessoa cancelou.</summary>
    int IdObra() const { return _intIdObra; }
    const std::string &TituloObra() const { return _strTituloObra; }

private:
    Modelo::TBancoObras &_objBanco;
    std::vector<uint8_t> _arrCapaDoAlbum;               // vazio = null
    std::shared_ptr<ListBox> _lstObras;
    std::shared_ptr<TextBox> _txtBusca, _txtTitulo, _txtAutor;
    std::shared_ptr<Border> _objMolduraCapa;
    std::shared_ptr<TextBlock> _txtAviso;
    std::vector<Modelo::TObra> _lstTodas;
    std::vector<uint8_t> _arrCapaNova;                  // vazio = null
    int _intIdObra = 0;
    std::string _strTituloObra;

    UIElementPtr MontarCabecalho(const std::string &strOQue);
    UIElementPtr MontarExistentes();
    UIElementPtr MontarNova();
    UIElementPtr MontarRodape();
    void CarregarObras();
    void Filtrar();
    std::shared_ptr<ListBoxItem> LinhaDaObra(const Modelo::TObra &o);
    void CortarCapa(const std::shared_ptr<BitmapSource> &objImagem);
    void MostrarCapaNova();
    void AoEnviarExistente();
    void AoCriarEEnviar();
    static Brush Recurso(const std::string &strChave) { return Tema::Recurso(strChave); }
};

} // namespace ARCA::Vistas

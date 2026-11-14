// ARCA no xOS - Vistas/JanelaEditorFaixas (de Vistas\JanelaEditorFaixas.cs).
#pragma once

#include "ImagensDoEncarte.h"
#include "Janela.h"

#include "../Modelo/CandidatoIdentificacao.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ARCA::Vistas {

/// <summary>
/// Catalogação à mão: para o disco que nenhum catálogo conhece.
///
/// Existe por causa de discos como o da dupla João do Morro &amp; Pé da
/// Serra — CD independente, vendido em apresentação, sem CD-TEXT, sem
/// código de barras e fora do Discogs, do MusicBrainz e do Apple Music. O
/// encarte na mão da pessoa é a única fonte, e o ARCA precisa aceitá-la.
///
/// O número e a duração de cada faixa vêm do próprio disco e não se
/// editam: são a régua contra a qual se confere o encarte. O resto é
/// digitado. A janela só devolve um <see cref="TCandidatoIdentificacao"/>;
/// gravar é o mesmo caminho da escolha entre Apple Music e Discogs.
/// </summary>
// Como a AquisicaoView a chama (o mesmo do original):
//   auto objJanela = std::make_shared<JanelaEditorFaixas>(lstDuracoes, artista, album, lstTitulos,
//                                                         lstCompositores, _capaBytes);
//   objJanela->Owner = Janela();
//   objJanela->ShowDialog();
//   if (!objJanela->Resultado) return;
//   AplicarCandidato(objJanela->Resultado);
class JanelaEditorFaixas : public Window
{
public:
    class LinhaFaixaManual
    {
    public:
        int Numero = 0;
        std::string Duracao;
        std::string Titulo;
        std::string Interprete;
        std::string Compositor;
    };

    /// <summary>O que foi catalogado, ou null se a pessoa cancelou.</summary>
    // { get; private set; } no original: so a janela o preenche
    std::shared_ptr<Modelo::TCandidatoIdentificacao> Resultado;

    /// <param name="lstDuracoesDisco">Duração de cada faixa do disco, em segundos (-1 se desconhecida).</param>
    /// <param name="lstTitulosAtuais">Títulos já conhecidos (do CUE), para não redigitar; pode ser null.</param>
    /// <param name="lstCompositoresAtuais">Compositores já conhecidos; pode ser null.</param>
    // null nas listas e na capa = vetor vazio (a janela so le ate o tamanho de cada uma)
    JanelaEditorFaixas(const std::vector<int> &lstDuracoesDisco, const std::string &strArtista, const std::string &strAlbum,
                       const std::vector<std::string> &lstTitulosAtuais, const std::vector<std::string> &lstCompositoresAtuais,
                       const std::vector<uint8_t> &arrCapaAtual);

private:
    std::vector<LinhaFaixaManual> _lstLinhas;
    std::shared_ptr<DataGrid> _objGrade;
    std::shared_ptr<TextBox> _txtArtista, _txtAlbum, _txtAno, _txtGenero;
    std::shared_ptr<Border> _objMolduraCapa;
    std::shared_ptr<TextBlock> _txtAviso;
    std::vector<uint8_t> _arrCapa;            // reduzida, para embutir nas faixas
    std::vector<uint8_t> _arrCapaPlena;       // sem redução, para Artwork\capa.jpg
    std::vector<TPaginaPendente> _lstEncarte;
    std::shared_ptr<StackPanel> _pnlEncarte;
    std::shared_ptr<TextBlock> _txtEncarteTitulo;
    bool _blnAplicado = false;
    std::string _strEstadoInicial;

    UIElementPtr MontarCabecalho(const std::vector<int> &lstDuracoesDisco);
    UIElementPtr MontarDadosAlbum();
    void AdicionarCampo(const std::shared_ptr<Grid> &objCampos, int intLinha, const std::string &strRotulo,
                        const std::shared_ptr<TextBox> &objCaixa);
    UIElementPtr MontarBarraFaixas();
    std::shared_ptr<DataGrid> MontarGrade();
    static std::shared_ptr<DataGridTextColumn> Coluna(const std::string &strCabecalho,
                                                      std::string LinhaFaixaManual::*ptrCampo,
                                                      DataGridLength objLargura, bool blnSoLeitura);
    static std::shared_ptr<DataGridTextColumn> Coluna(const std::string &strCabecalho, int LinhaFaixaManual::*ptrCampo,
                                                      DataGridLength objLargura, bool blnSoLeitura);
    UIElementPtr MontarRodape();
    void AoColarTitulos();
    void AoEscolherCapa();
    void AoDigitalizarCapa();
    void AoDigitalizarPagina();
    void AoPaginaDeArquivo();
    void CortarCapa(const std::shared_ptr<BitmapSource> &objImagem);
    void CortarPagina(const std::shared_ptr<BitmapSource> &objImagem);
    void AtualizarEncarte();
    void DividirPagina(int intIndice);
    void DefinirCapa(const std::vector<uint8_t> &arrBytes);
    void AoAplicar();
    void AoFechar(CancelEventArgs &e);
    std::string Instantaneo() const;
};

} // namespace ARCA::Vistas

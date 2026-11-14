// ARCA no xOS - Vistas/JanelaEscolhaFonte (de Vistas\JanelaEscolhaFonte.cs).
#pragma once

#include "Janela.h"
#include "Modelo/CandidatoIdentificacao.h"

#include <memory>
#include <string>
#include <vector>

namespace ARCA::Vistas {

/// Apple Music e Discogs lado a lado, para a pessoa escolher de onde vêm
/// os metadados do disco.
///
/// **Por que comparar com o disco, e não só mostrar.** O que decide se um
/// lançamento é o certo não é o título parecido — é a estrutura: a mesma
/// quantidade de faixas e as mesmas durações. Por isso cada faixa mostra a
/// diferença para a duração lida do próprio CD, e o que não confere vem no
/// tijolo de alarme. Uma diferença pequena e constante (alguns segundos) é
/// normal: o CD conta as pausas entre faixas, os catálogos não.
///
/// Nada é gravado aqui: a janela só devolve o candidato escolhido.
class JanelaEscolhaFonte : public Window
{
public:
    /// (objItunes e objDiscogs: nullptr = null, "nada encontrado")
    JanelaEscolhaFonte(std::shared_ptr<Modelo::TCandidatoIdentificacao> objItunes,
                       std::shared_ptr<Modelo::TCandidatoIdentificacao> objDiscogs,
                       const std::vector<int> &lstDuracoesDisco, const std::string &strBuscado);

    /// O candidato escolhido, ou nullptr (null) se a pessoa cancelou.
    const std::shared_ptr<Modelo::TCandidatoIdentificacao> &Escolhido() const { return _escolhido; }

private:
    /// Até quantos segundos de diferença uma faixa ainda "confere".
    static constexpr int intToleranciaSegundos = 10;

    std::vector<int> _lstDuracoesDisco;
    std::shared_ptr<Modelo::TCandidatoIdentificacao> _escolhido;

    UIElementPtr MontarCabecalho(const std::string &strBuscado);
    UIElementPtr MontarRodape();
    UIElementPtr MontarColuna(const std::string &strRotulo, const std::shared_ptr<Modelo::TCandidatoIdentificacao> &objC);
    std::shared_ptr<Border> Moldura(UIElementPtr objConteudo);
    UIElementPtr MontarIdentidade(const Modelo::TCandidatoIdentificacao &objC);
    UIElementPtr MontarFaixas(const Modelo::TCandidatoIdentificacao &objC);

    static void AdicionarCelula(Grid &objGrade, int intLinha, int intColuna, const UIElementPtr &objEl, bool blnDireita);
    std::shared_ptr<TextBlock> Linha(const std::string &strTexto, double dblTamanho, const std::string &strCor, Vistas::FontWeight objPeso);
    static Brush Recurso(const std::string &strChave);
    static std::string TamanhoOriginal(const std::vector<uint8_t> &arrBytes);
    static ImageSource AbrirCapa(const std::vector<uint8_t> &arrBytes);
};

} // namespace ARCA::Vistas

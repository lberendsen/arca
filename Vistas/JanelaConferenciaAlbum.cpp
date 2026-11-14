// ARCA no xOS - Vistas/JanelaConferenciaAlbum (de Vistas\JanelaConferenciaAlbum.cs).
#include "JanelaConferenciaAlbum.h"

#include "Base/Acervo.h"
#include "Base/Arquivo.h"
#include "Base/Despacho.h"
#include "Base/Excecoes.h"
#include "Base/Texto.h"
#include "Interop/EditorFlac.h"
#include "Modelo/Aquisicao.h"
#include "Modelo/FontesDisco.h"
#include "Modelo/Formato.h"
#include "Modelo/OffsetDrive.h"
#include "Modelo/OrdemFaixas.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <thread>

namespace ARCA::Vistas {

using namespace ARCA::Modelo;

namespace {
// string.Format("{0:+0;-0;0}") e "{0:+0;-0}"
std::string ComSinal(int n, bool blnZeroSemSinal = true)
{
    if (n > 0) return "+" + std::to_string(n);
    if (n < 0) return std::to_string(n);
    return blnZeroSemSinal ? "0" : "-0";
}
} // namespace

JanelaConferenciaAlbum::JanelaConferenciaAlbum(const std::string &strPasta, const std::string &strArtista,
                                               const std::string &strAlbum, const std::string &strRaizAcervo)
    : _strPasta(strPasta), _strArtista(strArtista), _strAlbum(strAlbum), _strRaizAcervo(strRaizAcervo)
{
    _objConf = std::make_shared<TConferencia>();

    Title = "Conferir álbum — bit-perfect";
    Width = 760;
    Height = 600;
    MinWidth = 560;
    MinHeight = 400;
    WindowStartupLocation = Vistas::WindowStartupLocation::CenterOwner;
    ShowInTaskbar = false;
    Background = Tema::Recurso("Canvas");

    _txtEstado = std::make_shared<TextBlock>();
    _txtEstado->Style = Tema::Estilo("Fraco");
    _txtEstado->TextWrapping = Vistas::TextWrapping::Wrap;
    _txtEstado->Margin = Thickness(0, 0, 0, 8);
    _txtVeredito = std::make_shared<TextBlock>();
    _txtVeredito->FontSize = 14;
    _txtVeredito->Foreground = Tema::Recurso("Texto");
    _txtVeredito->TextWrapping = Vistas::TextWrapping::Wrap;
    _txtVeredito->Margin = Thickness(0, 0, 0, 12);
    _pnlFaixas = std::make_shared<StackPanel>();
    _btnGravar = std::make_shared<Button>();
    _btnGravar->Content = "Gravar o resultado nas faixas";
    _btnGravar->Padding = Thickness(14, 6, 14, 6);
    _btnGravar->IsEnabled = false;
    _btnGravar->ToolTip = "Grava o veredito de cada faixa no atributo ARCA/Verification. O áudio não é tocado.";
    _btnGravar->Click += [this](RoutedEventArgs &) { AoGravar(); };

    auto objRaiz = std::make_shared<DockPanel>();
    auto objCab = std::make_shared<Border>();
    objCab->Background = Tema::Recurso("CabecalhoColuna");
    objCab->BorderBrush = Tema::Recurso("Borda");
    objCab->BorderThickness = Thickness(0, 0, 0, 1);
    objCab->Padding = Thickness(14, 10, 14, 10);
    auto objTitulo = std::make_shared<TextBlock>(strArtista + " — " + strAlbum);
    objTitulo->FontSize = 15;
    objTitulo->Foreground = Tema::Recurso("Texto");
    objCab->SetChild(objTitulo);
    DockPanel::SetDock(objCab, Dock::Top);
    objRaiz->Children.Add(objCab);

    auto objRodape = std::make_shared<Grid>();
    objRodape->ColumnDefinitions.push_back(ColumnDefinition());
    objRodape->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));
    objRodape->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Auto()));
    auto objFechar = std::make_shared<Button>();
    objFechar->Content = "Fechar";
    objFechar->IsCancel = true;
    objFechar->Padding = Thickness(18, 6, 18, 6);
    objFechar->Margin = Thickness(8, 0, 0, 0);
    objFechar->Click += [this](RoutedEventArgs &) { Close(); };
    Grid::SetColumn(_btnGravar, 1);
    Grid::SetColumn(objFechar, 2);
    objRodape->Children.Add(_btnGravar);
    objRodape->Children.Add(objFechar);
    auto objBordaRodape = std::make_shared<Border>();
    objBordaRodape->Background = Tema::Recurso("BarraFerramentas");
    objBordaRodape->BorderBrush = Tema::Recurso("Borda");
    objBordaRodape->BorderThickness = Thickness(0, 1, 0, 0);
    objBordaRodape->Padding = Thickness(14, 8, 14, 8);
    objBordaRodape->SetChild(objRodape);
    DockPanel::SetDock(objBordaRodape, Dock::Bottom);
    objRaiz->Children.Add(objBordaRodape);

    auto objCorpo = std::make_shared<StackPanel>();
    objCorpo->Margin = Thickness(14);
    objCorpo->Children.Add(_txtVeredito);
    objCorpo->Children.Add(_txtEstado);
    objCorpo->Children.Add(_pnlFaixas);
    auto objRolagem = std::make_shared<ScrollViewer>();
    objRolagem->VerticalScrollBarVisibility = ScrollBarVisibility::Auto;
    objRolagem->Content = objCorpo;
    objRaiz->Children.Add(objRolagem);
    Content = objRaiz;

    Closing += [this](CancelEventArgs &) { _objConf->blnFechando = true; };
    Loaded += [this](RoutedEventArgs &) { Iniciar(); };
}

JanelaConferenciaAlbum::~JanelaConferenciaAlbum()
{
    _objConf->blnFechando = true;
    _objConf->blnJanelaViva = false;
}

// ===============================================================
void JanelaConferenciaAlbum::Iniciar()
{
    _txtVeredito->Text = "Conferindo…";
    // Thread { IsBackground = true, Priority = BelowNormal }
    std::thread(Conferir, this, _objConf, _strPasta, _strArtista, _strAlbum, _strRaizAcervo).detach();
}

void JanelaConferenciaAlbum::Conferir(JanelaConferenciaAlbum *objJanela, std::shared_ptr<TConferencia> c, std::string strPasta,
                                      std::string strArtista, std::string strAlbum, std::string strRaizAcervo)
{
    auto Estado = [objJanela, c](const std::string &s) {
        Base::Despacha([objJanela, c, s] {
            if (c->blnJanelaViva) objJanela->_txtEstado->Text = s;
        });
    };
    auto cancelou = [c] { return c->blnFechando.load(); };
    std::vector<TDisco> &lstDiscos = c->lstDiscos;
    try
    {
        // LINUX: "*.wma" no original; o acervo no xOS e FLAC
        std::vector<std::string> lstArquivos = Base::Arquivos(strPasta, ".flac");

        // Agrupa pelas faixas que guardam o mesmo índice: um grupo por CD.
        // Na mesma leitura, o número de cada faixa: a ordem do disco vem
        // dele, não do nome do arquivo, que a pessoa pode mudar.
        Estado("Lendo o índice do CD gravado em cada faixa…");
        std::map<std::string, size_t> dic;
        std::map<std::string, int> dicNumeros;     // OrdinalIgnoreCase: os caminhos vem da mesma listagem
        for (const std::string &f : lstArquivos)
        {
            std::string strMcdi;
            {
                Interop::EditorFlac e;
                if (e.Abrir(f))
                {
                    strMcdi = e.LerMcdi();
                    dicNumeros[f] = TOrdemFaixas::NumeroDaFaixa(e.LerTudo());
                }
            }
            if (strMcdi.empty()) { c->intSemIndice++; continue; }
            auto it = dic.find(strMcdi);
            if (it == dic.end())
            {
                TDisco d;
                d.strMcdi = strMcdi;
                lstDiscos.push_back(d);
                it = dic.emplace(strMcdi, lstDiscos.size() - 1).first;
            }
            lstDiscos[it->second].lstArquivos.push_back(f);
        }

        for (size_t i = 0; i < lstDiscos.size(); i++)
        {
            TDisco &d = lstDiscos[i];
            std::string strQual = lstDiscos.size() > 1
                ? "disco " + std::to_string(i + 1) + " de " + std::to_string(lstDiscos.size()) + ": " : "";
            // Sem número em todas, ou com repetição, a ordem pelo número
            // não existe: fica a do nome, e a pessoa é avisada. Ordem
            // errada não dá falso "confere" — o tamanho de cada faixa é
            // conferido contra o índice do CD e a assinatura do
            // AccurateRip é por posição —, só "não confere".
            bool blnNumeracaoOk = TOrdemFaixas::NumeracaoCompleta(d.lstArquivos, dicNumeros);
            std::vector<std::string> lstNaOrdem = blnNumeracaoOk ? TOrdemFaixas::Ordenar(d.lstArquivos, &dicNumeros) : d.lstArquivos;
            if (!blnNumeracaoOk)
                std::sort(lstNaOrdem.begin(), lstNaOrdem.end(),
                          [](const std::string &a, const std::string &b) { return Base::Minusculas(a) < Base::Minusculas(b); });
            d.lstArquivos = lstNaOrdem;
            std::string strAvisoOrdem = blnNumeracaoOk ? ""
                : "o número da faixa (WM/TrackNumber) falta ou se repete; a ordem usada foi a dos nomes dos arquivos. "
                  "Se algo não conferir, veja o campo Faixa no painel de metadados";
            std::shared_ptr<TTocAudio> objToc = TTocAudio::DeMcdi(d.strMcdi, (int) d.lstArquivos.size());
            if (!objToc) { d.strMensagem = "o índice do CD gravado nas faixas não é legível"; continue; }

            Estado(strQual + "decodificando as faixas para conferir o tamanho de cada uma contra o índice do CD…");
            {
                TFonteWma objFonte(d.lstArquivos, objToc->lstInicios[0]);
                std::string strInvalido = objFonte.Validar(*objToc, cancelou);
                if (!strInvalido.empty())
                {
                    d.strMensagem = strInvalido + (!strAvisoOrdem.empty() ? " — " + strAvisoOrdem : "");
                    continue;
                }
                d.strObservacao = strAvisoOrdem.empty() ? objFonte.Observacao()
                    : objFonte.Observacao().empty() ? strAvisoOrdem
                    : objFonte.Observacao() + "; " + strAvisoOrdem;
                Estado(strQual + "consultando o AccurateRip e calculando as assinaturas…");
                d.objResultado = AccurateRip::Conferir(objFonte, *objToc, OffsetDrive::OffsetsDoAcervo(), cancelou);
            }
            const TResultadoAccurateRip &r = *d.objResultado;
            if (r.QtdConferem() > 0) OffsetDrive::RegistrarOffsetDoAcervo(r.intOffset);

            // A procedência do acervo: a conferência entra no ledger
            // (aqui, fora da interface — ele calcula o hash de cada faixa).
            if (r.strFalha.empty())
            {
                Estado(strQual + "registrando a conferência no ledger…");
                try
                {
                    std::string strDisco = lstDiscos.size() > 1 ? " (" + Base::Trim(strQual, ": ") + ")" : "";
                    Integracao::RegistrarEncarteNoLedger(strRaizAcervo, strArtista, strAlbum, strPasta, d.lstArquivos,
                        "conferência AccurateRip" + strDisco + ": " + std::to_string(r.QtdConferem()) + " de " +
                            std::to_string(r.lstFaixas.size()) + " conferem, deslocamento " + ComSinal(r.intOffset) +
                            (r.blnNaBase ? "" : " (disco fora da base)"));
                }
                catch (...) {}
            }
        }
        Base::Despacha([objJanela, c] {
            if (c->blnJanelaViva) objJanela->Mostrar();
        });
    }
    catch (const Base::OperationCanceledException &) {}
    catch (const std::exception &ex)
    {
        std::string strErro = ex.what();
        Base::Despacha([objJanela, c, strErro] {
            if (c->blnJanelaViva && !c->blnFechando)
            {
                objJanela->_txtEstado->Text = "";
                objJanela->_txtVeredito->Text = "A conferência falhou: " + strErro;
            }
        });
    }
}

// ===============================================================
void JanelaConferenciaAlbum::Mostrar()
{
    if (_objConf->blnFechando) return;
    std::vector<TDisco> &lstDiscos = _objConf->lstDiscos;
    _txtEstado->Text = _objConf->intSemIndice > 0
        ? std::to_string(_objConf->intSemIndice) + " faixa(s) não guardam o índice do CD e ficaram de fora." : "";
    _pnlFaixas->Children.Clear();

    if (lstDiscos.empty())
    {
        _txtVeredito->Text = "Este álbum não guarda o índice do CD de origem (WM/MCDI). Sem ele não há como conferir sem o disco — "
                             "extraia o CD pela AQUISIÇÃO, que confere na hora.";
        return;
    }

    int intFaixas = 0, intConferem = 0, intForaBase = 0;
    bool blnAlgumOffset = false;
    for (size_t i = 0; i < lstDiscos.size(); i++)
    {
        TDisco &d = lstDiscos[i];
        if (lstDiscos.size() > 1)
        {
            auto t = std::make_shared<TextBlock>("DISCO " + std::to_string(i + 1) + " DE " + std::to_string(lstDiscos.size()));
            t->Style = Tema::Estilo("Rotulo");
            t->Margin = Thickness(0, i == 0 ? 0 : 12, 0, 6);
            _pnlFaixas->Children.Add(t);
        }
        std::string strProblema = !d.strMensagem.empty() ? d.strMensagem
            : (d.objResultado && !d.objResultado->strFalha.empty() ? "não consegui consultar o AccurateRip: " + d.objResultado->strFalha : "");
        if (!strProblema.empty())
        {
            auto t = std::make_shared<TextBlock>("Não dá para conferir: " + strProblema + ".");
            t->TextWrapping = Vistas::TextWrapping::Wrap;
            t->Foreground = Tema::Recurso("Alarme");
            _pnlFaixas->Children.Add(t);
            continue;
        }
        if (!d.objResultado) continue;   // nao acontece: todo disco sem resultado tem mensagem
        if (!d.strObservacao.empty())
        {
            auto t = std::make_shared<TextBlock>(d.strObservacao + ".");
            t->Style = Tema::Estilo("Fraco");
            t->TextWrapping = Vistas::TextWrapping::Wrap;
            t->Margin = Thickness(0, 0, 0, 4);
            _pnlFaixas->Children.Add(t);
        }

        const TResultadoAccurateRip &r = *d.objResultado;
        intFaixas += (int) r.lstFaixas.size();
        if (!r.blnNaBase) intForaBase += (int) r.lstFaixas.size();
        intConferem += r.QtdConferem();
        if (r.QtdConferem() > 0 && r.intOffset != 0) blnAlgumOffset = true;
        for (size_t t = 0; t < r.lstFaixas.size(); t++)
            _pnlFaixas->Children.Add(Linha(Base::NomeSemExtensao(d.lstArquivos[t]), r, r.lstFaixas[t]));
        if (r.QtdConferem() > 0 && r.intOffset != 0)
        {
            auto t = std::make_shared<TextBlock>("Deslocamento " + ComSinal(r.intOffset, false) + " amostras (" +
                                                 Modelo::Formato::Num(std::abs(r.intOffset) * 1000.0 / 44100, 2) +
                                                 " ms): o offset do drive em que foi ripado, sem correção, ou outra prensagem. A música é a mesma.");
            t->Style = Tema::Estilo("Fraco");
            t->TextWrapping = Vistas::TextWrapping::Wrap;
            t->Margin = Thickness(0, 4, 0, 0);
            _pnlFaixas->Children.Add(t);
        }
    }

    if (intFaixas == 0)
        _txtVeredito->Text = "Não deu para conferir este álbum (veja abaixo).";
    else if (intForaBase == intFaixas)
        _txtVeredito->Text = "Este disco não está no AccurateRip: não há como comprovar sem o disco. "
                             "Se tiver o CD, extraia pela AQUISIÇÃO — sem a base, ela lê duas vezes e compara.";
    else if (intConferem == intFaixas)
        _txtVeredito->Text = "BIT-PERFECT COMPROVADO: as " + std::to_string(intFaixas) + " faixas conferem com o AccurateRip." +
                             (blnAlgumOffset ? " (Com deslocamento de posição — veja abaixo.)" : "");
    else
        _txtVeredito->Text = std::to_string(intConferem) + " de " + std::to_string(intFaixas) +
                             " faixa(s) conferem. As que não conferem provavelmente têm erro de leitura da época em que foram ripadas; "
                             "se tiver o CD, extraia de novo pela AQUISIÇÃO.";
    _btnGravar->IsEnabled = std::any_of(lstDiscos.begin(), lstDiscos.end(), [](const TDisco &d) {
        return d.objResultado && d.objResultado->strFalha.empty() && d.objResultado->blnNaBase;
    });
}

UIElementPtr JanelaConferenciaAlbum::Linha(const std::string &strNome, const TResultadoAccurateRip &r, const TFaixaAccurateRip &f)
{
    std::string strSit = !r.blnNaBase ? "fora da base"
        : f.Confere() ? "confere — " + f.strVersao + ", confiança " + std::to_string(f.intConfianca) + " de " + std::to_string(f.intTotalNaBase)
        : "NÃO confere (" + std::to_string(f.intTotalNaBase) + " registro(s) na base)";
    bool blnAlarme = r.blnNaBase && !f.Confere();
    auto objLinha = std::make_shared<Grid>();
    objLinha->Margin = Thickness(0, 0, 0, 3);
    objLinha->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Star(1)));
    objLinha->ColumnDefinitions.push_back(ColumnDefinition(GridLength::Star(1)));
    auto objNome = std::make_shared<TextBlock>(strNome);
    objNome->TextTrimming = Vistas::TextTrimming::CharacterEllipsis;
    objNome->Foreground = Tema::Recurso("Texto");
    objLinha->Children.Add(objNome);
    auto objSit = std::make_shared<TextBlock>(strSit);
    objSit->Foreground = blnAlarme ? Tema::Recurso("Alarme") : Tema::Recurso("Texto");
    objSit->FontWeight = blnAlarme ? FontWeights::SemiBold : FontWeights::Normal;
    Grid::SetColumn(objSit, 1);
    objLinha->Children.Add(objSit);
    return objLinha;
}

void JanelaConferenciaAlbum::AoGravar()
{
    int intGravadas = 0;
    std::vector<std::string> lstFalhas;
    std::string strHoje = Base::Agora("%d/%m/%Y");
    // LINUX: o acervo so de leitura abre durante a gravacao
    std::unique_ptr<Base::GravacaoNoAcervo> objGravacao;
    try { objGravacao = std::make_unique<Base::GravacaoNoAcervo>(); }
    catch (const std::exception &ex)
    {
        _txtEstado->Text = std::string("Não consegui gravar: ") + ex.what();
        return;
    }
    for (TDisco &d : _objConf->lstDiscos)
    {
        if (!d.objResultado) continue;
        const TResultadoAccurateRip &r = *d.objResultado;
        if (!r.strFalha.empty() || !r.blnNaBase) continue;
        for (size_t i = 0; i < r.lstFaixas.size() && i < d.lstArquivos.size(); i++)
        {
            const TFaixaAccurateRip &f = r.lstFaixas[i];
            std::string strTexto = f.Confere()
                ? "BIT-PERFECT COMPROVADO — AccurateRip " + f.strVersao + " (confiança " + std::to_string(f.intConfianca) +
                      "), conferido em " + strHoje +
                      (r.intOffset != 0 ? "; áudio deslocado " + ComSinal(r.intOffset, false) + " amostras" : "")
                : "NÃO comprovado — não confere com o AccurateRip (" + std::to_string(f.intTotalNaBase) + " registro(s)), conferido em " + strHoje;
            try
            {
                Interop::EditorFlac objEd;
                if (objEd.Abrir(d.lstArquivos[i]) && objEd.DefinirTexto("ARCA/Verification", strTexto) && objEd.Salvar()) intGravadas++;
                else lstFalhas.push_back(Base::NomeDoArquivo(d.lstArquivos[i]));
            }
            catch (...) { lstFalhas.push_back(Base::NomeDoArquivo(d.lstArquivos[i])); }
        }
    }
    objGravacao.reset();
    _btnGravar->IsEnabled = false;
    _txtEstado->Text = lstFalhas.empty()
        ? "Veredito gravado em " + std::to_string(intGravadas) + " faixa(s), no atributo ARCA/Verification."
        : "Gravado em " + std::to_string(intGravadas) + " faixa(s). Não consegui gravar em: " + Base::Junta(lstFalhas, ", ") +
              " (a faixa pode estar tocando).";
}

} // namespace ARCA::Vistas

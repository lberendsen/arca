// ARCA no xOS - Vistas/AquisicaoView (de Vistas\AquisicaoView.xaml e AquisicaoView.xaml.cs).
//
// O InitializeComponent monta a arvore do AquisicaoView.xaml, na mesma ordem, com as mesmas
// medidas, estilos e pinceis; o resto e o AquisicaoView.xaml.cs, metodo por metodo.
//
// LINUX: nos textos para o usuario, "WMA" vira "FLAC" (o formato do acervo no xOS), "SPTI" vira
// "SG_IO" (o caminho ate o drive no Linux, como o Interop/SptiImageExtractor portado ja diz) e
// "tags ASF" vira "tags FLAC". Os nomes de atributo continuam os do ASF: o EditorFlac os traduz.
#include "AquisicaoView.h"

#include "JanelaConteudoFaixas.h"
#include "JanelaEscolhaFonte.h"
#if __has_include("JanelaEditorFaixas.h")
#include "JanelaEditorFaixas.h"
#define ARCA_TEM_EDITOR_FAIXAS 1
#endif

#include "Base/Arquivo.h"
#include "Base/Despacho.h"
#include "Base/Excecoes.h"
#include "Base/Texto.h"
#include "Interop/CdromTocReader.h"
#include "Interop/EditorFlac.h"
#include "Interop/SptiImageExtractor.h"
#include "Modelo/AccurateRip.h"
#include "Modelo/AcquisitionLogger.h"
#include "Modelo/CdExtraExtractor.h"
#include "Modelo/ConferenciaBitPerfect.h"
#include "Modelo/DiscogsClient.h"
#include "Modelo/ExternalMetadataClient.h"
#include "Modelo/Formato.h"
#include "Modelo/GnuDbResolver.h"
#include "Modelo/ItunesResolver.h"
#include "Modelo/ProcessadorExtracaoAudio.h"
#include "Modelo/RelatorioExtracao.h"

#include <fcntl.h>
#include <linux/cdrom.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <thread>

namespace ARCA::Vistas {

using namespace ARCA::Modelo;
using ARCA::Interop::CdromTocReader;
using ARCA::Interop::EditorFlac;
using ARCA::Interop::SptiImageExtractor;

namespace {

namespace fs = std::filesystem;

// ===================================================================================
//  DECISAO-LORENZO (08/11/2026): o CD-TEXT manda nos titulos.
//
//  O original nao olhava o CD-TEXT do disco ao identificar: os titulos vinham do
//  Discogs/Apple Music/GnuDB, e em 07/11 um lancamento de mesmo numero de faixas e
//  outro repertorio trocou os titulos do Glenn Miller ("Moonlight Serenade" virou
//  "In The Mood"). Agora:
//    - logo depois da extracao, o CUE como o drive o escreveu (com o CD-TEXT) e
//      guardado em AudioCD.cdtext.cue, que nada mais reescreve;
//    - o CD-TEXT preenche artista e album quando as caixas estao vazias, antes do GnuDB;
//    - em toda lista de titulos vinda de fonte externa, a faixa que tem titulo no
//      CD-TEXT fica com ele, salvo quando o da fonte e o mesmo texto (similaridade
//      >= 0,60: ai vale a grafia da fonte, que costuma ter maiusculas melhores);
//    - o Discogs recebe as duracoes do disco e recusa lancamento de outro repertorio
//      (DiscogsClient::ConsultarLancamento).
//  Link colado pela pessoa e escolha na janela lado a lado nao passam por aqui: ali
//  a pessoa ja decidiu.
// ===================================================================================
const char *const STR_CUE_CDTEXT = "AudioCD.cdtext.cue";

struct TCdText
{
    std::string strAlbum, strArtista;
    std::vector<std::string> lstTitulos, lstArtistas;     // "" = a faixa nao tem
    bool TemTitulos() const
    {
        return std::any_of(lstTitulos.begin(), lstTitulos.end(), [](const std::string &t) { return !t.empty(); });
    }
};

// "Faixa 3" e afins nao sao titulo: e o que o ARCA escreve quando nao ha nenhum
bool TituloGenerico(const std::string &t)
{
    return Base::VazioOuBranco(t) || Base::Contem(t, "Faixa", true);
}

TCdText LerCdText(const std::string &strPastaTrabalho)
{
    TCdText cd;
    std::string strCue = Base::Combina(strPastaTrabalho, STR_CUE_CDTEXT);
    if (!Base::ArquivoExiste(strCue)) return cd;
    try
    {
        std::vector<TrackInfo> lst = ProcessadorExtracaoAudio::ParsearCue(strCue, cd.strAlbum, cd.strArtista);
        for (const TrackInfo &t : lst)
        {
            if (t.TrackType != "AUDIO") continue;
            cd.lstTitulos.push_back(TituloGenerico(t.Title) ? std::string() : Base::Trim(t.Title));
            cd.lstArtistas.push_back(Base::Trim(t.Artist));
        }
    }
    catch (...) {}
    return cd;
}

// A duracao (s) de cada faixa de audio pelo CUE: do INDEX 01 ao seguinte; a ultima ate o
// REM LEAD-OUT (ou o fim do .bin). Vazia se o CUE nao der.
std::vector<int> DuracoesDoCue(const std::string &strCue, int64_t lngTamBin)
{
    std::vector<int> lst;
    try
    {
        std::string a, b;
        int64_t lngLeadOutBytes = 0;
        std::vector<TrackInfo> lstF = ProcessadorExtracaoAudio::ParsearCue(strCue, a, b, lngLeadOutBytes);
        std::vector<TrackInfo> lstAudio;
        for (const TrackInfo &t : lstF) if (t.TrackType == "AUDIO") lstAudio.push_back(t);
        int64_t lngFimLba = lngLeadOutBytes > 0 ? lngLeadOutBytes / 2352 : lngTamBin / 2352;
        for (size_t i = 0; i < lstAudio.size(); i++)
        {
            int64_t lngFim = i + 1 < lstAudio.size() ? lstAudio[i + 1].StartLba : lngFimLba;
            int64_t lngQuadros = lngFim - lstAudio[i].StartLba;
            if (lngQuadros <= 0) return {};
            lst.push_back((int) ((lngQuadros + 37) / 75));
        }
    }
    catch (...) { return {}; }
    return lst;
}

// Aplica o CD-TEXT sobre os titulos (e artistas por faixa) de uma fonte externa.
// Devolve quantos titulos ficaram com o do disco.
int AplicarCdText(const TCdText &cd, std::vector<std::string> &lstTitulos, std::vector<std::string> &lstArtistas)
{
    if (!cd.TemTitulos()) return 0;
    if (lstTitulos.size() < cd.lstTitulos.size()) lstTitulos.resize(cd.lstTitulos.size());
    int intTrocados = 0;
    for (size_t i = 0; i < cd.lstTitulos.size(); i++)
    {
        const std::string &strDisco = cd.lstTitulos[i];
        if (strDisco.empty()) continue;
        if (!TituloGenerico(lstTitulos[i]) && DiscogsClient::CalcularSimilaridadeTexto(strDisco, lstTitulos[i]) >= 0.60) continue;
        lstTitulos[i] = strDisco;
        intTrocados++;
        // o artista da faixa vinha da mesma fonte errada: fica o do disco (ou nenhum)
        if (i < lstArtistas.size()) lstArtistas[i] = i < cd.lstArtistas.size() ? cd.lstArtistas[i] : std::string();
    }
    return intTrocados;
}

// string.Format("{0:F1}") na cultura pt-BR da Windows 7: virgula decimal, sem separador de milhar
std::string F(double v, int casas)
{
    char sz[64];
    snprintf(sz, sizeof sz, "%.*f", casas, v);
    std::string s = sz;
    for (char &c : s) if (c == '.') c = ',';
    return s;
}

// {0:D2}
std::string D2(int v)
{
    char sz[16];
    snprintf(sz, sizeof sz, "%02d", v);
    return sz;
}

// StringComparer.OrdinalIgnoreCase, para ordenar
bool MenorSemCaixa(const std::string &a, const std::string &b)
{
    return Base::Maiusculas(a) < Base::Maiusculas(b);
}

// string.IsNullOrEmpty(x.Trim())
bool VazioAposTrim(const std::string &s) { return Base::Trim(s).empty(); }

// o primeiro nao vazio (o ?? do C# com "" = null)
std::string PrimeiroNaoVazio(std::initializer_list<std::string> l)
{
    for (const std::string &s : l) if (!s.empty()) return s;
    return "";
}

// Directory.GetFiles(pasta, padrao) com padrao "prefixo*sufixo" (sem caixa, como no Windows)
std::vector<std::string> ArquivosComo(const std::string &pasta, const std::string &prefixo, const std::string &sufixo)
{
    std::vector<std::string> r;
    for (const std::string &a : Base::Arquivos(pasta))
    {
        std::string n = Base::NomeDoArquivo(a);
        if (Base::ComecaCom(n, prefixo, true) && Base::TerminaCom(n, sufixo, true)) r.push_back(a);
    }
    return r;
}

// Directory.GetFiles(pasta, "*", SearchOption.AllDirectories); lanca como o original
std::vector<std::string> TodosOsArquivos(const std::string &pasta)
{
    std::vector<std::string> r;
    for (auto &d : fs::recursive_directory_iterator(fs::u8path(pasta)))
        if (d.is_regular_file()) r.push_back(d.path().u8string());
    std::sort(r.begin(), r.end());
    return r;
}

// a parte de um item da ListaUnidades que e o dispositivo ("/dev/sr0 (ASUS DRW-24F1MT)" -> "/dev/sr0")
std::string DispositivoDoItem(const std::string &strItem)
{
    size_t p = strItem.find(' ');
    return p == std::string::npos ? strItem : strItem.substr(0, p);
}

// LINUX: o DriveInfo.IsReady do original. Aqui o proprio drive diz se ha disco lido e pronto
// (CDROM_DRIVE_STATUS = CDS_DISC_OK). So pergunta: nao le o disco, nao mexe na bandeja.
bool DiscoPronto(const std::string &strDispositivo)
{
    int fd = open(strDispositivo.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;
    int r = ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);
    close(fd);
    return r == CDS_DISC_OK;
}

// roda um programa sem shell (como o Base/Acervo), com a saida descartada; devolve o codigo
int Roda(const std::vector<std::string> &args)
{
    pid_t p = fork();
    if (p == 0)
    {
        int nulo = open("/dev/null", O_WRONLY);
        if (nulo >= 0)
        {
            dup2(nulo, 1);
            dup2(nulo, 2);
        }
        std::vector<char *> v;
        for (const std::string &a : args) v.push_back(const_cast<char *>(a.c_str()));
        v.push_back(nullptr);
        execvp(v[0], v.data());
        _exit(127);
    }
    if (p < 0) return -1;
    int st = 0;
    waitpid(p, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

// LINUX: no Windows a sessao de dados de um CD Extra ja estava montada como a propria unidade
// ("E:\"), e o original a lia por ali (Directory.Exists(@"E:\") e TCdExtraExtractor). Aqui ela e
// montada so leitura (mount -t iso9660 -o ro), numa pasta temporaria, enquanto este objeto
// existir; ao sair, desmonta e apaga a pasta. CD so de audio nao monta: Raiz() fica "", que e o
// "a pasta nao existe" do original.
class TSessaoDadosMontada
{
public:
    explicit TSessaoDadosMontada(const std::string &strDispositivo)
    {
        if (strDispositivo.empty() || !Base::ComecaCom(strDispositivo, "/dev/")) return;
        char szModelo[] = "/tmp/arca-cdextra-XXXXXX";
        if (!mkdtemp(szModelo)) return;
        m_strPasta = szModelo;
        if (Roda({"mount", "-t", "iso9660", "-o", "ro", strDispositivo, m_strPasta}) == 0) m_blnMontada = true;
        else
        {
            rmdir(m_strPasta.c_str());
            m_strPasta.clear();
        }
    }
    ~TSessaoDadosMontada()
    {
        if (m_blnMontada) Roda({"umount", m_strPasta});
        if (!m_strPasta.empty()) rmdir(m_strPasta.c_str());
    }
    TSessaoDadosMontada(const TSessaoDadosMontada &) = delete;
    TSessaoDadosMontada &operator=(const TSessaoDadosMontada &) = delete;
    const std::string &Raiz() const { static const std::string vazio; return m_blnMontada ? m_strPasta : vazio; }

private:
    std::string m_strPasta;
    bool m_blnMontada = false;
};

std::shared_ptr<TextBlock> Txt(const std::string &s, const char *estilo = nullptr)
{
    auto t = std::make_shared<TextBlock>(s);
    if (estilo) t->Style = Tema::Estilo(estilo);
    return t;
}

} // namespace

AquisicaoView::AquisicaoView()
{
    InitializeComponent();

    m_objIndicador = std::make_unique<clsPainelProgressoAquisicao>(
        PnlProgressoPercentual,
        PnlFaseRapida,
        TxtFaseLongaTitulo,
        TxtPercentualNumero,
        PbProgressoFase,
        TxtFaseVerbo,
        TxtFaseDetalhe);

    // LINUX: Minhas Músicas\_ARCA_Trabalho e Minhas Músicas no original; aqui os caminhos do xOS
    // (docs/arca/PORTE.md, secao 4), que ARCA_TRABALHO e ARCA_ACERVO trocam nos testes.
    PastaTrabalho = Base::PastaTrabalho();
    RaizAcervo = Base::PastaAcervo();

    MontarPassos();
    Loaded += [this](RoutedEventArgs &) { Iniciar(); };

    CaixaArtista->TextChanged += [this](TextChangedEventArgs &) { AvaliarPorta(); };
    CaixaAlbum->TextChanged += [this](TextChangedEventArgs &) { AvaliarPorta(); };
}

// ===============================================================
// O AquisicaoView.xaml
// ===============================================================
void AquisicaoView::InitializeComponent()
{
    Background = Tema::Recurso("Canvas");

    auto objDock = std::make_shared<DockPanel>();
    objDock->Margin = Thickness(14);

    // ===== Os três passos ========================================
    auto bPassos = std::make_shared<Border>();
    DockPanel::SetDock(bPassos, Dock::Top);
    bPassos->Background = Tema::Recurso("CabecalhoColuna");
    bPassos->BorderBrush = Tema::Recurso("Borda");
    bPassos->BorderThickness = Thickness(1);
    bPassos->Padding = Thickness(14, 10);
    bPassos->Margin = Thickness(0, 0, 0, 12);
    PainelPassos = std::make_shared<StackPanel>();
    PainelPassos->Name = "PainelPassos";
    PainelPassos->Orientation = Vistas::Orientation::Horizontal;
    bPassos->SetChild(PainelPassos);
    objDock->Children.Add(bPassos);

    // ===== A decisão, embaixo e sempre visível ===================
    auto bDecisao = std::make_shared<Border>();
    DockPanel::SetDock(bDecisao, Dock::Bottom);
    bDecisao->Background = Tema::Recurso("BarraFerramentas");
    bDecisao->BorderBrush = Tema::Recurso("RodapeBorda");
    bDecisao->BorderThickness = Thickness(1);
    bDecisao->Padding = Thickness(14, 10);
    bDecisao->Margin = Thickness(0, 12, 0, 0);
    auto gDecisao = std::make_shared<Grid>();
    gDecisao->ColumnDefinitions = {ColumnDefinition(GridLength::Star(1)), ColumnDefinition(GridLength::Auto())};
    auto sPorta = std::make_shared<StackPanel>();
    sPorta->VerticalAlignment = Vistas::VerticalAlignment::Center;
    TxtPorta = Txt("", "Fraco");
    TxtPorta->Name = "TxtPorta";
    TxtPorta->FontSize = 12;
    sPorta->Children.Add(TxtPorta);
    TxtTrabalho = Txt("", "Fraco");
    TxtTrabalho->Name = "TxtTrabalho";
    TxtTrabalho->FontSize = 10;
    TxtTrabalho->Margin = Thickness(0, 3, 0, 0);
    sPorta->Children.Add(TxtTrabalho);
    gDecisao->Children.Add(sPorta);
    BtnAprovar = std::make_shared<Button>();
    BtnAprovar->Name = "BtnAprovar";
    Grid::SetColumn(BtnAprovar, 1);
    BtnAprovar->Content = "Aprovar e integrar ao acervo";
    BtnAprovar->Padding = Thickness(22, 9);
    BtnAprovar->FontWeight = FontWeights::SemiBold;
    BtnAprovar->IsEnabled = false;
    BtnAprovar->Click += [this](RoutedEventArgs &e) { AoAprovar(e); };
    gDecisao->Children.Add(BtnAprovar);
    bDecisao->SetChild(gDecisao);
    objDock->Children.Add(bDecisao);

    // ===== Registro e Medidor de Fases ===========================
    auto bRegistro = std::make_shared<Border>();
    DockPanel::SetDock(bRegistro, Dock::Bottom);
    bRegistro->Height = 144;
    bRegistro->Background = Tema::Recurso("FundoLista");
    bRegistro->BorderBrush = Tema::Recurso("Borda");
    bRegistro->BorderThickness = Thickness(1);
    bRegistro->Margin = Thickness(0, 12, 0, 0);
    auto gRegistro = std::make_shared<Grid>();
    gRegistro->ColumnDefinitions = {ColumnDefinition(GridLength::Star(1)), ColumnDefinition(GridLength(290))};

    // Console de auditoria em texto monoespaçado
    AreaRegistro = std::make_shared<ScrollViewer>();
    AreaRegistro->Name = "AreaRegistro";
    AreaRegistro->VerticalScrollBarVisibility = ScrollBarVisibility::Auto;
    TxtRegistro = std::make_shared<TextBlock>();
    TxtRegistro->Name = "TxtRegistro";
    TxtRegistro->FontFamily = Tema::FonteMono;
    TxtRegistro->FontSize = 11;
    TxtRegistro->Margin = Thickness(8);
    TxtRegistro->TextWrapping = Vistas::TextWrapping::Wrap;
    TxtRegistro->Foreground = Tema::Recurso("Texto");
    AreaRegistro->Content = TxtRegistro;
    gRegistro->Children.Add(AreaRegistro);

    // Medidor de fases e progresso percentual
    auto bMedidor = std::make_shared<Border>();
    Grid::SetColumn(bMedidor, 1);
    bMedidor->Background = Tema::Recurso("PainelLateral");
    bMedidor->BorderBrush = Tema::Recurso("Borda");
    bMedidor->BorderThickness = Thickness(1, 0, 0, 0);
    bMedidor->Padding = Thickness(12, 8);
    auto gMedidor = std::make_shared<Grid>();
    gMedidor->RowDefinitions = {RowDefinition(GridLength::Auto()), RowDefinition(GridLength::Star(1)), RowDefinition(GridLength::Auto())};

    // Cabeçalho do medidor
    auto tStatus = Txt("STATUS DA OPERAÇÃO", "Rotulo");
    Grid::SetRow(tStatus, 0);
    tStatus->FontSize = 10;
    tStatus->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    gMedidor->Children.Add(tStatus);

    // Centro: número grande OU verbo da fase rápida
    auto gCentro = std::make_shared<Grid>();
    Grid::SetRow(gCentro, 1);
    gCentro->VerticalAlignment = Vistas::VerticalAlignment::Center;
    gCentro->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    gCentro->Margin = Thickness(0, 2);

    // Visualização de Fase com Progresso (% Grande para processos > 30s)
    PnlProgressoPercentual = std::make_shared<StackPanel>();
    PnlProgressoPercentual->Name = "PnlProgressoPercentual";
    PnlProgressoPercentual->Visibility = Vistas::Visibility::Collapsed;
    PnlProgressoPercentual->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    // LINUX: "DIGITALIZANDO DISCO (SPTI)" no original
    TxtFaseLongaTitulo = Txt("DIGITALIZANDO DISCO (SG_IO)");
    TxtFaseLongaTitulo->Name = "TxtFaseLongaTitulo";
    TxtFaseLongaTitulo->FontSize = 10;
    TxtFaseLongaTitulo->FontWeight = FontWeights::SemiBold;
    TxtFaseLongaTitulo->Foreground = Tema::Recurso("Texto");
    TxtFaseLongaTitulo->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    TxtFaseLongaTitulo->TextAlignment = Vistas::TextAlignment::Center;
    TxtFaseLongaTitulo->Margin = Thickness(0, 0, 0, 1);
    PnlProgressoPercentual->Children.Add(TxtFaseLongaTitulo);
    auto sNumero = std::make_shared<StackPanel>();
    sNumero->Orientation = Vistas::Orientation::Horizontal;
    sNumero->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    TxtPercentualNumero = Txt("0");
    TxtPercentualNumero->Name = "TxtPercentualNumero";
    TxtPercentualNumero->FontSize = 42;
    TxtPercentualNumero->FontWeight = FontWeights::Bold;
    TxtPercentualNumero->Foreground = Tema::Recurso("Acento");
    TxtPercentualNumero->FontFamily = Tema::Fonte;
    sNumero->Children.Add(TxtPercentualNumero);
    auto tPorcento = Txt("%");
    tPorcento->FontSize = 16;
    tPorcento->FontWeight = FontWeights::SemiBold;
    tPorcento->Foreground = Tema::Recurso("TextoFraco");
    tPorcento->Margin = Thickness(2, 0, 0, 7);
    tPorcento->VerticalAlignment = Vistas::VerticalAlignment::Bottom;
    sNumero->Children.Add(tPorcento);
    PnlProgressoPercentual->Children.Add(sNumero);
    PbProgressoFase = std::make_shared<ProgressBar>();
    PbProgressoFase->Name = "PbProgressoFase";
    PbProgressoFase->Width = 230;
    PbProgressoFase->Height = 5;
    PbProgressoFase->Margin = Thickness(0, 1, 0, 0);
    PbProgressoFase->Minimum = 0;
    PbProgressoFase->Maximum = 100;
    PbProgressoFase->Value = 0;
    PbProgressoFase->Foreground = Tema::Recurso("AcentoVivo");
    PbProgressoFase->Background = Tema::Recurso("CabecalhoColuna");
    PbProgressoFase->BorderBrush = Tema::Recurso("Borda");
    PnlProgressoPercentual->Children.Add(PbProgressoFase);
    gCentro->Children.Add(PnlProgressoPercentual);

    // Visualização de Fase Rápida / Estado (Verbo em destaque para processos rápidos)
    PnlFaseRapida = std::make_shared<StackPanel>();
    PnlFaseRapida->Name = "PnlFaseRapida";
    PnlFaseRapida->Visibility = Vistas::Visibility::Visible;
    PnlFaseRapida->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    PnlFaseRapida->VerticalAlignment = Vistas::VerticalAlignment::Center;
    TxtFaseVerbo = Txt("PRONTO");
    TxtFaseVerbo->Name = "TxtFaseVerbo";
    TxtFaseVerbo->FontSize = 15;
    TxtFaseVerbo->FontWeight = FontWeights::Bold;
    TxtFaseVerbo->Foreground = Tema::Recurso("Texto");
    TxtFaseVerbo->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    TxtFaseVerbo->TextAlignment = Vistas::TextAlignment::Center;
    TxtFaseVerbo->TextWrapping = Vistas::TextWrapping::Wrap;
    TxtFaseVerbo->MaxWidth = 260;
    PnlFaseRapida->Children.Add(TxtFaseVerbo);
    gCentro->Children.Add(PnlFaseRapida);
    gMedidor->Children.Add(gCentro);

    // Subtexto de Detalhe no rodapé do medidor
    TxtFaseDetalhe = Txt("Aguardando início da extração", "Fraco");
    TxtFaseDetalhe->Name = "TxtFaseDetalhe";
    Grid::SetRow(TxtFaseDetalhe, 2);
    TxtFaseDetalhe->FontSize = 10;
    TxtFaseDetalhe->TextAlignment = Vistas::TextAlignment::Center;
    TxtFaseDetalhe->TextTrimming = Vistas::TextTrimming::CharacterEllipsis;
    TxtFaseDetalhe->TextWrapping = Vistas::TextWrapping::NoWrap;
    TxtFaseDetalhe->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    TxtFaseDetalhe->MaxWidth = 265;
    TxtFaseDetalhe->Margin = Thickness(0, 1, 0, 0);
    gMedidor->Children.Add(TxtFaseDetalhe);
    bMedidor->SetChild(gMedidor);
    gRegistro->Children.Add(bMedidor);
    bRegistro->SetChild(gRegistro);
    objDock->Children.Add(bRegistro);

    // ===== Corpo =================================================
    auto gCorpo = std::make_shared<Grid>();
    gCorpo->ColumnDefinitions = {ColumnDefinition(GridLength(248)), ColumnDefinition(GridLength::Star(1)), ColumnDefinition(GridLength(300))};

    // extração
    auto bExtracao = std::make_shared<Border>();
    bExtracao->Background = Tema::Recurso("PainelLateral");
    bExtracao->BorderBrush = Tema::Recurso("Borda");
    bExtracao->BorderThickness = Thickness(1);
    bExtracao->Padding = Thickness(12);
    auto sExtracao = std::make_shared<StackPanel>();
    sExtracao->Children.Add(Txt("1 · EXTRAIR", "Rotulo"));
    auto tUnidade = Txt("Unidade", "Fraco");
    tUnidade->Margin = Thickness(0, 4, 0, 3);
    sExtracao->Children.Add(tUnidade);
    ListaUnidades = std::make_shared<ComboBox>();
    ListaUnidades->Name = "ListaUnidades";
    ListaUnidades->Margin = Thickness(0, 0, 0, 10);
    sExtracao->Children.Add(ListaUnidades);

    BtnExtrairCd = std::make_shared<Button>();
    BtnExtrairCd->Name = "BtnExtrairCd";
    BtnExtrairCd->Content = "Extrair CD de áudio";
    BtnExtrairCd->HorizontalAlignment = Vistas::HorizontalAlignment::Stretch;
    BtnExtrairCd->Padding = Thickness(0, 8);
    BtnExtrairCd->Click += [this](RoutedEventArgs &e) { AoExtrair(e); };
    sExtracao->Children.Add(BtnExtrairCd);
    TxtExtracao = Txt("", "Fraco");
    TxtExtracao->Name = "TxtExtracao";
    TxtExtracao->FontSize = 10;
    TxtExtracao->Margin = Thickness(0, 8, 0, 0);
    sExtracao->Children.Add(TxtExtracao);

    auto bSeparador = std::make_shared<Border>();
    bSeparador->Height = 1;
    bSeparador->Background = Tema::Recurso("Borda");
    bSeparador->Margin = Thickness(0, 14, 0, 12);
    sExtracao->Children.Add(bSeparador);

    auto tArea = Txt("Área de trabalho", "Fraco");
    tArea->Margin = Thickness(0, 0, 0, 4);
    sExtracao->Children.Add(tArea);
    BtnReler = std::make_shared<Button>();
    BtnReler->Name = "BtnReler";
    BtnReler->Content = "Reler a área de trabalho";
    BtnReler->HorizontalAlignment = Vistas::HorizontalAlignment::Stretch;
    BtnReler->Padding = Thickness(0, 6);
    BtnReler->Click += [this](RoutedEventArgs &e) { AoReler(e); };
    sExtracao->Children.Add(BtnReler);
    // Desistir de um disco extraído: apaga a área de trabalho
    // (só ela — o acervo não é tocado).
    BtnDescartar = std::make_shared<Button>();
    BtnDescartar->Name = "BtnDescartar";
    BtnDescartar->Content = "Descartar a área de trabalho…";
    BtnDescartar->HorizontalAlignment = Vistas::HorizontalAlignment::Stretch;
    BtnDescartar->Padding = Thickness(0, 6);
    BtnDescartar->Margin = Thickness(0, 6, 0, 0);
    BtnDescartar->Click += [this](RoutedEventArgs &e) { AoDescartar(e); };
    BtnDescartar->ToolTip = "Apagar o disco extraído que você não vai aprovar. Nada do acervo é tocado.";
    sExtracao->Children.Add(BtnDescartar);
    // LINUX: "via SPTI" no original
    auto tExplica = Txt("Extrações diretas via SG_IO ou arquivos pré-existentes na pasta de trabalho aparecem aqui para conferência de hash SHA-256 e aprovação.", "Fraco");
    tExplica->FontSize = 10;
    tExplica->Margin = Thickness(0, 8, 0, 0);
    sExtracao->Children.Add(tExplica);
    bExtracao->SetChild(sExtracao);
    gCorpo->Children.Add(bExtracao);

    // faixas
    auto dFaixas = std::make_shared<DockPanel>();
    Grid::SetColumn(dFaixas, 1);
    dFaixas->Margin = Thickness(12, 0, 12, 0);
    auto bTitulo = std::make_shared<Border>();
    DockPanel::SetDock(bTitulo, Dock::Top);
    bTitulo->Padding = Thickness(0, 0, 0, 6);
    TxtFaixasTitulo = Txt("AGUARDANDO EXTRAÇÃO", "Rotulo");
    TxtFaixasTitulo->Name = "TxtFaixasTitulo";
    bTitulo->SetChild(TxtFaixasTitulo);
    dFaixas->Children.Add(bTitulo);
    auto bGrade = std::make_shared<Border>();
    bGrade->Background = Tema::Recurso("FundoLista");
    bGrade->BorderBrush = Tema::Recurso("Borda");
    bGrade->BorderThickness = Thickness(1);
    ListaFaixas = std::make_shared<DataGrid>();
    ListaFaixas->Name = "ListaFaixas";
    ListaFaixas->Background = Brushes::Transparent;
    ListaFaixas->BorderThickness = Thickness(0);
    ListaFaixas->AutoGenerateColumns = false;
    ListaFaixas->IsReadOnly = true;
    ListaFaixas->HeadersVisibility = DataGridHeadersVisibility::Column;
    ListaFaixas->RowHeight = 24;
    ListaFaixas->GridLinesVisibility = DataGridGridLinesVisibility::Horizontal;
    ListaFaixas->HorizontalGridLinesBrush = Tema::Recurso("LinhaCabecalho");
    ListaFaixas->RowBackground = Brushes::Transparent;
    ListaFaixas->AlternatingRowBackground = Tema::Recurso("LinhaAlternada");
    ListaFaixas->Columns = {
        DataGridTextColumn::Nova<LinhaFaixa>("#", DataGridLength(44), [](LinhaFaixa &l) { return std::to_string(l.Numero); }),
        DataGridTextColumn::Nova<LinhaFaixa>("Arquivo", DataGridLength::Star(2.6), [](LinhaFaixa &l) { return l.Nome; }),
        DataGridTextColumn::Nova<LinhaFaixa>("Tamanho", DataGridLength(110), [](LinhaFaixa &l) { return l.Tamanho; }),
        DataGridTextColumn::Nova<LinhaFaixa>("Identidade (SHA-256)", DataGridLength::Star(1.4), [](LinhaFaixa &l) { return l.Hash; })};
    // {Binding Numero}: o numero ordena como numero
    ListaFaixas->Columns[0]->OrdenaPor<LinhaFaixa, int>([](LinhaFaixa &l) { return l.Numero; });
    bGrade->SetChild(ListaFaixas);
    dFaixas->Children.Add(bGrade);
    gCorpo->Children.Add(dFaixas);

    // identificação
    auto bIdent = std::make_shared<Border>();
    Grid::SetColumn(bIdent, 2);
    bIdent->Background = Tema::Recurso("PainelLateral");
    bIdent->BorderBrush = Tema::Recurso("Borda");
    bIdent->BorderThickness = Thickness(1);
    bIdent->Padding = Thickness(12);
    auto sIdent = std::make_shared<StackPanel>();
    sIdent->Children.Add(Txt("2 · IDENTIFICAR", "Rotulo"));

    auto bCapa = std::make_shared<Border>();
    bCapa->Width = 150;
    bCapa->Height = 150;
    bCapa->Background = Tema::Recurso("PainelLateral");
    bCapa->Margin = Thickness(0, 4, 0, 10);
    bCapa->BorderBrush = Tema::Recurso("Borda");
    bCapa->BorderThickness = Thickness(1);
    bCapa->HorizontalAlignment = Vistas::HorizontalAlignment::Left;
    auto gCapa = std::make_shared<Grid>();
    TxtSemCapa = Txt("sem capa", "Fraco");
    TxtSemCapa->Name = "TxtSemCapa";
    TxtSemCapa->FontSize = 10;
    TxtSemCapa->HorizontalAlignment = Vistas::HorizontalAlignment::Center;
    TxtSemCapa->VerticalAlignment = Vistas::VerticalAlignment::Center;
    gCapa->Children.Add(TxtSemCapa);
    ImgCapa = std::make_shared<Image>();
    ImgCapa->Name = "ImgCapa";
    ImgCapa->Stretch = Vistas::Stretch::UniformToFill;
    gCapa->Children.Add(ImgCapa);
    bCapa->SetChild(gCapa);
    sIdent->Children.Add(bCapa);

    auto tArtista = Txt("Artista", "Fraco");
    tArtista->Margin = Thickness(0, 0, 0, 3);
    sIdent->Children.Add(tArtista);
    CaixaArtista = std::make_shared<TextBox>();
    CaixaArtista->Name = "CaixaArtista";
    CaixaArtista->Margin = Thickness(0, 0, 0, 8);
    sIdent->Children.Add(CaixaArtista);
    auto tAlbum = Txt("Álbum", "Fraco");
    tAlbum->Margin = Thickness(0, 0, 0, 3);
    sIdent->Children.Add(tAlbum);
    CaixaAlbum = std::make_shared<TextBox>();
    CaixaAlbum->Name = "CaixaAlbum";
    CaixaAlbum->Margin = Thickness(0, 0, 0, 10);
    sIdent->Children.Add(CaixaAlbum);

    auto tBusca = Txt("Busca assistida", "Fraco");
    tBusca->Margin = Thickness(0, 0, 0, 3);
    sIdent->Children.Add(tBusca);
    CaixaBuscaAlbum = std::make_shared<TextBox>();
    CaixaBuscaAlbum->Name = "CaixaBuscaAlbum";
    CaixaBuscaAlbum->Margin = Thickness(0, 0, 0, 6);
    sIdent->Children.Add(CaixaBuscaAlbum);
    BtnIdentificar = std::make_shared<Button>();
    BtnIdentificar->Name = "BtnIdentificar";
    BtnIdentificar->Content = "Identificar álbum";
    BtnIdentificar->HorizontalAlignment = Vistas::HorizontalAlignment::Stretch;
    BtnIdentificar->Padding = Thickness(0, 7);
    BtnIdentificar->Click += [this](RoutedEventArgs &e) { AoIdentificar(e); };
    sIdent->Children.Add(BtnIdentificar);
    // Para o disco que nenhum catálogo conhece: o encarte é a fonte.
    BtnCatalogarManual = std::make_shared<Button>();
    BtnCatalogarManual->Name = "BtnCatalogarManual";
    BtnCatalogarManual->Content = "Catalogar à mão…";
    BtnCatalogarManual->HorizontalAlignment = Vistas::HorizontalAlignment::Stretch;
    BtnCatalogarManual->Padding = Thickness(0, 5);
    BtnCatalogarManual->Margin = Thickness(0, 6, 0, 0);
    BtnCatalogarManual->Click += [this](RoutedEventArgs &e) { AoCatalogarManual(e); };
    BtnCatalogarManual->ToolTip = "Digitar título, intérprete e compositor de cada faixa a partir do encarte";
    sIdent->Children.Add(BtnCatalogarManual);
    // Audiolivro e fala (entrevista, humor): marca por faixa, vale venha a identificação de onde vier.
    BtnConteudo = std::make_shared<Button>();
    BtnConteudo->Name = "BtnConteudo";
    BtnConteudo->Content = "Conteúdo das faixas…";
    BtnConteudo->HorizontalAlignment = Vistas::HorizontalAlignment::Stretch;
    BtnConteudo->Padding = Thickness(0, 5);
    BtnConteudo->Margin = Thickness(0, 6, 0, 0);
    BtnConteudo->Click += [this](RoutedEventArgs &e) { AoDefinirConteudo(e); };
    BtnConteudo->ToolTip = "Marcar faixas como audiolivro ou fala (entrevista, humor) em vez de música";
    sIdent->Children.Add(BtnConteudo);
    TxtIdentificacao = Txt("", "Fraco");
    TxtIdentificacao->Name = "TxtIdentificacao";
    TxtIdentificacao->FontSize = 10;
    TxtIdentificacao->Margin = Thickness(0, 8, 0, 0);
    sIdent->Children.Add(TxtIdentificacao);
    bIdent->SetChild(sIdent);
    gCorpo->Children.Add(bIdent);
    objDock->Children.Add(gCorpo);

    Content = objDock;
}

// ===============================================================
// O AquisicaoView.xaml.cs
// ===============================================================
void AquisicaoView::Iniciar()
{
    if (_passos.empty()) return;
    if (m_objIndicador) m_objIndicador->Resetar("PRONTO", "Aguardando início da extração");
    TxtTrabalho->Text = "Área de trabalho: " + PastaTrabalho;
    ListarUnidades();
    Reler();

    // Se houver um rip pendente de aprovação ao abrir o programa, tenta identificar automaticamente uma única vez
    // LINUX: "*.wma" no original; as faixas no xOS são .flac.
    if (!_blnAutoIdentificacaoJaExecutada && Base::PastaExiste(PastaTrabalho) && !Base::Arquivos(PastaTrabalho, ".flac").empty())
    {
        IdentificarInterno(false);
    }
}

// ===============================================================
void AquisicaoView::MontarPassos()
{
    for (const char *s : {"1 · EXTRAIR", "2 · IDENTIFICAR", "3 · APROVAR"})
    {
        if (!_passos.empty())
        {
            auto objSeta = std::make_shared<TextBlock>("›");
            objSeta->Foreground = Tema::Recurso("TextoFraco");
            objSeta->FontSize = 14;
            objSeta->Margin = Thickness(14, 0, 14, 0);
            objSeta->VerticalAlignment = Vistas::VerticalAlignment::Center;
            PainelPassos->Children.Add(objSeta);
        }

        auto t = std::make_shared<TextBlock>(s);
        t->FontSize = 12;
        t->FontWeight = FontWeights::SemiBold;
        t->Foreground = Tema::Recurso("TextoFraco");
        t->VerticalAlignment = Vistas::VerticalAlignment::Center;
        _passos.push_back(t);
        PainelPassos->Children.Add(t);
    }
}

void AquisicaoView::MarcarPasso(int ativo)
{
    for (size_t i = 0; i < _passos.size(); i++)
        _passos[i]->Foreground = Tema::Recurso((int) i == ativo ? "Acento" : "TextoFraco");
}

void AquisicaoView::ListarUnidades()
{
    ListaUnidades->Items.Clear();
    try
    {
        // LINUX: no original, DriveInfo.GetDrives() com DriveType.CDRom, que dava as letras
        // ("E:"). Aqui, os drives de CD do Linux (/dev/sr*), com o nome que cada um declara no
        // INQUIRY (fabricante e modelo, que o kernel guarda em /sys/class/block/srN/device).
        std::vector<std::string> lstNomes;
        std::error_code e;
        for (auto &d : fs::directory_iterator("/sys/class/block", e))
        {
            std::string n = d.path().filename().string();
            if (n.size() > 2 && n.compare(0, 2, "sr") == 0) lstNomes.push_back(n);
        }
        std::sort(lstNomes.begin(), lstNomes.end());
        for (const std::string &n : lstNomes)
        {
            std::string strNome;
            for (const char *campo : {"vendor", "model"})
            {
                try
                {
                    std::string v = Base::Trim(Base::LeTexto("/sys/class/block/" + n + "/device/" + campo));
                    if (!v.empty()) strNome += (strNome.empty() ? "" : " ") + v;
                }
                catch (...) {}
            }
            ListaUnidades->Items.Add("/dev/" + n + (strNome.empty() ? "" : " (" + strNome + ")"));
        }
    }
    catch (...) {}
    if (ListaUnidades->Items.Count() > 0) ListaUnidades->SelectedIndex = 0;
    else ListaUnidades->Items.Add("nenhuma unidade óptica");
}

// ===============================================================
/// O que a área de trabalho tem e que se perderia ao extrair outro
/// disco: tudo, menos o log (que é só auditoria). Caminhos relativos.
std::vector<std::string> AquisicaoView::MaterialNaAreaDeTrabalho()
{
    std::vector<std::string> lst;
    try
    {
        if (!Base::PastaExiste(PastaTrabalho)) return lst;
        for (const std::string &a : TodosOsArquivos(PastaTrabalho))
        {
            std::string strRel = a.substr(PastaTrabalho.size());
            while (!strRel.empty() && strRel[0] == '/') strRel.erase(0, 1);   // TrimStart('\\')
            if (!Base::IgualSemCaixa(strRel, "aquisicao_detalhada.log")) lst.push_back(strRel);
        }
        std::sort(lst.begin(), lst.end(), MenorSemCaixa);
    }
    catch (...) {}
    return lst;
}

/// Mostra o que está na área de trabalho e pergunta se pode apagar.
/// true sem perguntar quando ela já está vazia (só o log não conta).
bool AquisicaoView::ConfirmarDescarte(const std::string &strPergunta)
{
    std::vector<std::string> lstMaterial = MaterialNaAreaDeTrabalho();
    if (lstMaterial.empty()) return true;
    // LINUX: ".wma" no original
    int intFaixas = (int) std::count_if(lstMaterial.begin(), lstMaterial.end(),
                                        [](const std::string &s) { return Base::TerminaCom(s, ".flac", true); });
    std::string strAlbumAtual = Base::Trim(CaixaArtista->Text()).size() > 0 || Base::Trim(CaixaAlbum->Text()).size() > 0
        ? "\r\n" + Base::Trim(CaixaArtista->Text()) + " — " + Base::Trim(CaixaAlbum->Text()) + "\r\n" : "";
    std::vector<std::string> lstMostrar = lstMaterial.size() > 12
        ? std::vector<std::string>(lstMaterial.begin(), lstMaterial.begin() + 12) : lstMaterial;
    return MessageBox::Show(
               "A área de trabalho ainda tem material que não foi para o acervo" +
               (intFaixas > 0 ? " (" + std::to_string(intFaixas) + " faixa(s) extraída(s), aguardando aprovação)" : std::string()) + ":\r\n" +
               strAlbumAtual + "\r\n  " + Base::Junta(lstMostrar, "\r\n  ") +
               (lstMaterial.size() > lstMostrar.size() ? "\r\n  … e mais " + std::to_string(lstMaterial.size() - lstMostrar.size()) : std::string()) +
               "\r\n\r\n" + strPergunta,
               "Descartar o que está na área de trabalho?", MessageBoxButton::YesNo, MessageBoxImage::Warning,
               MessageBoxResult::No) == MessageBoxResult::Yes;
}

/// Apaga tudo da área de trabalho, inclusive Artwork (cujas páginas
/// entrariam no manifesto do disco seguinte). Devolve os arquivos que
/// o Windows não deixou apagar (um WMA tocando no player, por exemplo);
/// strFalha quando nem deu para ler a pasta. O acervo não é tocado.
std::vector<std::string> AquisicaoView::LimparAreaDeTrabalho(std::string &strFalha)
{
    strFalha.clear();
    std::vector<std::string> lstNaoApagados;
    try
    {
        if (!Base::PastaExiste(PastaTrabalho)) { Base::CriaPasta(PastaTrabalho); return lstNaoApagados; }
        std::error_code eLer;
        fs::directory_iterator objTeste(fs::u8path(PastaTrabalho), eLer);
        if (eLer) throw std::runtime_error(eLer.message());
        std::vector<std::string> lstArquivos = Base::Arquivos(PastaTrabalho);
        std::string strArtwork = Base::Combina(PastaTrabalho, Integracao::PastaArtwork);
        if (Base::PastaExiste(strArtwork))
        {
            std::vector<std::string> lstArt = Base::Arquivos(strArtwork);
            lstArquivos.insert(lstArquivos.end(), lstArt.begin(), lstArt.end());
        }
        for (const std::string &strArquivo : lstArquivos)
        {
            std::error_code e;
            if (!fs::remove(fs::u8path(strArquivo), e) || e)
            {
                std::string strRel = strArquivo.substr(PastaTrabalho.size());
                while (!strRel.empty() && strRel[0] == '/') strRel.erase(0, 1);
                lstNaoApagados.push_back(strRel);
            }
        }
    }
    catch (const std::exception &ex) { strFalha = ex.what(); }
    return lstNaoApagados;
}

/// A tela da AQUISIÇÃO volta ao começo: sem faixas, sem identificação, sem capa.
void AquisicaoView::ZerarTela()
{
    _manifesto.clear();
    _blnAutoIdentificacaoJaExecutada = false;
    ListaFaixas->ItemsSource = FonteDeItens();
    CaixaArtista->Text = "";
    CaixaAlbum->Text = "";
    CaixaBuscaAlbum->Text = "";
    _capaBytes.clear();
    _caminhoCapaSalva.clear();
    ImgCapa->Source = nullptr;
    TxtSemCapa->Visibility = Vistas::Visibility::Visible;
    IdDoDisco.clear();
    _strSugestaoConteudo.clear();
    TxtIdentificacao->Text = "";
}

/// Desistir de um disco extraído (Lorenzo, 06/10/2026: o reteste do
/// Blacksmith ficou na área de trabalho sem jeito de sair). Apaga só a
/// área de trabalho, com a mesma confirmação de quando se extrai outro
/// disco por cima. Nada vai ao ledger: nada entrou no acervo.
void AquisicaoView::AoDescartar(RoutedEventArgs &)
{
    if (_ocupado || _blnEmIdentificacao)
    {
        MessageBox::Show("Espere a extração, a conferência ou a identificação em andamento terminar antes de descartar.",
                         "Aquisição", MessageBoxButton::OK, MessageBoxImage::Information);
        return;
    }
    if (MaterialNaAreaDeTrabalho().empty())
    {
        Registrar("A área de trabalho já está vazia: nada a descartar.");
        return;
    }
    if (!ConfirmarDescarte("Descartar APAGA esse material da área de trabalho. Nada do acervo é tocado. Descartar?")) return;

    std::string strFalha;
    std::vector<std::string> lstNaoApagados = LimparAreaDeTrabalho(strFalha);
    ZerarTela();
    if (!strFalha.empty()) Registrar("Não consegui descartar: " + strFalha);
    else if (!lstNaoApagados.empty())
        Registrar("Descartado, menos: " + Base::Junta(lstNaoApagados, ", ") +
                  ". Se um deles estiver tocando, pare o player e descarte de novo.");
    else Registrar("Área de trabalho descartada.");
    Reler();
}

// ===============================================================
void AquisicaoView::AoExtrair(RoutedEventArgs &)
{
    if (_ocupado) return;
    // Uma identificação em andamento gravaria os dados do disco anterior
    // nas faixas do novo, ou renomearia WMA ainda em codificação.
    if (_blnEmIdentificacao)
    {
        MessageBox::Show("Há uma identificação em andamento. Espere ela terminar (ou cancele a escolha) antes de extrair outro disco.",
                         "Aquisição", MessageBoxButton::OK, MessageBoxImage::Information);
        return;
    }

    std::string strUnidade = ListaUnidades->SelectedItem();
    if (strUnidade.empty() || Base::ComecaCom(strUnidade, "nenhuma", true))
    {
        MessageBox::Show("Selecione uma unidade óptica válida contendo o disco de áudio.", "Aquisição", MessageBoxButton::OK, MessageBoxImage::Information);
        return;
    }

    // LINUX: char chrDrive = strUnidade[0] (a letra) no original; aqui o dispositivo ("/dev/sr0"),
    // que é o que o SptiImageExtractor, o CdromTocReader e a ConferenciaBitPerfect portados recebem.
    std::string strDrive = DispositivoDoItem(strUnidade);

    // Primeiro o disco, depois a área de trabalho: sem disco pronto
    // não há o que extrair, e nada deve ser apagado por isso.
    bool blnPronta = false;
    try { blnPronta = DiscoPronto(strDrive); } catch (...) {}
    if (!blnPronta)
    {
        // LINUX: "na unidade {0}:." no original (a letra e os dois pontos)
        MessageBox::Show("Não há disco pronto na unidade " + strDrive + ". Coloque o CD, espere a unidade ler e tente de novo.",
                         "Aquisição", MessageBoxButton::OK, MessageBoxImage::Information);
        return;
    }

    // A área de trabalho pode guardar um disco que ainda espera
    // aprovação, ou o que uma aprovação deixou para trás. Extrair
    // outro disco descarta isso — só com a pessoa sabendo.
    if (!ConfirmarDescarte("Extrair outro disco APAGA esse material. Descartar e extrair?")) return;

    // Limpeza completa da área de trabalho para evitar misturar arquivos
    // de diferentes discos — inclusive a subpasta Artwork, cujas páginas
    // entrariam no manifesto do disco seguinte.
    std::string strFalhaLimpeza;
    std::vector<std::string> lstNaoApagados = LimparAreaDeTrabalho(strFalhaLimpeza);
    if (!strFalhaLimpeza.empty())
    {
        Registrar("Falha ao preparar diretório de trabalho: " + strFalhaLimpeza);
        return;
    }
    if (!lstNaoApagados.empty())
        Registrar("Atenção: não consegui apagar da área de trabalho: " + Base::Junta(lstNaoApagados, ", ") + ".");

    ZerarTela();

    _ocupado = true;
    _blnErroNoMedidor = false;
    BtnExtrairCd->IsEnabled = false;
    BtnIdentificar->IsEnabled = false;
    BtnAprovar->IsEnabled = false;
    // LINUX: "extraindo via SPTI…" e "Abertura de porta SCSI/SPTI na unidade {0}:" no original
    TxtExtracao->Text = "extraindo via SG_IO…";
    TxtFaixasTitulo->Text = "EXTRAINDO CD DE ÁUDIO…";
    MarcarPasso(0);
    if (m_objIndicador) m_objIndicador->DefinirFaseRapida("SONDANDO HARDWARE…", "Abertura de porta SCSI/SG_IO na unidade " + strDrive);

    std::string strDestinoBin = Base::Combina(PastaTrabalho, "AudioCD.bin");
    std::string strDestinoCue = Base::Combina(PastaTrabalho, "AudioCD.cue");

    std::thread([this, strDrive, strDestinoBin, strDestinoCue] {
        try
        {
            AcquisitionLogger logger(PastaTrabalho, [this](const std::string &m) {
                RegistrarDeOutroFio(m);
            });

            // 1. Extração física direta do bitstream óptico
            SptiImageExtractor extractor(logger);
            bool blnSucessoSpti = extractor.ExtrairImagem(
                strDrive,
                strDestinoBin,
                PastaTrabalho,
                true,
                [this](double dblPct, double dblMbLidos, double dblMbTotal, double dblVel, int64_t, int64_t) {
                    if (m_objIndicador)
                    {
                        // LINUX: "DIGITALIZANDO DISCO (SPTI)" no original
                        m_objIndicador->AtualizarProgresso(
                            "DIGITALIZANDO DISCO (SG_IO)",
                            dblPct,
                            F(dblMbLidos, 1) + " MB / " + F(dblMbTotal, 1) + " MB (" + F(dblVel, 1) + " MB/s)");
                    }
                },
                [this](const std::string &strVerbo, const std::string &strDetalhe) {
                    if (m_objIndicador)
                    {
                        m_objIndicador->DefinirFaseRapida(strVerbo, strDetalhe);
                    }
                });

            if (!blnSucessoSpti)
            {
                throw Base::InvalidOperationException(extractor.MotivoFalha());
            }

            // Setores que viraram silêncio: o aviso aparece já no registro,
            // e de novo, em janela, quando a extração terminar.
            std::shared_ptr<TRelatorioExtracao> objRelatorioSpti = extractor.Relatorio();
            if (objRelatorioSpti && !objRelatorioSpti->Exata())
            {
                std::string strAviso = "⚠ EXTRAÇÃO NÃO EXATA: " + std::to_string(objRelatorioSpti->intSetoresMascarados) +
                                       " setor(es) ilegível(is) viraram silêncio — " + objRelatorioSpti->ResumoPorFaixa() + ".";
                Base::Despacha([this, strAviso] { Registrar(strAviso); });
            }

            // DECISAO-LORENZO (08/11/2026): o CUE como o drive o escreveu, com o CD-TEXT, antes de
            // qualquer enriquecimento reescrever TITLE/PERFORMER (ver STR_CUE_CDTEXT)
            try
            {
                fs::copy_file(strDestinoCue, Base::Combina(PastaTrabalho, STR_CUE_CDTEXT), fs::copy_options::overwrite_existing);
            }
            catch (...) {}
            TCdText cdText = LerCdText(PastaTrabalho);
            if (cdText.TemTitulos())
            {
                std::string strResumo = "CD-TEXT do disco: " + (cdText.strArtista.empty() ? std::string("?") : cdText.strArtista) + " — " +
                                        (cdText.strAlbum.empty() ? std::string("?") : cdText.strAlbum) + "; os títulos dele prevalecem.";
                Base::Despacha([this, strResumo] { Registrar(strResumo); });
            }
            // os titulos de uma fonte externa, com o CD-TEXT por cima (e o registro do que mudou)
            auto comCdText = [this, &cdText](std::vector<std::string> lstTitulos, std::vector<std::string> &lstArtistas) {
                int n = AplicarCdText(cdText, lstTitulos, lstArtistas);
                if (n > 0) RegistrarDeOutroFio("CD-TEXT: " + std::to_string(n) + " título(s) do disco mantido(s) no lugar dos da fonte externa.");
                return lstTitulos;
            };

            // 2. Leitura do DiscID para o ledger
            try
            {
                IdDoDisco = CdromTocReader::GetDiscId(strDrive);
                // Conferência: o mesmo ID refeito a partir do CUE é o que a
                // aprovação usa se o ARCA for reaberto no meio do caminho.
                // Se divergirem, o log mostra — e a recuperação não é confiável.
                std::string strIdDoCue = CdromTocReader::CalcularIdDoCue(strDestinoCue);
                std::string strIdLido = IdDoDisco;
                Base::Despacha([this, strIdDoCue, strIdLido] {
                    Registrar("DiscID do disco físico: " + strIdLido);
                    if (strIdDoCue.empty())
                        Registrar("DiscID pelo CUE: não calculável (multissessão ou CUE incompleto).");
                    else if (strIdDoCue == strIdLido)
                        Registrar("DiscID pelo CUE: confere.");
                    else
                        Registrar("ATENÇÃO: DiscID pelo CUE (" + strIdDoCue + ") difere do lido no drive. Não feche o ARCA antes de aprovar este disco.");
                    if (m_objIndicador) m_objIndicador->DefinirFaseRapida("DISCID IDENTIFICADO", "DiscID: " + strIdLido);
                });
            }
            catch (...)
            {
                IdDoDisco = "UnknownDisc";
            }

            // 2.5. Tentativa de identificação e enriquecimento prévio do CUE antes do fatiamento
            // ARCA-ORIGINAL: esta pré-identificação não olha o CD-TEXT do CUE (TITLE/PERFORMER do
            // disco): o álbum buscado vem das caixas (vazias depois do ZerarTela), do CD Extra ou do
            // GnuDB. E o Discogs aceita o lançamento pela contagem de faixas e título parecido, sem
            // conferir a ORDEM nem as durações. Visto no teste de 08/11/2026 com o "Mitos do Jazz
            // vol. 1" (Glenn Miller, CD-TEXT completo): o GnuDB deu "The very best of", o Discogs
            // achou "The Very Best Of Glenn Miller" (20 faixas, outra ordem) e o CUE e as faixas
            // saíram com os títulos dele — a faixa 1 (Moonlight Serenade no CD-TEXT) virou "In The
            // Mood". O portão de aprovação não pega isso: artista e álbum estão preenchidos.
            std::string strArtista;     // "" = null
            std::string strAlbum;
            Base::DespachaEEspera([&] {
                strArtista = Base::Trim(CaixaArtista->Text());
                strAlbum = Base::Trim(CaixaAlbum->Text());
            });
            // DECISAO-LORENZO (08/11/2026): com as caixas vazias, o disco diz quem e (antes do GnuDB)
            if (strArtista.empty()) strArtista = cdText.strArtista;
            if (strAlbum.empty()) strAlbum = cdText.strAlbum;

            // 2.5.1. Mineração de metadados e capa da Sessão 2 local (Enhanced CD / Copy Control)
            try
            {
                if (m_objIndicador) m_objIndicador->DefinirFaseRapida("MINERANDO CD EXTRA…", "Varredura da Sessão 2 por capas e multimídia");
                std::optional<TMetadadosCdExtra> metaSessao2;     // nullopt = null
                {
                    // LINUX: Directory.Exists(chrDrive + @":\") e a mineração na unidade, no
                    // original; aqui a sessão de dados montada só leitura (TSessaoDadosMontada).
                    TSessaoDadosMontada objSessao(strDrive);
                    if (!objSessao.Raiz().empty())
                    {
                        metaSessao2.emplace();
                        TCdExtraExtractor::MinerarSessaoDados(objSessao.Raiz(), [this](const std::string &m) { RegistrarDeOutroFio(m); }, *metaSessao2);
                    }
                }
                if ((!metaSessao2 || metaSessao2->lstTitulosFaixas.empty()) && Base::PastaExiste(PastaTrabalho))
                {
                    metaSessao2.emplace();
                    TCdExtraExtractor::MinerarSessaoDados(PastaTrabalho, [this](const std::string &m) { RegistrarDeOutroFio(m); }, *metaSessao2);
                }
                // Não há terceiro lugar onde procurar: uma pasta fixa
                // (era Z:\TEMP) guardava restos de outro CD Extra, e os
                // títulos e a capa dele iam parar em todo CD comum.

                if (metaSessao2)
                {
                    if (!metaSessao2->strArtistaAlbum.empty() && strArtista.empty())
                    {
                        strArtista = metaSessao2->strArtistaAlbum;
                    }
                    if (!metaSessao2->strAlbum.empty() && strAlbum.empty())
                    {
                        strAlbum = metaSessao2->strAlbum;
                    }
                    if (!metaSessao2->arrCapaBytes.empty())
                    {
                        _capaBytes = metaSessao2->arrCapaBytes;
                        std::vector<uint8_t> arrCapa = _capaBytes;
                        Base::Despacha([this, arrCapa] {
                            ExibirCapaNaUi(arrCapa);
                            try
                            {
                                std::string strCapaTrabalho = Base::Combina(PastaTrabalho, "cover.jpg");
                                Base::GravaBytes(strCapaTrabalho, arrCapa.data(), arrCapa.size());
                                _caminhoCapaSalva = strCapaTrabalho;
                            }
                            catch (...) {}
                        });
                    }
                    if (!metaSessao2->lstTitulosFaixas.empty())
                    {
                        std::vector<std::string> lstArtistasS2 = metaSessao2->lstArtistasFaixas;
                        std::vector<std::string> lstTitulosS2 = comCdText(metaSessao2->lstTitulosFaixas, lstArtistasS2);
                        ProcessadorExtracaoAudio::AtualizarCueComTitulos(strDestinoCue, strArtista, strAlbum, lstTitulosS2,
                                                                         lstArtistasS2, metaSessao2->lstCompositoresFaixas,
                                                                         [this](const std::string &msg) { RegistrarDeOutroFio(msg); });
                    }
                    Base::Despacha([this, strArtista, strAlbum] {
                        if (!strArtista.empty()) CaixaArtista->Text = strArtista;
                        if (!strAlbum.empty()) CaixaAlbum->Text = strAlbum;
                    });
                }
            }
            catch (const std::exception &exSessao2)
            {
                std::string strMsg = exSessao2.what();
                Base::Despacha([this, strMsg] {
                    Registrar("Aviso na mineração da Sessão 2: " + strMsg);
                });
            }

            try
            {
                std::vector<TrackInfo> lstFaixasPre = ProcessadorExtracaoAudio::ParsearCue(strDestinoCue);
                // Sem .bin, o fim do disco vem do REM LEAD-OUT do CUE; sem nenhum dos
                // dois, o GnuDB recusa em vez de consultar com um fim inventado.
                int64_t lngTamBin = Base::ArquivoExiste(strDestinoBin) ? Base::Tamanho(strDestinoBin) : 0;

                if (!lstFaixasPre.empty())
                {
                    std::string strGArtist, strGAlbum, strGGenre, strGYear;
                    std::vector<std::string> lstGTracks;
                    std::string strResGnu = GnuDbResolver::ResolveToc(strDestinoCue, lngTamBin, strGArtist, strGAlbum, strGGenre, strGYear, lstGTracks);

                    std::string strCandArtista = !strArtista.empty() ? strArtista : strGArtist;
                    std::string strCandAlbum = !strAlbum.empty() ? strAlbum : strGAlbum;

                    if (!strCandAlbum.empty())
                    {
                        if (m_objIndicador) m_objIndicador->DefinirFaseRapida("CONSULTANDO METADADOS…", "Consultando Discogs para títulos oficiais");
                        size_t intQtdPre = lstFaixasPre.size();
                        Base::Despacha([this, intQtdPre] {
                            Registrar("Consultando Discogs para fatiamento com títulos oficiais (" + std::to_string(intQtdPre) + " faixas)...");
                        });

                        std::shared_ptr<TDiscogsReleaseInfo> discogsPre = DiscogsClient::ConsultarLancamento(strCandArtista, strCandAlbum, (int) lstFaixasPre.size(),
                                                                                                            [this](const std::string &logMsg) { RegistrarDeOutroFio(logMsg); },
                                                                                                            DuracoesDoCue(strDestinoCue, lngTamBin));

                        if (discogsPre)
                        {
                            if (m_objIndicador) m_objIndicador->DefinirFaseRapida("DISCOGS CONFIRMADO", std::to_string(!discogsPre->lstTrackTitles.empty() ? discogsPre->lstTrackTitles.size() : lstFaixasPre.size()) + " faixas oficiais");
                            if (!discogsPre->strArtist.empty()) strArtista = discogsPre->strArtist;
                            if (!discogsPre->strAlbum.empty()) strAlbum = discogsPre->strAlbum;
                            if (!discogsPre->arrCoverBytes.empty())
                            {
                                _capaBytes = discogsPre->arrCoverBytes;
                                std::vector<uint8_t> arrCapa = _capaBytes;
                                Base::Despacha([this, arrCapa] {
                                    ExibirCapaNaUi(arrCapa);
                                    try
                                    {
                                        std::string strCapaTrabalho = Base::Combina(PastaTrabalho, "cover.jpg");
                                        Base::GravaBytes(strCapaTrabalho, arrCapa.data(), arrCapa.size());
                                        _caminhoCapaSalva = strCapaTrabalho;
                                    }
                                    catch (...) {}
                                });
                            }

                            if (!discogsPre->lstTrackTitles.empty())
                            {
                                std::vector<std::string> lstArtistasD = discogsPre->lstTrackArtists;
                                std::vector<std::string> lstTitulosD = comCdText(discogsPre->lstTrackTitles, lstArtistasD);
                                ProcessadorExtracaoAudio::AtualizarCueComTitulos(strDestinoCue, strArtista, strAlbum, lstTitulosD, lstArtistasD,
                                                                                 [this](const std::string &msg) { RegistrarDeOutroFio(msg); });
                            }

                            Base::Despacha([this, strArtista, strAlbum] {
                                if (!strArtista.empty()) CaixaArtista->Text = strArtista;
                                if (!strAlbum.empty()) CaixaAlbum->Text = strAlbum;
                            });
                        }
                        else
                        {
                            // Fallback para Apple Music / iTunes caso Discogs não tenha o lançamento ou capa
                            try
                            {
                                // Com o número de faixas do disco: sem ele, qualquer álbum de
                                // mesmo nome servia, e os títulos de outro disco iam para este.
                                std::shared_ptr<TItunesAlbumInfo> itunesPre = ItunesResolver::ConsultarAlbum(
                                    strCandArtista != "Desconhecido" ? strCandArtista : "", strCandAlbum, (int) lstFaixasPre.size(),
                                    [this](const std::string &logMsg) { RegistrarDeOutroFio(logMsg); });

                                if (itunesPre)
                                {
                                    if (m_objIndicador) m_objIndicador->DefinirFaseRapida("APPLE MUSIC CONFIRMADO", std::to_string(!itunesPre->lstFaixas.empty() ? itunesPre->lstFaixas.size() : lstFaixasPre.size()) + " faixas oficiais");
                                    if (strArtista.empty() && !itunesPre->strArtistName.empty()) strArtista = itunesPre->strArtistName;
                                    if (strAlbum.empty() && !itunesPre->strCollectionName.empty()) strAlbum = itunesPre->strCollectionName;

                                    std::string strUrlArt = PrimeiroNaoVazio({itunesPre->strArtworkUrl1400, itunesPre->strArtworkUrl600});
                                    if (!strUrlArt.empty())
                                    {
                                        std::vector<uint8_t> arrCapaItunes = ExternalMetadataClient::FetchImage(strUrlArt);
                                        if (!arrCapaItunes.empty())
                                        {
                                            _capaBytes = arrCapaItunes;
                                            std::vector<uint8_t> arrCapa = _capaBytes;
                                            Base::Despacha([this, arrCapa] {
                                                ExibirCapaNaUi(arrCapa);
                                                try
                                                {
                                                    std::string strCapaTrabalho = Base::Combina(PastaTrabalho, "cover.jpg");
                                                    Base::GravaBytes(strCapaTrabalho, arrCapa.data(), arrCapa.size());
                                                    _caminhoCapaSalva = strCapaTrabalho;
                                                }
                                                catch (...) {}
                                            });
                                        }
                                    }

                                    if (!itunesPre->lstFaixas.empty())
                                    {
                                        std::vector<std::string> lstItunesTracks;
                                        std::vector<std::string> lstItunesArtists;
                                        for (const TItunesTrackInfo &f : itunesPre->lstFaixas)
                                        {
                                            lstItunesTracks.push_back(f.strTrackName);
                                            lstItunesArtists.push_back(f.strArtistName);
                                        }
                                        lstItunesTracks = comCdText(lstItunesTracks, lstItunesArtists);
                                        ProcessadorExtracaoAudio::AtualizarCueComTitulos(strDestinoCue, strArtista, strAlbum, lstItunesTracks, lstItunesArtists,
                                                                                         [this](const std::string &msg) { RegistrarDeOutroFio(msg); });
                                    }

                                    Base::Despacha([this, strArtista, strAlbum] {
                                        if (!strArtista.empty()) CaixaArtista->Text = strArtista;
                                        if (!strAlbum.empty()) CaixaAlbum->Text = strAlbum;
                                    });
                                }
                                else if (strResGnu == "OK" && !lstGTracks.empty())
                                {
                                    if (strArtista.empty()) strArtista = strGArtist;
                                    if (strAlbum.empty()) strAlbum = strGAlbum;

                                    std::vector<std::string> lstArtistasG;
                                    std::vector<std::string> lstTitulosG = comCdText(lstGTracks, lstArtistasG);
                                    ProcessadorExtracaoAudio::AtualizarCueComTitulos(strDestinoCue, strArtista, strAlbum, lstTitulosG, lstArtistasG,
                                                                                     [this](const std::string &msg) { RegistrarDeOutroFio(msg); });

                                    Base::Despacha([this, strArtista, strAlbum] {
                                        if (!strArtista.empty()) CaixaArtista->Text = strArtista;
                                        if (!strAlbum.empty()) CaixaAlbum->Text = strAlbum;
                                    });
                                }
                            }
                            catch (...) {}
                        }
                    }
                }
            }
            catch (const std::exception &exPre)
            {
                std::string strMsg = exPre.what();
                Base::Despacha([this, strMsg] {
                    Registrar("Aviso na identificação prévia: " + strMsg);
                });
            }

            // 3. Conferência bit-perfect da leitura: offset do drive,
            //    AccurateRip e releitura do que ele não confirmar. Antes
            //    do corte, porque é ela que decide a correção de offset.
            if (!objRelatorioSpti) objRelatorioSpti = std::make_shared<TRelatorioExtracao>();
            std::function<void(const std::string &)> actRegistrar = [this](const std::string &m) { RegistrarDeOutroFio(m); };
            int intOffsetCorte = 0;
            try
            {
                intOffsetCorte = ConferenciaBitPerfect::Preparar(extractor, strDrive, PastaTrabalho, *objRelatorioSpti, actRegistrar,
                    [this](const std::string &strVerbo, const std::string &strDetalhe) { if (m_objIndicador) m_objIndicador->DefinirFaseRapida(strVerbo, strDetalhe); },
                    [this](const std::string &strTitulo, double dblPct, const std::string &strDetalhe) { if (m_objIndicador) m_objIndicador->AtualizarProgresso(strTitulo, dblPct, strDetalhe); });
            }
            catch (const std::exception &exConf)
            {
                actRegistrar("Aviso: a conferência bit-perfect falhou (" + std::string(exConf.what()) + "). As faixas sairão sem ela.");
            }
            std::vector<TConferenciaWma> lstConferenciaWma;

            // 4. Fatiamento e codificação para WMA Lossless
            // LINUX: "CODIFICANDO WMA LOSSLESS" e "Iniciando codificação WMA 9 Lossless" no original
            Base::Despacha([this] {
                TxtExtracao->Text = "fatiando faixas…";
                TxtFaixasTitulo->Text = "CODIFICANDO FLAC…";
                if (m_objIndicador)
                {
                    m_objIndicador->AtualizarProgresso("CODIFICANDO FLAC", 0.0, std::string("Iniciando codificação FLAC (Bit-Perfect)..."));
                }
            });

            bool blnFatiou = ProcessadorExtracaoAudio::FatiarEConverterParaWma(
                strDestinoCue,
                !strArtista.empty() ? strArtista : "",
                !strAlbum.empty() ? strAlbum : "",
                "",
                "",
                _capaBytes,
                [this](const std::string &msg) {
                    RegistrarDeOutroFio(msg);
                },
                [this](int trk, int total, double pct) {
                    Base::Despacha([this, trk, total, pct] {
                        double dblGlobal = total > 0 ? (((trk - 1) + (pct / 100.0)) / total * 100.0) : pct;
                        TxtExtracao->Text = "faixa " + std::to_string(trk) + "/" + std::to_string(total) + " (" + F(pct, 0) + "%)";
                        if (m_objIndicador)
                        {
                            m_objIndicador->AtualizarProgresso(
                                "CODIFICANDO FLAC",
                                dblGlobal,
                                "Faixa " + D2(trk) + " de " + D2(total) + " (" + F(pct, 0) + "%)");
                        }
                    });
                },
                intOffsetCorte,
                &lstConferenciaWma);

            if (!blnFatiou)
            {
                throw Base::InvalidOperationException("Falha ao fatiar e codificar faixas de áudio em FLAC.");
            }

            // 5. Veredito por faixa, gravado nas próprias faixas e no relatório.
            try { ConferenciaBitPerfect::Concluir(PastaTrabalho, *objRelatorioSpti, lstConferenciaWma, actRegistrar); }
            catch (const std::exception &exConc) { actRegistrar("Aviso: não consegui concluir a conferência bit-perfect: " + std::string(exConc.what())); }

            // 6. CD Extra: os vídeos da sessão de dados, copiados da unidade
            //    montada e conferidos (o .bin só tem o áudio).
            try
            {
                std::string strBinDados = Base::Combina(PastaTrabalho, "AudioCD.bin");
                std::shared_ptr<TTocAudio> objTocDados = Base::ArquivoExiste(strBinDados) ? TTocAudio::DeCue(strDestinoCue, Base::Tamanho(strBinDados)) : nullptr;
                if (objTocDados && objTocDados->intInicioDados >= 0)
                {
                    if (m_objIndicador) m_objIndicador->DefinirFaseRapida("COPIANDO VÍDEOS DO CD EXTRA…", "Cada vídeo é lido duas vezes e conferido");
                    // LINUX: chrDrive + @":\" no original (a unidade, que o Windows já montava);
                    // aqui a sessão de dados montada só leitura enquanto copia.
                    TSessaoDadosMontada objSessao(strDrive);
                    TCdExtraExtractor::CopiarVideosDaUnidade(objSessao.Raiz(), PastaTrabalho, actRegistrar);
                }
            }
            catch (const std::exception &exVid) { actRegistrar("Aviso: não consegui copiar os vídeos do CD Extra: " + std::string(exVid.what())); }

            Base::Despacha([this, objRelatorioSpti] {
                TxtExtracao->Text = "concluído";
                if (m_objIndicador)
                {
                    m_objIndicador->DefinirFaseRapida("CODIFICAÇÃO CONCLUÍDA", "Conferindo integridade dos arquivos gerados...");
                }
                _ocupado = false;
                BtnExtrairCd->IsEnabled = true;
                BtnIdentificar->IsEnabled = true;
                if (objRelatorioSpti && !objRelatorioSpti->Exata())
                {
                    TxtExtracao->Text = "concluído — NÃO EXATO";
                    Registrar("Extração concluída, mas não é cópia exata do disco. Conferindo manifesto...");
                    MessageBox::Show(
                        std::to_string(objRelatorioSpti->intSetoresMascarados) + " setor(es) do disco não puderam ser lidos e foram substituídos por silêncio:\r\n\r\n" +
                            objRelatorioSpti->ResumoPorFaixa() + "\r\n\r\n" +
                            "Essas faixas não são cópia exata do CD. Você pode limpar o disco e extrair de novo, "
                            "ou aprovar assim — a aprovação vai lembrar disto e o ledger vai registrar.",
                        "Extração não exata", MessageBoxButton::OK, MessageBoxImage::Warning);
                }
                else if (objRelatorioSpti && !objRelatorioSpti->dicVerificacao.empty()
                         && objRelatorioSpti->QtdComprovadas() == (int) objRelatorioSpti->dicVerificacao.size())
                {
                    TxtExtracao->Text = "concluído — BIT-PERFECT";
                    Registrar("Extração concluída: todas as faixas comprovadas bit-perfect. Conferindo manifesto...");
                }
                else if (objRelatorioSpti && !objRelatorioSpti->dicVerificacao.empty())
                {
                    bool blnAlgumaNao = false;
                    for (const auto &v : objRelatorioSpti->dicVerificacao)
                        if (v.second.Nivel() == NivelBitPerfect::NaoComprovada) blnAlgumaNao = true;
                    TxtExtracao->Text = blnAlgumaNao ? "concluído — NÃO COMPROVADO" : "concluído — lido duas vezes";
                    Registrar("Extração concluída. Bit-perfect: " + objRelatorioSpti->ResumoBitPerfect() + ".");
                    // LINUX: "no atributo ARCA/Verification" no original; no FLAC o campo é ARCA_VERIFICATION
                    if (blnAlgumaNao)
                        MessageBox::Show("Bit-perfect: " + objRelatorioSpti->ResumoBitPerfect() + ".\r\n\r\n" +
                                             "Você pode limpar o disco e extrair de novo, ou aprovar assim — a aprovação vai lembrar disto, "
                                             "e cada faixa leva o veredito no atributo ARCA_VERIFICATION.",
                                         "Extração não comprovada", MessageBoxButton::OK, MessageBoxImage::Warning);
                }
                else
                {
                    Registrar("Extração concluída com sucesso. Conferindo manifesto...");
                }

                // Se ainda não houver metadados, capa ou se as faixas forem genéricas, identifica automaticamente
                bool blnTemFaixasGenericas = false;
                try
                {
                    // LINUX: "*.wma" no original
                    for (const std::string &strArquivoWma : Base::Arquivos(PastaTrabalho, ".flac"))
                    {
                        if (Base::Contem(Base::NomeDoArquivo(strArquivoWma), "Faixa", true))
                        {
                            blnTemFaixasGenericas = true;
                            break;
                        }
                    }
                }
                catch (...) {}

                bool blnFaltaCapaPos = _capaBytes.empty() && !Base::ArquivoExiste(Base::Combina(PastaTrabalho, "cover.jpg"));

                if (VazioAposTrim(CaixaArtista->Text()) || VazioAposTrim(CaixaAlbum->Text()) || blnTemFaixasGenericas || blnFaltaCapaPos)
                {
                    IdentificarInterno(false);
                }
                else
                {
                    Reler();
                }
            });
        }
        catch (const std::exception &exGeral)
        {
            std::string strMsg = exGeral.what();
            Base::Despacha([this, strMsg] {
                _ocupado = false;
                BtnExtrairCd->IsEnabled = true;
                BtnIdentificar->IsEnabled = true;
                TxtExtracao->Text = "erro na extração";
                // ARCA-ORIGINAL: o TxtFaixasTitulo não volta: fica "EXTRAINDO CD DE ÁUDIO…" ou
                // "CODIFICANDO FLAC…" sobre a lista vazia até a próxima releitura.
                _blnErroNoMedidor = true; if (m_objIndicador) m_objIndicador->DefinirFaseRapida("ERRO NA EXTRAÇÃO", strMsg);
                Registrar("ERRO CRÍTICO NA EXTRAÇÃO: " + strMsg);
                // LINUX: "Erro SPTI" no original
                MessageBox::Show("Erro durante a extração do CD:\r\n" + strMsg, "Erro SG_IO", MessageBoxButton::OK, MessageBoxImage::Error);
                AvaliarPorta();
            });
        }
    }).detach();
}

void AquisicaoView::AoReler(RoutedEventArgs &) { Reler(); }

/// Lê o que está na área de trabalho e **congela o manifesto**: nome,
/// tamanho e SHA-256 de cada faixa. A partir daqui, o que se aprova é
/// exatamente isto — se algum arquivo mudar, a integração recusa.
void AquisicaoView::Reler()
{
    if (_ocupado) return;
    _manifesto.clear();
    ListaFaixas->ItemsSource = FonteDeItens();
    AvaliarPorta();

    try { if (!Base::PastaExiste(PastaTrabalho)) Base::CriaPasta(PastaTrabalho); }
    catch (const std::exception &ex) { Registrar(std::string("Não consegui criar a área de trabalho: ") + ex.what()); return; }

    _ocupado = true;
    _blnErroNoMedidor = false;
    TxtFaixasTitulo->Text = "CONFERINDO…";
    Registrar("Lendo a área de trabalho e congelando o manifesto…");
    if (m_objIndicador) m_objIndicador->DefinirFaseRapida("CONGELANDO MANIFESTO…", "Calculando SHA-256 e integridade");

    std::string pasta = PastaTrabalho;
    std::thread([this, pasta] {
        std::vector<ItemManifesto> m;
        std::string erro;            // "" = null
        bool blnErro = false;
        try
        {
            // LINUX: "*.wma" no original
            m = Integracao::CongelarManifesto(pasta, "*.flac", nullptr);
        }
        catch (const std::exception &ex) { erro = ex.what(); blnErro = true; }

        Base::Despacha([this, m, erro, blnErro] {
            _ocupado = false;
            if (blnErro)
            {
                Registrar("Falhou: " + erro);
                TxtFaixasTitulo->Text = "ERRO";
                _blnErroNoMedidor = true; if (m_objIndicador) m_objIndicador->DefinirFaseRapida("ERRO NO MANIFESTO", erro);
                AvaliarPorta();   // os botões voltam mesmo com o manifesto falhando
                return;
            }

            _manifesto = m;
            MostrarFaixas();
        });
    }).detach();
}

void AquisicaoView::MostrarFaixas()
{
    std::vector<LinhaFaixa> linhas;
    int intQtdAudio = 0;
    int intQtdVideo = 0;

    {
        const std::vector<std::string> arrExtsVideo = {".mpg", ".mpeg", ".avi", ".mov", ".wmv", ".mp4", ".mkv"};
        for (size_t i = 0; i < _manifesto.size(); i++)
        {
            std::string strNomeExibicao = _manifesto[i].Nome;
            std::string strExt = Base::Minusculas(Base::Extensao(strNomeExibicao));
            bool blnIsVideo = std::find(arrExtsVideo.begin(), arrExtsVideo.end(), strExt) != arrExtsVideo.end();

            if (Integracao::EhArtwork(strNomeExibicao))
            {
                // Capa/encarte: aparece na conferência, mas não conta como faixa.
                strNomeExibicao = "[Encarte] " + strNomeExibicao.substr(std::string(Integracao::PastaArtwork).size() + 1);
            }
            else if (blnIsVideo)
            {
                intQtdVideo++;
                if (!Base::ComecaCom(strNomeExibicao, "[Vídeo]", true))
                {
                    strNomeExibicao = "[Vídeo] " + strNomeExibicao;
                }
            }
            else
            {
                intQtdAudio++;
            }

            LinhaFaixa l;
            l.Numero = (int) i + 1;
            l.Nome = strNomeExibicao;
            l.Tamanho = Formato::Num(_manifesto[i].Tamanho / 1048576.0, 1) + " MB";
            l.Hash = Base::Sub(_manifesto[i].Sha256, 0, 16) + "…";
            linhas.push_back(l);
        }
    }

    ListaFaixas->ItemsSource = FonteDeItens();
    _lstLinhas = linhas;
    ListaFaixas->ItemsSource = FonteDeItens::De(&_lstLinhas);

    if (linhas.empty())
    {
        TxtFaixasTitulo->Text = "A ÁREA DE TRABALHO ESTÁ VAZIA";
        Registrar("Nada na área de trabalho.");
        if (m_objIndicador) m_objIndicador->Resetar("ÁREA DE TRABALHO VAZIA", "Nenhuma faixa pendente");
    }
    else
    {
        if (intQtdVideo > 0)
        {
            TxtFaixasTitulo->Text = std::to_string(intQtdAudio) + " FAIXAS + " + std::to_string(intQtdVideo) + " VÍDEO(S) CONFERIDOS";
            Registrar(std::to_string(intQtdAudio) + " faixas de áudio e " + std::to_string(intQtdVideo) + " arquivo(s) de vídeo conferidos e manifesto congelado.");
        }
        else
        {
            TxtFaixasTitulo->Text = std::to_string(linhas.size()) + (linhas.size() == 1 ? " FAIXA CONFERIDA" : " FAIXAS CONFERIDAS");
            Registrar(std::to_string(linhas.size()) + " faixas conferidas e manifesto congelado.");
        }
    }

    MarcarPasso(linhas.empty() ? 0 : 1);
    AvaliarPorta();

    // ===============================================================
    // RETOMADA INTELIGENTE DE CAPTURA ANTERIOR NA ÁREA DE TRABALHO
    // ===============================================================
    if (!linhas.empty() && !_ocupado)
    {
        // 1. Carrega a capa local se já existir cover.jpg
        if (_capaBytes.empty())
        {
            std::string strCaminhoCapa = Base::Combina(PastaTrabalho, "cover.jpg");
            if (Base::ArquivoExiste(strCaminhoCapa))
            {
                try
                {
                    std::vector<uint8_t> arrBytesCapa = Base::LeBytes(strCaminhoCapa);
                    if (!arrBytesCapa.empty())
                    {
                        _capaBytes = arrBytesCapa;
                        _caminhoCapaSalva = strCaminhoCapa;
                        ExibirCapaNaUi(arrBytesCapa);
                    }
                }
                catch (...) {}
            }
        }

        // 2. Tenta restaurar Artista e Álbum a partir das tags ASF dos arquivos WMA existentes
        // LINUX: as etiquetas dos FLAC, pelos mesmos nomes ASF (o EditorFlac os traduz).
        if (VazioAposTrim(CaixaArtista->Text()) || VazioAposTrim(CaixaAlbum->Text()))
        {
            try
            {
                std::vector<std::string> arrWmas = Base::Arquivos(PastaTrabalho, ".flac");
                if (!arrWmas.empty())
                {
                    EditorFlac editor;
                    if (editor.Abrir(arrWmas[0]))
                    {
                        std::vector<Interop::AtributoFlac> lstAtribs = editor.LerTudo();
                        std::string strArtTag;
                        std::string strAlbTag;
                        for (const Interop::AtributoFlac &at : lstAtribs)
                        {
                            if (Base::IgualSemCaixa(at.Nome, "Author") || Base::IgualSemCaixa(at.Nome, "WM/AlbumArtist"))
                            {
                                if (strArtTag.empty() && !at.Valor.empty()) strArtTag = at.Valor;
                            }
                            else if (Base::IgualSemCaixa(at.Nome, "WM/AlbumTitle"))
                            {
                                if (strAlbTag.empty() && !at.Valor.empty()) strAlbTag = at.Valor;
                            }
                        }

                        if (!strArtTag.empty() && VazioAposTrim(CaixaArtista->Text()))
                        {
                            CaixaArtista->Text = strArtTag;
                        }
                        if (!strAlbTag.empty() && VazioAposTrim(CaixaAlbum->Text()))
                        {
                            CaixaAlbum->Text = strAlbTag;
                        }
                    }
                }
            }
            catch (...) {}
        }

        // 2.1. Se ainda faltar Artista ou Álbum, tenta restaurar a partir do AudioCD.cue existente
        if (VazioAposTrim(CaixaArtista->Text()) || VazioAposTrim(CaixaAlbum->Text()))
        {
            try
            {
                std::string strCaminhoCue = Base::Combina(PastaTrabalho, "AudioCD.cue");
                if (Base::ArquivoExiste(strCaminhoCue))
                {
                    std::string strAlbumCue, strArtistaCue;
                    ProcessadorExtracaoAudio::ParsearCue(strCaminhoCue, strAlbumCue, strArtistaCue);
                    if (!strArtistaCue.empty() && VazioAposTrim(CaixaArtista->Text()))
                    {
                        CaixaArtista->Text = strArtistaCue;
                    }
                    if (!strAlbumCue.empty() && VazioAposTrim(CaixaAlbum->Text()))
                    {
                        CaixaAlbum->Text = strAlbumCue;
                    }
                }
            }
            catch (...) {}
        }
    }
}

// ===============================================================
/// Link exato na busca assistida vai direto àquele lançamento. Sem
/// link, consulta Apple Music e Discogs e deixa a pessoa escolher.
///
/// Início e fim de toda identificação, num lugar só. Identificar
/// renomeia e reetiqueta as faixas da área de trabalho, então enquanto
/// ela roda nem Extrair (que apaga a área de trabalho) nem Aprovar (que
/// a leva ao acervo) podem começar. No fim, Aprovar volta pelo
/// AvaliarPorta, que confere se há o que aprovar.
// ARCA-ORIGINAL: o primeiro <summary> acima (o do link exato) ficou solto em cima do
// DefinirEmIdentificacao, sem método; é do AoIdentificar.
void AquisicaoView::DefinirEmIdentificacao(bool blnEm)
{
    _blnEmIdentificacao = blnEm;
    if (blnEm) _blnErroNoMedidor = false;
    BtnIdentificar->IsEnabled = !blnEm && !_ocupado;
    BtnExtrairCd->IsEnabled = !blnEm && !_ocupado;
    BtnDescartar->IsEnabled = !blnEm && !_ocupado;
    if (blnEm) BtnAprovar->IsEnabled = false;
}

void AquisicaoView::AoIdentificar(RoutedEventArgs &)
{
    if (_blnEmIdentificacao || _ocupado) return;

    std::string strBusca = CaixaBuscaAlbum ? Base::Trim(CaixaBuscaAlbum->Text()) : "";
    if (DiscogsClient::EhLink(strBusca) || ItunesResolver::EhLinkOuId(strBusca))
        IdentificarInterno(true);
    else
        IdentificarComparando();
}

void AquisicaoView::IdentificarInterno(bool bManual)
{
    if (_blnEmIdentificacao) return;
    if (!bManual && _blnAutoIdentificacaoJaExecutada) return;

    DefinirEmIdentificacao(true);
    if (!bManual) _blnAutoIdentificacaoJaExecutada = true;

    std::string strBusca = CaixaBuscaAlbum ? Base::Trim(CaixaBuscaAlbum->Text()) : "";

    BtnIdentificar->IsEnabled = false;
    TxtIdentificacao->Text = "buscando metadados…";
    Registrar("Consultando serviços de identificação (Apple Music, Discogs, GnuDB)...");
    if (m_objIndicador) m_objIndicador->DefinirFaseRapida("CONSULTANDO METADADOS…", "Apple Music, Discogs e GnuDB");

    std::string strCaminhoCue = Base::Combina(PastaTrabalho, "AudioCD.cue");
    std::string strCaminhoBin = Base::Combina(PastaTrabalho, "AudioCD.bin");

    // Determina a quantidade esperada de faixas
    int intQtdFaixasEsperadas = 0;
    if (Base::ArquivoExiste(strCaminhoCue))
    {
        try
        {
            std::vector<TrackInfo> lstFaixasCue = ProcessadorExtracaoAudio::ParsearCue(strCaminhoCue);
            intQtdFaixasEsperadas = (int) lstFaixasCue.size();
        }
        catch (...) {}
    }
    if (intQtdFaixasEsperadas == 0 && Base::PastaExiste(PastaTrabalho))
    {
        try
        {
            // LINUX: "*.wma" no original
            intQtdFaixasEsperadas = (int) Base::Arquivos(PastaTrabalho, ".flac").size();
        }
        catch (...) {}
    }

    std::string strArtistaInicial;     // "" = null
    std::string strAlbumInicial;
    if (CaixaArtista && !VazioAposTrim(CaixaArtista->Text()))
    {
        strArtistaInicial = Base::Trim(CaixaArtista->Text());
    }
    if (CaixaAlbum && !VazioAposTrim(CaixaAlbum->Text()))
    {
        strAlbumInicial = Base::Trim(CaixaAlbum->Text());
    }

    std::thread([this, bManual, strBusca, strCaminhoCue, strCaminhoBin, intQtdFaixasEsperadas, strArtistaInicial, strAlbumInicial] {
        std::string strArtistaIdentificado = strArtistaInicial;
        std::string strAlbumIdentificado = strAlbumInicial;
        std::vector<uint8_t> arrCapa;                            // vazio = null
        std::vector<std::string> lstTitulosIdentificados;       // vazio = null
        std::vector<std::string> lstArtistasFaixasIdentificados;
        std::vector<std::string> lstCompositoresIdentificados;
        // Link exato (Discogs ou Apple Music) colado na busca: o
        // usuário já identificou o disco. Nada depois pode trocar o
        // lançamento por outro "parecido" achado por texto.
        bool blnFonteExata = false;

        try
        {
            // 0. Mineração da Sessão de Dados local de Enhanced CD / Copy Control
            try
            {
                if (m_objIndicador) m_objIndicador->DefinirFaseRapida("MINERANDO CD EXTRA…", "Sessão de Dados / Multimídia local");
                std::optional<TMetadadosCdExtra> metaSessao;   // nullopt = null
                std::string strDriveDetect;
                Base::DespachaEEspera([&] {
                    if (ListaUnidades && ListaUnidades->TemSelecao())
                    {
                        std::string strSel = ListaUnidades->SelectedItem();
                        // LINUX: strSel.Substring(0, 1) + @":\" no original (a raiz da unidade);
                        // aqui o dispositivo, cuja sessão de dados é montada logo abaixo.
                        if (!strSel.empty()) strDriveDetect = DispositivoDoItem(strSel);
                    }
                });

                if (!strDriveDetect.empty())
                {
                    // LINUX: Directory.Exists(strDriveDetect) e a mineração na unidade, no original
                    TSessaoDadosMontada objSessao(strDriveDetect);
                    if (!objSessao.Raiz().empty())
                    {
                        metaSessao.emplace();
                        TCdExtraExtractor::MinerarSessaoDados(objSessao.Raiz(), [this](const std::string &m) { RegistrarDeOutroFio(m); }, *metaSessao);
                    }
                }
                if ((!metaSessao || metaSessao->lstTitulosFaixas.empty()) && Base::PastaExiste(PastaTrabalho))
                {
                    metaSessao.emplace();
                    TCdExtraExtractor::MinerarSessaoDados(PastaTrabalho, [this](const std::string &m) { RegistrarDeOutroFio(m); }, *metaSessao);
                }
                // Sem fallback para pasta fixa (era Z:\TEMP): ver a extração.

                if (metaSessao)
                {
                    if (!metaSessao->strArtistaAlbum.empty() && strArtistaIdentificado.empty())
                    {
                        strArtistaIdentificado = metaSessao->strArtistaAlbum;
                    }
                    if (!metaSessao->strAlbum.empty() && strAlbumIdentificado.empty())
                    {
                        strAlbumIdentificado = metaSessao->strAlbum;
                    }
                    if (!metaSessao->arrCapaBytes.empty() && arrCapa.empty())
                    {
                        arrCapa = metaSessao->arrCapaBytes;
                    }
                    if (!metaSessao->lstTitulosFaixas.empty() && lstTitulosIdentificados.empty())
                    {
                        lstTitulosIdentificados = metaSessao->lstTitulosFaixas;
                        lstArtistasFaixasIdentificados = metaSessao->lstArtistasFaixas;
                        lstCompositoresIdentificados = metaSessao->lstCompositoresFaixas;
                    }
                }
            }
            catch (...) {}

            // 1a. Link de lançamento do Discogs: lê exatamente aquele.
            //     Se falhar, ConsultarPorLink lança e a identificação
            //     para com a mensagem — não cai numa busca por texto.
            if (DiscogsClient::EhLink(strBusca))
            {
                if (m_objIndicador) m_objIndicador->DefinirFaseRapida("CONSULTANDO DISCOGS…", "lançamento indicado pelo link");
                Base::Despacha([this, strBusca] { Registrar("Link do Discogs: " + strBusca); });
                std::shared_ptr<TDiscogsReleaseInfo> objDiscogsLink = DiscogsClient::ConsultarPorLink(strBusca, [this](const std::string &logMsg) {
                    RegistrarDeOutroFio(logMsg);
                });

                strArtistaIdentificado = objDiscogsLink->strArtist;
                strAlbumIdentificado = objDiscogsLink->strAlbum;
                lstTitulosIdentificados = objDiscogsLink->lstTrackTitles;
                lstArtistasFaixasIdentificados = objDiscogsLink->lstTrackArtists;
                lstCompositoresIdentificados = std::any_of(objDiscogsLink->lstTrackComposers.begin(), objDiscogsLink->lstTrackComposers.end(),
                                                           [](const std::string &c) { return !c.empty(); })
                    ? objDiscogsLink->lstTrackComposers : std::vector<std::string>();
                if (!objDiscogsLink->arrCoverBytes.empty())
                    arrCapa = objDiscogsLink->arrCoverBytes;
                blnFonteExata = true;

                int intQtdLink = (int) objDiscogsLink->lstTrackTitles.size();
                if (intQtdFaixasEsperadas > 0 && intQtdLink != intQtdFaixasEsperadas)
                {
                    Base::Despacha([this, intQtdLink, intQtdFaixasEsperadas] {
                        Registrar("ATENÇÃO: o lançamento do link tem " + std::to_string(intQtdLink) + " faixas e o disco tem " +
                                  std::to_string(intQtdFaixasEsperadas) + ". Confira antes de aprovar.");
                    });
                }
            }
            // 1b. Texto na busca assistida, ou link/ID do Apple Music:
            else if (!strBusca.empty())
            {
                if (m_objIndicador) m_objIndicador->DefinirFaseRapida("CONSULTANDO APPLE MUSIC…", strBusca);
                Base::Despacha([this, strBusca] { Registrar("Buscando no Apple Music por: " + strBusca); });
                std::shared_ptr<TItunesAlbumInfo> itunesAlbum = ItunesResolver::ConsultarPorUrlOuId(strBusca, intQtdFaixasEsperadas, [this](const std::string &logMsg) {
                    RegistrarDeOutroFio(logMsg);
                });

                if (itunesAlbum)
                {
                    // Só link ou ID é exato; um termo de busca ainda pode
                    // ser refinado pelos passos seguintes.
                    blnFonteExata = ItunesResolver::EhLinkOuId(strBusca);
                    strArtistaIdentificado = itunesAlbum->strArtistName;
                    strAlbumIdentificado = itunesAlbum->strCollectionName;

                    if (!itunesAlbum->lstFaixas.empty())
                    {
                        lstTitulosIdentificados.clear();
                        lstArtistasFaixasIdentificados.clear();
                        for (const TItunesTrackInfo &f : itunesAlbum->lstFaixas)
                        {
                            lstTitulosIdentificados.push_back(f.strTrackName);
                            lstArtistasFaixasIdentificados.push_back(f.strArtistName);
                        }
                    }

                    std::string strUrlCapa = PrimeiroNaoVazio({itunesAlbum->strArtworkUrl1400, itunesAlbum->strArtworkUrl600, itunesAlbum->strArtworkUrl100});
                    if (!strUrlCapa.empty())
                    {
                        try { arrCapa = ExternalMetadataClient::FetchImage(strUrlCapa); } catch (...) {}
                    }
                }
            }

            // DECISAO-LORENZO (08/11/2026): o CD-TEXT guardado na extracao
            TCdText cdText = LerCdText(PastaTrabalho);
            if (!bManual && !blnFonteExata)
            {
                if (strArtistaIdentificado.empty()) strArtistaIdentificado = cdText.strArtista;
                if (strAlbumIdentificado.empty()) strAlbumIdentificado = cdText.strAlbum;
            }

            // 2. Se não foi informado Artista ou Álbum, tenta descobrir a partir do CUE ou GnuDB:
            if (!bManual && !blnFonteExata && (strArtistaIdentificado.empty() || strAlbumIdentificado.empty()) && Base::ArquivoExiste(strCaminhoCue))
            {
                std::string strAlbumCue, strArtistaCue;
                std::vector<TrackInfo> lstFaixasCue = ProcessadorExtracaoAudio::ParsearCue(strCaminhoCue, strAlbumCue, strArtistaCue);

                if (strArtistaIdentificado.empty() && !strArtistaCue.empty())
                    strArtistaIdentificado = strArtistaCue;
                if (strAlbumIdentificado.empty() && !strAlbumCue.empty())
                    strAlbumIdentificado = strAlbumCue;

                // Se ainda faltar metadados, consulta o GnuDB via TOC
                if (strArtistaIdentificado.empty() || strAlbumIdentificado.empty())
                {
                    // Sem .bin, o fim do disco vem do REM LEAD-OUT do CUE; sem nenhum dos
                    // dois, o GnuDB recusa em vez de consultar com um fim inventado.
                    int64_t lngTamBin = Base::ArquivoExiste(strCaminhoBin) ? Base::Tamanho(strCaminhoBin) : 0;

                    if (!lstFaixasCue.empty())
                    {
                        if (m_objIndicador) m_objIndicador->DefinirFaseRapida("CONSULTANDO GNUDB…", "Resolução por assinatura de TOC");
                        std::string gArtist, gAlbum, gGenre, gYear;
                        std::vector<std::string> gTracks;
                        std::string resGnu = GnuDbResolver::ResolveToc(strCaminhoCue, lngTamBin, gArtist, gAlbum, gGenre, gYear, gTracks);

                        if (resGnu == "OK" && !gAlbum.empty())
                        {
                            std::string strArtistaGnu = gArtist;
                            if (!strArtistaGnu.empty() && Base::Contem(strArtistaGnu, ","))
                            {
                                // Split(new char[] { ',' }, 2)
                                size_t p = strArtistaGnu.find(',');
                                std::string strParte0 = strArtistaGnu.substr(0, p), strParte1 = strArtistaGnu.substr(p + 1);
                                if (!Base::VazioOuBranco(strParte0) && !Base::VazioOuBranco(strParte1))
                                {
                                    strArtistaGnu = Base::Trim(strParte1) + " " + Base::Trim(strParte0);
                                }
                            }

                            if (strArtistaIdentificado.empty()) strArtistaIdentificado = strArtistaGnu;
                            if (strAlbumIdentificado.empty()) strAlbumIdentificado = gAlbum;
                            if (!gTracks.empty() && lstTitulosIdentificados.empty())
                            {
                                lstTitulosIdentificados = gTracks;
                            }
                            Base::Despacha([this, strArtistaGnu, gAlbum] { Registrar("GnuDB localizou: " + strArtistaGnu + " — " + gAlbum); });
                        }
                    }
                }
            }

            // 3. Busca e Prevalência do Discogs e Apple Music (autoridade máxima de metadados e capas).
            //    Pulada com fonte exata: era aqui que um link colado era
            //    trocado por outro lançamento achado por texto.
            if (!blnFonteExata && !strAlbumIdentificado.empty())
            {
                if (m_objIndicador) m_objIndicador->DefinirFaseRapida("CONSULTANDO DISCOGS…", strAlbumIdentificado);
                std::shared_ptr<TDiscogsReleaseInfo> discogs = DiscogsClient::ConsultarLancamento(strArtistaIdentificado, strAlbumIdentificado, intQtdFaixasEsperadas,
                                                                                                 [this](const std::string &logMsg) { RegistrarDeOutroFio(logMsg); },
                                                                                                 DuracoesDoCue(strCaminhoCue, Base::ArquivoExiste(strCaminhoBin) ? Base::Tamanho(strCaminhoBin) : 0));

                if (discogs)
                {
                    if (m_objIndicador) m_objIndicador->DefinirFaseRapida("DISCOGS CONFIRMADO", std::to_string(!discogs->lstTrackTitles.empty() ? (int) discogs->lstTrackTitles.size() : intQtdFaixasEsperadas) + " faixas oficiais");
                    if (!discogs->arrCoverBytes.empty())
                    {
                        arrCapa = discogs->arrCoverBytes;
                    }

                    if (!discogs->strArtist.empty() && (strArtistaIdentificado.empty() || strArtistaIdentificado == "Desconhecido"))
                    {
                        strArtistaIdentificado = discogs->strArtist;
                    }

                    if (!discogs->strAlbum.empty())
                    {
                        strAlbumIdentificado = discogs->strAlbum;
                    }

                    if (!discogs->lstTrackTitles.empty())
                    {
                        lstTitulosIdentificados = discogs->lstTrackTitles;
                        lstArtistasFaixasIdentificados = discogs->lstTrackArtists;
                        size_t intQtd = discogs->lstTrackTitles.size();
                        Base::Despacha([this, intQtd] {
                            Registrar("Discogs: Lançamento oficial confirmado com " + std::to_string(intQtd) + " faixas.");
                        });
                    }
                }

                // Fallback de capa e faixas via Apple Music / iTunes caso Discogs não tenha capa ou não tenha o lançamento
                if (arrCapa.empty() || !discogs)
                {
                    if (m_objIndicador) m_objIndicador->DefinirFaseRapida("CONSULTANDO APPLE MUSIC…", strAlbumIdentificado);
                    std::shared_ptr<TItunesAlbumInfo> itunesAuto = ItunesResolver::ConsultarAlbum(
                        strArtistaIdentificado,
                        strAlbumIdentificado,
                        intQtdFaixasEsperadas,
                        [this](const std::string &logMsg) { RegistrarDeOutroFio(logMsg); });

                    if (itunesAuto)
                    {
                        if (m_objIndicador) m_objIndicador->DefinirFaseRapida("APPLE MUSIC CONFIRMADO", std::to_string(!itunesAuto->lstFaixas.empty() ? (int) itunesAuto->lstFaixas.size() : intQtdFaixasEsperadas) + " faixas oficiais");
                        if (arrCapa.empty())
                        {
                            std::string strUrlArt = PrimeiroNaoVazio({itunesAuto->strArtworkUrl1400, itunesAuto->strArtworkUrl600});
                            if (!strUrlArt.empty())
                            {
                                try { arrCapa = ExternalMetadataClient::FetchImage(strUrlArt); } catch (...) {}
                            }
                        }

                        if (!itunesAuto->strArtistName.empty() && (strArtistaIdentificado.empty() || strArtistaIdentificado == "Desconhecido"))
                        {
                            strArtistaIdentificado = itunesAuto->strArtistName;
                        }
                        if (!itunesAuto->strCollectionName.empty() && (strAlbumIdentificado.empty() || strAlbumIdentificado == "CD Desconhecido"))
                        {
                            strAlbumIdentificado = itunesAuto->strCollectionName;
                        }

                        if (!discogs && !itunesAuto->lstFaixas.empty())
                        {
                            lstTitulosIdentificados.clear();
                            lstArtistasFaixasIdentificados.clear();
                            for (const TItunesTrackInfo &f : itunesAuto->lstFaixas)
                            {
                                lstTitulosIdentificados.push_back(f.strTrackName);
                                lstArtistasFaixasIdentificados.push_back(f.strArtistName);
                            }
                            size_t intQtd = itunesAuto->lstFaixas.size();
                            Base::Despacha([this, intQtd] {
                                Registrar("Apple Music: Lançamento oficial confirmado com " + std::to_string(intQtd) + " faixas.");
                            });
                        }
                    }
                }
            }

            // 3.5. Fallback para CUE local: se nem Discogs nem iTunes localizaram o lançamento, aproveita títulos do CUE se existirem
            if (lstTitulosIdentificados.empty() && Base::ArquivoExiste(strCaminhoCue))
            {
                std::vector<TrackInfo> lstFaixasCue = ProcessadorExtracaoAudio::ParsearCue(strCaminhoCue);
                if (!lstFaixasCue.empty())
                {
                    bool blnTemTitulosNoCue = false;
                    for (const TrackInfo &ti : lstFaixasCue)
                    {
                        if (!ti.Title.empty() && !Base::Contem(ti.Title, "Faixa", true))
                        {
                            blnTemTitulosNoCue = true;
                            break;
                        }
                    }
                    if (blnTemTitulosNoCue)
                    {
                        lstTitulosIdentificados.clear();
                        for (const TrackInfo &ti : lstFaixasCue)
                        {
                            lstTitulosIdentificados.push_back(ti.Title);
                        }
                        if (lstCompositoresIdentificados.empty() &&
                            std::any_of(lstFaixasCue.begin(), lstFaixasCue.end(), [](const TrackInfo &ti) { return !ti.Composer.empty(); }))
                        {
                            for (const TrackInfo &ti : lstFaixasCue)
                            {
                                lstCompositoresIdentificados.push_back(ti.Composer);
                            }
                        }
                    }
                }
            }

            // DECISAO-LORENZO (08/11/2026): o CD-TEXT manda nos titulos (salvo link colado)
            if (!blnFonteExata)
            {
                int n = AplicarCdText(cdText, lstTitulosIdentificados, lstArtistasFaixasIdentificados);
                if (n > 0) RegistrarDeOutroFio("CD-TEXT: " + std::to_string(n) + " título(s) do disco mantido(s) no lugar dos da fonte externa.");
            }

            // 4. Renomeação física e injeção de tags nas faixas WMA existentes
            bool blnRenomeou = false;
            if (!lstTitulosIdentificados.empty())
            {
                // LINUX: "Injetando tags ASF" no original
                if (m_objIndicador) m_objIndicador->DefinirFaseRapida("APLICANDO METADADOS…", "Injetando tags FLAC e renomeando faixas");
                blnRenomeou = ProcessadorExtracaoAudio::RenomearETaggearFaixas(
                    PastaTrabalho,
                    lstTitulosIdentificados,
                    lstArtistasFaixasIdentificados,
                    strArtistaIdentificado,
                    strAlbumIdentificado,
                    lstCompositoresIdentificados,
                    arrCapa,
                    [this](const std::string &msg) { RegistrarDeOutroFio(msg); });
            }

            // 5. Aplicação dos resultados na interface
            Base::Despacha([this, strArtistaIdentificado, strAlbumIdentificado, arrCapa, blnRenomeou, bManual] {
                ConcluirIdentificacao(strArtistaIdentificado, strAlbumIdentificado, arrCapa, blnRenomeou, bManual);
            });
        }
        catch (const std::exception &exIdent)
        {
            std::string strMsg = exIdent.what();
            Base::Despacha([this, strMsg] { FalharIdentificacao(strMsg); });
        }
    }).detach();
}

/// Fim comum de toda identificação, na thread da interface: devolve os
/// botões, preenche artista e álbum, salva a capa e relê a área de
/// trabalho se as faixas foram renomeadas.
void AquisicaoView::ConcluirIdentificacao(const std::string &strArtistaIdentificado, const std::string &strAlbumIdentificado,
                                          const std::vector<uint8_t> &arrCapa, bool blnRenomeou, bool bManual)
{
    DefinirEmIdentificacao(false);
    TxtIdentificacao->Text = "";

    if (!strArtistaIdentificado.empty() && (VazioAposTrim(CaixaArtista->Text()) || bManual))
    {
        CaixaArtista->Text = strArtistaIdentificado;
    }
    if (!strAlbumIdentificado.empty() && (VazioAposTrim(CaixaAlbum->Text()) || bManual))
    {
        CaixaAlbum->Text = strAlbumIdentificado;
    }

    if (!arrCapa.empty())
    {
        _capaBytes = arrCapa;
        ExibirCapaNaUi(arrCapa);

        // Salva a capa na pasta de trabalho
        try
        {
            std::string strCaminhoCapaTrabalho = Base::Combina(PastaTrabalho, "cover.jpg");
            Base::GravaBytes(strCaminhoCapaTrabalho, arrCapa.data(), arrCapa.size());
            _caminhoCapaSalva = strCaminhoCapaTrabalho;
            Registrar("Capa oficial salva em cover.jpg.");
        }
        catch (...) {}
    }

    if (!VazioAposTrim(CaixaArtista->Text()) && !VazioAposTrim(CaixaAlbum->Text()))
    {
        Registrar("Identificação concluída: " + Base::Trim(CaixaArtista->Text()) + " — " + Base::Trim(CaixaAlbum->Text()));
        if (m_objIndicador) m_objIndicador->DefinirFaseRapida("METADADOS APLICADOS", Base::Trim(CaixaArtista->Text()) + " — " + Base::Trim(CaixaAlbum->Text()));
    }
    else
    {
        Registrar("Identificação automática não encontrou correspondência exata. Digite artista e álbum.");
        if (m_objIndicador) m_objIndicador->DefinirFaseRapida("METADADOS PENDENTES", "Preencha artista e álbum");
    }

    // Atualiza o manifesto e a tabela da interface caso faixas tenham sido renomeadas
    if (blnRenomeou)
    {
        Reler();
    }
    else
    {
        AvaliarPorta();
    }
}

void AquisicaoView::FalharIdentificacao(const std::string &strMensagem)
{
    DefinirEmIdentificacao(false);
    TxtIdentificacao->Text = "erro na busca";
    Registrar("Falha na identificação: " + strMensagem);
    _blnErroNoMedidor = true; if (m_objIndicador) m_objIndicador->DefinirFaseRapida("FALHA NA IDENTIFICAÇÃO", strMensagem);
    AvaliarPorta();
}

// ===============================================================
//  IDENTIFICAÇÃO COMPARADA (botão, sem link)
//
//  Com artista e/ou álbum digitados, o ARCA consulta Apple Music e
//  Discogs ao mesmo tempo e mostra os dois lado a lado; a pessoa
//  escolhe. Link exato continua indo pelo caminho direto, e a
//  identificação automática pós-extração continua sozinha.
// ===============================================================

void AquisicaoView::IdentificarComparando()
{
    std::string strArtista = Base::Trim(CaixaArtista->Text());
    std::string strAlbum = Base::Trim(CaixaAlbum->Text());
    std::string strBusca = CaixaBuscaAlbum ? Base::Trim(CaixaBuscaAlbum->Text()) : "";

    // Texto livre na busca assistida (não link) vale como álbum quando
    // as caixas estão vazias.
    if (strArtista.empty() && strAlbum.empty()) strAlbum = strBusca;
    if (strArtista.empty() && strAlbum.empty())
    {
        Registrar("Informe artista e/ou álbum — ou cole o link exato na busca assistida — e clique em Identificar.");
        if (m_objIndicador) m_objIndicador->DefinirFaseRapida("METADADOS PENDENTES", "Preencha artista e/ou álbum");
        return;
    }

    std::vector<int> lstDuracoesDisco = DuracoesDoDisco();
    int intQtdFaixas = (int) lstDuracoesDisco.size();
    if (intQtdFaixas == 0 && Base::PastaExiste(PastaTrabalho))
    {
        // LINUX: "*.wma" no original
        try { intQtdFaixas = (int) Base::Arquivos(PastaTrabalho, ".flac").size(); } catch (...) {}
    }

    std::string strBuscado = Base::Trim(strArtista + " — " + strAlbum, " —");
    DefinirEmIdentificacao(true);
    TxtIdentificacao->Text = "buscando no Apple Music e no Discogs…";
    Registrar("Comparando Apple Music e Discogs para: " + strBuscado);
    if (m_objIndicador) m_objIndicador->DefinirFaseRapida("CONSULTANDO METADADOS…", "Apple Music e Discogs");

    std::function<void(const std::string &)> actLog = [this](const std::string &strMsg) {
        RegistrarDeOutroFio(strMsg);
    };

    std::thread([this, strArtista, strAlbum, intQtdFaixas, actLog, strBuscado, lstDuracoesDisco] {
        // As duas consultas em paralelo: cada uma leva segundos, e a
        // pessoa espera pela mais lenta, não pela soma.
        std::shared_ptr<TCandidatoIdentificacao> objApple, objDiscogs;
        std::thread objTarefaApple([&] {
            try
            {
                std::shared_ptr<TItunesAlbumInfo> objAlbum = ItunesResolver::ConsultarAlbum(strArtista, strAlbum, intQtdFaixas, false, actLog);   // janela: a pessoa confere as durações
                if (!objAlbum) return;
                std::vector<uint8_t> arrCapaApple;
                std::string strUrlCapa = PrimeiroNaoVazio({objAlbum->strArtworkUrl1400, objAlbum->strArtworkUrl600, objAlbum->strArtworkUrl100});
                if (!strUrlCapa.empty())
                {
                    try { arrCapaApple = ExternalMetadataClient::FetchImage(strUrlCapa); }
                    catch (const std::exception &exCapa) { actLog(std::string("Apple Music: falha ao baixar a capa: ") + exCapa.what()); }
                }
                objApple = TCandidatoIdentificacao::DeItunes(objAlbum, arrCapaApple);
            }
            catch (const std::exception &ex) { actLog(std::string("Apple Music: falha na consulta: ") + ex.what()); objApple = nullptr; }
        });

        std::thread objTarefaDiscogs([&] {
            try
            {
                NetworkConfiguration::Initialize();
                objDiscogs = TCandidatoIdentificacao::DeDiscogs(
                    DiscogsClient::ConsultarLancamento(strArtista, strAlbum, intQtdFaixas, actLog));
            }
            catch (const std::exception &ex) { actLog(std::string("Discogs: falha na consulta: ") + ex.what()); objDiscogs = nullptr; }
        });

        objTarefaApple.join();
        objTarefaDiscogs.join();

        Base::Despacha([this, objApple, objDiscogs, lstDuracoesDisco, strBuscado] {
            if (!objApple && !objDiscogs)
            {
                FalharIdentificacao("nem o Apple Music nem o Discogs acharam '" + strBuscado + "'. Ajuste o texto ou cole o link exato.");
                return;
            }

            TxtIdentificacao->Text = "aguardando a escolha…";
            if (m_objIndicador) m_objIndicador->DefinirFaseRapida("ESCOLHA A FONTE", "Apple Music ou Discogs");

            auto objJanela = std::make_shared<JanelaEscolhaFonte>(objApple, objDiscogs, lstDuracoesDisco, strBuscado);
            objJanela->Owner = Janela();
            objJanela->ShowDialog();

            if (!objJanela->Escolhido())
            {
                DefinirEmIdentificacao(false);
                TxtIdentificacao->Text = "";
                Registrar("Identificação cancelada: nada foi alterado.");
                if (m_objIndicador) m_objIndicador->DefinirFaseRapida("PRONTO", "Identificação cancelada");
                AvaliarPorta();
                return;
            }
            AplicarCandidato(objJanela->Escolhido());
        });
    }).detach();
}

/// Catalogação à mão, para o disco que nenhum catálogo conhece. O
/// editor já abre com o que se sabe: durações do disco, títulos e
/// compositores que estejam no CUE, artista e álbum das caixas, capa
/// atual. Aplicar segue o mesmo caminho da escolha entre fontes.
void AquisicaoView::AoCatalogarManual(RoutedEventArgs &)
{
    if (_blnEmIdentificacao || _ocupado) return;

    std::vector<int> lstDuracoes = DuracoesDoDisco();
    if (lstDuracoes.empty())
    {
        // Sem CUE: o número de faixas sai dos WMA da área de trabalho.
        int intQtdWma = 0;
        try { if (Base::PastaExiste(PastaTrabalho)) intQtdWma = (int) Base::Arquivos(PastaTrabalho, ".flac").size(); } catch (...) {}
        for (int i = 0; i < intQtdWma; i++) lstDuracoes.push_back(-1);
    }
    if (lstDuracoes.empty())
    {
        Registrar("Catalogar à mão: a área de trabalho está vazia — extraia o disco primeiro.");
        return;
    }

    std::vector<std::string> lstTitulos, lstCompositores;    // vazias = null
    std::string strCue = Base::Combina(PastaTrabalho, "AudioCD.cue");
    if (Base::ArquivoExiste(strCue))
    {
        try
        {
            for (const TrackInfo &f : ProcessadorExtracaoAudio::ParsearCue(strCue))
                if (f.TrackType.empty() || Base::IgualSemCaixa(f.TrackType, "AUDIO"))
                {
                    lstTitulos.push_back(f.Title);
                    lstCompositores.push_back(f.Composer);
                }
        }
        catch (...) {}
    }

#ifdef ARCA_TEM_EDITOR_FAIXAS
    auto objJanela = std::make_shared<JanelaEditorFaixas>(lstDuracoes,
        Base::Trim(CaixaArtista->Text()), Base::Trim(CaixaAlbum->Text()), lstTitulos, lstCompositores, _capaBytes);
    objJanela->Owner = Janela();
    objJanela->ShowDialog();
    if (!objJanela->Resultado) return;

    DefinirEmIdentificacao(true);
    AplicarCandidato(objJanela->Resultado);
#else
    // A JanelaEditorFaixas é de outra frente do porte e ainda não está em Vistas.
    (void) lstTitulos;
    (void) lstCompositores;
    Registrar("Catalogar à mão: a janela do editor de faixas ainda não foi portada.");
#endif
}

/// Grava o candidato escolhido: renomeia as faixas, regrava as tags e o
/// CUE, e termina pelo mesmo caminho das outras identificações.
void AquisicaoView::AplicarCandidato(std::shared_ptr<TCandidatoIdentificacao> objC)
{
    Registrar("Usando os dados do " + objC->strFonte + ": " + objC->strArtista + " — " + objC->strAlbum + ".");
    _strSugestaoConteudo = SugestaoDeConteudo(objC);
    if (!_strSugestaoConteudo.empty()) Registrar(_strSugestaoConteudo + " Use “Conteúdo das faixas…” antes de aprovar, se for o caso.");
    TxtIdentificacao->Text = "aplicando metadados…";
    // LINUX: "Injetando tags ASF" no original
    if (m_objIndicador) m_objIndicador->DefinirFaseRapida("APLICANDO METADADOS…", "Injetando tags FLAC e renomeando faixas");

    std::thread([this, objC] {
        try
        {
            bool blnGravouArtwork = GravarArtwork(*objC);
            bool blnRenomeou = false;
            if (!objC->lstTitulos.empty())
            {
                blnRenomeou = ProcessadorExtracaoAudio::RenomearETaggearFaixas(
                    PastaTrabalho,
                    objC->lstTitulos,
                    objC->lstArtistasFaixas,
                    objC->strArtista,
                    objC->strAlbum,
                    objC->lstCompositores ? *objC->lstCompositores : std::vector<std::string>(),
                    objC->arrCapa,
                    objC->strAno,
                    objC->strGenero,
                    [this](const std::string &strMsg) { RegistrarDeOutroFio(strMsg); });
            }
            // Artwork novo também muda o manifesto: relê mesmo sem renomear.
            Base::Despacha([this, objC, blnRenomeou, blnGravouArtwork] {
                ConcluirIdentificacao(objC->strArtista, objC->strAlbum, objC->arrCapa, blnRenomeou || blnGravouArtwork, true);
            });
        }
        catch (const std::exception &ex)
        {
            std::string strMsg = ex.what();
            Base::Despacha([this, strMsg] { FalharIdentificacao(strMsg); });
        }
    }).detach();
}

/// Gêneros e estilos com que o Discogs ("Non-Music", "Spoken Word",
/// "Comedy", "Interview", "Audiobook") e o Apple Music ("Spoken Word",
/// "Comedy") dizem que um disco é fala. Só sugere: o que vai para o
/// arquivo a pessoa escolhe.
static const char *const s_arrGenerosDeFala[] =
    {"Non-Music", "Spoken Word", "Comedy", "Interview", "Audiobook", "Speech", "Poetry"};

std::string AquisicaoView::SugestaoDeConteudo(const std::shared_ptr<TCandidatoIdentificacao> &objC)
{
    if (!objC || objC->strGenero.empty()) return "";
    for (const char *strGenero : s_arrGenerosDeFala)
        if (Base::Contem(objC->strGenero, strGenero, true))
            return "O " + objC->strFonte + " classifica este disco como “" + objC->strGenero + "”: pode haver faixas de audiolivro ou fala.";
    return "";
}

/// Marca faixas da área de trabalho como música, audiolivro ou fala. É
/// uma tag (a classe de mídia do ASF), então muda o arquivo: o
/// manifesto é congelado de novo, como depois de uma identificação.
void AquisicaoView::AoDefinirConteudo(RoutedEventArgs &)
{
    if (_blnEmIdentificacao || _ocupado) return;

    std::vector<std::string> lstArquivos;
    // LINUX: "*.wma" no original
    try { if (Base::PastaExiste(PastaTrabalho)) lstArquivos = Base::Arquivos(PastaTrabalho, ".flac"); }
    catch (...) {}
    if (lstArquivos.empty())
    {
        Registrar("Conteúdo das faixas: a área de trabalho não tem faixas — extraia o disco primeiro.");
        return;
    }
    // Os nomes começam pelo número da faixa ("01 - …"): a ordem do nome é a do disco.
    std::sort(lstArquivos.begin(), lstArquivos.end(), MenorSemCaixa);

    auto objJanela = std::make_shared<JanelaConteudoFaixas>(lstArquivos, _strSugestaoConteudo);
    objJanela->Owner = Janela();
    objJanela->ShowDialog();
    if (!objJanela->Alteradas()) return;

    int intGravadas = 0;
    for (const auto &objPar : *objJanela->Alteradas())
    {
        std::string strNome = Base::NomeDoArquivo(objPar.first);
        EditorFlac objEditor;
        if (objEditor.Abrir(objPar.first) && TClasseMidia::Gravar(&objEditor, objPar.second) && objEditor.Salvar())
        {
            intGravadas++;
            Registrar("Conteúdo: " + strNome + " → " + NomeDoConteudo(objPar.second) + ".");
        }
        else Registrar("Conteúdo: não consegui marcar " + strNome + " — " + objEditor.Falha());
    }
    Registrar("Conteúdo das faixas: " + std::to_string(intGravadas) + " de " + std::to_string(objJanela->Alteradas()->size()) + " marcada(s).");

    // A tag mudou os arquivos: o manifesto antigo recusaria a aprovação.
    if (intGravadas > 0) Reler();
}

std::string AquisicaoView::NomeDoConteudo(ConteudoFaixa eConteudo)
{
    switch (eConteudo)
    {
        case ConteudoFaixa::Audiolivro: return "audiolivro";
        case ConteudoFaixa::Fala: return "fala";
        default: return "música";
    }
}

/// Grava a capa (em resolução plena, quando houver) e as páginas do
/// encarte em Artwork\ da área de trabalho, de onde a aprovação as leva
/// ao álbum. Antes, apaga só o que ela mesma gravaria (capa.jpg e
/// encarte_*.jpg) — o resto da pasta não é dela. Roda fora da
/// interface. Devolve true se gravou algo.
bool AquisicaoView::GravarArtwork(const TCandidatoIdentificacao &objC)
{
    const std::vector<uint8_t> &arrCapa = !objC.arrCapaPlena.empty() ? objC.arrCapaPlena : objC.arrCapa;
    bool blnTemEncarte = !objC.lstEncarte.empty();
    if (arrCapa.empty() && !blnTemEncarte) return false;

    std::string strPasta = Base::Combina(PastaTrabalho, Integracao::PastaArtwork);
    Base::CriaPasta(strPasta);
    // Só um encarte novo substitui o anterior: escolher depois uma capa
    // da internet não pode apagar páginas já digitalizadas.
    if (blnTemEncarte)
        for (const std::string &strVelho : ArquivosComo(strPasta, "encarte_", ".jpg"))
            fs::remove(fs::u8path(strVelho));

    int intGravados = 0;
    if (!arrCapa.empty())
    {
        Base::GravaBytes(Base::Combina(strPasta, "capa.jpg"), arrCapa.data(), arrCapa.size());
        intGravados++;
    }
    if (blnTemEncarte)
    {
        for (size_t i = 0; i < objC.lstEncarte.size(); i++)
        {
            Base::GravaBytes(Base::Combina(strPasta, "encarte_" + D2((int) i + 1) + ".jpg"), objC.lstEncarte[i].data(), objC.lstEncarte[i].size());
            intGravados++;
        }
    }
    int intTotal = intGravados;
    size_t intPaginas = objC.lstEncarte.size();
    Base::Despacha([this, intTotal, blnTemEncarte, intPaginas] {
        Registrar("Artwork: " + std::to_string(intTotal) + " imagem(ns) prontas para ir ao álbum (capa" +
                  (blnTemEncarte ? " e " + std::to_string(intPaginas) + " página(s) do encarte" : std::string()) + ").");
    });
    return true;
}

/// Duração de cada faixa do disco, em segundos, a partir do CUE da área
/// de trabalho: distância entre o início de uma faixa e o da seguinte,
/// e da última até o lead-out (ou o fim do .bin). Lista vazia se não
/// houver CUE. É a régua com que a janela de escolha confere cada
/// lançamento.
std::vector<int> AquisicaoView::DuracoesDoDisco()
{
    std::vector<int> lstDuracoes;
    std::string strCue = Base::Combina(PastaTrabalho, "AudioCD.cue");
    if (!Base::ArquivoExiste(strCue)) return lstDuracoes;
    try
    {
        std::string strAlbumCue, strArtistaCue;
        int64_t lngLeadOut = 0;
        std::vector<TrackInfo> lstTodas = ProcessadorExtracaoAudio::ParsearCue(strCue, strAlbumCue, strArtistaCue, lngLeadOut);
        std::vector<TrackInfo> lstFaixas;
        for (const TrackInfo &f : lstTodas)
            if (f.TrackType.empty() || Base::IgualSemCaixa(f.TrackType, "AUDIO")) lstFaixas.push_back(f);
        if (lngLeadOut <= 0)
        {
            std::string strBin = Base::Combina(PastaTrabalho, "AudioCD.bin");
            if (Base::ArquivoExiste(strBin)) lngLeadOut = Base::Tamanho(strBin);
        }
        const double dblBytesPorSegundo = 176400.0;     // 44.100 Hz x 2 canais x 2 bytes
        for (size_t i = 0; i < lstFaixas.size(); i++)
        {
            int64_t lngFim = i + 1 < lstFaixas.size() ? lstFaixas[i + 1].ByteOffset : lngLeadOut;
            int64_t lngBytes = lngFim - lstFaixas[i].ByteOffset;
            // Math.Round do .NET: ao par
            lstDuracoes.push_back(lngBytes > 0 ? (int) std::nearbyint(lngBytes / dblBytesPorSegundo) : -1);
        }
    }
    catch (...) { lstDuracoes.clear(); }
    return lstDuracoes;
}

void AquisicaoView::ExibirCapaNaUi(const std::vector<uint8_t> &arrBytes)
{
    try
    {
        ImageSource bi = BitmapSource::DeBytes(arrBytes);

        ImgCapa->Source = bi;
        TxtSemCapa->Visibility = Vistas::Visibility::Collapsed;
    }
    catch (const std::exception &ex)
    {
        Registrar(std::string("Falha ao renderizar imagem da capa: ") + ex.what());
    }
}

// ===============================================================
/// O portão. Só abre com as duas condições que o Lorenzo exigiu: existe
/// um rip conferido, e os metadados foram confirmados.
void AquisicaoView::AvaliarPorta()
{
    // Extrair e Identificar dependem dos dois estados juntos. Antes, a
    // identificação os religava só se o manifesto não estivesse sendo
    // calculado, e o fim do manifesto não religava nada: ao abrir o
    // ARCA com um disco na área de trabalho (manifesto + identificação
    // automática ao mesmo tempo), Extrair ficava desligado até fechar
    // o programa (Lorenzo, 06/10/2026). Aqui, toda reavaliação acerta.
    bool blnLivre = !_ocupado && !_blnEmIdentificacao;
    BtnExtrairCd->IsEnabled = blnLivre;
    BtnIdentificar->IsEnabled = blnLivre;
    BtnDescartar->IsEnabled = blnLivre;

    bool temRip = !_manifesto.empty();
    bool temMeta = CaixaArtista && CaixaAlbum
                && Base::Trim(CaixaArtista->Text()).size() > 0
                && Base::Trim(CaixaAlbum->Text()).size() > 0;

    BtnAprovar->IsEnabled = temRip && temMeta && !_ocupado && !_blnEmIdentificacao;

    if (!temRip)
    {
        TxtPorta->Text = "Nada para aprovar — a área de trabalho está vazia.";
        if (!_ocupado && !_blnErroNoMedidor && m_objIndicador)
        {
            m_objIndicador->Resetar("PRONTO", "Aguardando início da extração");
        }
    }
    else if (!temMeta)
    {
        TxtPorta->Text = "Confirme artista e álbum antes de aprovar. "
                         "Nada entra na biblioteca sem identificação.";
        if (!_ocupado && !_blnErroNoMedidor && m_objIndicador)
        {
            m_objIndicador->DefinirFaseRapida("AGUARDANDO IDENTIFICAÇÃO", "Preencha artista e álbum ou clique em Identificar");
        }
    }
    else
    {
        TxtPorta->Text = std::to_string(_manifesto.size()) + (_manifesto.size() == 1 ? " item pronto para " : " itens prontos para ") +
                         Base::Trim(CaixaArtista->Text()) + " — " + Base::Trim(CaixaAlbum->Text()) + ".";
        if (!_ocupado && !_blnErroNoMedidor && m_objIndicador)
        {
            m_objIndicador->DefinirAguardandoAprovacao(std::to_string(_manifesto.size()) + " item(ns) pronto(s) para o acervo");
        }
    }

    if (temRip && temMeta) MarcarPasso(2);
}

void AquisicaoView::AoAprovar(RoutedEventArgs &)
{
    if (_manifesto.empty() || _ocupado || _blnEmIdentificacao) return;

    std::string artista = Base::Trim(CaixaArtista->Text());
    std::string album = Base::Trim(CaixaAlbum->Text());

    // Lido do arquivo, e não da memória: vale mesmo com o ARCA reaberto
    // entre a extração e a aprovação.
    std::shared_ptr<TRelatorioExtracao> objRelatorio = TRelatorioExtracao::Ler(PastaTrabalho);
    bool blnNaoExata = objRelatorio && !objRelatorio->Exata();
    std::string strAvisoExtracao = blnNaoExata
        ? "ATENÇÃO: esta extração NÃO é cópia exata do disco.\r\n" + std::to_string(objRelatorio->intSetoresMascarados) +
              " setor(es) ilegível(is) viraram silêncio — " + objRelatorio->ResumoPorFaixa() + ".\r\n\r\n"
        : "";

    // O veredito bit-perfect de cada faixa: qualquer uma não comprovada
    // também pede atenção (e Cancelar como padrão).
    if (objRelatorio && !objRelatorio->dicVerificacao.empty())
    {
        bool blnAlgumaNao = false;
        for (const auto &v : objRelatorio->dicVerificacao)
            if (v.second.Nivel() == NivelBitPerfect::NaoComprovada) blnAlgumaNao = true;
        strAvisoExtracao += std::string(blnAlgumaNao ? "ATENÇÃO — " : "") + "Bit-perfect: " + objRelatorio->ResumoBitPerfect() + ".\r\n\r\n";
        blnNaoExata |= blnAlgumaNao;
    }

    if (MessageBox::Show(
            strAvisoExtracao +
                "Integrar " + std::to_string(_manifesto.size()) + " item(ns) ao acervo como\r\n\r\n" +
                artista + " — " + album + "\r\n\r\n" +
                "Os arquivos saem da área de trabalho e passam a fazer parte da biblioteca.",
            "Aprovar e integrar", MessageBoxButton::OKCancel,
            blnNaoExata ? MessageBoxImage::Warning : MessageBoxImage::Question,
            blnNaoExata ? MessageBoxResult::Cancel : MessageBoxResult::OK) != MessageBoxResult::OK)
        return;

    _ocupado = true;
    _blnErroNoMedidor = false;
    BtnAprovar->IsEnabled = false;
    Registrar("——— integração ———");
    if (m_objIndicador)
    {
        m_objIndicador->AtualizarProgresso("INTEGRANDO NO ACERVO", 0.0, std::string("Iniciando integração transacional..."));
    }

    // O DiscID tem de ser do material que está na área de trabalho. A
    // autoridade é o AudioCD.cue que está ali — ele tem o índice inteiro do
    // disco que gerou estas faixas. O ID na memória pode ser de outro disco
    // (o anterior, já aprovado) ou ter se perdido ao reabrir o ARCA; ele
    // só vale quando o CUE não permite calcular (multissessão).
    std::string strIdDoCue = CdromTocReader::CalcularIdDoCue(Base::Combina(PastaTrabalho, "AudioCD.cue"));
    if (!strIdDoCue.empty())
    {
        if (!IdDoDisco.empty() && IdDoDisco != "UnknownDisc" && IdDoDisco != strIdDoCue)
            Registrar("ATENÇÃO: o DiscID na memória (" + IdDoDisco + ") não é o do AudioCD.cue da área de trabalho; vale o do CUE.");
        else if (IdDoDisco != strIdDoCue)
            Registrar("DiscID recuperado do AudioCD.cue: " + strIdDoCue);
        IdDoDisco = strIdDoCue;
    }
    else if (IdDoDisco.empty() || IdDoDisco == "UnknownDisc")
    {
        Registrar("ATENÇÃO: DiscID desconhecido (sem AudioCD.cue utilizável). O ledger registrará a aquisição sem ele.");
    }

    std::vector<ItemManifesto> manifesto = _manifesto;
    std::string trabalho = PastaTrabalho, raiz = RaizAcervo, id = IdDoDisco;

    std::thread([this, manifesto, trabalho, raiz, artista, album, id]() mutable {
        // DECISAO-LORENZO (08/11/2026): o que esta nas caixas Artista e Album vai para as etiquetas
        // (Integracao::GravarArtistaEAlbum)
        int intRegravadas = 0;
        std::string strFalhaEtiquetas = Integracao::GravarArtistaEAlbum(manifesto, artista, album, intRegravadas);
        if (intRegravadas > 0)
        {
            Base::Despacha([this, manifesto] { _manifesto = manifesto; });   // uma nova tentativa usa os arquivos regravados
            RegistrarDeOutroFio("Artista e álbum das caixas gravados em " + std::to_string(intRegravadas) + " faixa(s): " + artista + " — " + album + ".");
        }
        if (!strFalhaEtiquetas.empty())
        {
            RegistrarDeOutroFio("NÃO INTEGRADO: " + strFalhaEtiquetas);
            Base::Despacha([this] {
                _ocupado = false;
                BtnAprovar->IsEnabled = true;
                if (m_objIndicador) m_objIndicador->DefinirFaseRapida("ERRO NA INTEGRAÇÃO", "etiquetas de artista e álbum");
            });
            return;
        }

        ResultadoIntegracao r = Integracao::Integrar(
            manifesto, trabalho, raiz, artista, album, id,
            [this](const std::string &m) { RegistrarDeOutroFio(m); },
            [this](double dblPct, const std::string &strDetalhe) {
                if (m_objIndicador)
                {
                    m_objIndicador->AtualizarProgresso("INTEGRANDO NO ACERVO", dblPct, strDetalhe);
                }
            });

        Base::Despacha([this, r] {
            _ocupado = false;
            if (!r.Ok)
            {
                Registrar("NÃO INTEGRADO: " + r.Erro);
                // A falha pode vir no meio da promoção: aí uma parte já
                // está no acervo. Dizer "nada foi perdido" escondia isso.
                if (r.Integrados.empty())
                    Registrar("Nada foi perdido — o material continua na área de trabalho, e o acervo não foi tocado.");
                else
                    Registrar("ATENÇÃO: a falha veio no meio da promoção. " + std::to_string(r.Integrados.size()) + " de " + std::to_string(_manifesto.size()) +
                              " item(ns) JÁ ESTÃO no acervo, em " + r.PastaDestino + " " +
                              "(o que existia antes com o mesmo nome virou .backup). O material completo continua na área de trabalho; "
                              "aprovar de novo termina a integração sem duplicar o que já entrou.");
                if (m_objIndicador)
                {
                    _blnErroNoMedidor = true;
                    m_objIndicador->DefinirFaseRapida("ERRO NA INTEGRAÇÃO", r.Erro);
                }
                AvaliarPorta();
                return;
            }

            Registrar(std::to_string(r.Arquivos) + " item(ns) integrados em " + r.PastaDestino);

            // O que ARCAwm não fazia: avisar a biblioteca. Sem isto o
            // material só apareceria numa varredura futura do player.
            int entraram = 0;
            // Só faixas e vídeos: as imagens do encarte vão para a pasta
            // Artwork do álbum, não para a biblioteca do player.
            int intMidias = 0;
            for (const std::string &a : r.Integrados)
            {
                std::string strNoAlbum = a;
                if (a.size() > r.PastaDestino.size())
                {
                    strNoAlbum = a.substr(r.PastaDestino.size());
                    while (!strNoAlbum.empty() && strNoAlbum[0] == '/') strNoAlbum.erase(0, 1);   // TrimStart('\\')
                }
                if (Integracao::EhArtwork(strNoAlbum)) continue;
                intMidias++;
                if (AcrescentarNaBiblioteca && AcrescentarNaBiblioteca(a)) entraram++;
            }
            Registrar(std::to_string(entraram) + " de " + std::to_string(intMidias) + " faixa(s) acrescentadas à biblioteca.");
            if (intMidias < r.Arquivos)
                Registrar(std::to_string(r.Arquivos - intMidias) + " imagem(ns) de capa e encarte em " + Base::Combina(r.PastaDestino, Integracao::PastaArtwork));

            // ARCA-ORIGINAL: o AvaliarPorta do fim deste bloco (sem manifesto, nada ocupado, sem erro)
            // troca na hora este "INTEGRAÇÃO CONCLUÍDA" por "PRONTO / Aguardando início da
            // extração": o medidor nunca chega a mostrar a conclusão (só o registro mostra). O
            // _blnErroNoMedidor protege só os erros. Visto no teste de 08/11/2026.
            if (m_objIndicador)
            {
                m_objIndicador->DefinirFaseRapida("INTEGRAÇÃO CONCLUÍDA", r.NaoIntegrados.empty()
                    ? std::to_string(r.Arquivos) + " item(ns) integrados ao acervo"
                    : std::to_string(r.Arquivos) + " item(ns) integrados; " + std::to_string(r.NaoIntegrados.size()) + " arquivo(s) ficaram na área de trabalho");
            }

            _manifesto.clear();
            IdDoDisco.clear();   // o disco aprovado saiu; o ID não vale para o próximo material
            ListaFaixas->ItemsSource = FonteDeItens();
            CaixaArtista->Text = "";
            CaixaAlbum->Text = "";
            CaixaBuscaAlbum->Text = "";
            _capaBytes.clear();
            _caminhoCapaSalva.clear();
            ImgCapa->Source = nullptr;
            TxtSemCapa->Visibility = Vistas::Visibility::Visible;
            TxtFaixasTitulo->Text = "INTEGRADO";
            MarcarPasso(0);
            AvaliarPorta();
        });
    }).detach();
}

// ===============================================================
void AquisicaoView::Registrar(const std::string &m)
{
    // LINUX: Environment.NewLine ("\r\n" no Windows) vira "\n"
    TxtRegistro->Text += (TxtRegistro->Text.size() > 0 ? "\n" : "") +
                         Base::Agora("%H:%M:%S") + "  " + m;
    AreaRegistro->ScrollToEnd();
}

void AquisicaoView::RegistrarDeOutroFio(const std::string &m)
{
    Base::Despacha([this, m] { Registrar(m); });
}

// ---- o encaixe no casco (Vistas/MainWindow.h) ----
std::shared_ptr<VistaModulo> CriaModuloAquisicao(MainWindow &janela)
{
    (void) janela;
    return std::make_shared<AquisicaoView>();
}

} // namespace ARCA::Vistas

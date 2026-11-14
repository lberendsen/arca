// ARCA no xOS - o programa (de App.xaml e App.xaml.cs: StartupUri="MainWindow.xaml", com o
// Tema.xaml nos recursos da aplicacao).
//
// Uma janela do X com o titulo "ARCA" e a classe "arca", desenhada com NanoVG em OpenGL ES 2 pela
// GT210. O laco de eventos (Vistas/Janela.cpp) espera no X e no descritor dos despachos
// (Base::DescritorDespacho): parado, nao gasta processador. Uma instancia por vez: se ja ha um ARCA
// aberto neste X, ele vem para a frente e este sai.
#include "Vistas/JanelaScanner.h"
#include "Vistas/MainWindow.h"
#include "Modelo/EspectrogramaCache.h"
#include "Modelo/OndaCache.h"
#include "Modelo/RenomeacaoFaixa.h"

#include <cstdio>
#include <memory>

using namespace ARCA::Vistas;

namespace {

// O lugar de um modulo que a sua frente ainda nao entregou (CriaModuloXxx nao foi ligada).
std::shared_ptr<VistaModulo> Vazio(const std::string &nome)
{
    auto m = std::make_shared<VistaModulo>();
    m->Background = Tema::Recurso("Canvas");
    auto t = std::make_shared<TextBlock>(nome + " — em porte");
    t->Style = Tema::Estilo("Fraco");
    t->HorizontalAlignment = HorizontalAlignment::Center;
    t->VerticalAlignment = VerticalAlignment::Center;
    m->Content = t;
    return m;
}

} // namespace

int main()
{
    std::string erro;
    if (!Application::Inicia(erro))
    {
        fprintf(stderr, "xos-arca: %s\n", erro.c_str());
        return 1;
    }
    if (Application::JaAberto())
    {
        fprintf(stderr, "xos-arca: ja ha um ARCA aberto; ele veio para a frente\n");
        return 0;
    }
    // LINUX: o dialogo do scanner (no Windows era o do WIA): ScannerSane::Digitalizar passa a abrir a JanelaScanner
    RegistrarJanelaScanner();
    // a renomeacao leva junto os caches de onda e espectrograma (no original, chamada direta)
    ARCA::Modelo::TRenomeacaoFaixa::CaminhoSidecarOnda = ARCA::Modelo::OndaCache::CaminhoSidecar;
    ARCA::Modelo::TRenomeacaoFaixa::CaminhoSidecarEspectrograma = ARCA::Modelo::EspectrogramaCache::CaminhoSidecar;

    auto janela = std::make_shared<MainWindow>();
    // os tres modulos, cada um da sua frente (ver o fim de Vistas/MainWindow.h)
    std::shared_ptr<VistaModulo> aquisicao = CriaModuloAquisicao ? CriaModuloAquisicao(*janela) : nullptr;
    std::shared_ptr<VistaModulo> biblioteca = CriaModuloBiblioteca ? CriaModuloBiblioteca(*janela) : nullptr;
    std::shared_ptr<VistaModulo> execucao = CriaModuloExecucao ? CriaModuloExecucao(*janela) : nullptr;
    janela->EncaixaModulos(aquisicao ? aquisicao : Vazio("AQUISIÇÃO"), biblioteca ? biblioteca : Vazio("BIBLIOTECA"),
                           execucao ? execucao : Vazio("EM EXECUÇÃO"));
    janela->Show();
    Application::MarcaInstancia(*janela);
    return Application::Run(*janela);
}

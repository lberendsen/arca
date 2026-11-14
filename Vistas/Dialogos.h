// ARCA no xOS - Vistas/Dialogos: o MessageBox e o OpenFileDialog que o ARCA pedia ao Windows.
// LINUX: no Windows eram as caixas do sistema (o caderno, "Tudo escuro": "fica de fora a barra de
// titulo das janelas, que e do Windows 7, e as caixas de mensagem do sistema"). Aqui nao ha caixa
// do sistema: sao janelas modais do proprio ARCA, no visual do Tema, com o mesmo comportamento
// (botoes, botao padrao, Esc, resultado).
#pragma once

#include "Janela.h"

#include <optional>
#include <string>
#include <vector>

namespace ARCA::Vistas {

enum class MessageBoxButton { OK, OKCancel, YesNoCancel, YesNo };
enum class MessageBoxImage { None, Error, Question, Warning, Information };
enum class MessageBoxResult { None, OK, Cancel, Yes, No };

namespace MessageBoxImages {
constexpr MessageBoxImage Hand = MessageBoxImage::Error, Stop = MessageBoxImage::Error,
                          Exclamation = MessageBoxImage::Warning, Asterisk = MessageBoxImage::Information;
}

class MessageBox
{
public:
    // MessageBox.Show(texto, titulo, botoes, imagem, padrao) e MessageBox.Show(dono, ...)
    static MessageBoxResult Show(const std::string &texto, const std::string &titulo = "",
                                 MessageBoxButton botoes = MessageBoxButton::OK,
                                 MessageBoxImage imagem = MessageBoxImage::None,
                                 MessageBoxResult padrao = MessageBoxResult::None);
    static MessageBoxResult Show(Window *dono, const std::string &texto, const std::string &titulo = "",
                                 MessageBoxButton botoes = MessageBoxButton::OK,
                                 MessageBoxImage imagem = MessageBoxImage::None,
                                 MessageBoxResult padrao = MessageBoxResult::None);
};

// Microsoft.Win32.OpenFileDialog: navega pelas pastas, filtra pelas extensoes do Filter
// ("Imagens (*.jpg;*.png)|*.jpg;*.png|Todos|*.*") e devolve o caminho em FileName.
class OpenFileDialog
{
public:
    std::string Title;
    std::string Filter;
    int FilterIndex = 1;                 // 1 = o primeiro filtro, como no Windows
    std::string InitialDirectory;
    std::string FileName;
    std::vector<std::string> FileNames;
    bool Multiselect = false;
    bool CheckFileExists = true;
    std::optional<bool> ShowDialog(Window *dono = nullptr);
};

} // namespace ARCA::Vistas

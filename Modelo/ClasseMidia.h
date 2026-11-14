// ARCA no xOS - Modelo/ClasseMidia (de Modelo\ClasseMidia.cs).
#pragma once

#include "Interop/EditorFlac.h"

#include <string>
#include <vector>

namespace ARCA::Modelo {

/// O que uma faixa é: música, audiolivro ou fala (entrevista, humor).
enum class ConteudoFaixa { Musica = 0, Audiolivro = 1, Fala = 2 };

/// A classe de mídia do ASF: WM/MediaClassPrimaryID e SecondaryID.
///
/// É a marcação que o próprio formato prevê para dizer que um áudio não é
/// música — melhor que usar o Gênero, que continua livre ("Comédia" num CD
/// de piada). A classificação é por faixa: as entrevistas do Thriller são
/// fala dentro de um álbum de música, e continuam com o mesmo Álbum.
///
/// Os GUIDs são os da documentação do Windows Media Format SDK. A Microsoft
/// dá o stand-up como exemplo de "fala que não é audiolivro".
// LINUX: no FLAC os dois atributos são WM_MEDIACLASSPRIMARYID e WM_MEDIACLASSSECONDARYID (texto
// "{GUID}"), como a conversão do acervo gravou; os nomes ASF e os GUIDs continuam os mesmos.
class TClasseMidia
{
public:
    static const std::string AtributoPrimario;      // "WM/MediaClassPrimaryID"
    static const std::string AtributoSecundario;    // "WM/MediaClassSecondaryID"

    // LINUX: System.Guid vira texto na forma do Guid.ToString() (minúsculas, com hífens), que é
    // como o EditorFlac os devolve e compara.
    static const std::string guidPrimariaMusica;
    static const std::string guidPrimariaAudioNaoMusica;
    static const std::string guidSecundariaAudiolivro;
    static const std::string guidSecundariaFala;

    /// Grava a classe no arquivo aberto (sem Salvar — quem chama confirma).
    /// Música grava a primária de música e tira a secundária, para que uma
    /// faixa reclassificada não fique com a marca antiga.
    static bool Gravar(Interop::EditorFlac *objEditor, ConteudoFaixa eConteudo);

    /// A classe lida dos atributos do arquivo. Sem marca, ou com marca que
    /// não é nossa, é música — é o que o acervo inteiro é hoje.
    static ConteudoFaixa Ler(const std::vector<Interop::AtributoFlac> &lstAtributos);
};

} // namespace ARCA::Modelo

// ARCA no xOS - Modelo/ClasseMidia (de Modelo\ClasseMidia.cs).
#include "ClasseMidia.h"

#include "Base/Texto.h"

namespace ARCA::Modelo {

using namespace ARCA::Base;
using ARCA::Interop::AtributoFlac;
using ARCA::Interop::EditorFlac;
using ARCA::Interop::TipoAtributoFlac;

const std::string TClasseMidia::AtributoPrimario = "WM/MediaClassPrimaryID";
const std::string TClasseMidia::AtributoSecundario = "WM/MediaClassSecondaryID";

const std::string TClasseMidia::guidPrimariaMusica = "d1607dbc-e323-4be2-86a1-48a42a28441e";
const std::string TClasseMidia::guidPrimariaAudioNaoMusica = "01cd0f29-da4e-4157-897b-6275d50c4f11";
const std::string TClasseMidia::guidSecundariaAudiolivro = "e0236beb-c281-4ede-a36d-7af76a3d45b5";
const std::string TClasseMidia::guidSecundariaFala = "3a172a13-2bd9-4831-835b-114f6a95943f";

bool TClasseMidia::Gravar(EditorFlac *objEditor, ConteudoFaixa eConteudo)
{
    if (objEditor == nullptr) return false;
    if (eConteudo == ConteudoFaixa::Musica)
    {
        objEditor->RemoverAtributo(AtributoSecundario);
        return objEditor->DefinirGuid(AtributoPrimario, guidPrimariaMusica);
    }
    const std::string &guidSecundaria = eConteudo == ConteudoFaixa::Audiolivro ? guidSecundariaAudiolivro : guidSecundariaFala;
    return objEditor->DefinirGuid(AtributoPrimario, guidPrimariaAudioNaoMusica)
        && objEditor->DefinirGuid(AtributoSecundario, guidSecundaria);
}

ConteudoFaixa TClasseMidia::Ler(const std::vector<AtributoFlac> &lstAtributos)
{
    // Guid.Empty
    std::string guidPrimaria = "00000000-0000-0000-0000-000000000000", guidSecundaria = guidPrimaria;
    for (const AtributoFlac &objAtributo : lstAtributos)
    {
        if (objAtributo.Tipo != TipoAtributoFlac::Guid || objAtributo.Valor.empty()) continue;
        // try { guidValor = new Guid(objAtributo.Valor); } catch { continue; }
        std::string guidValor = EditorFlac::NormalizarGuid(objAtributo.Valor);
        if (guidValor.empty()) continue;
        if (IgualSemCaixa(objAtributo.Nome, AtributoPrimario)) guidPrimaria = guidValor;
        else if (IgualSemCaixa(objAtributo.Nome, AtributoSecundario)) guidSecundaria = guidValor;
    }
    if (guidPrimaria != guidPrimariaAudioNaoMusica) return ConteudoFaixa::Musica;
    if (guidSecundaria == guidSecundariaAudiolivro) return ConteudoFaixa::Audiolivro;
    return ConteudoFaixa::Fala;
}

} // namespace ARCA::Modelo

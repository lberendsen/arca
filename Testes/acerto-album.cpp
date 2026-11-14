// ARCA no xOS - acerto de um album ja integrado, a pedido do Lorenzo (nao e do ARCA original):
// grava WM/AlbumTitle e WM/AlbumArtist nas faixas FLAC da pasta, renomeia a pasta (no mesmo
// artista) e registra no ledger. Para os albuns integrados antes de 08/11/2026, quando as caixas
// Artista e Album ainda nao iam para as etiquetas.
//   acerto-album PASTA "NOVO NOME DA PASTA" "Artista do album" "Album"
// Usa a raiz do acervo de ARCA_ACERVO (padrao /mnt/DADOS/Músicas) e a remontagem em escrita do
// Base/Acervo.h. Quem chama confere antes que nenhuma faixa esta aberta e faz a copia de seguranca.
#include "Base/Acervo.h"
#include "Base/Arquivo.h"
#include "Base/Texto.h"
#include "Interop/EditorFlac.h"
#include "Modelo/Aquisicao.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <unistd.h>

using namespace ARCA;
namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    if (argc < 5) { printf("uso: acerto-album PASTA NOVO_NOME ARTISTA ALBUM\n"); return 2; }
    std::string strPasta = argv[1], strNovoNome = argv[2], strArtista = argv[3], strAlbum = argv[4];
    std::string strRaiz = Base::PastaAcervo();
    if (!Base::PastaExiste(strPasta) || !Base::ComecaCom(strPasta, strRaiz + "/"))
    {
        printf("recuso: %s nao e uma pasta dentro do acervo (%s)\n", strPasta.c_str(), strRaiz.c_str());
        return 2;
    }
    std::string strNova = Base::Combina(Base::PastaDe(strPasta), strNovoNome);
    if (strNova != strPasta && fs::exists(fs::u8path(strNova)))
    {
        printf("recuso: ja existe %s\n", strNova.c_str());
        return 2;
    }
    try
    {
        Base::GravacaoNoAcervo objGravacao;
        int intFaixas = 0;
        for (const std::string &a : Base::Arquivos(strPasta, ".flac"))
        {
            Interop::EditorFlac ed;
            if (!ed.Abrir(a)) { printf("FALHA ao abrir %s: %s\n", a.c_str(), ed.Falha().c_str()); return 1; }
            ed.DefinirTexto("WM/AlbumTitle", strAlbum);
            ed.DefinirTexto("WM/AlbumArtist", strArtista);
            if (!ed.Salvar()) { printf("FALHA ao gravar %s: %s\n", a.c_str(), ed.Falha().c_str()); return 1; }
            ed.Fechar();
            intFaixas++;
        }
        printf("etiquetas gravadas em %d faixa(s)\n", intFaixas);
        if (strNova != strPasta)
        {
            fs::rename(fs::u8path(strPasta), fs::u8path(strNova));
            printf("pasta renomeada: %s\n", strNova.c_str());
        }
        std::vector<std::string> lstArquivos;
        for (const auto &d : fs::recursive_directory_iterator(fs::u8path(strNova)))
            if (d.is_regular_file()) lstArquivos.push_back(d.path().u8string());
        std::sort(lstArquivos.begin(), lstArquivos.end());
        std::string strEvento = "álbum acertado a pedido: WM/AlbumTitle e WM/AlbumArtist gravados nas faixas";
        if (strNova != strPasta) strEvento += "; pasta antes: " + strPasta;
        bool ok = Modelo::Integracao::RegistrarEncarteNoLedger(strRaiz, strArtista, strAlbum, strNova, lstArquivos, strEvento);
        printf("ledger: %s\n", ok ? "registrado" : "FALHOU");
        sync();
        return ok ? 0 : 1;
    }
    catch (const std::exception &e)
    {
        printf("FALHA: %s\n", e.what());
        return 1;
    }
}

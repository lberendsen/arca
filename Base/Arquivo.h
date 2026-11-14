// ARCA no xOS - Base/Arquivo: File/Directory/Path do .NET e os caminhos do ARCA no xOS.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ARCA::Base {

namespace fs = std::filesystem;

// ---- caminhos (docs/arca/PORTE.md, secao 4) ----
// Cada um pode ser trocado por variavel de ambiente: e assim que os testes rodam contra um acervo
// de mentira, sem chegar perto do DADOS.
std::string PastaAcervo();        // ARCA_ACERVO   ou /mnt/DADOS/Músicas     ("Minhas Músicas")
std::string PastaTrabalho();      // ARCA_TRABALHO ou /var/lib/arca/trabalho ("_ARCA_Trabalho")
std::string PastaCache();         // ARCA_CACHE    ou /var/cache/arca        (%LOCALAPPDATA%\ARCA)
std::string ArquivoLedger();      // <PastaAcervo>/ARCA_Ledger.jsonl
std::string TokenDiscogs();       // conteudo de /var/lib/xos/discogs-token (ou ARCA_DISCOGS_TOKEN); "" se nao houver
std::string DispositivoCd();      // ARCA_CD ou /dev/sr0

// ---- File / Directory ----
bool Existe(const std::string &caminho);              // File.Exists || Directory.Exists
bool ArquivoExiste(const std::string &caminho);
bool PastaExiste(const std::string &caminho);
void CriaPasta(const std::string &caminho);           // Directory.CreateDirectory (com os pais)
int64_t Tamanho(const std::string &caminho);          // FileInfo.Length; -1 se nao existe
std::vector<uint8_t> LeBytes(const std::string &caminho);           // lanca std::runtime_error
std::string LeTexto(const std::string &caminho);                    // UTF-8, tira BOM; lanca
std::vector<std::string> LeLinhas(const std::string &caminho);      // sem \r\n; lanca
void GravaBytes(const std::string &caminho, const void *dados, size_t n);   // lanca
void GravaTexto(const std::string &caminho, const std::string &texto);      // lanca
void AcrescentaTexto(const std::string &caminho, const std::string &texto); // File.AppendAllText; lanca
// grava e forca ao disco (FileStream.Flush(true)): fsync no arquivo e na pasta
void GravaBytesSincronizado(const std::string &caminho, const void *dados, size_t n);
void SincronizaArquivo(const std::string &caminho);   // fsync de um arquivo ja gravado (e da pasta)

// ---- Path ----
std::string Combina(const std::string &a, const std::string &b);    // Path.Combine
std::string NomeDoArquivo(const std::string &caminho);              // GetFileName
std::string NomeSemExtensao(const std::string &caminho);            // GetFileNameWithoutExtension
std::string Extensao(const std::string &caminho);                   // GetExtension (com o ponto)
std::string PastaDe(const std::string &caminho);                    // GetDirectoryName
std::string TrocaExtensao(const std::string &caminho, const std::string &ext);   // ChangeExtension
// Path.GetInvalidFileNameChars() do Windows: o acervo e lido pela Windows 7 tambem, entao os nomes
// seguem as regras dela mesmo no Linux
const std::string &CaracteresInvalidosEmNome();   // \ / : * ? " < > | e controles

// lista (Directory.GetFiles / GetDirectories), em ordem alfabetica, sem recursao
std::vector<std::string> Arquivos(const std::string &pasta, const std::string &extensao = "");  // ".flac"; "" = todos
std::vector<std::string> Pastas(const std::string &pasta);

} // namespace ARCA::Base

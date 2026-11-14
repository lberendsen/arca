// ARCA no xOS - Modelo/TextoIdentificacao: auxiliar da frente de identificacao (nao vem de um .cs).
// LINUX: o GnuDbResolver, o DiscogsClient e o ItunesResolver do original comparam texto com o que o
// .NET da pronto: Normalize(FormD) + UnicodeCategory.NonSpacingMark, char.IsLetterOrDigit,
// ToLowerInvariant, as classes \w \s \d \p{L} \p{N} do Regex, o JavaScriptSerializer e o
// List<T>.Sort. O Base/Texto.h cobre o latim; as regras de pontuacao do original dependem do
// comportamento exato (o "Mari Cântăreți" do acervo e romeno; ha discos em grego e cirilico), entao
// aqui ficam as mesmas regras com as tabelas da Unicode (geradas do unicodedata do Python, so o BMP:
// no .NET, um caractere fora do BMP sao dois char substitutos, que nao sao letra, nem digito, nem
// espaco - e e assim que fica aqui tambem).
#pragma once

#include "Base/Json.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ARCA::Modelo::TextoNet {

// ---- categorias (char.IsXxx / classes do Regex do .NET) ----
bool EhLetra(uint32_t c);            // \p{L}, char.IsLetter
bool EhDigito(uint32_t c);           // \d, \p{Nd}, char.IsDigit
bool EhNumero(uint32_t c);           // \p{N}
bool EhLetraOuDigito(uint32_t c);    // char.IsLetterOrDigit
bool EhMarca(uint32_t c);            // UnicodeCategory.NonSpacingMark (\p{Mn})
bool EhPalavra(uint32_t c);          // \w = [\p{L}\p{Mn}\p{Nd}\p{Pc}]
bool EhEspaco(uint32_t c);           // \s, char.IsWhiteSpace

uint32_t Minuscula(uint32_t c);                        // char.ToLowerInvariant
std::string Minusculas(const std::string &s);          // string.ToLowerInvariant
// Normalize(FormD), sem os NonSpacingMark, e Normalize(FormC) de volta: o RemoverAcentos do original
// (o NormalizarTextoParaComparacao do ItunesResolver nao volta ao FormC; para o que ele compara da
// no mesmo, porque o que sobra sao letras-base; a excecao, o hangul, fica como esta nos dois casos)
std::string SemMarcas(const std::string &s);

// comparacao sem caixa por caractere (StringComparison.OrdinalIgnoreCase, RegexOptions.IgnoreCase)
bool IgualSemCaixa(uint32_t a, uint32_t b);
bool ComecaComSemCaixa(const std::string &s, const std::string &prefixo);
bool IgualSemCaixa(const std::string &a, const std::string &b);

// Regex.Replace(s, @"\s+", substituto)
std::string TrocaEspacos(const std::string &s, const std::string &substituto);

// Regex.Replace(s, @"\s*\((?!(?:ao vivo|live|acustico|acoustic|remastered|deluxe)\b)[^\)]+\)\s*$", "",
//               RegexOptions.IgnoreCase)
// - a mesma expressao esta no GnuDbResolver e no DiscogsClient do original
std::string TiraParentesesDoFim(const std::string &s);

// Regex.Replace(s, @"\s*[\(\[][^\]\)]*[\)\]]", substituto) e, com palavras, a variante que so
// casa se o trecho entre os colchetes tiver uma delas (sem caixa):
// @"\s*[\(\[][^\]\)]*(disc|cd|...)[^\]\)]*[\)\]]"
std::string TiraAnotacoes(const std::string &s, const std::string &substituto,
                          const std::vector<std::string> &palavras = {});

// Regex.Replace(s, @"\s+\(\d+\)$", "") - o "(2)" de desambiguacao do Discogs
std::string TiraSufixoNumerico(const std::string &s);

// ---- JavaScriptSerializer.Deserialize<Dictionary<string, object>> ----
// Lanca como o .NET: texto que nao e JSON, raiz que nao e objeto, ou texto maior que o
// MaxJsonLength padrao (2.097.152 caracteres).
Base::Json Desserializa(const std::string &strJson);
// objeto.ToString() de um valor do dicionario; null lanca NullReferenceException (como no original,
// em que "d[chave].ToString()" com valor null lanca)
std::string TextoDe(const Base::Json &v);
// Convert.ToInt32 / Convert.ToInt64 de um valor do dicionario (null = 0)
int ParaInt32(const Base::Json &v);
int64_t ParaInt64(const Base::Json &v);
// dic.ContainsKey(chave)
inline bool Tem(const Base::Json &d, const char *chave) { return d.is_object() && d.contains(chave); }

// ---- List<T>.Sort(Comparison<T>) ----
// O ARCA mira o .NET 4.0 (TargetFrameworkVersion v4.0): o Sort e o quicksort antigo do
// ArraySortHelper (DepthLimitedQuickSort, limite 32, heapsort ao estourar), que NAO e estavel. Em
// empate de pontuacao, quem fica na frente e o que esse algoritmo deixa na frente; std::sort ou
// std::stable_sort poderiam trocar o candidato tentado primeiro.
template <typename T, typename Cmp>
void OrdenaComoNet(std::vector<T> &v, Cmp comparar)
{
    auto swapSeMaior = [&](int a, int b) {
        if (a != b && comparar(v[a], v[b]) > 0) std::swap(v[a], v[b]);
    };
    std::function<void(int, int, int)> rapido;
    auto desce = [&](int i, int n, int lo) {
        T d = v[lo + i - 1];
        while (i <= n / 2)
        {
            int filho = 2 * i;
            if (filho < n && comparar(v[lo + filho - 1], v[lo + filho]) < 0) filho++;
            if (!(comparar(d, v[lo + filho - 1]) < 0)) break;
            v[lo + i - 1] = v[lo + filho - 1];
            i = filho;
        }
        v[lo + i - 1] = d;
    };
    auto heap = [&](int lo, int hi) {
        int n = hi - lo + 1;
        for (int i = n / 2; i >= 1; i--) desce(i, n, lo);
        for (int i = n; i > 1; i--)
        {
            if (lo != lo + i - 1) std::swap(v[lo], v[lo + i - 1]);
            desce(1, i - 1, lo);
        }
    };
    rapido = [&](int left, int right, int limite) {
        do
        {
            if (limite == 0) { heap(left, right); return; }
            int i = left, j = right;
            int meio = i + ((j - i) >> 1);
            swapSeMaior(i, meio);
            swapSeMaior(i, j);
            swapSeMaior(meio, j);
            T x = v[meio];
            do
            {
                while (comparar(v[i], x) < 0) i++;
                while (comparar(x, v[j]) < 0) j--;
                if (i > j) break;
                if (i < j) std::swap(v[i], v[j]);
                i++;
                j--;
            } while (i <= j);
            limite--;
            if (j - left <= right - i)
            {
                if (left < j) rapido(left, j, limite);
                left = i;
            }
            else
            {
                if (i < right) rapido(i, right, limite);
                right = j;
            }
        } while (left < right);
    };
    if (v.size() > 1) rapido(0, (int) v.size() - 1, 32);
}

} // namespace ARCA::Modelo::TextoNet

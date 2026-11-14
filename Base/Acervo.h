// ARCA no xOS - Base/Acervo: o DADOS e montado so de leitura (/etc/local.d/discos.start).
// LINUX: na Windows 7 o acervo estava sempre gravavel. Aqui, quem grava no acervo abre um
// GravacaoNoAcervo: enquanto ele existir, o DADOS esta em leitura e escrita; ao sair do escopo
// (dando certo ou com excecao) o DADOS volta a so leitura, com sync antes. Aninhado conta
// referencias: so o ultimo a sair remonta. Se o acervo nao estiver no DADOS (ARCA_ACERVO de teste),
// nao remonta nada.
#pragma once

#include <string>

namespace ARCA::Base {

class GravacaoNoAcervo
{
public:
    GravacaoNoAcervo();          // lanca std::runtime_error se nao conseguir remontar
    ~GravacaoNoAcervo();
    GravacaoNoAcervo(const GravacaoNoAcervo &) = delete;
    GravacaoNoAcervo &operator=(const GravacaoNoAcervo &) = delete;
};

// o ponto de montagem do acervo, se ele estiver num disco que o ARCA precisa remontar; "" se nao
std::string MontagemDoAcervo();

} // namespace ARCA::Base

// ARCA no xOS - Base/Despacho: o Dispatcher do WPF.
// Fios de trabalho (extracao, identificacao, codificacao) nao mexem na tela: pedem com Despacha(f),
// e o laco da janela (xos-arca.cpp) roda f no fio da tela. O laco espera eventos do X e tambem o
// descritor DescritorDespacho(), que fica legivel quando ha pedido na fila.
#pragma once

#include <functional>

namespace ARCA::Base {

void Despacha(std::function<void()> f);       // de qualquer fio; BeginInvoke
void DespachaEEspera(std::function<void()> f); // Invoke: espera rodar (nao chamar do fio da tela)
int DescritorDespacho();                       // para select()/poll() do laco da janela
void RodaDespachos();                          // no fio da tela: roda tudo o que estiver na fila
bool NoFioDaTela();                            // CheckAccess
void MarcaFioDaTela();                         // chamado uma vez pelo laco da janela

} // namespace ARCA::Base

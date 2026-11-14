// ARCA no xOS - Base/Despacho (ver Despacho.h).
#include "Despacho.h"

#include <condition_variable>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace ARCA::Base {

namespace {
std::mutex trava;
std::deque<std::function<void()>> fila;
int tubo[2] = {-1, -1};
std::thread::id fioTela;
bool marcado = false;

void AbreTubo()
{
    static std::once_flag f;
    std::call_once(f, [] {
        if (pipe(tubo) == 0)
        {
            fcntl(tubo[0], F_SETFL, O_NONBLOCK);
            fcntl(tubo[1], F_SETFL, O_NONBLOCK);
        }
    });
}
} // namespace

void MarcaFioDaTela()
{
    AbreTubo();
    fioTela = std::this_thread::get_id();
    marcado = true;
}

bool NoFioDaTela() { return !marcado || std::this_thread::get_id() == fioTela; }

int DescritorDespacho()
{
    AbreTubo();
    return tubo[0];
}

void Despacha(std::function<void()> f)
{
    AbreTubo();
    {
        std::lock_guard<std::mutex> g(trava);
        fila.push_back(std::move(f));
    }
    char c = 1;
    ssize_t n = write(tubo[1], &c, 1);    // acorda o laco; tubo cheio = ja vai acordar
    (void) n;
}

void DespachaEEspera(std::function<void()> f)
{
    if (NoFioDaTela())
    {
        f();
        return;
    }
    std::mutex m;
    std::condition_variable cv;
    bool feito = false;
    Despacha([&] {
        f();
        std::lock_guard<std::mutex> g(m);
        feito = true;
        cv.notify_one();
    });
    std::unique_lock<std::mutex> u(m);
    cv.wait(u, [&] { return feito; });
}

void RodaDespachos()
{
    char b[256];
    while (read(tubo[0], b, sizeof b) > 0) {}
    for (;;)
    {
        std::function<void()> f;
        {
            std::lock_guard<std::mutex> g(trava);
            if (fila.empty()) break;
            f = std::move(fila.front());
            fila.pop_front();
        }
        f();
    }
}

} // namespace ARCA::Base

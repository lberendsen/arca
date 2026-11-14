// ARCA no xOS - Base/Acervo (ver Acervo.h).
#include "Acervo.h"
#include "Arquivo.h"
#include "Texto.h"

#include <mutex>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace ARCA::Base {

namespace {
std::mutex trava;
int contagem = 0;

int Roda(const char *a0, const char *a1, const char *a2, const char *a3)
{
    pid_t p = fork();
    if (p == 0)
    {
        execlp(a0, a0, a1, a2, a3, (char *) nullptr);
        _exit(127);
    }
    if (p < 0) return -1;
    int st = 0;
    waitpid(p, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
} // namespace

std::string MontagemDoAcervo()
{
    const std::string dados = "/mnt/DADOS";
    std::string a = PastaAcervo();
    if (a == dados || ComecaCom(a, dados + "/")) return dados;
    return "";
}

GravacaoNoAcervo::GravacaoNoAcervo()
{
    std::lock_guard<std::mutex> g(trava);
    std::string m = MontagemDoAcervo();
    if (contagem++ > 0 || m.empty()) return;
    if (Roda("mount", "-o", "remount,rw", m.c_str()) != 0)
    {
        contagem--;
        throw std::runtime_error("não foi possível abrir o acervo para gravação (" + m + ")");
    }
}

GravacaoNoAcervo::~GravacaoNoAcervo()
{
    std::lock_guard<std::mutex> g(trava);
    std::string m = MontagemDoAcervo();
    if (--contagem > 0 || m.empty()) return;
    sync();
    Roda("mount", "-o", "remount,ro", m.c_str());
}

} // namespace ARCA::Base

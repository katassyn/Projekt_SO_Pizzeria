#include "szef.h"
#include "common.h"

static volatile sig_atomic_t flagaPozar = 0;

void handlerPozar(int sig) {
    // na razie tylko komunikat o tym ze jest pozar
    printf("[SZEF] Otrzymalem sygnal pozaru!\n");
    flagaPozar = 1;
}

void run_szef()
{
    printf("[SZEF] Start procesu kasjera (PID=%d)\n", getpid());

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handlerPozar;
    sigaction(SIGUSR1, &sa, NULL);

    // Glowna petla kasjera
    while (!flagaPozar) {
        // Tu w przyszlosci bedziemy odbierac komunikaty, obslugiwac klientow.
        usleep(500000);
    }

    printf("[SZEF] Koncze prace, bo sygnal o pozarze.\n");
}

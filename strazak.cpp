#include "strazak.h"
#include "common.h"

void run_strazak(pid_t kasjerPid)
{
    printf("[STRAŻAK] Start procesu strazaka (PID=%d)\n", getpid());

    sleep(5);
    printf("[STRAŻAK] Wysylam sygnal pozaru do kasjera (PID=%d)\n", kasjerPid);
    kill(kasjerPid, SIGUSR1);

    printf("[STRAŻAK] Koncze.\n");
    exit(0);
}

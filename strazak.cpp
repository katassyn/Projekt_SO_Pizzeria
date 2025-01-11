#include "strazak.h"
#include "common.h"

void run_strazak()
{
    printf("[STRAŻAK] Start procesu strazaka (PID=%d)\n", getpid());

    // Tymczasowo STRAŻAK jest tylko informacyjnie
    // Nie ma jak odebrac PID kasjera

    sleep(3);
    printf("[STRAŻAK] Na razie nic nie robie, bo nie znam PID kasjera.\n");

    printf("[STRAŻAK] Koncze.\n");
}

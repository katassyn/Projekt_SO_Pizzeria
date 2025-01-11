#include "klient.h"
#include "common.h"

void run_klient(int idGrupy)
{
    printf("[KLIENT %d] Start procesu (PID=%d)\n", idGrupy, getpid());

    // tymczasowa logika:
    sleep(2);

    printf("[KLIENT %d] Koncze dzialanie.\n", idGrupy);
}

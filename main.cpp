#include "common.h"
#include "szef.h"
#include "klient.h"
#include "strazak.h"

int main()
{
    printf("[MAIN] Uruchamiam symulacje Pizzerii.\n");

    //(1) uruchom kasjera
    pid_t kasjerPid = fork();
    if (kasjerPid == -1) {
        perror("fork kasjerPid");
        exit(EXIT_FAILURE);
    }
    if (kasjerPid == 0) {
        // Proces kasjera
        run_szef();
        exit(0);
    }

    //(2) uruchom kilku klientow
    for (int i = 0; i < LICZBA_GRUP; i++) {
        pid_t klientPid = fork();
        if (klientPid == -1) {
            perror("fork klient");
            exit(EXIT_FAILURE);
        }
        if (klientPid == 0) {
            run_klient(i);
            exit(0);
        }
    }

    //(3) uruchom strazaka (na razie bez pozaru, tylko informacja)
    pid_t strazakPid = fork();
    if (strazakPid == -1) {
        perror("fork strazak");
        exit(EXIT_FAILURE);
    }
    if (strazakPid == 0) {
        run_strazak(kasjerPid);
        exit(0);
    }

    //(4)czekamy na zakonczenie wszystkich procesow potomnych
    int status;
    for (int i = 0; i < (1 + LICZBA_GRUP + 1); i++) {
        wait(&status);
    }

    printf("[MAIN] Wszystkie procesy zakonczone. Koniec symulacji.\n");
    return 0;
}

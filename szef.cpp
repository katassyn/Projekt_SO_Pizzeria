#include "szef.h"
#include "common.h"

static volatile sig_atomic_t flagaPozar = 0;
static int msqid = -1;

void handlerPozar(int sig) {
    // na razie tylko komunikat o tym ze jest pozar
    printf("[SZEF] Otrzymalem sygnal pozaru!\n");
    flagaPozar = 1;
}

void run_szef()
{
    printf("[SZEF] Start procesu kasjera (PID=%d)\n", getpid());
    //(1) inicjalizacja obslugi pozaru
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handlerPozar;
    sigaction(SIGUSR1, &sa, NULL);

    //(2) tworzenie kolejki komunikatow
    key_t key = ftok(NAZWA_KOLEJKI, 123);  //"pizzeria" + 123 unikalny klucz
    if (key == -1) {
        perror("ftok");
        exit(EXIT_FAILURE);
    }
    msqid = msgget(key, 0666 | IPC_CREAT); //tworzenie kolejki
    if (msqid == -1) {
        perror("msgget");
        exit(EXIT_FAILURE);
    }
    printf("[SZEF] Utworzylem kolejke o msqid=%d\n", msqid);

    //(3) Glowna petla kasjera
    while (!flagaPozar) {
        //Odbieranie zadan
        MsgRequest req;
        ssize_t rcvSize = msgrcv(msqid, &req, sizeof(req) - sizeof(long),
                                 MSG_TYPE_REQUEST, IPC_NOWAIT);
        if (rcvSize > 0) {
            //Mamy nowego klienta z zapytaniem
            printf("[SZEF] Otrzymalem zapytanie od PID=%d (groupSize=%d)\n",
                   req.senderPid, req.groupSize);

            //Tu docelowo bedziemy sprawdzac czy jest wolny stolik
            //Na razie na sztywno przyznajemy:
            MsgResponse resp;
            resp.mtype = MSG_TYPE_RESPONSE; // typ odpowiedzi
            resp.accepted = 1;// 1=przyjmujemy
            resp.tableId = 42;// np. stolik nr 42

            //Wysylamy odp
            if (msgsnd(msqid, &resp, sizeof(resp) - sizeof(long), 0) < 0) {
                perror("[SZEF] msgsnd(resp)");
            }
            else {
                printf("[SZEF] Wyslalem odpowiedz do PID=%d\n", req.senderPid);
            }
        }
        else {
            //Brak komunikatu czekamy chwile
            usleep(200000);
        }
    }
    //(4) Gdy flagaPozar=1 -> sprzatanie i wyjscie
    printf("[SZEF] Kasuje kolejke...\n");
    if (msqid != -1) {
        if (msgctl(msqid, IPC_RMID, NULL) < 0) {
            perror("[SZEF] msgctl(IPC_RMID)");
        }
        else {
            printf("[SZEF] Kolejka usunieta.\n");
        }
    }

    printf("[SZEF] Koncze prace, bo sygnal o pozarze.\n");
}

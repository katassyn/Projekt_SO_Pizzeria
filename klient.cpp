#include "klient.h"
#include "common.h"

void run_klient(int idGrupy)
{
    printf("[KLIENT %d] Start procesu (PID=%d)\n", idGrupy, getpid());

    //(1)Otwieramy kolejke (ta sama co w szefie)
    key_t key = ftok(NAZWA_KOLEJKI, 123);
    if (key == -1) {
        perror("[KLIENT] ftok");
        exit(1);
    }
    int msqid = msgget(key, 0666);
    if (msqid < 0) {
        perror("[KLIENT] msgget");
        exit(1);
    }

    //(2) Wysylamy prosbe o stolik
    MsgRequest req;
    req.mtype = MSG_TYPE_REQUEST;  // typ zapytania
    req.groupSize = 2;             // np. 2-osobowa grupa sztywno
    req.senderPid = getpid();

    if (msgsnd(msqid, &req, sizeof(req) - sizeof(long), 0) < 0) {
        perror("[KLIENT] msgsnd");
        exit(1);
    }
    printf("[KLIENT %d] Wyslalem zapytanie o stolik.\n", idGrupy);

    //(3)Odbieramy odpowiedz
    MsgResponse resp;
    if (msgrcv(msqid, &resp, sizeof(resp) - sizeof(long), MSG_TYPE_RESPONSE, 0) < 0) {
        perror("[KLIENT] msgrcv");
        exit(1);
    }

    if (resp.accepted == 1) {
        printf("[KLIENT %d] Otrzymalem potwierdzenie, stolik ID=%d\n",
               idGrupy, resp.tableId);
    } else {
        printf("[KLIENT %d] Odmowa.\n", idGrupy);
    }
    //(4) Symulacja siedzenia przy stoliku
    sleep(2);

    printf("[KLIENT %d] Koncze dzialanie.\n", idGrupy);
}

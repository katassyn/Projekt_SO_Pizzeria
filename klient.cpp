#include "klient.h"
#include "common.h"
#include <time.h>
static volatile sig_atomic_t isEvacuation = 0;
extern int g_shmId;
extern int g_semId;
static SharedStats* g_statsC = nullptr;

void handlerWyjdz(int sig) {
    printf("[KLIENT] Odebralem sygnal %d, koncze natychmiast.\n", sig);
    isEvacuation = 1;
    _exit(0);
}

static void lockMutex()
{
    struct sembuf op;
    op.sem_num = MUTEX;
    op.sem_op  = -1;
    op.sem_flg = 0;
    if(semop(g_semId, &op, 1)<0){
        perror("[KLIENT] semop -1");
    }
}

static void unlockMutex()
{
    struct sembuf op;
    op.sem_num = MUTEX;
    op.sem_op  = +1;
    op.sem_flg = 0;
    if(semop(g_semId, &op, 1)<0){
        perror("[KLIENT] semop +1");
    }
}

void run_klient(int idGrupy)
{
    printf("[KLIENT %d] Start procesu (PID=%d)\n", idGrupy, getpid());

    //Rejestracja obslugi SIGUSR2
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handlerWyjdz;
    sigaction(SIGUSR2, &sa, nullptr);

    //(1)Otwieramy kolejke (ta sama co w szefie)
    key_t key = ftok(NAZWA_KOLEJKI, 123);
    if (key == -1) {
        perror("[KLIENT] ftok");
        exit(1);
    }
    int msqid = msgget(key, 0600);
    if (msqid < 0) {
        perror("[KLIENT] msgget");
        exit(1);
    }

    //(2) Wysylamy prosbe o stolik
    srand(time(nullptr) ^ (getpid()<<16));
    int randomSize = (rand() % 3) + 1; // 1-3os grupa
    int tableId = -1;
    int accepted = 0;

    while(!accepted && !globalEmergency) {
        if(!isEvacuation) {
            MsgRequest req;
            req.mtype = MSG_TYPE_REQUEST;
            req.groupSize = randomSize;
            req.senderPid = getpid();

            if (msgsnd(msqid, &req, sizeof(req) - sizeof(long), 0) < 0) {
                if(errno != EINTR) {
                    perror("[KLIENT] msgsnd");
                }
                _exit(1);
            }
            printf("[KLIENT %d] Wyslalem zapytanie o stolik (groupSize=%d).\n",
                   idGrupy, req.groupSize);

            // Odbieramy odpowiedz
            MsgResponse resp;
            if (msgrcv(msqid, &resp, sizeof(resp) - sizeof(long),
                       MSG_TYPE_RESPONSE, 0) < 0) {
                if(errno != EINTR) {
                    perror("[KLIENT] msgrcv");
                }
                _exit(1);
            }

            if (resp.accepted == 1) {
                accepted = 1;
                tableId = resp.tableId;
                printf("[KLIENT %d] Otrzymalem potwierdzenie, stolik ID=%d\n",
                       idGrupy, tableId);
            } else {
                //sprawdzanie ewakuacji by nie czekac na darmo
                if(globalEmergency) {
                    printf("[KLIENT %d] Przerywam czekanie z powodu ewakuacji.\n",
                           idGrupy);
                    _exit(0);
                }
                printf("[KLIENT %d] Odmowa. Czekam i probuje ponownie.\n", idGrupy);
                sleep(2);
            }
        }
    }

    //jesli ewakuacja to konczymy
    if(globalEmergency) {
        printf("[KLIENT %d] Przerywam z powodu ewakuacji.\n", idGrupy);
        _exit(0);
    }

    //(3)Symulacja siedzenia przy stoliku
    sleep(2);

    //dolaczenie do pamieci dzielonej
    g_statsC = (SharedStats*) shmat(g_shmId, nullptr, 0);
    if(g_statsC == (void*)-1){
        perror("[KLIENT] shmat");
        g_statsC = nullptr;
    }

    //(4)Zwalniamy stolik
    if(g_statsC && !isEvacuation){
        lockMutex();
        g_statsC->totalCustomersAtTable -= randomSize;
        if(g_statsC->totalCustomersAtTable<0){
            g_statsC->totalCustomersAtTable=0; //w razie bledow
        }
        //aktualiacja statystyki
//        g_statsC->totalCustomersServed += randomSize;
//        g_statsC->totalGroupsServed++;
//        g_statsC->groupsBySize[randomSize-1]++;
        unlockMutex();
    }

    // odlaczamy sie
    if(g_statsC){
        shmdt(g_statsC);
        g_statsC=nullptr;
    }

    //info o zwolnienu stolika
    if(!isEvacuation && !globalEmergency) {
        printf("[KLIENT %d] Koncze jedzenie. Zwalniam stolik ID=%d.\n",idGrupy, tableId);
        MsgLeave leaveMsg;
        leaveMsg.mtype = MSG_TYPE_LEAVE;
        leaveMsg.tableId = tableId;
        leaveMsg.groupSize = randomSize;
        leaveMsg.senderPid = getpid();

        if (msgsnd(msqid, &leaveMsg, sizeof(leaveMsg) - sizeof(long), 0) < 0) {
            if(errno != EINTR) {
                perror("[KLIENT] msgsnd(LEAVE)");
            }
        }
    }

    printf("[KLIENT %d] Koncze dzialanie.\n", idGrupy);
    _exit(0);
}

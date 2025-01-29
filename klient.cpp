#include "klient.h"
#include "common.h"
#include <time.h>
static volatile sig_atomic_t isEvacuation = 0;
extern volatile sig_atomic_t globalEmergency;
extern int g_shmId;
extern int g_semId;
static SharedStats* g_statsC = nullptr;
static pid_t childPids[MAX_CLIENTS];
static int childCount= 0;

void handlerWyjdz(int sig) {
    printf("[KLIENT - LIDER %d] Odebralem sygnal.\n", sig);
    isEvacuation = 1;
    for(int i = 0; i < childCount; i++) {
        if(childPids[i] > 0) {
            kill(childPids[i], SIGUSR2);
            waitpid(childPids[i], nullptr, 0);
            childPids[i] = 0;
        }
    }
    printf("[KLIENT - LIDER %d] Wszyscy czlonkowie zabici, lider konczy.\n",
           getpid());
    _exit(0);
}

static void pozarCzlonek(int sig)
{
    printf("[CZŁONEK %d] Otrzymalem sygnał %d (ewakuacja), konczę!\n",getpid(), sig);
    _exit(0);
}
static void lockMutex() {
    lockSemaphore(g_semId, MUTEX);
}

static void unlockMutex() {
    unlockSemaphore(g_semId, MUTEX);
}
static void runGroupMember(int tableId)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = pozarCzlonek;
    sa.sa_flags = 0;
    sigaction(SIGUSR2, &sa, nullptr);

    prctl(PR_SET_NAME, "klient-czlonek", 0, 0, 0);

    sleep(2);

    printf("[CZLONEK %d] Skonczyłem jedzenie (stolik %d). Koncze normalnie.\n",
           getpid(), tableId);
}

void run_klient(int idGrupy)
{

    prctl(PR_SET_NAME, "klient", 0, 0, 0);
    printf("[KLIENT - LIDER %d] Start procesu lidera grupy (PID=%d)\n", idGrupy, getpid());

    //Rejestracja obslugi SIGUSR2
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = handlerWyjdz;
    sa.sa_flags = 0;
    sigaction(SIGUSR2, &sa, nullptr);

    //(1)Otwieramy kolejke (ta sama co w szefie)
    key_t key = ftok(NAZWA_KOLEJKI, 123);
    if (key == -1) {
        perror("[KLIENT - LIDER] ftok");
        exit(1);
    }
    int msqid = msgget(key, 0600);
    if (msqid < 0) {
        perror("[KLIENT - LIDER] msgget");
        exit(1);
    }

    //(2) Wysylamy prosbe o stolik
    srand(getpid()^time(nullptr));
    int randomSize = (rand() % 3) + 1; // 1-3os grupa
    int tableId = -1;
    int accepted = 0;

    while(!accepted && !globalEmergency) {
        if(isEvacuation) {
            _exit(0);
        }

            MsgRequest req;
            req.mtype = MSG_TYPE_REQUEST;
            req.groupSize = randomSize;
            req.senderPid = getpid();

        if (msgsnd(msqid, &req, sizeof(req) - sizeof(long), 0) < 0) {
            if(errno == EINTR) {
                if(isEvacuation || globalEmergency) {
                    _exit(0);
                }
                // Przerwano przez sygnal
                continue;
            }
            else if(errno == EIDRM|| errno == EINVAL) {
                // Kolejka usunieta
                printf("[KLIENT - LIDER %d] Kolejka usunieta, koncze.\n", idGrupy);
                _exit(0);
            }
            perror("[KLIENT - LIDER] msgsnd");
            _exit(1);
        }

        printf("[KLIENT - LIDER %d] Wyslalem zapytanie o stolik (groupSize=%d).\n",
               idGrupy, req.groupSize);

        if(isEvacuation || globalEmergency) {
            _exit(0);
        }

        MsgResponse resp;
        ssize_t r = msgrcv(msqid, &resp, sizeof(resp) - sizeof(long),
                           MSG_TYPE_RESPONSE, 0);
        if(r < 0) {
            if(errno == EINTR) {
                // Przerwano przez sygnal
                if(isEvacuation || globalEmergency) {
                    _exit(0);
                }
                continue;
            }
            else if(errno == EIDRM) {
                // Kolejka usunieta
                printf("[KLIENT - LIDER %d] Kolejka usunieta, koncze.\n", idGrupy);
                _exit(0);
            }
            perror("[KLIENT - LIDER] msgrcv");
            _exit(1);
        }

            if (resp.accepted == 1) {
                accepted = 1;
                tableId = resp.tableId;
                printf("[KLIENT - LIDER %d] Otrzymalem potwierdzenie, stolik ID=%d\n",
                       idGrupy, tableId);
            } else {
                //sprawdzanie ewakuacji by nie czekac na darmo
                if(globalEmergency || isEvacuation) {
                    printf("[KLIENT - LIDER %d] Przerywam czekanie z powodu ewakuacji.\n",
                           idGrupy);
                    _exit(0);
                }
                //printf("[KLIENT - LIDER %d] Odmowa. Czekam i probuje ponownie.\n", idGrupy);
                //sleep(2);
            }

    }

    //jesli ewakuacja to konczymy
    if(globalEmergency || isEvacuation) {
        printf("[KLIENT - LIDER %d] Przerywam z powodu ewakuacji.\n", idGrupy);
        _exit(0);
    }

    //tworzymy pozostalych czlonkow grupy
    childCount = 0;
    for (int i = 1; i < randomSize; i++) {
        pid_t cpid = fork();
        if (cpid < 0) {
            perror("[KLIENT - LIDER] fork for member");
            continue;
        }
        if (cpid == 0) {
            // Proces-członek
            runGroupMember(tableId);
            _exit(0);
        } else {
            childPids[childCount++] = cpid;
        }
    }

    sleep(2);
    //dolaczenie do pamieci dzielonej
    g_statsC = (SharedStats*) shmat(g_shmId, nullptr, 0);
    if(g_statsC == (void*)-1){
        perror("[KLIENT - LIDER] shmat");
        g_statsC = nullptr;
    }

    //(4)Zwalniamy stolik
    if(g_statsC && !isEvacuation){
        lockSemaphore(g_semId, MUTEX);
        g_statsC->totalCustomersAtTable -= randomSize;
        if(g_statsC->totalCustomersAtTable<0){
            g_statsC->totalCustomersAtTable=0; //w razie bledow
        }
        //aktualiacja statystyki
//        g_statsC->totalCustomersServed += randomSize;
//        g_statsC->totalGroupsServed++;
//        g_statsC->groupsBySize[randomSize-1]++;
        unlockSemaphore(g_semId, MUTEX);
    }


    //info o zwolnienu stolika
    if(!isEvacuation && !globalEmergency) {
        printf("[KLIENT - LIDER %d] Koncze jedzenie. Zwalniam stolik ID=%d.\n",idGrupy, tableId);
        MsgLeave leaveMsg;
        leaveMsg.mtype = MSG_TYPE_LEAVE;
        leaveMsg.tableId = tableId;
        leaveMsg.groupSize = randomSize;
        leaveMsg.senderPid = getpid();

        if (msgsnd(msqid, &leaveMsg, sizeof(leaveMsg) - sizeof(long), 0) < 0) {
            if(errno != EINTR) {
                perror("[KLIENT - LIDER] msgsnd(LEAVE)");
            }
        }
    }
    int status;
    while (waitpid(-1, &status, 0) > 0) {
    }

    printf("[KLIENT %d] Koncze dzialanie.\n", idGrupy);
    if(g_statsC) {
        lockSemaphore(g_semId, PENDING_REQUESTS_MUTEX);
        g_statsC->pendingRequests--;
        unlockSemaphore(g_semId, PENDING_REQUESTS_MUTEX);
    }
    if(g_statsC){
        shmdt(g_statsC);
        g_statsC=nullptr;
    }
    _exit(0);
}

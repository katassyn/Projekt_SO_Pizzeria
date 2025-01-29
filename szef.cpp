#include "szef.h"
#include "common.h"

static volatile sig_atomic_t flagaPozar = 0;
static int msqid = -1;
static Table tables[50];  // max stolikow
static int tableCount = 0;//ile w uzyciu
static ClientQueue waitingQueue;
extern int g_shmId;
extern int g_semId;

static SharedStats* g_stats = nullptr; // wskaźnik do pamieci dzielonej

//Lista PID klientow
static pid_t clientPids[MAX_CLIENTS];
static int clientCount = 0;
static pid_t waitingClientPids[MAX_CLIENTS];
static int waitingClientCount = 0;
static void initQueue() {
    waitingQueue.front = 0;
    waitingQueue.rear = -1;
    waitingQueue.size = 0;
    waitingQueue.isShuttingDown = 0;
}
static void lockMutex()
{
    lockSemaphore(g_semId, MUTEX);
}

static void unlockMutex()
{
    unlockSemaphore(g_semId, MUTEX);
}
//lepsza obsluga zamykania
static void handleShutdown() {
    lockSemaphore(g_semId, SHUTDOWN_MUTEX);

    //flaga zamykania
    waitingQueue.isShuttingDown = 1;

    //powiadamianie klientow
    for(int i = 0; i < waitingClientCount; i++) {
        if(waitingClientPids[i] > 0) {
            kill(waitingClientPids[i], SIGUSR2);
            usleep(1000);
        }
    }

    for(int i = 0; i < clientCount; i++) {
        if(clientPids[i] > 0) {
            kill(clientPids[i], SIGUSR2);
            usleep(1000);
        }
    }
    //czyscimy
    for(int i = 0; i < clientCount; i++) {
        if(clientPids[i] > 0) {
            waitpid(clientPids[i], NULL, 0);
        }
    }

    unlockSemaphore(g_semId, SHUTDOWN_MUTEX);
}

void handlerPozar(int sig) {
    printf("[SZEF] Otrzymalem sygnal pozaru!\n");
    globalEmergency = 1;
    handleShutdown();
    flagaPozar = 1;
}

//Init stolikow
static void init_tables() {
    int idCounter = 1;

    // X1 stolikow 1-osobowych itd
    for(int i=0; i<X1; i++){
        tables[tableCount].id = idCounter++;
        tables[tableCount].capacity = 1;
        tables[tableCount].occupied = 0;
        tables[tableCount].groupSize = 0;
        tableCount++;
    }
    for(int i=0; i<X2; i++){
        tables[tableCount].id = idCounter++;
        tables[tableCount].capacity = 2;
        tables[tableCount].occupied = 0;
        tables[tableCount].groupSize = 0;
        tableCount++;
    }
    for(int i=0; i<X3; i++){
        tables[tableCount].id = idCounter++;
        tables[tableCount].capacity = 3;
        tables[tableCount].occupied = 0;
        tables[tableCount].groupSize = 0;
        tableCount++;
    }
    for(int i=0; i<X4; i++){
        tables[tableCount].id = idCounter++;
        tables[tableCount].capacity = 4;
        tables[tableCount].occupied = 0;
        tables[tableCount].groupSize = 0;
        tableCount++;
    }

    printf("[SZEF] Inicjalizacja stolikow: tableCount=%d\n", tableCount);
    for(int i=0; i<tableCount; i++){
        printf("[SZEF] Stolik ID=%d capacity=%d\n",
               tables[i].id, tables[i].capacity);
    }
}

/*
 (1) pusty o capacity == groupSize
 (2) Znajdz stolik juz zajety przez groupSize
 (3) Jesli nie ma, pusty o capacity > groupSize
*/
static int find_table(int groupSize)
{
    lockMutex();

    if(groupSize <= 0 || groupSize > 4) {
        printf("[SZEF] Blad: Nieprawidlowy rozmiar grupy: %d\n", groupSize);
        unlockMutex();
        return -1;
    }

    //(1) pusty o capacity == groupSize
    for(int i=0; i<tableCount; i++){
        if(tables[i].occupied==0 && tables[i].capacity==groupSize){
            tables[i].occupied = groupSize;
            tables[i].groupSize= groupSize;
            if(g_stats) {
                //lockMutex();
                g_stats->totalGroupsServed++;
                g_stats->groupsBySize[groupSize-1]++;
                g_stats->totalCustomersServed += groupSize;

            }
            unlockMutex();
            return tables[i].id;
        }
    }
    //(2) Znajdz stolik juz zajety przez groupSize
    for(int i=0; i<tableCount; i++){
        if(tables[i].occupied > 0 && tables[i].groupSize == groupSize) {
            int freeSeats = tables[i].capacity - tables[i].occupied;
            if(freeSeats >= groupSize) {
                tables[i].occupied += groupSize;
                if(g_stats) {
                    //lockMutex();
                    g_stats->totalGroupsServed++;
                    g_stats->groupsBySize[groupSize-1]++;
                    g_stats->totalCustomersServed += groupSize;

                }
                unlockMutex();
                return tables[i].id;
            }
        }
    }

    //(3)Jesli nie ma, pusty o capacity > groupSize
    for(int i=0; i<tableCount; i++){
        if(tables[i].occupied==0 && tables[i].capacity>groupSize){
            tables[i].occupied = groupSize;
            tables[i].groupSize= groupSize;
            if(g_stats) {
                //lockMutex();
                g_stats->totalGroupsServed++;
                g_stats->groupsBySize[groupSize-1]++;
                g_stats->totalCustomersServed += groupSize;

            }
            unlockMutex();
            return tables[i].id;
        }
    }
    // brak
    unlockMutex();
    return -1;
}

static void free_table(int tableId, int groupSize)
{
    lockMutex();
    if(tableId <= 0 || groupSize <= 0 || groupSize > 4) {
        printf("[SZEF] Blad: Nieprawidlowe parametry zwalniania stolika (id=%d, groupSize=%d)\n",
               tableId, groupSize);
        unlockMutex();
        return;
    }

    for(int i=0; i<tableCount; i++){
        if(tables[i].id == tableId){
            printf("[SZEF] Zwalniam %d miejsc w stoliku ID=%d (before occupied=%d)\n",
                   groupSize, tableId, tables[i].occupied);
            tables[i].occupied -= groupSize;
            if(tables[i].occupied < 0){
                tables[i].occupied = 0;
            }
            if(tables[i].occupied==0){
                tables[i].groupSize=0;
            }
            break;
        }
    }
    unlockMutex();
}

static void addClientPid(pid_t pid)
{
    lockSemaphore(g_semId, QUEUE_MUTEX);
    for(int i=0; i<clientCount; i++){
        if(clientPids[i] == pid) {
            unlockSemaphore(g_semId, QUEUE_MUTEX);
            return;
        }
    }
    if(clientCount<MAX_CLIENTS){
        clientPids[clientCount++] = pid;
        printf("[SZEF] Zarejestrowalem PID klienta=%d (clientCount=%d)\n", pid, clientCount);
    } else {
        printf("[SZEF] UWAGA: Brak miejsca w clientPids!\n");
    }
    unlockSemaphore(g_semId, QUEUE_MUTEX);
}

static void addWaitingClientPid(pid_t pid)
{
    for(int i=0; i<waitingClientCount; i++){
        if(waitingClientPids[i] == pid) {
            return;
        }
    }
    if(waitingClientCount<MAX_CLIENTS){
        waitingClientPids[waitingClientCount++] = pid;
        printf("[SZEF] Zarejestrowalem klienta w kolejce PID=%d (waitingClientCount=%d)\n",
               pid, waitingClientCount);
    } else {
        printf("[SZEF] UWAGA: Brak miejsca w waitingClientPids!\n");
    }
}

static void removeWaitingClientPid(pid_t pid)
{
    for(int i=0; i<waitingClientCount; i++){
        if(waitingClientPids[i] == pid){
            for(int j=i; j<waitingClientCount-1; j++){
                waitingClientPids[j] = waitingClientPids[j+1];
            }
            waitingClientCount--;
            printf("[SZEF] Usunalem klienta z kolejki PID=%d (waitingClientCount=%d)\n",
                   pid, waitingClientCount);
            return;
        }
    }
}

void printSessionStats() {
    if(!g_stats) return;
    FILE* file = fopen("statystykiSesji.txt", "a");
    if(!file) {
        perror("[SZEF] File error");
        return;
    }

    printf("\n%s=== Statystyki sesji ===%s\n", MAGENTA, RESET);
    printf("%sŁączna liczba obsłużonych klientów: %d%s\n",GREEN, g_stats->totalCustomersServed, RESET);
    printf("%sŁączna liczba obsłużonych grup: %d%s\n",BLUE, g_stats->totalGroupsServed, RESET);
    printf("%sGrupy według rozmiaru:%s\n", YELLOW, RESET);
    printf("1-osobowe: %d\n", g_stats->groupsBySize[0]);
    printf("2-osobowe: %d\n", g_stats->groupsBySize[1]);
    printf("3-osobowe: %d\n", g_stats->groupsBySize[2]);
    printf("4-osobowe: %d\n", g_stats->groupsBySize[3]); // dodatek by sprawdzic czy nie ma bledow
    printf("%s=============================\n%s", MAGENTA, RESET);

    fprintf(file, "\n=== Statystyki sesji ===\n");
    fprintf(file, "Łączna liczba obsłużonych klientów: %d\n", g_stats->totalCustomersServed);
    fprintf(file, "Łączna liczba obsłużonych grup: %d\n", g_stats->totalGroupsServed);
    fprintf(file, "Grupy według rozmiaru:\n");
    fprintf(file, "1-osobowe: %d\n", g_stats->groupsBySize[0]);
    fprintf(file, "2-osobowe: %d\n", g_stats->groupsBySize[1]);
    fprintf(file, "3-osobowe: %d\n", g_stats->groupsBySize[2]);
    fprintf(file, "4-osobowe: %d\n", g_stats->groupsBySize[3]);
    fprintf(file, "=============================\n");

    fclose(file);
}

static void addQueue(pid_t pid, int groupSize) {
    lockSemaphore(g_semId, QUEUE_MUTEX);
    if (waitingQueue.size >= MAX_CLIENTS) {
        printf("[SZEF] Kolejka jest pelna!\n");
        unlockSemaphore(g_semId, QUEUE_MUTEX);
        return;
    }

    waitingQueue.rear = (waitingQueue.rear + 1) % MAX_CLIENTS;
    waitingQueue.entries[waitingQueue.rear].pid = pid;
    waitingQueue.entries[waitingQueue.rear].groupSize = groupSize;
    waitingQueue.size++;

    printf("[SZEF] Dodano do kolejki PID=%d (pozycja=%d)\n", pid, waitingQueue.size);
    unlockSemaphore(g_semId, QUEUE_MUTEX);
}

static QueueEntry delQueue() {
    lockSemaphore(g_semId, QUEUE_MUTEX);
    QueueEntry entry = {0, 0};
    if (waitingQueue.size <= 0) {
        unlockSemaphore(g_semId, QUEUE_MUTEX);
        return entry;
    }

    entry = waitingQueue.entries[waitingQueue.front];
    waitingQueue.front = (waitingQueue.front + 1) % MAX_CLIENTS;
    waitingQueue.size--;

    unlockSemaphore(g_semId, QUEUE_MUTEX);
    return entry;
}

static bool isNextInQueue(pid_t pid) {
    lockSemaphore(g_semId, QUEUE_MUTEX);
    if (waitingQueue.size <= 0) {
        unlockSemaphore(g_semId, QUEUE_MUTEX);
        return false;
    }
    bool result = waitingQueue.entries[waitingQueue.front].pid == pid;
    unlockSemaphore(g_semId, QUEUE_MUTEX);
    return result;
}

static bool isInQueue(pid_t pid) {
    lockSemaphore(g_semId, QUEUE_MUTEX);
    for(int i = 0; i < waitingQueue.size; i++) {
        if(waitingQueue.entries[waitingQueue.front + i].pid == pid) {
            unlockSemaphore(g_semId, QUEUE_MUTEX);
            return true;
        }
    }
    unlockSemaphore(g_semId, QUEUE_MUTEX);
    return false;
}
void run_szef()
{
    prctl(PR_SET_NAME, "kasjer", 0, 0, 0);
    // Dolacz do pamieci
    g_stats = (SharedStats*) shmat(g_shmId, nullptr, 0);
    if(g_stats==(void*)-1){
        perror("[SZEF] shmat");
        g_stats = nullptr;
    }

    printf("[SZEF] Start procesu kasjera (PID=%d)\n", getpid());

    // obsluga pozaru
    struct sigaction sa;
    memset(&sa,0,sizeof(sa));
    sa.sa_handler = handlerPozar;
    sigaction(SIGUSR1, &sa, nullptr);

    // tworzenie kolejki
    key_t key = ftok(NAZWA_KOLEJKI, 123);
    if(key==-1){
        perror("[SZEF] ftok");
        exit(1);
    }
    msqid = msgget(key, 0600 | IPC_CREAT);
    if(msqid<0){
        perror("[SZEF] msgget");
        exit(1);
    }
    printf("[SZEF] Utworzylem kolejke o msqid=%d\n", msqid);
    printSessionStats();
    init_tables();
    initQueue();
    // Pentla glowna
    signal(SIGPIPE, SIG_IGN);
    while(!flagaPozar) {
        // Odbieranie MSG_TYPE_REQUEST
        MsgRequest req;
        ssize_t rcvSize = msgrcv(msqid, &req, sizeof(req)-sizeof(long),
                                 MSG_TYPE_REQUEST, IPC_NOWAIT);
        if(rcvSize>0){
            printf("[SZEF] Otrzymalem zapytanie od PID=%d (groupSize=%d)\n",
                   req.senderPid, req.groupSize);

            MsgResponse resp;
            resp.mtype = MSG_TYPE_RESPONSE;
            //sprawdzamy czy kolejak istanieje
            if(waitingQueue.size > 0) {
                if(isNextInQueue(req.senderPid)) { //czy dany klient jest w niej pueerwszy
                    int tid = find_table(req.groupSize);
                    if(tid >= 0) {
                        resp.accepted = 1;
                        resp.tableId = tid;
                        printf("[SZEF] Przydzielam stolik ID=%d dla groupSize=%d\n",tid, req.groupSize);
                        delQueue();
                        removeWaitingClientPid(req.senderPid);
                        addClientPid(req.senderPid);
                        if(g_stats) {
                            lockMutex();
                            g_stats->totalCustomersAtTable += req.groupSize;
                            unlockMutex();
                        }
                    } else {
                        resp.accepted = 0;
                        resp.tableId = -1;
                        printf("[SZEF] Brak miejsca dla groupSize=%d\n", req.groupSize);
                    }
                } else {
                    if(!isInQueue(req.senderPid)) {
                        addQueue(req.senderPid, req.groupSize);
                        addWaitingClientPid(req.senderPid);
                        printf("[SZEF] Dodano klienta %d do kolejki, pozycja %d\n",
                               req.senderPid, waitingQueue.size);
                    }
                    resp.accepted = 0;
                    resp.tableId = -1;
                }
            } else {
                // Kolejka jest pusta
                int tid = find_table(req.groupSize);
                if(tid >= 0) {
                    resp.accepted = 1;
                    resp.tableId = tid;
                    printf("[SZEF] Przydzielam stolik ID=%d dla groupSize=%d\n",tid, req.groupSize);
                    addClientPid(req.senderPid);
                    if(g_stats) {
                        lockMutex();
                        g_stats->totalCustomersAtTable += req.groupSize;
                        unlockMutex();
                    }
                } else {
                    resp.accepted = 0;
                    resp.tableId = -1;
                    addQueue(req.senderPid, req.groupSize);
                    addWaitingClientPid(req.senderPid);
                        printf("[SZEF] Brak miejsca, dodano klienta %d do kolejki\n",req.senderPid);
                }
            }

            //wysylamy odp
            int retry_count = 0;
            while(retry_count < 3) { // 3 proby zapytania
                if(msgsnd(msqid, &resp, sizeof(resp)-sizeof(long), IPC_NOWAIT) >= 0) {
                    printf("[SZEF] Wyslalem odpowiedz do PID=%d\n", req.senderPid);
                    break;
                } else {
                    if(errno == EAGAIN) {
                        retry_count++;
                        usleep(10000);
                        continue;
                    } else if(errno == EINTR) {
                        if(flagaPozar) break;
                        continue;
                    } else {
                        perror("[SZEF] msgsnd(resp)");
                        break;
                    }
                }
            }
        } else if(rcvSize < 0) {
            if(errno == EINTR) {
                if(flagaPozar) break;
                continue;
            } else if(errno != ENOMSG) {
                perror("[SZEF] msgrcv(MSG_TYPE_REQUEST)");
            }
        }

        // Odbieranie MSG_TYPE_LEAVE
        MsgLeave leaveMsg;
        ssize_t rcvSize2 = msgrcv(msqid, &leaveMsg, sizeof(leaveMsg)-sizeof(long),
                                  MSG_TYPE_LEAVE, IPC_NOWAIT);
        if(rcvSize2 > 0) {
            printf("[SZEF] Otrzymalem info o wyjsciu klienta PID=%d, stolik=%d\n",
                   leaveMsg.senderPid, leaveMsg.tableId);
            free_table(leaveMsg.tableId, leaveMsg.groupSize);
        } else if(rcvSize2<0) {
            if(errno == EINTR) {
                if(flagaPozar) break;
                continue;
            } else if(errno!=ENOMSG){
                perror("[SZEF] msgrcv(MSG_TYPE_LEAVE)");
            }
        }

        if(rcvSize < 0 && rcvSize2 < 0 && errno == ENOMSG) {
            usleep(100000);
        }
    }    printSessionStats();

    printf("[SZEF] Kasuje kolejke...\n");
    if(msqid>=0){
        if(msgctl(msqid, IPC_RMID, nullptr)<0){
            perror("[SZEF] msgctl(IPC_RMID)");
        } else {
            printf("[SZEF] Kolejka usunieta.\n");
        }
    }

    // Odlaczamy sie od pamieci
    if(g_stats){
        shmdt(g_stats);
        g_stats=nullptr;
    }

    printf("[SZEF] Koncze prace, bo sygnal o pozarze.\n");
}

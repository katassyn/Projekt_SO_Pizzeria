#include "szef.h"
#include "common.h"

static volatile sig_atomic_t flagaPozar = 0;
static int msqid = -1;
static Table tables[50];  // max stolikow
static int tableCount = 0;//ile w uzyciu

extern int g_shmId;
extern int g_semId;

static SharedStats* g_stats = nullptr; // wskaźnik do pamieci dzielonej

//Lista PID klientow
static pid_t clientPids[MAX_CLIENTS];
static int clientCount = 0;
static pid_t waitingClientPids[MAX_CLIENTS];
static int waitingClientCount = 0;
static void lockMutex()
{
    struct sembuf op;
    op.sem_num = MUTEX;
    op.sem_op  = -1;//  (zablokowanie)
    op.sem_flg = 0;
    if(semop(g_semId, &op, 1) < 0){
        perror("[SZEF] semop -1");
    }
}

static void unlockMutex()
{
    struct sembuf op;
    op.sem_num = MUTEX;
    op.sem_op  = +1; // (oblokowanie)
    op.sem_flg = 0;
    if(semop(g_semId, &op, 1) < 0){
        perror("[SZEF] semop +1");
    }
}

void handlerPozar(int sig) {
    printf("[SZEF] Otrzymalem sygnal pozaru!\n");
    flagaPozar = 1;
    globalEmergency = 1;

    // Ewakuacja klientow w lokalu
    printf("[SZEF] Ewakuacja klientow w lokalu (%d)...\n", clientCount);
    for(int i = 0; i < clientCount; i++){
        if (clientPids[i] > 0) {
            kill(clientPids[i], SIGUSR2);
        }
    }
    //czekamy na zamknecie wszystkich
    for(int i = 0; i < clientCount; i++){
        if (clientPids[i] > 0) {
            waitpid(clientPids[i], nullptr, 0);
            clientPids[i] = 0;
        }
    }
    clientCount = 0;

    // Ewakuacja klientow w kolejce
    printf("[SZEF] Ewakuacja klientow w kolejce (%d)...\n", waitingClientCount);
    for(int i = 0; i < waitingClientCount; i++){
        if (waitingClientPids[i] > 0) {
            kill(waitingClientPids[i], SIGUSR2);
        }
    }
    for(int i = 0; i < waitingClientCount; i++){
        if (waitingClientPids[i] > 0) {
            waitpid(waitingClientPids[i], nullptr, 0);
            waitingClientPids[i] = 0;
        }
    }
    waitingClientCount = 0;

    // Resetuj stan stolikow
    for(int i=0; i<tableCount; i++) {
        tables[i].occupied = 0;
        tables[i].groupSize = 0;
    }

    printf("[SZEF] Wszyscy klienci opuscili lokal i kolejke.\n");
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
    for(int i=0; i<clientCount; i++){
        if(clientPids[i] == pid) {
            return;
        }
    }
    if(clientCount<MAX_CLIENTS){
        clientPids[clientCount++] = pid;
        printf("[SZEF] Zarejestrowalem PID klienta=%d (clientCount=%d)\n", pid, clientCount);
    } else {
        printf("[SZEF] UWAGA: Brak miejsca w clientPids!\n");
    }
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

    printf("\n%s=== Statystyki sesji ===%s\n", MAGENTA, RESET);
    printf("%sŁączna liczba obsłużonych klientów: %d%s\n",GREEN, g_stats->totalCustomersServed, RESET);
    printf("%sŁączna liczba obsłużonych grup: %d%s\n",BLUE, g_stats->totalGroupsServed, RESET);
    printf("%sGrupy według rozmiaru:%s\n", YELLOW, RESET);
    printf("1-osobowe: %d\n", g_stats->groupsBySize[0]);
    printf("2-osobowe: %d\n", g_stats->groupsBySize[1]);
    printf("3-osobowe: %d\n", g_stats->groupsBySize[2]);
    printf("4-osobowe: %d\n", g_stats->groupsBySize[3]); // dodatek by sprawdzic czy nie ma bledow
    printf("%s=============================\n%s", MAGENTA, RESET);
}

void run_szef()
{
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

    // Pentla glowna
    while(!flagaPozar) {
        // Odbieranie MSG_TYPE_REQUEST
        MsgRequest req;
        ssize_t rcvSize = msgrcv(msqid, &req, sizeof(req)-sizeof(long),
                                 MSG_TYPE_REQUEST, IPC_NOWAIT);
        if(rcvSize>0){
            printf("[SZEF] Otrzymalem zapytanie od PID=%d (groupSize=%d)\n",
                   req.senderPid, req.groupSize);

            int tid = find_table(req.groupSize);

            MsgResponse resp;
            resp.mtype = MSG_TYPE_RESPONSE;

            if(tid<0){
                resp.accepted = 0;
                resp.tableId  = -1;
                printf("[SZEF] Brak miejsca dla groupSize=%d\n", req.groupSize);
                addWaitingClientPid(req.senderPid);
            } else {
                resp.accepted = 1;
                resp.tableId  = tid;
                printf("[SZEF] Przydzielam stolik ID=%d dla groupSize=%d\n", tid, req.groupSize);
                removeWaitingClientPid(req.senderPid);
                addClientPid(req.senderPid);

                //Zwiekszamy liczbe klientow
                if(g_stats){
                    lockMutex();
                    g_stats->totalCustomersAtTable += req.groupSize;
                    unlockMutex();
                }
            }

            if(msgsnd(msqid, &resp, sizeof(resp)-sizeof(long), 0)<0){ //blad wyslania wiad
                perror("[SZEF] msgsnd(resp)");
            } else {
                printf("[SZEF] Wyslalem odpowiedz do PID=%d\n", req.senderPid);
            }
        }
        else if(rcvSize<0 && errno!=ENOMSG){  //otrzymanie blednej wiad
            perror("[SZEF] msgrcv(MSG_TYPE_REQUEST)");
        }

        // Odbieranie MSG_TYPE_LEAVE
        MsgLeave leaveMsg;
        ssize_t rcvSize2 = msgrcv(msqid, &leaveMsg, sizeof(leaveMsg)-sizeof(long),
                                  MSG_TYPE_LEAVE, IPC_NOWAIT);
        if(rcvSize2>0){
            printf("[SZEF] Otrzymalem info o wyjsciu klienta PID=%d, stolik=%d\n",
                   leaveMsg.senderPid, leaveMsg.tableId);
            free_table(leaveMsg.tableId, leaveMsg.groupSize);
        }
        else if(rcvSize2<0 && errno!=ENOMSG){
            perror("[SZEF] msgrcv(MSG_TYPE_LEAVE)");
        }
        //zwalnianie pentli
        if(rcvSize<0 && rcvSize2<0){
            usleep(200000);
        }
    }
    printSessionStats();

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

#include "common.h"
#include "szef.h"
#include "klient.h"
#include "strazak.h"

int g_shmId = -1;
int g_semId = -1;
// pozarowa

volatile sig_atomic_t globalEmergency = 0;
bool stopSpawning = false;
//flaga do kasjera
bool kasjerIsDying = false;
// Struktura do inicjalizacji semafora
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

pid_t startKasjer();

static void createSharedMemoryAndSemaphore();
static void removeSharedMemoryAndSemaphore();

static volatile sig_atomic_t stopMain = 0;

//handler ctrl+c
void handlerStop(int sig) {
    printf("[MAIN] Otrzymano sygnal %d, koncze prace main.\n", sig);
    stopMain = 1;
}

//Funkcja uruchamiająca kasjera
pid_t startKasjer()
{
    pid_t pid = fork();
    if(pid == -1){
        perror("[MAIN] fork kasjer");
        return -1;
    }
    if(pid == 0){
        run_szef(); // kasjer
        _exit(0);
    }
    printf("[MAIN] Uruchomilem kasjera PID=%d\n", pid);
    return pid;
}

//Tworzenie pamieci + semafora
static void createSharedMemoryAndSemaphore()
{
    // Plik do ftok
    int fd = open(NAZWA_KOLEJKI, O_CREAT | O_RDWR, 0600);
    if(fd<0){
        perror("[MAIN] open NAZWA_KOLEJKI");
        exit(1);
    }
    close(fd);

    key_t shmKey = ftok(NAZWA_KOLEJKI, 321);
    if(shmKey==-1){
        perror("[MAIN] ftok shm");
        exit(1);
    }
    // Segment pamięci
    g_shmId = shmget(shmKey, sizeof(SharedStats), 0600 | IPC_CREAT);
    if(g_shmId<0){
        perror("[MAIN] shmget");
        exit(1);
    }
    printf("[MAIN] Stworzylem segment pamieci dzielonej shmId=%d\n", g_shmId);
    SharedStats* stats = (SharedStats*)shmat(g_shmId, nullptr, 0);
    if(stats != (void*)-1) {
        stats->totalCustomersAtTable = 0;
        stats->totalCustomersServed = 0;
        stats->totalGroupsServed = 0;
        stats->pendingRequests = 0;
        memset(stats->groupsBySize, 0, sizeof(stats->groupsBySize));
        shmdt(stats);
    }
    // Tworzenie semafora
    key_t semKey = ftok(NAZWA_KOLEJKI, 312);
    if(semKey==-1){
        perror("[MAIN] ftok sem");
        exit(1);
    }
    g_semId = semget(semKey, TOTAL_SEMS, 0600 | IPC_CREAT);
    if(g_semId<0){
        perror("[MAIN] semget");
        exit(1);
    }
    union semun arg;
    for(int i = 0; i < TOTAL_SEMS; i++) {
        arg.val = 1;
        if(semctl(g_semId, i, SETVAL, arg)<0){
            perror("[MAIN] semctl SETVAL");
            exit(1);
        }
    }
}

// Usuwanie pamieci + semafora
static void removeSharedMemoryAndSemaphore()
{
    if(g_shmId >= 0){
        if(shmctl(g_shmId, IPC_RMID, nullptr) < 0){
            perror("[MAIN] shmctl(IPC_RMID)");
        } else {
            printf("[MAIN] Usunalem segment pamieci dzielonej (shmId=%d).\n", g_shmId);
        }
        g_shmId = -1;
    }
    if(g_semId >= 0){
        if(semctl(g_semId, 0, IPC_RMID) < 0){
            perror("[MAIN] semctl(IPC_RMID)");
        } else {
            printf("[MAIN] Usunalem semafor (semId=%d).\n", g_semId);
        }
        g_semId = -1;
    }
}

int main(int argc, char** argv)
{

    //(1) Walidacja parametrow
    if(argc<2){
        fprintf(stderr, "Uzycie: %s X\n", argv[0]);
        return 1;
    }
    char *endptr;
    errno = 0;
    long val = strtol(argv[1], &endptr, 10);
    if (errno == ERANGE) {
        fprintf(stderr, "[MAIN] Blad: Liczba poza zakresem\n");
        return 1;
    }
    if (endptr == argv[1]) {
        fprintf(stderr, "[MAIN] Blad: Nie znaleziono liczby\n");
        return 1;
    }
    if (*endptr != '\0') {
        fprintf(stderr, "[MAIN] Blad: Nieprawidlowe znaki po liczbie\n");
        return 1;
    }
    int X = (int)val;
    if(X<=0 || X>=480)
    {
        fprintf(stderr, "[MAIN] Blad: X musi byc > 0 i mniejszy od 480.\n");
        return 1;
    }
    // Walidacja  stolikow
    if(X1 <= 0 || X2 <= 0 || X3 <= 0 || X4 <= 0) {
        fprintf(stderr, "[MAIN] Blad: Liczba stolikow kazdego typu musi byc > 0\n");
        return 1;
    }

    if(MAX_CLIENTS < 100) {
        fprintf(stderr, "[MAIN] Blad: MAX_CLIENTS musi byc >= 100\n");
        return 1;
    }
    // Rejestracja handlera ctrl+c
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handlerStop;
    sigaction(SIGINT, &sa, nullptr);
    createSharedMemoryAndSemaphore();

    printf("[MAIN] Uruchamiam symulacje Pizzerii na %d minut.\n", X);

    srand(time(nullptr));
    double pClient = 0.8;  // szansa na klienta
    double pStrazak= 0.05;  //szansa na strazaka

    time_t startTime = time(nullptr);

    pid_t kasjerPid = startKasjer(); // Pierwszy kasjer
    if(kasjerPid<0) return 1;

    bool cleaning = false;
    bool fireAlreadyCalled = false;
    time_t cleaningStart=0;

    int totalClientow=0;
    int totalStrazakow=0;
    //(2) glowna petla
    while(!stopMain) {
        double elapsedSec = difftime(time(nullptr), startTime);
        double elapsedMin = elapsedSec / 60.0;
        if(elapsedMin >= X){
            printf("[MAIN] Uplynelo %d minut. Koncze petle.\n", X);
            break;
        }

        // Sprawdzamy stan kasjera
        if(kasjerPid > 0) {
            //sleep(1);//dajemy czas na otwarcie kolejki
            int status;
            pid_t w = waitpid(kasjerPid, &status, WNOHANG);
            if(w == kasjerPid || globalEmergency) {
                //Kasjer zakonczyl prace
                if(w == kasjerPid) {
                    printf("[MAIN] Kasjer PID=%d zakonczyl prace.\n", kasjerPid);
                }
                kasjerPid = -1;
                cleaning = true;
                cleaningStart = time(nullptr);
                printf("[MAIN] Rozpoczynam przerwe 5s...\n");
                continue;
            }

            //Kasjer dziala i nie ma czyszczenia
            if(!cleaning && !kasjerIsDying && !stopSpawning) {
                double r = (rand()/(double)RAND_MAX);
                if(r < pClient) {
                    SharedStats* stats = (SharedStats*)shmat(g_shmId, nullptr, 0);
                    if(stats != (void*)-1){
                        lockSemaphore(g_semId,PENDING_REQUESTS_MUTEX);
                        if(stats->pendingRequests < MAX_PENDING_REQUESTS)
                        {
                            stats->pendingRequests++;
                            unlockSemaphore(g_semId,PENDING_REQUESTS_MUTEX);
                            pid_t klientPid = fork();
                            if(klientPid == -1) {
                                perror("[MAIN] fork klient");
                                lockSemaphore(g_semId, PENDING_REQUESTS_MUTEX);
                                stats->pendingRequests--;
                                unlockSemaphore(g_semId, PENDING_REQUESTS_MUTEX);
                                shmdt(stats);
                            } else if(klientPid == 0) {
                                shmdt(stats);
                                run_klient(totalClientow);
                                _exit(0);
                            }
                            totalClientow++;
                        } else{
                            unlockSemaphore(g_semId,PENDING_REQUESTS_MUTEX);
                        }
                        shmdt(stats);
                    }

                }

                if(!fireAlreadyCalled) {
                    double r2 = (rand() / (double)RAND_MAX);
                    if(r2 < pStrazak) {
                        pid_t sPid = fork();
                        if(sPid == -1) {
                            perror("[MAIN] fork strazak");
                        } else if(sPid == 0) {
                            run_strazak(kasjerPid);
                            _exit(0);
                        }
                        totalStrazakow++;
                        fireAlreadyCalled = true;
                        kasjerIsDying = true;
                    }
                }
            }
        }

        //obsluga czyszczenia po pozarze
        if(cleaning) {
            double cleanElapsed = difftime(time(nullptr), cleaningStart);
            if(cleanElapsed >= 5.0) {
                cleaning = false;
                globalEmergency = 0;
                fireAlreadyCalled = false;
                kasjerIsDying = false;
                printf("[MAIN] Przerwa 5s minela. Sprawdzam czas.\n");
                double nowSec = difftime(time(nullptr), startTime);
                double nowMin = nowSec/60.0;
                if(nowMin < X && !stopMain) {
                    printf("[MAIN] Czyszczę zasoby przed uruchomieniem nowego kasjera...\n");
                    removeSharedMemoryAndSemaphore();
                    createSharedMemoryAndSemaphore();
                    int status;
                    pid_t pid;
                    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
                        if (pid != kasjerPid) {
                            printf("[MAIN] Usunieto proces zombie PID=%d\n", pid);
                        }
                    }
                    kasjerPid = startKasjer();
                    if(kasjerPid > 0) {
                        printf("[MAIN] Zrestartowano kasjera PID=%d\n", kasjerPid);
                    }
                }
            }
            sleep(1);
            continue;
        }

        //nie ma kasjera i nie trwa czyszczenie uruchom nowego
        if(kasjerPid < 0 && !cleaning && !kasjerIsDying) {
            kasjerPid = startKasjer();
            if(kasjerPid > 0) {
                printf("[MAIN] Uruchomiono nowego kasjera PID=%d\n", kasjerPid);
            }
            continue;
        }
        //usleep(100000);
        sleep(1);
    }
    stopSpawning = true;
    //sleep(2);//dajemy czas
    printf("[MAIN] Koncze tworzenie procesow. Utworzono klientow=%d, strazakow=%d.\n",
           totalClientow, totalStrazakow);

    //(3)Zamykanie
    if(kasjerPid>0){
        printf("[MAIN] Wysylam sygnal pozaru do kasjera (PID=%d)\n", kasjerPid);
        kill(kasjerPid, SIGUSR1);
        waitpid(kasjerPid, nullptr, 0);
    }

    //Czekamy na reszte procesow
    int status;
    while(waitpid(-1, &status, 0)>0){}

    removeSharedMemoryAndSemaphore();

    printf("[MAIN] Wszystkie procesy zakonczone. Koniec symulacji.\n");
    return 0;
}

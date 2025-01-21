
#ifndef PROJEKT_COMMON_H
#define PROJEKT_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/shm.h>
#define NAZWA_KOLEJKI "/tmp/pizzeria_queue"
#define MAX_CLIENTS 100000
// Kody dla kolorów
#define RESET   "\033[0m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
//Rodzaje komunikatow
#define MSG_TYPE_REQUEST  1
#define MSG_TYPE_RESPONSE 2
#define MSG_TYPE_LEAVE    3
#define MUTEX 0


extern volatile sig_atomic_t globalEmergency;
// Struktura (klient -> kasjer)
typedef struct {
    long mtype;
    int groupSize;
    pid_t senderPid;  // PID klienta
} MsgRequest;

// Struktura (kasjer -> klient) - pytanie o stolik
typedef struct {
    long mtype;
    int accepted;   //1=przydzielony stolik, 0=odmowa
    int tableId;
} MsgResponse;

//Struktura (klient -> kasjer) - zwolnienie stolika
typedef struct {
    long mtype;
    int tableId;  // ktory stolik zwalniamy
    int groupSize;
    pid_t senderPid;
} MsgLeave;

// Struktura stolika
typedef struct {
    int id;           // id stolika
    int capacity;     // max osob 1-4
    int occupied;     // ile osob aktualnie siedzi
    int groupSize;
} Table;

// Ilosc dostepnych stolikow
static const int X1 = 1; // 1-osobowych
static const int X2 = 1; //2
static const int X3 = 1; // 3
static const int X4 = 1;

// Struktura w pamięci dzielonej - statystyki
typedef struct {
    int totalCustomersAtTable;
    int totalGroupsServed;
    int groupsBySize[4];
    int totalCustomersServed;
} SharedStats;
#endif //PROJEKT_COMMON_H

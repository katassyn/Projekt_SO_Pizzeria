//
// Created by makss on 11.01.2025.
//

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

#define LICZBA_GRUP 5  // tymczasowo, do testow
#define NAZWA_KOLEJKI "/tmp/pizzeria_queue"

//Rodzaje komunikatow
#define MSG_TYPE_REQUEST  1
#define MSG_TYPE_RESPONSE 2

// Struktura (klient -> kasjer)
typedef struct {
    long mtype;
    int groupSize;
    pid_t senderPid;  // PID klienta
} MsgRequest;

// Struktura (kasjer -> klient)
typedef struct {
    long mtype;
    int accepted;     //1=przydzielony stolik, 0=odmowa
    int tableId;
} MsgResponse;

#endif //PROJEKT_COMMON_H

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

#endif //PROJEKT_COMMON_H

#ifndef PROJETO_SO_STRUCTS_H
#define PROJETO_SO_STRUCTS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <asm/errno.h>
#include <errno.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/msg.h>
#define BUFFER 1000
#define DIM 200

typedef struct Flight* List_node;
typedef struct Flight flight;
typedef struct Pistas ocupacao_pista;
typedef struct Config config;
typedef struct Mem_shared mem_struct;
typedef struct Estatisticas estatisticas;

struct Flight {
    char type[BUFFER];
    char manobra[BUFFER];
    char id[DIM];
    int init, eta, fuel, takeoff;
    long msg_id;
    pthread_t thread;
    int holding;
    pthread_cond_t cond_ordem;
    List_node next;
};

struct Pistas{
    char pista[DIM];
    char id_voo[DIM];
    int ocupado;
};

struct Config {
    int ut, d_depar, t_depar, d_arri, t_arri, holdmin, holdmax, max_depar, max_arri;
};

struct Estatisticas{
    int total_voos_criados;
    int total_arri;
    int timeM_espera_arri;
    int total_dep;
    int timeM_espera_dep;
    int timeM__holding;
    int timeM_urgency;
    int voos_other_airp;
    int voos_rej_torre;
};

struct Mem_shared {
    config conf;
    int time_unit;
    char fim;
    flight *array_arrivals, *array_departures;
    estatisticas stats;
    pthread_cond_t cond_time;
    pthread_mutex_t mutex_time, mutex_ordem;
    ocupacao_pista array_partidas[2], array_chegadas[2];
};

struct mymsg{
    long mytype;
    long id;
    int pos;
    flight f;
};



#endif //PROJETO_SO_STRUCTS_H
#include "structs.h"
#include "headers.h"

#define BUFFER 1000
#define DIM 200
#define ID 100
#define PIPE_NAME "input_pipe"
#define DEBUG 0

//init-->tempo quando fica pronto e vai para a fila de espera
//takeoff-->instante desejado de descolagem

//init -->tempo inicial quando esta no radar do aeroporto
//eta--> tempo até à pista
//fuel --> combustivel no instante inicial

//variaveis globais
int shmid, mqid, int_pipe, num_arr, num_dep;
mem_struct *shared_mem;
pthread_t thread_tempo;
List_node lista_voos_pipe, lista_arrivals, lista_departures;
FILE *ficheiro_log;
sem_t acesso_mem_fim, acesso_mem_unit, acesso_mem_stats, acesso_log, acesso_array_dep, acesso_array_arr, acesso_lista_voos, acesso_lista_arr, acesso_lista_dep, acesso_pistas_arr, acesso_pistas_dep;
pthread_condattr_t condattr;
pthread_mutexattr_t mutexattr;


void ler_ficheiro(char *ficheiro) {
    FILE *fp = fopen(ficheiro, "r");
    char linha[ID];
    int i = 0;
    while (fgets(linha, ID, fp)) {
        if (i == 0) shared_mem->conf.ut = atoi(strtok(linha, "\n"));
        if (DEBUG)printf("%d\n", atoi(strtok(linha, "\n")));
        if (i == 1) {
            shared_mem->conf.d_depar = atoi(strtok(linha, ","));
            shared_mem->conf.t_depar = atoi(strtok(NULL, "\n"));
        }
        if (i == 2) {
            shared_mem->conf.d_arri = atoi(strtok(linha, ","));
            shared_mem->conf.t_arri = atoi(strtok(NULL, "\n"));
        }
        if (i == 3) {
            shared_mem->conf.holdmin = atoi(strtok(linha, ","));
            shared_mem->conf.holdmax = atoi(strtok(NULL, "\n"));
        }
        if (i == 4) {
            shared_mem->conf.max_depar = atoi(strtok(linha, "\n"));
        }
        if (i == 5) {
            shared_mem->conf.max_arri = atoi(strtok(linha, "\n"));
        }
        i++;
    }
    fclose(fp);
}

int indice_zero(flight *array, int dim_array) {
    int i;
    for (i = 0; i < dim_array; i++) {
        if (array[i].thread == 0)
            return i;
    }
    return -1; /*return -1 caso nenhum indice do array esteja a 0*/
}

void work_torre_controlo() {
    char buffer[BUFFER];
    struct mymsg msg;
    pthread_t atualiza_valores;
    pthread_create(&atualiza_valores, NULL, decrementa_valores, NULL);
    signal(SIGINT, SIG_IGN);
    if (DEBUG) printf("sou o filho (torre de controlo), com pid: %d\n", getpid());
    sprintf(buffer, "Torre de controlo criada! A correr com o pid %d", getpid());
    escrever_log(ficheiro_log, buffer);
    signal(SIGUSR1, signal_stats);
    while (shared_mem->fim >= 0) {
        //do stuff
        msgrcv(mqid, &msg, sizeof(msg) - sizeof(long), -30, 0);
        if (strcmp(msg.f.type, "ARRIVAL") == 0) {
            sem_wait(&acesso_array_arr);
            msg.pos = indice_zero(shared_mem->array_arrivals, shared_mem->conf.max_arri);
            shared_mem->array_arrivals[msg.pos].thread = 1;
            sem_post(&acesso_array_arr);
            msg.mytype = msg.id; //para saber que thread vai ler
            msgsnd(mqid, &msg, sizeof(msg) - sizeof(long), 0);
            sem_wait(&acesso_lista_arr);
            inserir_arrival(msg.f);
            sem_post(&acesso_lista_arr);
        } else if (strcmp(msg.f.type, "DEPARTURE") == 0) {
            sem_wait(&acesso_array_dep);
            msg.pos = indice_zero(shared_mem->array_departures, shared_mem->conf.max_depar);
            shared_mem->array_departures[msg.pos].thread = 1;
            sem_post(&acesso_array_dep);
            msg.mytype = msg.id; //para saber que thread vai ler
            msgsnd(mqid, &msg, sizeof(msg) - sizeof(long), 0);
            sem_wait(&acesso_lista_dep);
            inserir_departure(msg.f);
            sem_post(&acesso_lista_dep);
        }else{
	    break;
	}
    }
    escrever_log(ficheiro_log, "Torre de controlo removida.");
    exit(0);
}

void gestor_simulacao() {
    int tamanho;
    char comando[BUFFER];

    signal(SIGINT, sigint);
    /*Criacao da thread tempo que cria cada thread voo no instante correto*/
    pthread_create(&thread_tempo, NULL, incrementa_tempo, lista_voos_pipe);

    /*criar named pipe*/
    if ((mkfifo(PIPE_NAME, O_CREAT | O_EXCL | 0666) < 0) && (errno != EEXIST)) {
        perror("Cannot create pipe: ");
        exit(0);
    }

    /*Abertura do pipe*/
    if ((int_pipe = open(PIPE_NAME, O_RDWR)) < 0) {
        perror("Cannot open pipe for reading: ");
        exit(0);
    }

    /*Leitura do pipe*/
    while (!shared_mem->fim) {
        tamanho = read(int_pipe, comando, BUFFER);
        comando[tamanho - 1] = '\0';
        validar_pipe(ficheiro_log, comando);  /*validacao do pipe*/
        if (strcmp(comando, "exit") == 0) {
            sem_wait(&acesso_mem_fim);
            shared_mem->fim = 1;
            sem_post(&acesso_mem_fim);
        }
    }

    /*thread que incrementa tempo*/
    pthread_join(thread_tempo, NULL);
}

/*validacao do pipe*/
void validar_pipe(FILE *ficheiro, char *comando) {
    regex_t regex_d, regex_a, regex_fim;
    flight voo;
    char buffer[BUFFER];
    regcomp(&regex_d, "DEPARTURE [A-Z][A-Z][0-9]+ init: [0-9]+ takeoff: [0-9]+", REG_EXTENDED);
    regcomp(&regex_a, "ARRIVAL [A-Z][A-Z][0-9]+ init: [0-9]+ eta: [0-9]+ fuel: [0-9]+", REG_EXTENDED);
    regcomp(&regex_fim, "exit", 0);
    if (regexec(&regex_d, comando, 0, NULL, 0) == 0) {
        sprintf(buffer, "NEW COMMAND => %s", comando);
        escrever_log(ficheiro, buffer);
        sscanf(comando, "%s %s init: %d takeoff: %d", voo.type, voo.id, &(voo.init), &(voo.takeoff));
        if (shared_mem->time_unit > voo.init) {
            sprintf(buffer, "Impossivel criar voo: %s", voo.id);
            escrever_log(ficheiro, buffer);
            return;
        }
        //adicionar a lista
        sem_wait(&acesso_lista_voos);
        inserir_voo_pipe(voo.type, voo.init, 0, voo.id, 0, voo.takeoff);
        sem_post(&acesso_lista_voos);

    } else if (regexec(&regex_a, comando, 0, NULL, 0) == 0) {
        sprintf(buffer, "NEW COMMAND => %s", comando);
        escrever_log(ficheiro, buffer);
        sscanf(comando, "%s %s init: %d eta: %d fuel: %d", voo.type, voo.id, &(voo.init), &(voo.eta), &(voo.fuel));
        if (shared_mem->time_unit > voo.init) {
            sprintf(buffer, "Impossivel criar voo: %s", voo.id);
            escrever_log(ficheiro, buffer);
            return;
        } else if (voo.eta > voo.fuel) {
            sprintf(buffer, "%s LEAVING TO OTHER AIRPORT => FUEL = 0", voo.id);
            escrever_log(ficheiro, buffer);
            return;
        }
        //adicionar a lista
        sem_wait(&acesso_lista_voos);
        inserir_voo_pipe(voo.type, voo.init, voo.eta, voo.id, voo.fuel, 0);
        sem_post(&acesso_lista_voos);

    } else if (regexec(&regex_fim, comando, 0, NULL, 0) == 0) {
        sprintf(buffer, "Inicio do termino do programa.");
        escrever_log(ficheiro, buffer);
    } else {
        sprintf(buffer, "WRONG COMMAND => %s", comando);
        escrever_log(ficheiro, buffer);
    }
}

void escrever_log(FILE *ficheiro, char *mensagem) {
    time_t actual_time;
    struct tm *mytime;
    char buffer[BUFFER];

    time(&actual_time);

    mytime = localtime(&actual_time);

    strftime(buffer, BUFFER, "%X", mytime);
    sem_wait(&acesso_log);
    fprintf(ficheiro, "%s %s\n", buffer, mensagem);
    printf("%s %s\n", buffer, mensagem);
    sem_post(&acesso_log);
    if (DEBUG) printf("%s %s\n", buffer, mensagem);
    fflush(ficheiro);
}

void copia_voo_mem(flight *mem, flight voo){
    strcpy(mem->id, voo.id);
    strcpy(mem->type, voo.type);
    strcpy(mem->manobra, voo.manobra);
    mem->takeoff = voo.takeoff;
    mem->eta = voo.eta;
    mem->fuel = voo.fuel;
    mem->thread = voo.thread;
    mem->init = voo.init;
    mem->msg_id = voo.msg_id;
    mem->holding = voo.holding;
}

void *work_thread_voo(void *no_lista) {
    List_node no = (List_node) (no_lista);
    char buffer[BUFFER];
    struct Flight f;
    struct mymsg msg;
    memcpy(&f, no, sizeof(struct Flight));
    f.thread = pthread_self();
    f.next = NULL;
    sem_wait(&acesso_lista_voos);
    remove_voo(f.id, lista_voos_pipe);
    sem_post(&acesso_lista_voos);
    sprintf(buffer, "Inicio da thread do voo %s", f.id);
    escrever_log(ficheiro_log, buffer);
    msg.id = f.msg_id;
    msg.mytype = 10;

    if (strcmp(f.type, "DEPARTURE") != 0 && 4 + f.eta + shared_mem->conf.d_arri >= f.fuel) {
        msg.mytype = 20;
        sprintf(buffer, "%s EMERGENCY LANDING REQUESTED", f.id);
        escrever_log(ficheiro_log, buffer);
    }

    memcpy(&msg.f, &f, sizeof(f));

    msgsnd(mqid, &msg, sizeof(msg) - sizeof(long), 0);
    msgrcv(mqid, &msg, sizeof(msg) - sizeof(long), f.msg_id, 0);

    if (msg.pos == -1) {
        sem_wait(&acesso_mem_stats);
        shared_mem->stats.voos_rej_torre++;
        sem_post(&acesso_mem_stats);
        sprintf(buffer, "Voo %s rejeitado", f.id);
        escrever_log(ficheiro_log, buffer);
        pthread_exit(NULL);
    } else {
        if (strcmp(f.type, "ARRIVAL") == 0) {
            sem_wait(&acesso_array_arr);
            //shared_mem->array_arrivals[msg.pos] = f;
            copia_voo_mem(&shared_mem->array_arrivals[msg.pos], f);
            sem_post(&acesso_array_arr);
        } else {
            sem_wait(&acesso_array_dep);
            //shared_mem->array_departures[msg.pos] = f;
            copia_voo_mem(&shared_mem->array_departures[msg.pos], f);
            sem_post(&acesso_array_dep);
        }
        sprintf(buffer, "Voo %s no slot %d", f.id, msg.pos);
        escrever_log(ficheiro_log, buffer);
    }

    if (strcmp(f.type, "ARRIVAL") == 0) {
        sem_wait(&acesso_array_arr);
        pthread_mutex_lock(&shared_mem->mutex_ordem);
        while (strcmp(shared_mem->array_arrivals[msg.pos].manobra, "") == 0)
            pthread_cond_wait(&shared_mem->array_arrivals[msg.pos].cond_ordem, &shared_mem->mutex_ordem);
        pthread_mutex_unlock(&shared_mem->mutex_ordem);
        sem_post(&acesso_array_arr);
        //verifica se não há departures a usar a pista
        //usa a pista
        sem_wait(&acesso_pistas_dep);
        if ((shared_mem->array_partidas[0].ocupado == 0) && (shared_mem->array_partidas[1].ocupado == 0)) {
            for (int i = 0; i < 2; i++) {
                sem_wait(&acesso_array_arr);
                sem_wait(&acesso_pistas_arr);
                if (strcmp(shared_mem->array_arrivals[msg.pos].id, shared_mem->array_chegadas[i].id_voo) == 0) {
                    sprintf(buffer, "%s ARRIVAL %s started", shared_mem->array_chegadas[i].id_voo,
                            shared_mem->array_chegadas[i].pista);
                    escrever_log(ficheiro_log, buffer);
                    sem_wait(&acesso_mem_stats);
                    shared_mem->stats.total_arri++;
                    sem_post(&acesso_mem_stats);
                    sleep(shared_mem->conf.d_arri);
                    sprintf(buffer, "%s LANDING %s concluded", shared_mem->array_chegadas[i].id_voo,
                            shared_mem->array_chegadas[i].pista);
                    escrever_log(ficheiro_log, buffer);
                    sleep(shared_mem->conf.t_arri);
                    sem_wait(&acesso_pistas_arr);
                    shared_mem->array_chegadas[i].ocupado = 0;
                    sem_post(&acesso_pistas_arr);
                    sprintf(buffer, "Pista %s livre", shared_mem->array_chegadas[i].pista);
                    escrever_log(ficheiro_log, buffer);
                    sem_post(&acesso_pistas_arr);
                    sem_post(&acesso_array_arr);
                    break;
                } else {
                    sem_post(&acesso_pistas_arr);
                    sem_post(&acesso_array_arr);
                }    
            }
	    sem_post(&acesso_pistas_arr);
        }
        sem_post(&acesso_pistas_dep);
    } else {
        sem_wait(&acesso_array_dep);
        pthread_mutex_lock(&shared_mem->mutex_ordem);
        while (strcmp(shared_mem->array_departures[msg.pos].manobra, "") == 0)
            pthread_cond_wait(&shared_mem->array_departures[msg.pos].cond_ordem, &shared_mem->mutex_ordem);
        pthread_mutex_unlock(&shared_mem->mutex_ordem);
        sem_post(&acesso_array_dep);
        //verifica se não há arrivals a usar a pista
        //usa a pista
        sem_wait(&acesso_pistas_arr);
        if ((shared_mem->array_chegadas[0].ocupado == 0) && (shared_mem->array_chegadas[1].ocupado == 0)) {
            for (int i = 0; i < 2; i++) {
                sem_wait(&acesso_array_dep);
                sem_wait(&acesso_array_arr);
                if (strcmp(shared_mem->array_departures[msg.pos].id, shared_mem->array_partidas[i].id_voo) == 0) {
                    sprintf(buffer, "%s ARRIVAL %s started", shared_mem->array_partidas[i].id_voo, shared_mem->array_partidas[i].pista);
                    escrever_log(ficheiro_log, buffer);
                    sem_wait(&acesso_mem_stats);
                    shared_mem->stats.total_dep++;
                    sem_post(&acesso_mem_stats);
                    sleep(shared_mem->conf.d_depar);
                    sprintf(buffer, " %s DEPARTURE %s concluded", shared_mem->array_partidas[i].id_voo, shared_mem->array_partidas[i].pista);
                    escrever_log(ficheiro_log, buffer);
                    sleep(shared_mem->conf.t_depar);
                    shared_mem->array_partidas[i].ocupado = 0;
                    sprintf(buffer, "Pista %s livre", shared_mem->array_partidas[i].pista);
                    escrever_log(ficheiro_log, buffer);
                    sem_post(&acesso_array_arr);
                    sem_post(&acesso_array_dep);
                    break;
                }
                sem_post(&acesso_array_arr);
                sem_post(&acesso_array_dep);
            }
        }
        sem_post(&acesso_pistas_arr);
    }

    if (DEBUG)printf("O voo com id %s foi criado\n", f.id);
    /*sprintf(buffer, "Fim da thread do voo: %s", f.id);
    escrever_log(ficheiro_log, buffer);*/

    //apagar indice
    if (strcmp(f.type, "ARRIVAL") == 0) {
        sem_wait(&acesso_array_arr);
        offthread(shared_mem->array_arrivals, shared_mem->conf.max_arri, pthread_self());
        sem_post(&acesso_array_arr);
    } else {
        sem_wait(&acesso_array_dep);
        offthread(shared_mem->array_departures, shared_mem->conf.max_depar, pthread_self());
        sem_post(&acesso_array_dep);
    }
    pthread_exit(NULL);
}

void offthread(flight *array, int dim_array, pthread_t id) {
    int i;
    for (i = 0; i < dim_array; i++) {
        if (id == array[i].thread) {
            memset(&array[i].thread, 0, sizeof(pthread_t));
        }
    }
}

//esta funcao incrementa o tempo e ao mesmo tempo cria as threads do voo
void *incrementa_tempo(void *cabecalho) {
    List_node lista;
    pthread_t nova_thread;
    int tempo_atual;
    while (!(shared_mem->fim)) {
        lista = (List_node) cabecalho;
        sem_wait(&acesso_mem_unit);
        tempo_atual = (shared_mem->time_unit);
        sem_post(&acesso_mem_unit);
        sem_wait(&acesso_lista_voos);
        while (lista != NULL && lista->next != NULL && lista->next->init == tempo_atual) {
            //cria a thread voo
            if (strcmp(lista->next->type, "ARRIVAL") == 0) {
                sem_wait(&acesso_mem_stats);
                lista->next->msg_id = ++(shared_mem->stats.total_voos_criados) + 30;
                sem_post(&acesso_mem_stats);
                pthread_create(&nova_thread, NULL, work_thread_voo, lista->next);
            } else if (strcmp(lista->next->type, "DEPARTURE") == 0) {
                sem_wait(&acesso_mem_stats);
                lista->next->msg_id = ++(shared_mem->stats.total_voos_criados) + 30;
                sem_post(&acesso_mem_stats);
                pthread_create(&nova_thread, NULL, work_thread_voo, lista->next);
            }
            //espera que a pthread seja criada
            lista = lista->next;
        }
        sem_post(&acesso_lista_voos);
        usleep(1000 * shared_mem->conf.ut);
        sem_wait(&acesso_mem_unit);
        (shared_mem->time_unit)++;
        pthread_cond_signal(&shared_mem->cond_time);
        sem_post(&acesso_mem_unit);
    }
    pthread_exit(NULL);
}

void cleanup() {
    close(int_pipe);
    elimina_lista_voo(lista_voos_pipe);
    elimina_lista_voo(lista_arrivals);
    elimina_lista_voo(lista_departures);
    sem_destroy(&acesso_mem_fim);
    sem_destroy(&acesso_mem_unit);
    sem_destroy(&acesso_mem_stats);
    sem_destroy(&acesso_log);
    sem_destroy(&acesso_array_arr);
    sem_destroy(&acesso_array_dep);
    sem_destroy(&acesso_lista_voos);
    sem_destroy(&acesso_lista_dep);
    sem_destroy(&acesso_lista_arr);
    sem_destroy(&acesso_pistas_arr);
    sem_destroy(&acesso_array_dep);
    escrever_log(ficheiro_log, "Fim do programa!");
    fclose(ficheiro_log);
}

void signal_stats(int signum) {
    signal(SIGUSR1, SIG_IGN);
    printf("Número total de voos criados: %d\n", shared_mem->stats.total_voos_criados);
    printf("Número total de voos que aterraram: %d\n", shared_mem->stats.total_arri);
    printf("Tempo médio de espera (para além do ETA) para aterrar: %d\n", shared_mem->stats.timeM_espera_arri);
    printf("Número total de voos que descolaram: %d\n", shared_mem->stats.total_dep);
    printf("Tempo médio de espera para descolar: %d\n", shared_mem->stats.timeM_espera_dep);
    printf("Número médio de manobras de holding por voo de aterragem: %d\n", shared_mem->stats.timeM__holding);
    printf("Número médio de manobras de holding por voo em estado de urgência: %d\n", shared_mem->stats.timeM_urgency);
    printf("Número de voos redirecionados para outro aeroporto: %d\n", shared_mem->stats.voos_other_airp);
    printf("Voos rejeitados pela Torre de Controlo: %d\n", shared_mem->stats.voos_rej_torre);
    signal(SIGUSR1, signal_stats);
}

void sigint(int signum) {
    struct mymsg msg;
    signal(SIGINT, SIG_IGN);
    sem_wait(&acesso_mem_fim);
    shared_mem->fim = 1;
    sem_post(&acesso_mem_fim);
    msg.mytype = 30;
    msgsnd(mqid, &msg, sizeof(msg) - sizeof(long), 0);
    pthread_join(thread_tempo, NULL);
   /*
    for (int i = 0; i < shared_mem->conf.max_depar; i++) {
        pthread_join(shared_mem->array_departures[i].thread, NULL);
    }
    for (int i = 0; i < shared_mem->conf.max_arri; i++) {
        pthread_join(shared_mem->array_arrivals[i].thread, NULL);
    }*/
    sem_wait(&acesso_mem_fim);
    shared_mem->fim = -1;
    sem_post(&acesso_mem_fim);
    wait(NULL);
    cleanup();
    exit(0);
}

void *decrementa_valores(void *inutil) {
    List_node lista_arri, lista_dep;
    int i, j;
    while (shared_mem->fim >= 0) {
        lista_arri = lista_arrivals;
        pthread_mutex_lock(&shared_mem->mutex_time);
        pthread_cond_wait(&shared_mem->cond_time, &shared_mem->mutex_time);
        sem_wait(&acesso_lista_arr);
        while (lista_arri->next != NULL) {
            lista_arri->next->eta--;
            lista_arri->next->fuel--;
        }
        sem_post(&acesso_lista_arr);
        pthread_mutex_unlock(&shared_mem->mutex_time);

        if (escalonamento() == 'a') {
            sem_wait(&acesso_lista_arr);
            lista_arri = lista_arrivals;
            lista_arri = lista_arri->next;
            for (j = 0; j < 2; j++) {
                sem_wait(&acesso_pistas_arr);
                if (shared_mem->array_chegadas[j].ocupado == 0) { //verificar que pista livre
                    i = 0;
                    if (lista_arri != NULL) {
                        sem_wait(&acesso_array_arr);
                        while (strcmp(shared_mem->array_arrivals[i].id, lista_arri->id) !=
                               0) { //escolher voo a mandar para pista(primeiro da lista)
                            i++;
                        }
			shared_mem->array_chegadas[j].ocupado = 1;
                        strcpy(shared_mem->array_arrivals[i].manobra, "Aterrar");   //mandar ordem pela shm
                        strcpy(shared_mem->array_chegadas[j].id_voo, lista_arri->id);
                        pthread_cond_signal(&shared_mem->array_arrivals[i].cond_ordem);
                        sem_wait(&acesso_lista_arr);
                        remove_voo(shared_mem->array_arrivals[i].id, lista_arrivals);
                        sem_post(&acesso_lista_arr);
                        sem_post(&acesso_array_arr);
                    }
                }
                sem_post(&acesso_pistas_arr);
            }
            sem_post(&acesso_lista_arr);
        } else {
            sem_wait(&acesso_lista_dep);
            lista_dep = lista_departures;
            lista_dep = lista_dep->next;
            for (j = 0; j < 2; j++) {
                sem_wait(&acesso_pistas_dep);
                if (shared_mem->array_partidas[j].ocupado == 0) {
                    i = 0;
                    if (lista_dep != NULL) {
                        sem_wait(&acesso_array_dep);
                        while (strcmp(shared_mem->array_departures[i].id, lista_dep->id) != 0) {
                            i++;
                        }
			shared_mem->array_partidas[j].ocupado = 1;
                        strcpy(shared_mem->array_departures[i].manobra, "Descolar");
                        strcpy(shared_mem->array_partidas[j].id_voo, lista_dep->id);
                        pthread_cond_signal(&shared_mem->array_departures[i].cond_ordem);
                        sem_wait(&acesso_lista_dep);
                        remove_voo(shared_mem->array_departures[i].id, lista_departures);
                        sem_post(&acesso_lista_dep);
                        sem_post(&acesso_array_dep);
                    }
                }
                sem_post(&acesso_pistas_dep);
            }
            sem_post(&acesso_lista_dep);
        }
    }
    pthread_exit(NULL);
}

void nomes_pistas() {
    strcpy(shared_mem->array_chegadas[0].pista, "28L");
    shared_mem->array_chegadas[0].ocupado = 0; //inicialmente nao tem voos
    strcpy(shared_mem->array_chegadas[1].pista, "28R");
    shared_mem->array_chegadas[1].ocupado = 0; //inicialmente nao tem voos
    strcpy(shared_mem->array_partidas[0].pista, "01L");
    shared_mem->array_partidas[0].ocupado = 0; //inicialmente nao tem voos
    strcpy(shared_mem->array_partidas[1].pista, "01R");
    shared_mem->array_partidas[1].ocupado = 0; //inicialmente nao tem voos
}

char escalonamento() {//TODO
    List_node cabecalho_arr, cabecalho_dep;
    cabecalho_arr = lista_arrivals;
    cabecalho_dep = lista_departures;
    sem_wait(&acesso_lista_arr);
    sem_wait(&acesso_lista_dep);
    cabecalho_arr = cabecalho_arr->next;
    cabecalho_dep = cabecalho_dep->next;
    if ((cabecalho_arr != NULL && cabecalho_dep != NULL && ((cabecalho_arr->eta) > (cabecalho_dep->takeoff))) || (cabecalho_arr == NULL && cabecalho_dep != NULL)) {
        sem_post(&acesso_lista_dep);
        sem_post(&acesso_lista_arr);
        return 'd';
    } else if((cabecalho_arr != NULL && cabecalho_dep != NULL  && ((cabecalho_arr->eta) < (cabecalho_dep->takeoff))) || (cabecalho_arr != NULL && cabecalho_dep == NULL )){
        sem_post(&acesso_lista_dep);
        sem_post(&acesso_lista_arr);
        return 'a';
    }else{
	sem_post(&acesso_lista_dep);
        sem_post(&acesso_lista_arr);	
        return 0;
    }
}
//se tiver 0 esta nos primeiros 5 voos
void manobra_holding() {
    List_node aux = lista_arrivals;
    aux = aux->next;
    int cont = 0;
    while (aux != NULL) {
        if (cont <= 5) {
            aux->holding = 0;
        } else {
            aux->holding = 1;
        }
        cont++;
        aux = aux->next;
    }
}

int main() {
    char buffer[BUFFER];
    ficheiro_log = fopen("ficheiro_log.txt", "w");

    /*inicializacao dos semaforos*/
    sem_init(&acesso_mem_fim, 1, 1);
    sem_init(&acesso_mem_unit, 1, 1);
    sem_init(&acesso_mem_stats, 1, 1);
    sem_init(&acesso_log, 1, 1);
    sem_init(&acesso_array_arr, 1, 1);
    sem_init(&acesso_array_dep, 1, 1);
    sem_init(&acesso_lista_voos, 1, 1);
    sem_init(&acesso_lista_arr, 1, 1);
    sem_init(&acesso_lista_dep, 1, 1);
    sem_init(&acesso_pistas_arr, 1, 1);
    sem_init(&acesso_pistas_dep, 1, 1);

    sprintf(buffer, "Inicio do programa! A correr no processo %d", getpid());
    escrever_log(ficheiro_log, buffer);
    fflush(ficheiro_log);
    pid_t torre_controlo;
    lista_voos_pipe = cria_no_lista_voos();
    lista_arrivals = cria_no_lista_voos();
    lista_departures = cria_no_lista_voos();

    /*criacao da fila de mensagens*/
    mqid = msgget(IPC_PRIVATE, IPC_CREAT | 0700);
    if (mqid < 0) {
        perror("Erro ao criar a fila de mensagens");
    }

    /*Criacao da memoria partilhada*/
    shmid = shmget(IPC_PRIVATE, sizeof(mem_struct), IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("Erro ao criar a memoria partilhada!\n");;
    }
    /*Anexar memoria partilhada*/
    shared_mem = (mem_struct *) shmat(shmid, NULL, 0);
    memset(shared_mem, 0, sizeof(mem_struct));

    ler_ficheiro("config.txt");
    escrever_log(ficheiro_log, "Configuracoes lidas!");

    num_arr = shmget(IPC_PRIVATE, sizeof(flight) * shared_mem->conf.max_arri, IPC_CREAT | 0666);
    shared_mem->array_arrivals = (flight *) shmat(num_arr, NULL, 0);
    memset(shared_mem->array_arrivals, 0, sizeof(flight) * shared_mem->conf.max_arri);
    num_dep = shmget(IPC_PRIVATE, sizeof(flight) * shared_mem->conf.max_depar, IPC_CREAT | 0666);
    shared_mem->array_departures = (flight *) shmat(num_dep, NULL, 0);
    memset(shared_mem->array_departures, 0, sizeof(flight) * shared_mem->conf.max_depar);

    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&shared_mem->cond_time, &condattr);

    for(int i = 0; i < shared_mem->conf.max_arri; i++){
        pthread_cond_init(&shared_mem->array_arrivals[i].cond_ordem, &condattr);
    }

    for(int i = 0; i < shared_mem->conf.max_depar; i++){
        pthread_cond_init(&shared_mem->array_departures[i].cond_ordem, &condattr);
    }

    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shared_mem->mutex_time, &mutexattr);

    pthread_mutex_init(&shared_mem->mutex_ordem, &mutexattr);


    nomes_pistas();

    /*criar processo torre de controlo*/
    torre_controlo = fork();
    if (!torre_controlo) {
        if (DEBUG) printf("%d, sou o filho (torre de controlo), com pid: %d\n", torre_controlo, getpid());
        /*chama funcao work_torre_controlo*/
        work_torre_controlo();
    } else {
        if (DEBUG)printf("sou o pai,com o pid: %d\n", getpid());
        /*chamar funcao gestor de simulacao*/
        gestor_simulacao();
    }

    if (DEBUG)
        printf("leitura do ficheiro shared_mem:\nUnidade de tempo: %d\nDuracao da descolagem: %d\nIntervalo entre descolagens: %d\nDuracao da aterragem: %d\nIntervalo entre aterragens: %d\nHolding minima: %d\nHolding maxima: %d\nMaxima partidas: %d\nMaximo chegadas: %d\n",
               shared_mem->conf.ut, shared_mem->conf.d_depar, shared_mem->conf.t_depar, shared_mem->conf.d_arri,
               shared_mem->conf.t_arri, shared_mem->conf.holdmin, shared_mem->conf.holdmax, shared_mem->conf.max_depar,
               shared_mem->conf.max_arri);
    cleanup();
    return 0;
}

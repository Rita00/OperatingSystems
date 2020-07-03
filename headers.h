#ifndef PROJETO_SO_HEADERS_H
#define PROJETO_SO_HEADERS_H
#include "structs.h"

//headers de lista_voos.c
List_node cria_no_lista_voos();
void inserir_voo_pipe(char *type, int init, int eta, char *flight_code, int fuel, int takeoff);
void inserir_arrival(flight f);
void inserir_departure(flight f);
void remove_voo(char *flight_code, List_node lista);
void elimina_lista_voo(List_node lista_voos);
void imprime_lista_voos();

//headers de main.c
void ler_ficheiro(char *ficheiro);
int indice_zero(flight *array, int dim_array);
void work_torre_controlo();
void gestor_simulacao();
void validar_pipe(FILE *ficheiro, char *comando);
void escrever_log(FILE *ficheiro, char *mensagem);
void *work_thread_voo(void *no_lista);
void offthread(flight *array, int dim_array, pthread_t id);
void *incrementa_tempo(void *cabecalho);
void cleanup();
void signal_stats(int signum);
void sigint(int signum);
void *decrementa_valores(void *inutil);
void nomes_pistas();
char escalonamento();
void manobra_holding();

#endif //PROJETO_SO_HEADERS_H

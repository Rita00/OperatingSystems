#include "structs.h"

List_node lista_voos_pipe;
List_node lista_arrivals;
List_node lista_departures;


List_node cria_no_lista_voos() {
    return (List_node) calloc(1, sizeof(struct Flight));
}

void inserir_voo_pipe(char *type, int init, int eta, char *flight_code, int fuel, int takeoff) {
    List_node aux, cabecalho;
    cabecalho = lista_voos_pipe;
    while (cabecalho->next != NULL) {
        cabecalho = cabecalho->next;
        if (strcmp(cabecalho->id, flight_code) == 0) return;
    }
    cabecalho = lista_voos_pipe;
    while (cabecalho->next != NULL && init > cabecalho->next->init) {
        cabecalho = cabecalho->next;
    }
    aux = cria_no_lista_voos();
    aux->next = cabecalho->next;
    cabecalho->next = aux;
    if (strcmp(type, "DEPARTURE") == 0) {
        strcpy(aux->type, type);
        aux->init = init;
        strcpy(aux->id, flight_code);
        aux->takeoff = takeoff;
    } else {
        strcpy(aux->type, type);
        aux->init = init;
        aux->eta = eta;
        strcpy(aux->id, flight_code);
        aux->fuel = fuel;
    }

}

void inserir_arrival(flight f){
    List_node aux, cabecalho;
    cabecalho = lista_arrivals;
    while(cabecalho->next != NULL && (f.fuel - f.eta) > (cabecalho->next->fuel) - (cabecalho->next->eta)){
        cabecalho = cabecalho->next;
    }
    aux = cria_no_lista_voos();
    memcpy(aux, &f, sizeof(flight));
    aux->next = cabecalho->next;
    cabecalho->next = aux;
}

void inserir_departure(flight f){
    List_node aux, cabecalho;
    cabecalho = lista_departures;
    while(cabecalho->next != NULL && f.takeoff > cabecalho->next->takeoff){
        cabecalho = cabecalho->next;
    }
    aux = cria_no_lista_voos();
    memcpy(aux, &f, sizeof(flight));
    aux->next = cabecalho->next;
    cabecalho->next = aux;
}

void remove_voo(char *flight_code, List_node lista){
    List_node aux, cabecalho;
    cabecalho = lista;
    while (cabecalho->next != NULL) {
        if (strcmp(cabecalho->next->id, flight_code) == 0) {
            aux = cabecalho->next;
            cabecalho->next = cabecalho->next->next;
            free(aux);
        } else cabecalho = cabecalho->next;
    }
}

void elimina_lista_voo(List_node lista) {
    List_node aux = lista->next;
    free(lista);
    lista = aux;
    while (lista != NULL) {
        aux = lista;
        lista = lista->next;
        free(aux);
    }
}

/*testar codigo*/
void imprime_lista_voos(List_node lista) {
    //CORRIGIR PROBLEMA DA VARIAVEL GLOBAL
    while (lista->next != NULL) {
        lista = lista->next;
        if (strcmp(lista->type, "DEPARTURE") == 0) {
            printf("%s %s %d %d\n", lista->type, lista->id, lista->init, lista->takeoff);
        } else {
            printf("%s %s %d %d %d\n", lista->type, lista->id, lista->init, lista->eta,
                   lista->fuel);
        }
    }
}

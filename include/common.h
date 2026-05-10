#ifndef COMMON_H
#define COMMON_H

typedef struct {
    int user_id;
    int command_id;
    char command[256];
    char reply_pipe[64];
    int status; // 0 = pendente, 1 = em execução, 2 = concluída, 3 = (-s) pedido de terminação do controller, 4 = (-c) pedido de consulta dos comandos em execução e em espera
    int operador_redirecionamento; // 0 = sem operador de redirecionamento, 1 = >, 2 = 2>, 3 = <, 4 = |
} Request;

void runner_pede_terminacao_controller();
void runner_pede_consulta_controller();

int substitui_comando_no_array (Request req_arr[], int size, Request req);

int escrever_no_log(Request req, double tempo_gasto);
char *trim_espacos(char *str);
int prepara_argumentos(char *comando, char *args[], int max_args);

int controller_envia_Ok_para_runner(Request req);
int controller_envia_lista_para_runner(Request req, Request req_arr[], int query_size, int comando_em_execucao);

#endif 
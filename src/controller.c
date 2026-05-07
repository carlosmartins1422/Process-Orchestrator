#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "runner.h"
#include <string.h>
#include <sys/wait.h>
#include <sys/time.h>
#include "controller.h"
#include "common.h"



int main (int argc, char *argv[]) {

    int parallel_commands = 1;
    // lê por FIFO
    char sched_policy[32] = "FIFO";

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "parallel-commands") == 0)
            parallel_commands = atoi(argv[i + 1]); // lê o nº máximo de comandos paralelos
        if (strcmp(argv[i], "sched-policy") == 0)
            strncpy(sched_policy, argv[i + 1], sizeof(sched_policy) - 1); // lê a política de escalonamento
    }

    printf("[controller] parallel-commands: %d\n", parallel_commands);
    printf("[controller] sched-policy: %s\n", sched_policy);

    mkfifo("tmp/pipe_req", 0666); // Cria o pipe chamado "pipe" com permissões de leitura e escrita para todos os usuários

    Request req_arr[100]; // Declara um array de estruturas Request para armazenar os dados lidos do pipe

    int fd_req = open("tmp/pipe_req", O_RDWR); // Abre o pipe para leitura e 
    if (fd_req == -1) {
        perror("Erro ao abrir o pipe");
        return 1;
    }

    int query_size = 0; // Variável para controlar o tamanho do array de Request
    int running = 0; // Variável para contar os processos ativos
    int substitui_req = 0; // Variável para controlar se o comando lido do pipe corresponde a um command_id já armazenado no array de Request e substitui no array se encontrado
    struct timeval start_times[100]; // Variáveis para armazenar o tempo de início e término da execução do comando
    int shutdown_pedido = 0; // Variável para controlar se o pedido de terminação do controller foi recebido
    int indice_comando_terminal = 0; // Variável para armazenar o índice do comando de terminação do controller no array de Request

    while (1) { // Loop infinito para continuar lendo do pipe
        Request req; // Inicializa a estrutura Request para armazenar os dados lidos do pipe
        int n = read (fd_req, &req, sizeof(Request)); // Lê a estrutura e armazena os dados na estrutura req
        if (n == -1) {
            perror("Erro ao ler do pipe");
            close(fd_req);
            return 1;
        }
        // é imediato , nao entra na fila
        if (req.status == 4) { // status 4 para indicar que é um pedido de consulta da lista de comandos
            controller_envia_lista_para_runner(req, req_arr, query_size, running);
            continue; // Volta para o início do loop para ler o próximo comando do pipe
        }
        substitui_req = substitui_comando_no_array(req_arr, query_size, req); // Verifica se o command_id do comando lido do pipe já está presente no array de Request e substitui se encontrado
        if (!substitui_req && !shutdown_pedido) { // Verifica se o comando lido do pipe já existe e se o pedido de terminação do controller já foi recebido
            if (req.status == 3) indice_comando_terminal = query_size; // Se este comando for o pedido de terminação, guarda o índice 
            req_arr[query_size++] = req; // Armazena a estrutura lida no array de Request
        }

        // FASE 3: quando um comando termina, encontra-o pelo command_id, escreve log e decrementa contador
        if (req.status == 2) {
            for (int i = 0; i < query_size; i++) {
                if (req_arr[i].command_id == req.command_id && req_arr[i].status == 2) {
                    struct timeval end;
                    gettimeofday(&end, NULL); // Marca o tempo de término da execução do comando
                    double elapsed = (end.tv_sec  - start_times[i].tv_sec)
                                    +(end.tv_usec - start_times[i].tv_usec) / 1000000.0; // Calcula o tempo decorrido em segundos
                    escrever_no_log(req_arr[i], elapsed); // Escreve no arquivo de log o comando executado e o tempo gasto
                    running--; // Decrementa o contador de processos ativos
                    break;
        }
    }
}
        int execucoes_por_user[1024]={0};
        for (int i = 0; i < query_size; i++){
            if (req_arr[i].status == 1 ) // em exec
                execucoes_por_user[req_arr[i].user_id]++;
        }
        int lancados = 0; 
        // alterna entre users
        for (int i = 0; i < query_size; i++) {
            if (running >= parallel_commands) break; // respeita limite total
            if (req_arr[i].status == 0) { // comando pendente
                // Verifica se existe outro user pendente com menos slots em execução
                int justo = 1; // assume que é justo lançar este
                for (int j = 0; j < query_size; j++) {
                    if (req_arr[j].status == 0 && req_arr[j].user_id != req_arr[i].user_id) 
                    {
                        if (execucoes_por_user[req_arr[j].user_id] < execucoes_por_user[req_arr[i].user_id])
                        {
                            justo = 0;
                            break;
                        }
                    }
                }
                if(justo){
                    req_arr[i].status = 1;
                    running++;
                    lancados++;
                    execucoes_por_user[req_arr[i].user_id]++;
                    gettimeofday(&start_times[i],NULL); // marca o tempo de inicio da exec do comando 
                    controller_envia_Ok_para_runner(req_arr[i]);
                }
            }
        }

  
        if (req.status == 3 && !shutdown_pedido) { // Dizer que foi pedido a terminação do controller (status 3)
            shutdown_pedido = 1; // Define a variável shutdown_pedido como 1 para indicar que o pedido de terminação do controller foi recebido
        }
        if (shutdown_pedido && running == 0) { // Se foi pedido terminação, não há nada a correr e o comando que acabou de ser executado é o último do array
            controller_envia_Ok_para_runner(req_arr[indice_comando_terminal]); // Envia a mensagem de resposta Ok para o runner para indicar que o controller vai encerrar
            break; // Sai do loop para encerrar o runner
        }
    }

    close(fd_req); // Fecha o pipe de pedido
    unlink("tmp/pipe_req"); // Remove o pipe de pedido criado pelo controller

    return 0;
}

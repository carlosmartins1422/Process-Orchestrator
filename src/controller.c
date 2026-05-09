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
#include <time.h>
#include "controller.h"
#include "common.h"


int main (int argc, char *argv[]) {

    int parallel_commands = atoi(argv[1]); // Converte o argumento de string para inteiro e atribui a parallel_commands, que indica o número máximo de comandos que podem ser executados em paralelo
    // lê por FIFO
    char sched_policy[32];
    strncpy(sched_policy, argv[2], sizeof(sched_policy) - 1); // Copia o argumento de string para sched_policy, que indica a política de escalonamento a ser utilizada
    sched_policy[sizeof(sched_policy) - 1] = '\0'; // Garante que a string sched_policy seja terminada com um caractere nulo

    printf("[controller] parallel-commands: %d\n", parallel_commands);
    
    if (strcmp(sched_policy, "FIFO") != 0 && strcmp(sched_policy, "RANDOM") != 0 && strcmp(sched_policy, "FAIR") != 0) { // Verifica se a política de escalonamento é válida (FCFS, SJF ou RR)
        fprintf(stderr, "Política de escalonamento inválida. Use FIFO, RANDOM ou FAIR.\n");
        return 1;
    }

    mkfifo("tmp/pipe_req", 0666); // Cria o pipe chamado "pipe" com permissões de leitura e escrita para todos os usuários

    Request req_arr[100]; // Declara um array de estruturas Request para armazenar os dados lidos do pipe

    int fd_req = open("tmp/pipe_req", O_RDWR); // Abre o pipe para leitura e 
    if (fd_req == -1) {
        perror("Erro ao abrir o pipe");
        return 1;
    }

    if (strcmp(sched_policy, "FIFO") == 0) {
        printf("[controller] scheduling policy: FIFO\n");

        
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
    
    
    
    
    
    
    
    } else if (strcmp(sched_policy, "RANDOM") == 0) {
        printf("[controller] scheduling policy: RANDOM\n");

            srand(time(NULL));

            int query_size = 0;
            int running = 0;
            int substitui_req = 0;
            struct timeval start_times[100];
            int shutdown_pedido = 0;
            int indice_comando_terminal = 0;

            while (1) {
                Request req;
                int n = read(fd_req, &req, sizeof(Request)); // coloca o comando lido do pipe na estrutura req
                if (n == -1) {
                    perror("Erro ao ler do pipe");
                    close(fd_req);
                    return 1;
                }

                if (req.status == 4) {
                    controller_envia_lista_para_runner(req, req_arr, query_size, running); // Envia a lista de comandos em execução e em espera para o runner quando receber um comando com status 4, que indica um pedido de consulta
                    continue;
                }

                substitui_req = substitui_comando_no_array(req_arr, query_size, req); // Verifica se o command_id do comando lido do pipe já está presente no array de Request e substitui se encontrado, e devolve 1 se encontrou e substituiu, ou 0 se não encontrou
                if (!substitui_req && !shutdown_pedido) {
                    if (req.status == 3) indice_comando_terminal = query_size; // guarda o índice do comando de terminação do controller 
                    req_arr[query_size++] = req; // Armazena a estrutura lida no array de Request e incrementa o tamanho do array
                }

                if (req.status == 2) { //se o comando foi concluído, encontra-o pelo command_id, escreve log e decrementa contador de processos ativos
                    for (int i = 0; i < query_size; i++) {
                        if (req_arr[i].command_id == req.command_id && req_arr[i].status == 2) {
                            struct timeval end;
                            gettimeofday(&end, NULL);
                            double elapsed = (end.tv_sec  - start_times[i].tv_sec)
                                            +(end.tv_usec - start_times[i].tv_usec) / 1000000.0;
                            escrever_no_log(req_arr[i], elapsed);
                            running--;
                            break;
                        }
                    }
                }

                while (running < parallel_commands) { // Enquanto o número de processos ativos for menor que o limite de comandos em paralelo, seleciona aleatoriamente um comando pendente para executar
                    int pendentes[100];
                    int total_pendentes = 0;

                    for (int i = 0; i < query_size; i++) { // ciclo para encontrar os índices dos comandos pendentes no array de Request e armazená-los no array pendentes
                        if (req_arr[i].status == 0) {
                            pendentes[total_pendentes++] = i;
                        }
                    }

                    if (total_pendentes == 0) break;

                    int indice_aleatorio = pendentes[rand() % total_pendentes]; // escolhe aleatoriamente indice de um comando pendente 
                    req_arr[indice_aleatorio].status = 1;
                    running++;
                    gettimeofday(&start_times[indice_aleatorio], NULL);
                    controller_envia_Ok_para_runner(req_arr[indice_aleatorio]);
                }

                if (req.status == 3 && !shutdown_pedido) {// Dizer que foi pedido a terminação do controller (status 3)
                    shutdown_pedido = 1;
                }

                if (shutdown_pedido && running == 0) { // Se foi pedido terminação, não há nada a correr e o comando que acabou de ser executado é o último do array
                    controller_envia_Ok_para_runner(req_arr[indice_comando_terminal]);
                    break;
                }
            }
    } else if (strcmp(sched_policy, "FAIR") == 0) {
        printf("[controller] scheduling policy: FAIR\n");

        int query_size = 0;
        int running = 0;
        int substitui_req = 0;
        struct timeval start_times[100];
        int shutdown_pedido = 0;
        int indice_comando_terminal = 0;
        int comandos_por_user[1024] = {0}; // inicializa todo o array a zeros

        while (1) {
            Request req;
            int n = read(fd_req, &req, sizeof(Request));
            if (n == -1) {
                perror("Erro ao ler do pipe");
                close(fd_req);
                return 1;
            }

            if (req.status == 4) {// Se o comando lido do pipe tem status 4, indica que é um pedido de consulta da lista de comandos
                controller_envia_lista_para_runner(req, req_arr, query_size, running);
                continue;
            }

            substitui_req = substitui_comando_no_array(req_arr, query_size, req);
            if (!substitui_req && !shutdown_pedido) {
                if (req.status == 3) indice_comando_terminal = query_size;
                req_arr[query_size++] = req;
            }

            if (req.status == 2) { // processa comando concluido
                for (int i = 0; i < query_size; i++) {
                    if (req_arr[i].command_id == req.command_id && req_arr[i].status == 2) {
                        struct timeval end;
                        gettimeofday(&end, NULL);
                        double elapsed = (end.tv_sec  - start_times[i].tv_sec)
                                        +(end.tv_usec - start_times[i].tv_usec) / 1000000.0;
                        escrever_no_log(req_arr[i], elapsed);
                        running--;
                        break;
                    }
                }
            }

            while (running < parallel_commands) { // enquanto o número de processos ativos for menor que o limite de comandos em paralelo, seleciona o comando pendente do user com menos comandos em execução para executar
                int melhor_indice = -1;

                for (int i = 0; i < query_size; i++) {
                    if (req_arr[i].status != 0) continue; // ignora comandos que não estão pendentes

                    if (melhor_indice == -1 || comandos_por_user[req_arr[i].user_id] < comandos_por_user[req_arr[melhor_indice].user_id]) {
                        melhor_indice = i;
                    }
                }

                if (melhor_indice == -1) break;

                req_arr[melhor_indice].status = 1;
                running++;
                comandos_por_user[req_arr[melhor_indice].user_id]++;
                gettimeofday(&start_times[melhor_indice], NULL);
                controller_envia_Ok_para_runner(req_arr[melhor_indice]);
            }

            if (req.status == 3 && !shutdown_pedido) { // Dizer que foi pedido a terminação do controller (status 3)
                shutdown_pedido = 1;
            }

            if (shutdown_pedido && running == 0) { // Se foi pedido terminação, não há nada a correr e o comando que acabou de ser executado é o último do array
                controller_envia_Ok_para_runner(req_arr[indice_comando_terminal]);
                break;
            }
        }
    }


    close(fd_req); // Fecha o pipe de pedido
    unlink("tmp/pipe_req"); // Remove o pipe de pedido criado pelo controller

    return 0;
}

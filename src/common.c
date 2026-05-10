#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "runner.h"
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>
#include "controller.h"
#include "common.h"

void runner_pede_terminacao_controller() {
    int fd_req = open("tmp/pipe_req", O_WRONLY); // Abre o pipe para escrita
    if (fd_req == -1) {
        perror("Erro ao abrir o pipe");
        return;
    }

    Request req;
    req.user_id = 0; // User_id 0 para indicar que é um pedido de terminação
    req.command_id = 0; // Command_id 0 para indicar que é um pedido de terminação
    strcpy(req.command, "Controller Shutdown"); // Comando de terminação
    req.status = 3; // Status 3 para indicar que é um pedido de terminação
    // Preenche o campo reply_pipe com o nome do pipe de resposta específico para este comando
    snprintf(req.reply_pipe, sizeof(req.reply_pipe), "tmp/pipe_res_%d", req.command_id);
    mkfifo(req.reply_pipe, 0666); // Cria o pipe de resposta específico para este comando com permissões de leitura e escrita para todos os usuários


    write(fd_req, &req, sizeof(Request)); // Envia o Request pelo pipe
    printf("[runner] sent shutdown notification\n");
    printf("[runner] waiting for controller to shutdown...\n");

    int fd_res_this_pipe_reply = open(req.reply_pipe, O_RDONLY); // Abre o pipe de resposta específico para este comando para leitura
    if (fd_res_this_pipe_reply == -1) {
        perror("Erro ao abrir o pipe de resposta");
        close(fd_req);
        return;
    }

    char buffer[10];
    int n = read(fd_res_this_pipe_reply, buffer, sizeof(buffer)); // Lê a resposta do pipe
    if (n == -1) {
        perror("Erro ao ler do pipe de resposta");
        close(fd_res_this_pipe_reply);
        close(fd_req);
        return;
    }

    buffer[n] = '\0'; // Adiciona o terminador de string
    if (strcmp(buffer, "Ok\n") == 0) {
        printf("[runner] controller exited.\n");
    }

    close(fd_res_this_pipe_reply); // Fecha o pipe de resposta
    close(fd_req); // Fecha o pipe
    unlink(req.reply_pipe); // Remove o pipe de resposta específico para este comando
}

void runner_pede_consulta_controller() {
    int fd_req = open("tmp/pipe_req", O_WRONLY); // abre o pipe principal para escrita

    Request req;
    req.command_id = getpid(); // usa o pid como id único do pedido
    strcpy(req.command, "List Query");
    req.status = 4; // status 4 = pedido de consulta (novo)
    
    snprintf(req.reply_pipe, sizeof(req.reply_pipe),
             "tmp/pipe_res_%d", req.command_id);// cria nome único para o pipe de resposta
    
    mkfifo(req.reply_pipe, 0666);// cria o pipe de resposta

    write(fd_req, &req, sizeof(Request));// envia o pedido ao controller

    int fd_res = open(req.reply_pipe, O_RDONLY); // abre o pipe de resposta para leitura

    char buffer[4096];
    int n = read(fd_res, buffer, sizeof(buffer) - 1); // lê a lista enviada pelo controller
    if (n > 0) {
        buffer[n] = '\0';
        printf("%s", buffer); // imprime a lista no terminal do runner
    }

    close(fd_res);
    close(fd_req);
    unlink(req.reply_pipe);// remove o pipe temporário de resposta
}

int substitui_comando_no_array (Request req_arr[], int size, Request req) { // Função para verificar se um command_id específico está presente no array de Request
    for (int i = 0; i < size; i++) { // Loop para percorrer o array de Request
        if (req_arr[i].command_id == req.command_id) { // Verifica se o command_id do elemento atual do array corresponde ao command_id procurado
            req_arr[i].status = req.status; // Atualiza o status do comando para o status do Request recebido
            return 1; // Retorna 1 (verdadeiro) se o command_id for encontrado no array
        }
    }
    return 0; // Retorna 0 (falso) se o command_id não for encontrado no array
}


int escrever_no_log(Request req, double tempo_gasto) { // Função para escrever no arquivo de log o comando executado e o tempo gasto
    int fd_long = open("log.txt", O_WRONLY | O_CREAT | O_APPEND, 0666); // Abre o arquivo de log para escrita, criando-o se não existir e adicionando ao final do arquivo
    if (fd_long == -1) {
        perror("Erro ao abrir o arquivo de log");
        return 1;
    }
    char buffer_log[512]; // Buffer para armazenar a mensagem de log
    sprintf(buffer_log, "Utilizador: %d\nComando executado: %s\nTempo gasto: %.6f segundos\n\n", req.user_id, req.command, tempo_gasto);
    write(fd_long, buffer_log, strlen(buffer_log)); // Escreve a mensagem de log no arquivo
    close(fd_long); // Fecha o arquivo de log
    return 0;
}


int controller_envia_Ok_para_runner(Request req) { // Função para enviar a mensagem de resposta Ok para o runner
    char msg_Ok[] = "Ok\n"; // Mensagem de resposta a ser enviada para o runner
    int fd_res_this_pipe_reply = open(req.reply_pipe, O_WRONLY); // Abre o pipe de resposta para escrita
    if (fd_res_this_pipe_reply == -1) {
        perror("Erro ao abrir o pipe de resposta");
        return 1;
    }
    write(fd_res_this_pipe_reply, msg_Ok, strlen(msg_Ok)); // Envia a mensagem de resposta Ok pelo pipe
    close(fd_res_this_pipe_reply); // Fecha o pipe de resposta após enviar a mensagem
    return 0;
}

int controller_envia_lista_para_runner(Request req, Request req_arr[],
                                        int query_size, int comando_em_execucao) {
    char buffer[4096];
    buffer[0] = '\0';
    char linha[512];

    strcat(buffer, " Comandos em execução \n");
    for (int i = 0; i < query_size; i++) {
        if (req_arr[i].status == 1) { // status 1 = em execução
            snprintf(linha, sizeof(linha),
                     "  [ID: %d] [user: %d] %s\n",
                     req_arr[i].command_id,
                     req_arr[i].user_id,
                     req_arr[i].command);
            strcat(buffer, linha);  // adiciona à resposta
        }
    }

    strcat(buffer, " Comandos em espera \n");
    for (int i = 0; i < query_size; i++) {
        if (req_arr[i].status == 0) { // status 0 = pendente ou em espera
            snprintf(linha, sizeof(linha),
                     "  [ID: %d] [user: %d] %s\n",
                     req_arr[i].command_id,
                     req_arr[i].user_id,
                     req_arr[i].command);
            strcat(buffer, linha); 
        }
    }

    int fd_res = open(req.reply_pipe, O_WRONLY);// abre o pipe de resposta do runner -c
    write(fd_res, buffer, strlen(buffer)); // envia a lista ao runner
    close(fd_res);
    return 0;
}

char *trim_espacos(char *str) { // elimina espaços
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }

    if (*str == '\0') {
        return str;
    }

    char *fim = str + strlen(str) - 1;
    while (fim > str && isspace((unsigned char)*fim)) {
        *fim = '\0';
        fim--;
    }

    return str;
}

int prepara_argumentos(char *comando, char *args[], int max_args) { // Função para preparar os argumentos para a execução do comando, dividindo a string do comando em tokens e armazenando os ponteiros para os tokens no array args
    int i = 0;
    char *token = strtok(comando, " ");

    while (token != NULL && i < max_args - 1) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }

    args[i] = NULL;
    return i;
}

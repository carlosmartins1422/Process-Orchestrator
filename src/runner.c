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
#include "common.h"


static void executa_comando(char *comando) {
    char comando_copia[256];
    strncpy(comando_copia, comando, sizeof(comando_copia) - 1); // copia o comando para uma variável local para evitar modificar a string original
    comando_copia[sizeof(comando_copia) - 1] = '\0';

    if (strstr(comando_copia, "2>") != NULL) { // logica para redirecionamento de stderr
        char *redirecionamento = strstr(comando_copia, "2>"); // procura o operador de redirecionamento de stderr e guarda o ponteiro para ele em redirecionamento
        *redirecionamento = '\0'; // divide a string
        char *comando_base = trim_espacos(comando_copia);
        char *ficheiro_erro = trim_espacos(redirecionamento + 2);

        int fd_erro = open(ficheiro_erro, O_WRONLY | O_CREAT | O_TRUNC, 0666); // Abre o ficheiro de erro para escrita, criando-o se não existir e truncando-o se já existir
        if (fd_erro == -1) {
            perror("Erro ao abrir o ficheiro de erro");
            exit(1);
        }

        if (dup2(fd_erro, STDERR_FILENO) == -1) { // Redireciona a saída de erro para o ficheiro de erro
            perror("Erro ao redirecionar stderr");
            close(fd_erro);
            exit(1);
        }

        close(fd_erro); // Fecha o ficheiro de erro, pois já foi redirecionado para a saída de erro

        char *args[64];
        if (prepara_argumentos(comando_base, args, 64) == 0) { // divide string em tokens e prepara os argumentos para a execução do comando
            fprintf(stderr, "Comando inválido.\n");
            exit(1);
        }

        execvp(args[0], args); // Executa o comando usando execvp, que procura o comando no PATH e executa com os argumentos fornecidos
        perror("Erro ao executar o comando");
        exit(1);
    }

    if (strchr(comando_copia, '|') != NULL) { // logica para redirecionamento de pipe
        char *pipe_pos = strchr(comando_copia, '|'); // procura o operador de pipe e guarda o ponteiro para ele
        *pipe_pos = '\0'; // divide a string em dois comandos
        char *comando_esquerda = trim_espacos(comando_copia);
        char *comando_direita = trim_espacos(pipe_pos + 1);

        int pipefd[2]; // pipe para comunicação entre os processos dos comandos à esquerda e à direita do pipe
        if (pipe(pipefd) == -1) {
            perror("Erro ao criar pipe");
            exit(1);
        }

        pid_t pid_esquerda = fork();// Cria um processo filho para executar o comando à esquerda do pipe
        if (pid_esquerda == -1) {
            perror("Erro ao criar processo para o comando à esquerda do pipe");
            close(pipefd[0]);
            close(pipefd[1]);
            exit(1);
        }

        if (pid_esquerda == 0) { // Processo filho para o comando à esquerda do pipe
            char *args_esquerda[64];

            close(pipefd[0]);

            if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
                perror("Erro ao redirecionar stdout para o pipe");
                close(pipefd[1]);
                exit(1);
            }

            close(pipefd[1]);

            if (prepara_argumentos(comando_esquerda, args_esquerda, 64) == 0) {
                fprintf(stderr, "Comando inválido.\n");
                exit(1);
            }

            execvp(args_esquerda[0], args_esquerda);
            perror("Erro ao executar o comando à esquerda do pipe");
            exit(1);
        }

        pid_t pid_direita = fork();// Cria um processo filho para executar o comando à direita do pipe
        if (pid_direita == -1) {
            perror("Erro ao criar processo para o comando à direita do pipe");
            close(pipefd[0]);
            close(pipefd[1]);
            waitpid(pid_esquerda, NULL, 0);
            exit(1);
        }

        if (pid_direita == 0) { // Processo filho para o comando à direita do pipe
            char *args_direita[64];

            close(pipefd[1]);

            if (dup2(pipefd[0], STDIN_FILENO) == -1) {
                perror("Erro ao redirecionar stdin a partir do pipe");
                close(pipefd[0]);
                exit(1);
            }

            close(pipefd[0]);

            if (prepara_argumentos(comando_direita, args_direita, 64) == 0) {
                fprintf(stderr, "Comando inválido.\n");
                exit(1);
            }

            execvp(args_direita[0], args_direita);
            perror("Erro ao executar o comando à direita do pipe");
            exit(1);
        }

        close(pipefd[0]);
        close(pipefd[1]);

        int status_esquerda; // Variável para armazenar o status de término do processo do comando à esquerda do pipe
        int status_direita; // Variável para armazenar o status de término do processo do comando à direita do pipe

        waitpid(pid_esquerda, &status_esquerda, 0);
        waitpid(pid_direita, &status_direita, 0);

        if (WIFEXITED(status_esquerda) && WEXITSTATUS(status_esquerda) == 0 &&
            WIFEXITED(status_direita) && WEXITSTATUS(status_direita) == 0) {
            exit(0);
        }

        exit(1);
    }

    if (strchr(comando_copia, '<') != NULL) { // logica para redirecionamento de stdin
        char *redirecionamento = strchr(comando_copia, '<'); // procura o operador de redirecionamento de stdin e guarda o ponteiro para ele em redirecionamento
        *redirecionamento = '\0'; // divide a string
        char *comando_base = trim_espacos(comando_copia);
        char *ficheiro_entrada = trim_espacos(redirecionamento + 1);

        int fd_entrada = open(ficheiro_entrada, O_RDONLY); // Abre o ficheiro de entrada para leitura
        if (fd_entrada == -1) {
            perror("Erro ao abrir o ficheiro de entrada");
            exit(1);
        }

        if (dup2(fd_entrada, STDIN_FILENO) == -1) { // Redireciona a entrada padrão para o ficheiro de entrada
            perror("Erro ao redirecionar stdin");
            close(fd_entrada);
            exit(1);
        }

        close(fd_entrada); // Fecha o ficheiro de entrada, pois já foi redirecionado para a entrada padrão

        char *args[64];
        if (prepara_argumentos(comando_base, args, 64) == 0) { // divide string em tokens e prepara os argumentos para a execução do comando
            fprintf(stderr, "Comando inválido.\n");
            exit(1);
        }

        execvp(args[0], args); // Executa o comando usando execvp, que procura o comando no PATH e executa com os argumentos fornecidos
        perror("Erro ao executar o comando");
        exit(1);
    }

    if (strchr(comando_copia, '>') != NULL) { // logica para redirecionamento de stdout
        char *redirecionamento = strchr(comando_copia, '>'); // procura o operador de redirecionamento de stdout e guarda o ponteiro para ele em redirecionamento
        *redirecionamento = '\0'; // divide a string
        char *comando_base = trim_espacos(comando_copia); 
        char *ficheiro_saida = trim_espacos(redirecionamento + 1);

        int fd_saida = open(ficheiro_saida, O_WRONLY | O_CREAT | O_TRUNC, 0666); // Abre o ficheiro de saída para escrita, criando-o se não existir e truncando-o se já existir, com permissões de leitura e escrita para todos os usuários
        if (fd_saida == -1) {
            perror("Erro ao abrir o ficheiro de saída");
            exit(1);
        }

        if (dup2(fd_saida, STDOUT_FILENO) == -1) { // Redireciona a saída padrão para o ficheiro de saída
            perror("Erro ao redirecionar stdout");
            close(fd_saida);
            exit(1);
        }

        close(fd_saida); // Fecha o ficheiro de saída, pois já foi redirecionado para a saída padrão

        char *args[64];
        if (prepara_argumentos(comando_base, args, 64) == 0) { // divide string em tokens e prepara os argumentos para a execução do comando
            fprintf(stderr, "Comando inválido.\n");
            exit(1);
        }

        execvp(args[0], args); // Executa o comando usando execvp, que procura o comando no PATH e executa com os argumentos fornecidos
        perror("Erro ao executar o comando");
        exit(1);
    }

    char *args[64];
    if (prepara_argumentos(comando_copia, args, 64) == 0) { // divide string em tokens e prepara os argumentos para a execução do comando
        fprintf(stderr, "Comando inválido.\n");
        exit(1);
    }

    execvp(args[0], args);
    perror("Erro ao executar o comando");
    exit(1);
}

int main (int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "Uso: %s -e <user_id> \"comando\"\n ou %s -c\n ou %s -s\n", argv[0], argv[0], argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-e") == 0) { // Modo de execução
        if (argc < 4) { // Verifica se o número de argumentos é menor que 4 (programa, modo de utilização, user_id, comando)
            fprintf(stderr, "Uso: %s -e <user_id> \"comando\"\n", argv[0]);
            return 1;
        }
        int fd_req = open("tmp/pipe_req", O_WRONLY); // Abre o pipe para escrita
        if (fd_req == -1) {
            perror("Erro ao abrir o pipe");
            return 1;
        }

        Request req;
        req.user_id = atoi(argv[2]); // Converte o argumento de string para inteiro e atribui a user_id
        req.command_id = getpid(); // Usa o PID do processo como command_idº
        req.command[0] = '\0'; // Inicializa a string command com o terminador de string
        strcpy(req.command, argv[3]); // Guarda o comando completo recebido entre aspas
        req.operador_redirecionamento = 0;

        if (strstr(req.command, "2>") != NULL) req.operador_redirecionamento = 2;
        else if (strchr(req.command, '>') != NULL) req.operador_redirecionamento = 1;
        else if (strchr(req.command, '<') != NULL) req.operador_redirecionamento = 3;
        else if (strchr(req.command, '|') != NULL) req.operador_redirecionamento = 4;

        req.status = 0; // Define o status como 0 (pendente)
        printf("[runner] command %d submitted\n", req.command_id); // Imprime uma mensagem de status no console
        // Preenche o campo reply_pipe com o nome do pipe de resposta específico para este comando
        snprintf(req.reply_pipe, sizeof(req.reply_pipe), "tmp/pipe_res_%d", req.command_id);
        mkfifo(req.reply_pipe, 0666); // Cria o pipe de resposta específico para este comando com permissões de leitura e escrita para todos os usuários
    
        write (fd_req, &req, sizeof(Request)); // Envia o Request pelo pipe
    
        //-----------------------------------------------------Resposta----------------------------------------------
        int fd_res_this_pipe_reply = open(req.reply_pipe, O_RDONLY); // Abre um pipe específico para receber a resposta
        if (fd_res_this_pipe_reply == -1) {
            perror("Erro ao abrir o pipe de resposta");
            return 1;
        }
    
        char buffer[10];
        int n = read (fd_res_this_pipe_reply, buffer, sizeof(buffer)); // Lê a resposta do pipe
        if (n == -1) {
            perror("Erro ao ler do pipe de resposta");
            close(fd_res_this_pipe_reply);
            return 1;
        }
    
        buffer[n] = '\0'; // Adiciona o terminador de string
        if (strcmp(buffer, "Ok\n") == 0) {
    
            pid_t pid = fork(); // Cria um processo filho para executar o comando
            if (pid == -1) {
                perror("Erro ao criar processo filho");
                close(fd_res_this_pipe_reply);
                return 1;
            } else if (pid == 0) { // Processo filho
                req.status = 1; // Define o status como 1 (em execução)
                write(fd_req, &req, sizeof(Request)); // Escreve o status atualizado de volta no pipe de pedido para que o controller possa atualizar o status do comando
                printf("[runner] executing command %d...\n", req.command_id); // Imprime uma mensagem de status no console
                executa_comando(req.command);
            } else { // Processo pai
                int status;
                pid_t w = waitpid(pid, &status, 0); // Espera o processo filho terminar
                if (w == -1) {
                    perror("Erro ao esperar o processo filho");
                    close(fd_res_this_pipe_reply);
                    return 1;
                }
                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) { // Verifica se o processo filho terminou com sucesso
                    req.status = 2; // Define o status como 2 (concluída)
                }
                printf("[runner] command %d finished.\n", req.command_id); // Imprime uma mensagem de status no console
                write(fd_req, &req, sizeof(Request)); // Envia o status a dizer que o comando foi concluído
            }
        } else {
            printf("Erro na execução do comando: %s\n", buffer);
        }
    
    
        close(fd_req); // Fecha o pipe
        close(fd_res_this_pipe_reply); // Fecha o pipe de resposta
        unlink(req.reply_pipe); // Remove o pipe de resposta específico para este comando

    } else if (strcmp(argv[1], "-c") == 0) { // Consultar comandos em execução    
        if (argc != 2){
            fprintf(stderr, "Uso: %s -c\n", argv[0]);
            return 1;
        }
        runner_pede_consulta_controller(); // Envia pedido de consulta ao controller e imprime a lista de comandos em execução e em espera
    } else if (strcmp(argv[1], "-s") == 0) { // Pede a terminação do programa controller
        if (argc != 2) { // Verifica se o número de argumentos é diferente de 2 (programa, modo de utilização)
            fprintf(stderr, "Uso: %s -s\n", argv[0]);
            return 1;
        }
        runner_pede_terminacao_controller(); // Chama a função para pedir a terminação do controller
    } else {
        if (argc != 2) { // Verifica se o número de argumentos é diferente de 2 (programa, modo de utilização)
            fprintf(stderr, "Uso: %s -e <user_id> \"comando\"\n ou %s -c\n ou %s -s\n", argv[0], argv[0], argv[0]);
            return 1;
        }
        fprintf(stderr, "Modo de utilização inválido. Use -e para execução, -c para consulta ou -s para terminação.\n");
        return 1;
    }
}

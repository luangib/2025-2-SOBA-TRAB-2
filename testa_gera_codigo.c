/*
 * Arquivo de Teste para o Gerador de Código LBS (INF1018 - 2025.2)
 *
 * Aluno: [Nome_do_Aluno1] [Matricula] [Turma]
 * Aluno: [Nome_do_Aluno2] [Matricula] [Turma]
 */

#include <stdio.h>
#include <stdlib.h>

 // Tipo e protótipo da função que será implementada no gera_codigo.c
typedef int (*funcp) (int x);
void gera_codigo(FILE* f, unsigned char code[], funcp* entry);


int main(int argc, char* argv[]) {
    FILE* myfp;
    // Ponteiro para a função gerada, que recebe 1 int e retorna 1 int.
    funcp funcaoLBS;
    int res;

    // Buffer de código de máquina. O tamanho é uma estimativa.
    unsigned char codigo[500];

    if (argc < 3) {
        fprintf(stderr, "Uso: %s <arquivo_lbs> <argumento_x>\n", argv[0]);
        fprintf(stderr, "Exemplo: %s fat.lbs 5\n", argv[0]);
        exit(1);
    }

    if ((myfp = fopen(argv[1], "r")) == NULL) {
        perror("Falha na abertura do arquivo fonte LBS");
        exit(1);
    }

    // Argumento para a função gerada
    int argumento_x = atoi(argv[2]);

    printf("Iniciando a geracao de codigo para '%s'...\n", argv[1]);

    // Chama a função que gera o código. O endereço de `funcaoLBS` é passado para ser preenchido.
    gera_codigo(myfp, codigo, &funcaoLBS);
    fclose(myfp);

    if (funcaoLBS == NULL) {
        fprintf(stderr, "Erro na geracao de codigo.\n");
        return 1;
    }

    printf("Codigo gerado com sucesso. Endereco de entrada: %p\n", (void*)funcaoLBS);
    printf("Chamando a funcao LBS gerada com argumento: %d\n", argumento_x);

    // Chama a função gerada, passando o argumento
    res = (*funcaoLBS)(argumento_x);

    printf("Resultado da execucao: %d\n", res);

    return 0;
}
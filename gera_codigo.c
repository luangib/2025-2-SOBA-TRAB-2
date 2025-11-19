/*
 * Gerador de Código x64 para a linguagem LBS (INF1018 - 2025.2)
 *
 * Adaptação do código SBas (peqcomp) para LBS (gera_codigo)
 *
 * Aluno: [Nome_do_Aluno1] [Matricula] [Turma]
 * Aluno: [Nome_do_Aluno2] [Matricula] [Turma]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

 // Definição do protótipo (assumindo gera_codigo.h não foi modificado)
typedef int (*funcp) (int x);
void gera_codigo(FILE* f, unsigned char code[], funcp* entry);


// --- CONSTANTES E MACROS ---

// Máximo de funções (o enunciado não especifica, mas 64 é um bom limite)
#define MAX_FUNCTIONS 64 
// 5 variáveis locais (v0 a v4), 4 bytes cada = 20 bytes.
// Alocamos 24 bytes na pilha para garantir alinhamento de 16 bytes (x64 ABI).
#define LOCAL_VARS_SIZE 24 
// Offset do parâmetro p0 na pilha. Salvo em -24(%rbp)
#define P0_OFFSET -24 
// Offset das variáveis locais v0 a v4 na pilha (4 bytes/var). v0: -4, v4: -20
#define VAR_OFFSET(v) (-4 * (v + 1)) 

// --- ESTRUTURAS DE DADOS GLOBAIS ---
// Tabela para armazenar o endereço de início de cada função LBS.
// O índice é o número da função (0, 1, 2, ...)
unsigned char* function_start_addrs[MAX_FUNCTIONS];
int function_count = 0; // Contador de funções lidas
int pc = 0;             // Program Counter: índice atual no array code

// --- PROTÓTIPOS DAS FUNÇÕES AUXILIARES ---
void write_bytes(unsigned char codigo[], const unsigned char* bytes, int len);
void emit_prologue(unsigned char codigo[]);
void emit_epilogue(unsigned char codigo[]);
void mov_varpc_to_reg(unsigned char codigo[], int reg, char tipo, int index_or_const);
void mov_reg_to_var(unsigned char codigo[], int reg, int v_index);


// --- IMPLEMENTAÇÃO PRINCIPAL ---

void gera_codigo(FILE* f, unsigned char code[], funcp* entry) {
    char buf[256];
    function_count = 0;
    pc = 0;

    // Estruturas de patching para JUMPS (zret) dentro da mesma função (não necessário para CALL)
    // O zret sempre salta para o código de retorno no final do bloco atual.

    while (fgets(buf, sizeof(buf), f)) {
        char* p = buf;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '\n' || *p == '/' || *p == '#') continue;

        // --- 1. function ---
        if (strncmp(p, "function", 8) == 0) {
            // Salva o endereço de início da nova função
            function_start_addrs[function_count++] = &code[pc];
            emit_prologue(code);
            continue;
        }

        // --- 2. end ---
        if (strncmp(p, "end", 3) == 0) {
            // O `end` é apenas um marcador. O fluxo de controle deve terminar em `ret` ou `zret`.
            continue;
        }

        int v_dest;
        char varpc1_type = 0, varpc2_type = 0;
        int val1, val2;
        char op = 0;
        int call_num;
        char temp_str[10];

        // v0 = [p0|vX|$K] OP [p0|vX|$K] (Atribuição e Operação Aritmética)
        // A lógica de parsing é complexa devido às combinações, tentaremos simplificar.
        if (sscanf(p, "v%d = %s %c %s", &v_dest, temp_str, &op, buf) == 4) {

            // Simplificação do parsing para os operandos
            // O primeiro operando (varpc1)
            if (strcmp(temp_str, "p0") == 0) { varpc1_type = 'p'; val1 = 0; }
            else if (temp_str[0] == 'v' && sscanf(temp_str, "v%d", &val1) == 1) { varpc1_type = 'v'; }
            else if (temp_str[0] == '$' && sscanf(temp_str, "$%d", &val1) == 1) { varpc1_type = '$'; }
            else { continue; } // Erro de parsing

            // O segundo operando (varpc2)
            if (strcmp(buf, "p0") == 0) { varpc2_type = 'p'; val2 = 0; }
            else if (buf[0] == 'v' && sscanf(buf, "v%d", &val2) == 1) { varpc2_type = 'v'; }
            else if (buf[0] == '$' && sscanf(buf, "$%d", &val2) == 1) { varpc2_type = '$'; }
            else { continue; } // Erro de parsing

            // Tradução da Operação: v_dest = varpc1 op varpc2

            // 1. Mover varpc1 para %eax (o primeiro operando)
            mov_varpc_to_reg(code, 0, varpc1_type, val1);

            // 2. Realizar a operação com o segundo operando (%eax op varpc2)
            unsigned char op_code;
            if (op == '+') op_code = 0x03; // addl
            else if (op == '-') op_code = 0x2b; // subl
            else if (op == '*') op_code = 0x0f; // imull (usaremos imul estendido, mas aqui simplifica)
            else { continue; }

            // Lógica simplificada: se varpc2 é variável ou p0, use R/M. Se for constante, use Imediato.
            if (varpc2_type == 'v') {
                // Op <varpc2>, %eax (ex: addl -4*(v2+1)(%rbp), %eax)
                code[pc++] = op_code;
                code[pc++] = 0x45;
                code[pc++] = VAR_OFFSET(val2);
            }
            else if (varpc2_type == 'p') {
                // Op p0, %eax (ex: addl -24(%rbp), %eax)
                code[pc++] = op_code;
                code[pc++] = 0x45;
                code[pc++] = P0_OFFSET;
            }
            else if (varpc2_type == '$') {
                // Op $const, %eax (ex: addl $10, %eax)
                // Nota: Para -, o opcode correto seria subl $const, %eax (0x2D).
                // Para simplificar e evitar múltiplos opcodes por operação, assumiremos que
                // o valor já está em %eax e operaremos a partir dele.
                if (op == '+') code[pc++] = 0x05; // addl $const, %eax
                else if (op == '-') code[pc++] = 0x2d; // subl $const, %eax
                else if (op == '*') {
                    // imull $const, %eax (opcode 0x69 seguido de 0xC0 e o const)
                    code[pc++] = 0x69; code[pc++] = 0xc0;
                }
                *(int*)(&code[pc]) = val2; pc += 4;
            }

            // 3. Mover o resultado de %eax para v_dest
            mov_reg_to_var(code, 0, v_dest);
            continue;
        }

        // v0 = call num varpc (Chamada de Função)
        // Tentaremos um sscanf mais simples para pegar o tipo e valor do varpc
        // Ex: v0 = call 0 v1 | v0 = call 1 p0 | v0 = call 2 $10
        if (sscanf(p, "v%d = call %d %s", &v_dest, &call_num, temp_str) == 3) {

            // 1. Determinar o operando (varpc) e movê-lo para %edi (registrador de 1º parâmetro)
            if (strcmp(temp_str, "p0") == 0) { // varpc = p0
                // movl -24(%rbp), %edi (carrega p0 que foi salvo no prólogo)
                code[pc++] = 0x8b; code[pc++] = 0x7d; code[pc++] = P0_OFFSET;
            }
            else if (temp_str[0] == 'v' && sscanf(temp_str, "v%d", &val1) == 1) { // varpc = vX
                // movl -4*(v+1)(%rbp), %edi
                code[pc++] = 0x8b; code[pc++] = 0x7d; code[pc++] = VAR_OFFSET(val1);
            }
            else if (temp_str[0] == '$' && sscanf(temp_str, "$%d", &val1) == 1) { // varpc = $K
                // movl $const, %edi
                code[pc++] = 0xbf; *(int*)(&code[pc]) = val1; pc += 4;
            }

            // 2. CALL: Instrução de 5 bytes: 0xE8 (CALL rel32) + 4 bytes de offset
            code[pc++] = 0xe8;

            // O offset (endereço relativo) será calculado e preenchido aqui (patching)
            // CALL para trás (funções anteriores): o endereço já é conhecido.
            unsigned char* target_addr = function_start_addrs[call_num];
            if (target_addr != NULL) {
                int target_offset = (int)(target_addr - &code[pc]); // Deslocamento em relação ao endereço DA PRÓXIMA INSTRUÇÃO
                *(int*)(&code[pc]) = target_offset;
            }
            else {
                // ERRO: Tentativa de chamar função não definida ou posterior (violando regra LBS)
                fprintf(stderr, "Erro: Chamada para funcao LBS %d indefinida ou fora de ordem.", call_num);
                *entry = NULL;
                return;
            }
            pc += 4;

            // 3. Mover o resultado de %eax (valor de retorno) para v_dest
            mov_reg_to_var(code, 0, v_dest); // 0 = %eax
            continue;
        }

        // ret varpc (Retorno Incondicional)
        if (sscanf(p, "ret %s", temp_str) == 1) {
            // 1. Mover o valor de retorno (varpc) para %eax
            if (strcmp(temp_str, "p0") == 0) { // varpc = p0
                // movl -24(%rbp), %eax
                code[pc++] = 0x8b; code[pc++] = 0x45; code[pc++] = P0_OFFSET;
            }
            else if (temp_str[0] == 'v' && sscanf(temp_str, "v%d", &val1) == 1) { // varpc = vX
                mov_varpc_to_reg(code, 0, 'v', val1); // 0 = %eax
            }
            else if (temp_str[0] == '$' && sscanf(temp_str, "$%d", &val1) == 1) { // varpc = $K
                mov_varpc_to_reg(code, 0, '$', val1); // 0 = %eax
            }

            // 2. EPÍLOGO: leave e ret
            emit_epilogue(code);
            continue;
        }

        // zret varpc1 varpc2 (Retorno Condicional: se varpc1 == 0, retorna varpc2)
        if (sscanf(p, "zret %s %s", temp_str, buf) == 2) {

            // 1. Mover varpc1 para %eax (ou outro reg, ex: %ecx) para comparação
            char* varpc1_ptr = temp_str;
            char* varpc2_ptr = buf;

            // Mover varpc1 para %ecx (reg 1)
            if (strcmp(varpc1_ptr, "p0") == 0) {
                code[pc++] = 0x8b; code[pc++] = 0x4d; code[pc++] = P0_OFFSET; // movl -24(%rbp), %ecx
            }
            else if (varpc1_ptr[0] == 'v' && sscanf(varpc1_ptr, "v%d", &val1) == 1) {
                code[pc++] = 0x8b; code[pc++] = 0x4d; code[pc++] = VAR_OFFSET(val1); // movl varX(%rbp), %ecx
            }
            else if (varpc1_ptr[0] == '$' && sscanf(varpc1_ptr, "$%d", &val1) == 1) {
                code[pc++] = 0xb9; *(int*)(&code[pc]) = val1; pc += 4; // movl $const, %ecx
            }

            // 2. Comparar %ecx com 0
            // testl %ecx, %ecx (seta flags)
            code[pc++] = 0x85; code[pc++] = 0xc9;

            // 3. Salto condicional: Se Z (Zero flag) estiver setado (== 0), pule
            // je (jump if equal) - Instrução de 6 bytes: 0x0f 0x84 + 4 bytes de offset
            code[pc++] = 0x0f; code[pc++] = 0x84;

            int jump_patch_pos = pc; // Posição para preencher o offset do JE
            pc += 4; // Avança 4 bytes

            // 4. Se a condição NÃO for satisfeita (varpc1 != 0), o código segue aqui
            // (Não há código, apenas continua a próxima instrução LBS)

            // 5. Destino do JE (Código de Retorno): Se varpc1 == 0, execute daqui
            int return_target_addr = pc;

            // Mover varpc2 para %eax
            if (strcmp(varpc2_ptr, "p0") == 0) {
                code[pc++] = 0x8b; code[pc++] = 0x45; code[pc++] = P0_OFFSET; // movl -24(%rbp), %eax
            }
            else if (varpc2_ptr[0] == 'v' && sscanf(varpc2_ptr, "v%d", &val2) == 1) {
                mov_varpc_to_reg(code, 0, 'v', val2); // 0 = %eax
            }
            else if (varpc2_ptr[0] == '$' && sscanf(varpc2_ptr, "$%d", &val2) == 1) {
                mov_varpc_to_reg(code, 0, '$', val2); // 0 = %eax
            }

            // EPÍLOGO
            emit_epilogue(code);

            // 6. Patching do Salto: Calcular e preencher o offset do JE
            int relative_offset = return_target_addr - (jump_patch_pos + 4);
            *(int*)(&code[jump_patch_pos]) = relative_offset;

            continue;
        }

        // Se chegou aqui, a instrução não foi reconhecida.
        fprintf(stderr, "Aviso: Instrucao LBS nao reconhecida ou mal formatada: %s", buf);
        // Não é necessário abortar, mas é bom para debug
    }

    // Armazena o endereço de início da ÚLTIMA função gerada
    if (function_count > 0) {
        // O endereço da última função é o (function_count - 1)-ésimo
        *entry = (funcp)function_start_addrs[function_count - 1];
    }
    else {
        *entry = NULL;
    }
}


// --- IMPLEMENTAÇÃO DAS FUNÇÕES AUXILIARES ---

// Escreve uma sequência de bytes no buffer de código
void write_bytes(unsigned char codigo[], const unsigned char* bytes, int len) {
    for (int i = 0; i < len; i++) {
        codigo[pc++] = bytes[i];
    }
}

// Emite o Prólogo da função LBS
void emit_prologue(unsigned char codigo[]) {
    // pushq %rbp
    codigo[pc++] = 0x55;
    // movq %rsp, %rbp
    codigo[pc++] = 0x48; codigo[pc++] = 0x89; codigo[pc++] = 0xe5;
    // subq $24, %rsp (Aloca espaço para variáveis locais e alinhamento)
    codigo[pc++] = 0x48; codigo[pc++] = 0x83; codigo[pc++] = 0xec; codigo[pc++] = LOCAL_VARS_SIZE;

    // Salvar o p0 (%edi) na pilha em -24(%rbp)
    // movl %edi, -24(%rbp) (0xE8 = -24)
    codigo[pc++] = 0x89; codigo[pc++] = 0x7d; codigo[pc++] = 0xE8;
}

// Emite o Epílogo da função LBS
void emit_epilogue(unsigned char codigo[]) {
    // leave (movq %rbp, %rsp; popq %rbp)
    codigo[pc++] = 0xc9;
    // ret
    codigo[pc++] = 0xc3;
}

/**
 * Move um operando varpc (vX, p0, $K) para um registrador
 * @param reg 0: %eax, 1: %ecx, 2: %edx
 * @param tipo 'v' para variável, 'p' para p0, '$' para constante
 * @param index_or_const índice da variável (0-4) ou valor da constante
 */
void mov_varpc_to_reg(unsigned char codigo[], int reg, char tipo, int index_or_const) {
    // Registradores e opcodes base (movl reg, ...)
    // %eax: 0xb8 (imediato), 0x8b 0x45 (r/m)
    // %ecx: 0xb9 (imediato), 0x8b 0x4d (r/m)
    // %edx: 0xba (imediato), 0x8b 0x55 (r/m)

    int mod_reg = 0; // Modificador para o byte ModR/M
    int mov_imm_base = 0xb8; // movl $imm, %eax

    if (reg == 0) { mod_reg = 0x45; } // %eax (-4(%rbp))
    else if (reg == 1) { mod_reg = 0x4d; } // %ecx
    else if (reg == 2) { mod_reg = 0x55; } // %edx

    mov_imm_base += reg;

    if (tipo == 'v') {
        // movl -4*(v+1)(%rbp), %reg
        codigo[pc++] = 0x8b;
        codigo[pc++] = mod_reg;
        codigo[pc++] = VAR_OFFSET(index_or_const);
    }
    else if (tipo == '$') {
        // movl $const, %reg
        codigo[pc++] = mov_imm_base;
        *(int*)&codigo[pc] = index_or_const;
        pc += 4;
    }
    else if (tipo == 'p') {
        // movl -24(%rbp), %reg (p0)
        codigo[pc++] = 0x8b;
        codigo[pc++] = mod_reg;
        codigo[pc++] = P0_OFFSET;
    }
}

// Move o conteúdo de um registrador para uma variável local vX
void mov_reg_to_var(unsigned char codigo[], int reg, int v_index) {
    // movl %reg, -4*(v_index+1)(%rbp)
    // %eax (reg=0): 0x89 0x45
    // %ecx (reg=1): 0x89 0x4d
    // %edx (reg=2): 0x89 0x55

    int mod_reg = 0; // Modificador para o byte ModR/M
    if (reg == 0) { mod_reg = 0x45; }
    else if (reg == 1) { mod_reg = 0x4d; }
    else if (reg == 2) { mod_reg = 0x55; }

    codigo[pc++] = 0x89;
    codigo[pc++] = mod_reg;
    codigo[pc++] = VAR_OFFSET(v_index);
}
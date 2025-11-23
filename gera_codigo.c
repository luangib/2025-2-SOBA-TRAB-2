/*
 * Gerador de Código x64 para a linguagem LBS (INF1018 - 2025.2)
 *
 * Adaptação do código SBas (peqcomp) para LBS (gera_codigo)
 *
 * Aluno: Luan Francisco Gibson Coutinho 2411167 3WB
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Definição do protótipo (assumindo gera_codigo.h não foi modificado)
typedef int (*funcp) (int x);
void gera_codigo (FILE *f, unsigned char code[], funcp *entry);


// --- CONSTANTES E MACROS ---

// Máximo de funções (o enunciado não especifica, mas 64 é um bom limite)
#define MAX_FUNCTIONS 64 
// 5 variáveis locais (v0 a v4), 4 bytes cada = 20 bytes.
// Alocamos 24 bytes na pilha para garantir alinhamento de 16 bytes (x64 ABI).
#define LOCAL_VARS_SIZE 0x18 // 24 decimal
// Offset do parâmetro p0 na pilha. Salvo em -24(%rbp)
#define P0_OFFSET -24 
// Offset das variáveis locais v0 a v4 na pilha (4 bytes/var). v0: -4, v4: -20
#define VAR_OFFSET(v) (-4 * (v + 1)) 

// --- ESTRUTURAS DE DADOS GLOBAIS ---
// Tabela para armazenar o endereço de início de cada função LBS.
// O índice é o número da função (0, 1, 2, ...)
unsigned char *function_start_addrs[MAX_FUNCTIONS];
int function_count = 0; // Contador de funções lidas
int pc = 0;             // Program Counter: índice atual no array code

// --- PROTÓTIPOS DAS FUNÇÕES AUXILIARES ---
void write_bytes(unsigned char codigo[], const unsigned char *bytes, int len);
void emit_prologue(unsigned char codigo[]);
void emit_epilogue(unsigned char codigo[]);
void mov_varpc_to_reg(unsigned char codigo[], int reg, char tipo, int index_or_const);
void mov_reg_to_var(unsigned char codigo[], int reg, int v_index);
// Função auxiliar para parsing de varpc (ex: "p0", "v1", "$10")
void parse_varpc(const char *token, char *type_out, int *val_out);


// --- IMPLEMENTAÇÃO PRINCIPAL ---

void gera_codigo(FILE *f, unsigned char code[], funcp *entry) {
    char buf[256];
    function_count = 0;
    pc = 0;

    // Inicializa a tabela de endereços de funções
    for (int i = 0; i < MAX_FUNCTIONS; i++) {
        function_start_addrs[i] = NULL;
    }
    
    while (fgets(buf, sizeof(buf), f)) {
        char *p = buf;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '\n' || *p == '/' || *p == '#') continue;

        // --- 1. function ---
        if (strncmp(p, "function", 8) == 0) {
            // Salva o endereço de início da nova função
            if (function_count >= MAX_FUNCTIONS) {
                fprintf(stderr, "Erro: Limite maximo de funcoes LBS excedido.\n");
                *entry = NULL;
                return;
            }
            function_start_addrs[function_count++] = &code[pc];
            emit_prologue(code);
            continue;
        }

        // --- 2. end ---
        if (strncmp(p, "end", 3) == 0) {
            continue;
        }

        int v_dest;
        char varpc1_type = 0, varpc2_type = 0;
        int val1 = 0, val2 = 0;
        char op = 0;
        int call_num;
        char token1_str[10], token2_str[10];
        
        // --- Operação Aritmética e Atribuição (v_dest = varpc1 op varpc2) ---
        // O sscanf aqui pode casar com "v0 = call 0 p0" erroneamente (token1="call", op='0').
        // Por isso, precisamos verificar se parse_varpc tem sucesso. Se falhar, NÃO executamos continue,
        // para permitir que o fluxo caia no bloco de 'call' abaixo.
        if (sscanf(p, "v%d = %s %c %s", &v_dest, token1_str, &op, token2_str) == 4) {
            
            // 1. ANÁLISE DOS OPERANDOS
            parse_varpc(token1_str, &varpc1_type, &val1);
            parse_varpc(token2_str, &varpc2_type, &val2);

            // Só processamos se ambos os operandos forem válidos (v, p, $)
            if (varpc1_type != 0 && varpc2_type != 0) { 

                // 2. TRADUÇÃO DA OPERAÇÃO
                // Passo A: Mover varpc1 para %eax (o acumulador)
                mov_varpc_to_reg(code, 0, varpc1_type, val1); // 0 = %eax

                // Passo B: Aplicar a operação (OPL varpc2, %eax)
                if (varpc2_type == '$') {
                    // Caso Imediato ($K)
                    if (op == '+') code[pc++] = 0x05; // addl $const, %eax
                    else if (op == '-') code[pc++] = 0x2d; // subl $const, %eax
                    else if (op == '*') {
                        code[pc++] = 0x69; code[pc++] = 0xc0; // imull $const, %eax, %eax
                    }
                    *(int*)(&code[pc]) = val2; pc += 4; 
                } else {
                    // Caso Memória (vX ou p0)
                    unsigned char op_code = 0;
                    if (op == '+') op_code = 0x03; // addl
                    else if (op == '-') op_code = 0x2b; // subl
                    else if (op == '*') op_code = 0x0f; // imull

                    code[pc++] = op_code; 
                    if (op == '*') code[pc++] = 0xaf; // imull precisa de 0xaf

                    if (varpc2_type == 'v') {
                        code[pc++] = 0x45; 
                        code[pc++] = VAR_OFFSET(val2);
                    } else if (varpc2_type == 'p') {
                        code[pc++] = 0x45; 
                        code[pc++] = P0_OFFSET;
                    }
                }

                // Passo C: Mover o resultado de %eax para v_dest
                mov_reg_to_var(code, 0, v_dest);
                
                // Sucesso: instrução processada, vai para a próxima linha
                continue;
            }
            // Se falhou no parse (ex: era um 'call'), NÃO fazemos continue, 
            // deixando cair para a verificação de 'call' abaixo.
        }

        // --- Chamada de Função (v_dest = call num varpc) ---
        if (sscanf(p, "v%d = call %d %s", &v_dest, &call_num, token1_str) == 3) {
            
            // 1. Determinar o operando (varpc) e movê-lo para %edi
            parse_varpc(token1_str, &varpc1_type, &val1);
            if (varpc1_type == 0) { continue; }
            
            mov_varpc_to_reg(code, 3, varpc1_type, val1); // 3 = %edi

            // 2. CALL: Instrução de 5 bytes: 0xE8 (CALL rel32)
            code[pc++] = 0xe8;
            
            // O offset será calculado aqui
            unsigned char *target_addr = function_start_addrs[call_num];
            if (target_addr != NULL) {
                // Cálculo do offset relativo
                int target_offset = (int)(target_addr - &code[pc + 4]);
                *(int*)(&code[pc]) = target_offset;
            } else {
                fprintf(stderr, "Erro: Chamada para funcao LBS %d indefinida ou fora de ordem.", call_num);
                *entry = NULL;
                return;
            }
            pc += 4;

            // 3. Mover o resultado de %eax para v_dest
            mov_reg_to_var(code, 0, v_dest); 
            continue;
        }


        // --- Retorno Incondicional (ret varpc) ---
        if (sscanf(p, "ret %s", token1_str) == 1) {
            parse_varpc(token1_str, &varpc1_type, &val1);
            if (varpc1_type == 0) { continue; }

            mov_varpc_to_reg(code, 0, varpc1_type, val1); // 0 = %eax
            emit_epilogue(code);
            continue;
        }

        // --- Retorno Condicional (zret varpc1 varpc2) ---
        if (sscanf(p, "zret %s %s", token1_str, token2_str) == 2) {
            
            parse_varpc(token1_str, &varpc1_type, &val1);
            parse_varpc(token2_str, &varpc2_type, &val2);
            if (varpc1_type == 0 || varpc2_type == 0) { continue; }

            // Mover varpc1 para %ecx para comparação
            mov_varpc_to_reg(code, 1, varpc1_type, val1); 

            // testl %ecx, %ecx
            code[pc++] = 0x85; code[pc++] = 0xc9;
            
            // JNZ (Jump if Not Zero) -> Pula o bloco de retorno
            code[pc++] = 0x0f; code[pc++] = 0x85; 
            
            int jump_patch_pos = pc; 
            pc += 4; 

            // Código de Retorno
            mov_varpc_to_reg(code, 0, varpc2_type, val2); 
            emit_epilogue(code);
            
            // Patching do Salto
            int relative_offset = pc - (jump_patch_pos + 4);
            *(int*)(&code[jump_patch_pos]) = relative_offset;
            
            continue;
        }

        fprintf(stderr, "Aviso: Instrucao LBS nao reconhecida: %s", buf);
    }
    
    if (function_count > 0) {
        *entry = (funcp)function_start_addrs[function_count - 1];
    } else {
        *entry = NULL;
    }
}


// --- IMPLEMENTAÇÃO DAS FUNÇÕES AUXILIARES ---

void parse_varpc(const char *token, char *type_out, int *val_out) {
    if (token == NULL || *token == '\0') {
        *type_out = 0;
        return;
    }
    if (strcmp(token, "p0") == 0) {
        *type_out = 'p';
        *val_out = 0; 
    } else if (token[0] == 'v' && sscanf(token, "v%d", val_out) == 1) {
        *type_out = 'v';
    } else if (token[0] == '$' && sscanf(token, "$%d", val_out) == 1) {
        *type_out = '$';
    } else {
        *type_out = 0; 
    }
}

void write_bytes(unsigned char codigo[], const unsigned char *bytes, int len) {
    for (int i = 0; i < len; i++) {
        codigo[pc++] = bytes[i];
    }
}

void emit_prologue(unsigned char codigo[]) {
    // pushq %rbp; movq %rsp, %rbp; subq $24, %rsp
    codigo[pc++] = 0x55;
    codigo[pc++] = 0x48; codigo[pc++] = 0x89; codigo[pc++] = 0xe5;
    codigo[pc++] = 0x48; codigo[pc++] = 0x83; codigo[pc++] = 0xec; codigo[pc++] = LOCAL_VARS_SIZE; 
    
    // movl %edi, -24(%rbp) (Salva p0)
    codigo[pc++] = 0x89; codigo[pc++] = 0x7d; codigo[pc++] = 0xE8; 
}

void emit_epilogue(unsigned char codigo[]) {
    // leave; ret
    codigo[pc++] = 0xc9; 
    codigo[pc++] = 0xc3; 
}

void mov_varpc_to_reg(unsigned char codigo[], int reg, char tipo, int index_or_const) {
    
    int mod_reg_map[] = {0x45, 0x4d, 0x55, 0x7d}; 
    int mov_imm_map[] = {0xb8, 0xb9, 0xba, 0xbf}; // Correção para %edi (0xbf)

    if (reg > 3) return; 
    
    int mod_reg = mod_reg_map[reg];
    int mov_imm_opcode = mov_imm_map[reg];

    if (tipo == 'v') {
        codigo[pc++] = 0x8b; 
        codigo[pc++] = mod_reg; 
        codigo[pc++] = VAR_OFFSET(index_or_const);
    } else if (tipo == '$') {
        codigo[pc++] = mov_imm_opcode; 
        *(int*)&codigo[pc] = index_or_const;
        pc += 4;
    } else if (tipo == 'p') {
        codigo[pc++] = 0x8b; 
        codigo[pc++] = mod_reg; 
        codigo[pc++] = P0_OFFSET;
    }
}

void mov_reg_to_var(unsigned char codigo[], int reg, int v_index) {
    int mod_reg_map[] = {0x45, 0x4d, 0x55}; 
    if (reg > 2) return; 
    int mod_reg = mod_reg_map[reg];
    codigo[pc++] = 0x89; 
    codigo[pc++] = mod_reg; 
    codigo[pc++] = VAR_OFFSET(v_index);
}
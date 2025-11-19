.globl simples
simples:
  pushq %rbp
  movq %rsp, %rbp
  subq $24, %rsp    # Alocação de pilha (5 vars * 4 bytes + alinhamento)
  movl $100, %eax
  leave             # movq %rbp, %rsp; popq %rbp
  ret
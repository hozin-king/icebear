#ifndef ICEBEAR_DECODER_H_
#define ICEBEAR_DECODER_H_

#include <stdint.h>

/* Phase-1 subset of A32, see native/README.md for exact coverage. */

typedef enum IcebearOp {
  ICEBEAR_OP_UNKNOWN = 0,
  ICEBEAR_OP_MOV,
  ICEBEAR_OP_MVN,
  ICEBEAR_OP_ADD,
  ICEBEAR_OP_ADC,
  ICEBEAR_OP_SUB,
  ICEBEAR_OP_SBC,
  ICEBEAR_OP_RSB,
  ICEBEAR_OP_RSC,
  ICEBEAR_OP_CMP,
  ICEBEAR_OP_CMN,
  ICEBEAR_OP_AND,
  ICEBEAR_OP_EOR,
  ICEBEAR_OP_ORR,
  ICEBEAR_OP_BIC,
  ICEBEAR_OP_TST,
  ICEBEAR_OP_TEQ,
  ICEBEAR_OP_MUL,
  ICEBEAR_OP_MLA,
  ICEBEAR_OP_SVC,
  ICEBEAR_OP_B,
  ICEBEAR_OP_BL,
  ICEBEAR_OP_BX,
  ICEBEAR_OP_LDR,
  ICEBEAR_OP_STR,
  ICEBEAR_OP_STM,
  ICEBEAR_OP_LDM,
} IcebearOp;

typedef struct IcebearInsn {
  IcebearOp op;
  uint32_t cond; /* checked against NZCV by the interpreter, not here */
  uint32_t rd;
  uint32_t rn;
  uint32_t rm;           /* register form of operand2, or MUL's Rm */
  uint32_t rs;           /* MUL only: the other source register */
  uint32_t imm;          /* immediate operand2, branch offset, or LDR/STR offset */
  uint32_t shift_type;   /* 0=LSL,1=LSR,2=ASR,3=ROR, register operand2 only */
  uint32_t shift_amount; /* 0-31, register operand2 only */
  int is_imm;            /* 1 if operand2 is the immediate form */
  int shift_by_reg;      /* 1 if shift amount comes from Rs (not immediate) */
  int sets_flags;        /* the S bit */
  int u;                 /* LDR/STR: 1 = add imm to base, 0 = subtract;
                            STM/LDM: 1 = increment, 0 = decrement */
  int b;                 /* LDR/STR: 1 = byte access, 0 = word */
  int p;                 /* STM/LDM: 1 = pre-indexed, 0 = post-indexed */
  int w;                 /* STM/LDM: 1 = write back final address to Rn */
  uint32_t reg_list;     /* STM/LDM: 16-bit register list bitmask */
} IcebearInsn;

/* 0 and fills *out on success, -1 for anything outside native/README.md's
 * documented subset. */
int icebear_decode(uint32_t word, IcebearInsn* out);

#endif /* ICEBEAR_DECODER_H_ */

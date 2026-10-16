#include "icebear/decoder.h"

#include "icebear/cpu.h"

int icebear_decode(uint32_t word, IcebearInsn* out) {
  out->cond = (word >> 28) & 0xF;
  out->rd = out->rn = out->rm = out->rs = out->imm = 0;
  out->shift_type = 0;
  out->shift_amount = 0;
  out->is_imm = 0;
  out->shift_by_reg = 0;
  out->sets_flags = 0;
  out->u = 0;
  out->b = 0;
  out->p = 0;
  out->w = 0;
  out->reg_list = 0;
  out->op = ICEBEAR_OP_UNKNOWN;

  if (out->cond == 0xF) {
    return -1; /* 0xF is ARMv5+'s unconditional-extension selector, not a real cond */
  }

  /* SVC/SWI: bits 27-24 = 1111, the rest is a legacy immediate EABI code
   * ignores; the actual syscall number is in r7 at execution time, not
   * decoded here, see icebear_interp_run's SVC case in interp.c. */
  if (((word >> 24) & 0xF) == 0xF) {
    out->op = ICEBEAR_OP_SVC;
    return 0;
  }

  /* BX Rm: cond 0001 0010 1111 1111 1111 0001 Rm */
  if (((word >> 20) & 0xFF) == 0x12 && ((word >> 4) & 0xFFFF) == 0xFFF1) {
    out->op = ICEBEAR_OP_BX;
    out->rm = word & 0xF;
    return 0;
  }

  /* B/BL imm24: bits 27-25 = 101, bit 24 = L (0=B, 1=BL) */
  if (((word >> 25) & 0x7) == 0x5) {
    uint32_t l = (word >> 24) & 0x1;
    uint32_t imm24 = word & 0xFFFFFF;
    uint32_t offset;
    if (imm24 & 0x800000) {
      offset = (imm24 | 0xFF000000u) << 2;
    } else {
      offset = imm24 << 2;
    }
    out->op = l ? ICEBEAR_OP_BL : ICEBEAR_OP_B;
    out->imm = offset; /* two's complement offset, added as unsigned */
    return 0;
  }

  /* MUL Rd,Rm,Rs: cond 000000 A S Rd 0000 Rs 1001 Rm. Same bits27-26 as
   * data-processing below, so this must be checked first or AND/EOR would
   * silently steal it (their opcodes are 0000/0001, exactly A/S here). */
  if (((word >> 22) & 0x3F) == 0x0 && ((word >> 4) & 0xF) == 0x9) {
    uint32_t a = (word >> 21) & 0x1;
    uint32_t s = (word >> 20) & 0x1;
    uint32_t rd = (word >> 16) & 0xF;
    uint32_t rn_field = (word >> 12) & 0xF;
    uint32_t rs = (word >> 8) & 0xF;
    uint32_t rm = word & 0xF;
    if (rn_field == ICEBEAR_REG_PC || rd == ICEBEAR_REG_PC ||
        rm == ICEBEAR_REG_PC || rs == ICEBEAR_REG_PC) {
      return -1; /* PC as an operand is UNPREDICTABLE */
    }
    if (!a && rn_field != 0) {
      return -1; /* MUL: Rn field must be SBZ */
    }
    out->op = a ? ICEBEAR_OP_MLA : ICEBEAR_OP_MUL;
    out->rd = rd;
    out->rm = rm;
    out->rs = rs;
    out->rn = rn_field; /* MLA accumulate register; ignored for MUL */
    out->sets_flags = (int)s;
    return 0;
  }

  /* Block data transfer (STM/LDM): bits 27-25 = 100.
   * P (bit 24) = pre/post, U (bit 23) = up/down, S (bit 22) must be 0
   * (S=1 with PC restores CPSR — privileged, not supported),
   * W (bit 21) = writeback, L (bit 20) = 1 LDM / 0 STM.
   * PUSH = STMDB SP!,{regs}; POP = LDMIA SP!,{regs}. */
  if (((word >> 25) & 0x7) == 0x4) {
    uint32_t p = (word >> 24) & 0x1;
    uint32_t u = (word >> 23) & 0x1;
    uint32_t s = (word >> 22) & 0x1;
    uint32_t w = (word >> 21) & 0x1;
    uint32_t l = (word >> 20) & 0x1;
    uint32_t rn = (word >> 16) & 0xF;
    uint32_t reg_list = word & 0xFFFF;

    if (s) {
      return -1; /* S=1 restores CPSR / user-mode banked regs: not supported */
    }
    if (reg_list == 0) {
      return -1; /* empty register list is UNPREDICTABLE */
    }
    if (rn == ICEBEAR_REG_PC) {
      return -1; /* PC as base is UNPREDICTABLE */
    }
    /* LDM with writeback where Rn is in the list and not the only reg:
     * UNPREDICTABLE on real hardware; reject to stay safe. */
    if (l && w && (reg_list & (1u << rn)) && (reg_list & ~(1u << rn))) {
      return -1;
    }

    out->op = l ? ICEBEAR_OP_LDM : ICEBEAR_OP_STM;
    out->rn = rn;
    out->reg_list = reg_list;
    out->p = (int)p;
    out->u = (int)u;
    out->w = (int)w;
    return 0;
  }

  /* Data-processing: bits 27-26 == 00 */
  if (((word >> 26) & 0x3) == 0x0) {
    uint32_t i = (word >> 25) & 0x1;
    uint32_t opcode = (word >> 21) & 0xF;
    uint32_t s = (word >> 20) & 0x1;
    uint32_t rn = (word >> 16) & 0xF;
    uint32_t rd = (word >> 12) & 0xF;
    uint32_t operand2 = word & 0xFFF;
    IcebearOp op;

    switch (opcode) {
      case 0x0:
        op = ICEBEAR_OP_AND;
        break;
      case 0x1:
        op = ICEBEAR_OP_EOR;
        break;
      case 0x2:
        op = ICEBEAR_OP_SUB;
        break;
      case 0x3:
        op = ICEBEAR_OP_RSB;
        break;
      case 0x4:
        op = ICEBEAR_OP_ADD;
        break;
      case 0x5:
        op = ICEBEAR_OP_ADC;
        break;
      case 0x6:
        op = ICEBEAR_OP_SBC;
        break;
      case 0x7:
        op = ICEBEAR_OP_RSC;
        break;
      case 0x8:
      case 0x9:
      case 0xA:
      case 0xB:
        /* TST/TEQ/CMP/CMN: S=0 here isn't one of these at all (it overlaps
         * MRS/MSR's encoding instead), so decode nothing rather than
         * guess. */
        if (!s) {
          return -1;
        }
        op = opcode == 0x8   ? ICEBEAR_OP_TST
             : opcode == 0x9 ? ICEBEAR_OP_TEQ
             : opcode == 0xA ? ICEBEAR_OP_CMP
                             : ICEBEAR_OP_CMN;
        break;
      case 0xC:
        op = ICEBEAR_OP_ORR;
        break;
      case 0xD:
        op = ICEBEAR_OP_MOV;
        break;
      case 0xE:
        op = ICEBEAR_OP_BIC;
        break;
      case 0xF:
        op = ICEBEAR_OP_MVN;
        break;
      default:
        return -1;
    }

    out->op = op;
    out->rn = rn;
    out->rd = rd;
    out->sets_flags = (int)s;

    if (i) {
      uint32_t imm8 = operand2 & 0xFF;
      uint32_t rot = ((operand2 >> 8) & 0xF) * 2;
      uint32_t val = imm8;
      if (rot != 0) {
        val = (imm8 >> rot) | (imm8 << (32 - rot));
      }
      out->is_imm = 1;
      out->imm = val;
    } else {
      uint32_t shift_by_reg = (operand2 >> 4) & 0x1;
      uint32_t shift_type = (operand2 >> 5) & 0x3;
      out->rm = operand2 & 0xF;
      out->shift_type = shift_type;
      if (shift_by_reg) {
        /* Register-specified shift: Rs in bits 11-8, bit 7 must be 0. */
        if ((operand2 >> 7) & 0x1) {
          return -1;
        }
        uint32_t rs = (operand2 >> 8) & 0xF;
        if (rs == ICEBEAR_REG_PC) {
          return -1; /* UNPREDICTABLE */
        }
        out->shift_by_reg = 1;
        out->rs = rs;
        out->shift_amount = 0; /* unused, amount read from Rs at runtime */
      } else {
        uint32_t shift_amount = (operand2 >> 7) & 0x1F;
        if (shift_type != 0 && shift_amount == 0) {
          return -1; /* #0 means #32 for LSR/ASR, RRX for ROR, see decoder.h */
        }
        out->shift_amount = shift_amount;
      }
    }
    return 0;
  }

  /* LDR/STR immediate offset, no writeback: bits 27-26=01, I=0, P=1, W=0 */
  if (((word >> 26) & 0x3) == 0x1 && ((word >> 25) & 0x1) == 0 && ((word >> 24) & 0x1) == 1 &&
      ((word >> 21) & 0x1) == 0) {
    uint32_t u = (word >> 23) & 0x1;
    uint32_t b = (word >> 22) & 0x1;
    uint32_t l = (word >> 20) & 0x1;
    uint32_t rn = (word >> 16) & 0xF;
    uint32_t rt = (word >> 12) & 0xF;
    uint32_t imm12 = word & 0xFFF;

    out->op = l ? ICEBEAR_OP_LDR : ICEBEAR_OP_STR;
    out->rn = rn;
    out->rd = rt;
    out->imm = imm12;
    out->u = (int)u;
    out->b = (int)b;
    return 0;
  }

  return -1;
}

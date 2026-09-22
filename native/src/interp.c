#include "icebear/interp.h"

#include "icebear/decoder.h"

/* Little-endian (real ARM32 Android/Linux), explicit shifts to stay strict-aliasing-safe. */
static uint32_t icebear_load_u32_le(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void icebear_store_u32_le(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int icebear_check_word_access(const IcebearMemory* mem, uint32_t addr) {
  if ((addr % 4u) != 0) {
    return -1;
  }
  if ((uint64_t)addr + 4u > mem->size) { /* uint64_t so addr near UINT32_MAX can't wrap */
    return -1;
  }
  return 0;
}

static int icebear_check_byte_access(const IcebearMemory* mem, uint32_t addr) {
  return addr < mem->size ? 0 : -1;
}

/* Real hardware: reading PC as an operand gives addr+8, not cpu->r[15]. */
static uint32_t icebear_read_reg(const IcebearCpu* cpu, uint32_t insn_addr, uint32_t reg) {
  if (reg == ICEBEAR_REG_PC) {
    return insn_addr + 8u;
  }
  return cpu->r[reg];
}

/* shift_amount is always in a safe 0-31 (LSL) or 1-31 (LSR/ASR/ROR) range, decode
 * guarantees it. ASR is hand-rolled since signed right-shift is implementation-defined. */
static uint32_t icebear_apply_shift(uint32_t value, uint32_t shift_type, uint32_t shift_amount) {
  switch (shift_type) {
    case 0: /* LSL */
      return shift_amount == 0 ? value : value << shift_amount;
    case 1: /* LSR */
      return value >> shift_amount;
    case 2: { /* ASR */
      uint32_t sign_fill = (value & 0x80000000u) ? (~0u << (32 - shift_amount)) : 0u;
      return (value >> shift_amount) | sign_fill;
    }
    case 3: /* ROR */
      return (value >> shift_amount) | (value << (32 - shift_amount));
    default:
      return value;
  }
}

static uint32_t icebear_read_operand2(const IcebearCpu* cpu, uint32_t addr, const IcebearInsn* insn) {
  if (insn->is_imm) {
    return insn->imm;
  }
  uint32_t value = icebear_read_reg(cpu, addr, insn->rm);
  return icebear_apply_shift(value, insn->shift_type, insn->shift_amount);
}

/* NZCV for ADD/ADDS: result = lhs + rhs. */
static uint32_t icebear_flags_for_add(uint32_t lhs, uint32_t rhs, uint32_t result) {
  uint32_t flags = 0;
  flags |= (result == 0) ? ICEBEAR_CPSR_Z : 0;
  flags |= (result & 0x80000000u) ? ICEBEAR_CPSR_N : 0;
  flags |= (result < lhs) ? ICEBEAR_CPSR_C : 0;
  flags |= ((~(lhs ^ rhs) & (lhs ^ result)) & 0x80000000u) ? ICEBEAR_CPSR_V : 0;
  return flags;
}

/* NZCV for SUB/SUBS/CMP: result = lhs - rhs. */
static uint32_t icebear_flags_for_sub(uint32_t lhs, uint32_t rhs, uint32_t result) {
  uint32_t flags = 0;
  flags |= (result == 0) ? ICEBEAR_CPSR_Z : 0;
  flags |= (result & 0x80000000u) ? ICEBEAR_CPSR_N : 0;
  flags |= (lhs >= rhs) ? ICEBEAR_CPSR_C : 0;
  flags |= ((lhs ^ rhs) & (lhs ^ result) & 0x80000000u) ? ICEBEAR_CPSR_V : 0;
  return flags;
}

/* NZCV for ADC (result = lhs + rhs + carry_in); SBC/RSC reuse this too,
 * feeding it ~rhs (see their cases below), the same trick real ALU
 * hardware uses since A-B-1+C == A+~B+C in two's complement. */
static uint32_t icebear_flags_for_adc(uint32_t lhs, uint32_t rhs, uint32_t carry_in, uint32_t result) {
  uint32_t flags = 0;
  uint64_t wide = (uint64_t)lhs + rhs + carry_in;
  flags |= (result == 0) ? ICEBEAR_CPSR_Z : 0;
  flags |= (result & 0x80000000u) ? ICEBEAR_CPSR_N : 0;
  flags |= (wide > 0xFFFFFFFFu) ? ICEBEAR_CPSR_C : 0;
  flags |= ((~(lhs ^ rhs) & (lhs ^ result)) & 0x80000000u) ? ICEBEAR_CPSR_V : 0;
  return flags;
}

/* NZ for AND/EOR/ORR/BIC/MVN/TST/TEQ: C should come from the shifter, not
 * implemented (see MOVS below), and V is unaffected by these; both kept
 * as-is rather than cleared. */
static uint32_t icebear_flags_for_logical(const IcebearCpu* cpu, uint32_t result) {
  uint32_t n = (result & 0x80000000u) ? ICEBEAR_CPSR_N : 0;
  uint32_t z = (result == 0) ? ICEBEAR_CPSR_Z : 0;
  return (cpu->cpsr & (ICEBEAR_CPSR_C | ICEBEAR_CPSR_V)) | n | z;
}

static void icebear_set_nzcv(IcebearCpu* cpu, uint32_t flags) {
  cpu->cpsr &= ~(ICEBEAR_CPSR_N | ICEBEAR_CPSR_Z | ICEBEAR_CPSR_C | ICEBEAR_CPSR_V);
  cpu->cpsr |= flags;
}

static int icebear_cond_holds(uint32_t cond, uint32_t cpsr) {
  int n = (cpsr & ICEBEAR_CPSR_N) != 0;
  int z = (cpsr & ICEBEAR_CPSR_Z) != 0;
  int c = (cpsr & ICEBEAR_CPSR_C) != 0;
  int v = (cpsr & ICEBEAR_CPSR_V) != 0;

  switch (cond) {
    case 0x0:
      return z; /* EQ */
    case 0x1:
      return !z; /* NE */
    case 0x2:
      return c; /* CS/HS */
    case 0x3:
      return !c; /* CC/LO */
    case 0x4:
      return n; /* MI */
    case 0x5:
      return !n; /* PL */
    case 0x6:
      return v; /* VS */
    case 0x7:
      return !v; /* VC */
    case 0x8:
      return c && !z; /* HI */
    case 0x9:
      return !c || z; /* LS */
    case 0xA:
      return n == v; /* GE */
    case 0xB:
      return n != v; /* LT */
    case 0xC:
      return !z && (n == v); /* GT */
    case 0xD:
      return z || (n != v); /* LE */
    case 0xE:
      return 1; /* AL */
    default:
      return 0;
  }
}

int icebear_interp_run(IcebearCpu* cpu, IcebearMemory* mem, uint32_t stop_addr, uint32_t max_steps) {
  uint32_t addr = cpu->r[ICEBEAR_REG_PC];

  for (uint32_t step = 0; step < max_steps; step++) {
    if (addr == stop_addr) {
      return 0;
    }
    if (icebear_check_word_access(mem, addr) != 0) {
      return -1;
    }

    IcebearInsn insn;
    if (icebear_decode(icebear_load_u32_le(mem->bytes + addr), &insn) != 0) {
      return -1;
    }

    uint32_t next_addr = addr + 4u;

    /* condition false = no-op, covers B/BX too, no per-case handling needed */
    if (icebear_cond_holds(insn.cond, cpu->cpsr)) {
      switch (insn.op) {
        case ICEBEAR_OP_MOV: {
          uint32_t result = icebear_read_operand2(cpu, addr, &insn);
          cpu->r[insn.rd] = result;
          if (insn.sets_flags) {
            icebear_set_nzcv(cpu, icebear_flags_for_logical(cpu, result));
          }
          break;
        }

        case ICEBEAR_OP_MVN: {
          uint32_t result = ~icebear_read_operand2(cpu, addr, &insn);
          cpu->r[insn.rd] = result;
          if (insn.sets_flags) {
            icebear_set_nzcv(cpu, icebear_flags_for_logical(cpu, result));
          }
          break;
        }

        case ICEBEAR_OP_AND:
        case ICEBEAR_OP_EOR:
        case ICEBEAR_OP_ORR:
        case ICEBEAR_OP_BIC: {
          uint32_t rhs = icebear_read_operand2(cpu, addr, &insn);
          uint32_t lhs = icebear_read_reg(cpu, addr, insn.rn);
          uint32_t result = insn.op == ICEBEAR_OP_AND   ? (lhs & rhs)
                             : insn.op == ICEBEAR_OP_EOR ? (lhs ^ rhs)
                             : insn.op == ICEBEAR_OP_ORR ? (lhs | rhs)
                                                        : (lhs & ~rhs);
          cpu->r[insn.rd] = result;
          if (insn.sets_flags) {
            icebear_set_nzcv(cpu, icebear_flags_for_logical(cpu, result));
          }
          break;
        }

        case ICEBEAR_OP_TST:
        case ICEBEAR_OP_TEQ: {
          uint32_t rhs = icebear_read_operand2(cpu, addr, &insn);
          uint32_t lhs = icebear_read_reg(cpu, addr, insn.rn);
          uint32_t result = insn.op == ICEBEAR_OP_TST ? (lhs & rhs) : (lhs ^ rhs);
          icebear_set_nzcv(cpu, icebear_flags_for_logical(cpu, result));
          break;
        }

        case ICEBEAR_OP_ADD: {
          uint32_t rhs = icebear_read_operand2(cpu, addr, &insn);
          uint32_t lhs = icebear_read_reg(cpu, addr, insn.rn);
          uint32_t result = lhs + rhs;
          cpu->r[insn.rd] = result;
          if (insn.sets_flags) {
            icebear_set_nzcv(cpu, icebear_flags_for_add(lhs, rhs, result));
          }
          break;
        }

        case ICEBEAR_OP_CMN: {
          uint32_t rhs = icebear_read_operand2(cpu, addr, &insn);
          uint32_t lhs = icebear_read_reg(cpu, addr, insn.rn);
          icebear_set_nzcv(cpu, icebear_flags_for_add(lhs, rhs, lhs + rhs));
          break;
        }

        case ICEBEAR_OP_ADC: {
          uint32_t rhs = icebear_read_operand2(cpu, addr, &insn);
          uint32_t lhs = icebear_read_reg(cpu, addr, insn.rn);
          uint32_t carry_in = (cpu->cpsr & ICEBEAR_CPSR_C) ? 1u : 0u;
          uint32_t result = lhs + rhs + carry_in;
          cpu->r[insn.rd] = result;
          if (insn.sets_flags) {
            icebear_set_nzcv(cpu, icebear_flags_for_adc(lhs, rhs, carry_in, result));
          }
          break;
        }

        case ICEBEAR_OP_SUB: {
          uint32_t rhs = icebear_read_operand2(cpu, addr, &insn);
          uint32_t lhs = icebear_read_reg(cpu, addr, insn.rn);
          uint32_t result = lhs - rhs;
          cpu->r[insn.rd] = result;
          if (insn.sets_flags) {
            icebear_set_nzcv(cpu, icebear_flags_for_sub(lhs, rhs, result));
          }
          break;
        }

        case ICEBEAR_OP_RSB: {
          uint32_t rhs = icebear_read_operand2(cpu, addr, &insn);
          uint32_t lhs = icebear_read_reg(cpu, addr, insn.rn);
          uint32_t result = rhs - lhs;
          cpu->r[insn.rd] = result;
          if (insn.sets_flags) {
            icebear_set_nzcv(cpu, icebear_flags_for_sub(rhs, lhs, result));
          }
          break;
        }

        case ICEBEAR_OP_SBC: {
          uint32_t rhs = ~icebear_read_operand2(cpu, addr, &insn); /* A-B-1+C == A+~B+C */
          uint32_t lhs = icebear_read_reg(cpu, addr, insn.rn);
          uint32_t carry_in = (cpu->cpsr & ICEBEAR_CPSR_C) ? 1u : 0u;
          uint32_t result = lhs + rhs + carry_in;
          cpu->r[insn.rd] = result;
          if (insn.sets_flags) {
            icebear_set_nzcv(cpu, icebear_flags_for_adc(lhs, rhs, carry_in, result));
          }
          break;
        }

        case ICEBEAR_OP_RSC: {
          uint32_t op2 = icebear_read_operand2(cpu, addr, &insn);
          uint32_t not_rn = ~icebear_read_reg(cpu, addr, insn.rn); /* B-A-1+C == B+~A+C */
          uint32_t carry_in = (cpu->cpsr & ICEBEAR_CPSR_C) ? 1u : 0u;
          uint32_t result = op2 + not_rn + carry_in;
          cpu->r[insn.rd] = result;
          if (insn.sets_flags) {
            icebear_set_nzcv(cpu, icebear_flags_for_adc(op2, not_rn, carry_in, result));
          }
          break;
        }

        case ICEBEAR_OP_CMP: {
          uint32_t rhs = icebear_read_operand2(cpu, addr, &insn);
          uint32_t lhs = icebear_read_reg(cpu, addr, insn.rn);
          icebear_set_nzcv(cpu, icebear_flags_for_sub(lhs, rhs, lhs - rhs));
          break;
        }

        case ICEBEAR_OP_MUL: {
          uint32_t result = icebear_read_reg(cpu, addr, insn.rm) * icebear_read_reg(cpu, addr, insn.rs);
          cpu->r[insn.rd] = result;
          if (insn.sets_flags) {
            /* MULS: C,V left as-is, same convention as the other logical-flag ops. */
            icebear_set_nzcv(cpu, icebear_flags_for_logical(cpu, result));
          }
          break;
        }

        case ICEBEAR_OP_SVC:
          return 1; /* cpu->r[PC] == addr still, caller thunks r7/r0-r6 and resumes, see interp.h */

        case ICEBEAR_OP_B:
          next_addr = addr + 8u + insn.imm; /* PC reads as addr+8 */
          break;

        case ICEBEAR_OP_BL:
          cpu->r[ICEBEAR_REG_LR] = addr + 4u;
          next_addr = addr + 8u + insn.imm;
          break;

        case ICEBEAR_OP_BX:
          next_addr = cpu->r[insn.rm];
          break;

        case ICEBEAR_OP_LDR:
        case ICEBEAR_OP_STR: {
          if (insn.rd == ICEBEAR_REG_PC) {
            return -1; /* indirect branch via LDR PC, not supported yet */
          }
          uint32_t base = icebear_read_reg(cpu, addr, insn.rn);
          uint32_t eaddr = insn.u ? base + insn.imm : base - insn.imm;

          if (insn.b) {
            if (icebear_check_byte_access(mem, eaddr) != 0) {
              return -1;
            }
            if (insn.op == ICEBEAR_OP_LDR) {
              cpu->r[insn.rd] = mem->bytes[eaddr]; /* zero-extended */
            } else {
              mem->bytes[eaddr] = (uint8_t)(cpu->r[insn.rd] & 0xFFu);
            }
          } else {
            if (icebear_check_word_access(mem, eaddr) != 0) {
              return -1;
            }
            if (insn.op == ICEBEAR_OP_LDR) {
              cpu->r[insn.rd] = icebear_load_u32_le(mem->bytes + eaddr);
            } else {
              icebear_store_u32_le(mem->bytes + eaddr, cpu->r[insn.rd]);
            }
          }
          break;
        }

        default:
          return -1;
      }
    }

    addr = next_addr;
    cpu->r[ICEBEAR_REG_PC] = addr;
  }

  return -1; /* step limit hit */
}

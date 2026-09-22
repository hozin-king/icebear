#ifndef ICEBEAR_CPU_H_
#define ICEBEAR_CPU_H_

#include <stdint.h>

/* AArch32 register file, r13-r15 addressable as plain GPRs too. */
typedef struct IcebearCpu {
  uint32_t r[16];
  uint32_t cpsr; /* NZCV in bits 31-28 */
} IcebearCpu;

#define ICEBEAR_REG_SP 13
#define ICEBEAR_REG_LR 14
#define ICEBEAR_REG_PC 15

#define ICEBEAR_CPSR_N (1u << 31)
#define ICEBEAR_CPSR_Z (1u << 30)
#define ICEBEAR_CPSR_C (1u << 29)
#define ICEBEAR_CPSR_V (1u << 28)

#endif /* ICEBEAR_CPU_H_ */

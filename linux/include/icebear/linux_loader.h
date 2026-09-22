#ifndef ICEBEAR_LINUX_LOADER_H_
#define ICEBEAR_LINUX_LOADER_H_

#include <stdint.h>

typedef enum IcebearLinuxLoadStatus {
  ICEBEAR_LINUX_LOAD_OK = 0,
  ICEBEAR_LINUX_LOAD_BAD_ELF,
  ICEBEAR_LINUX_LOAD_TOO_LARGE,
  ICEBEAR_LINUX_LOAD_INTERP_FAILED,       /* hit something undecodable; not a crash */
  ICEBEAR_LINUX_LOAD_UNSUPPORTED_SYSCALL, /* a real SVC, but not one of the handful thunked */
} IcebearLinuxLoadStatus;

typedef struct IcebearLinuxLoadResult {
  IcebearLinuxLoadStatus status;
  uint32_t r0;              /* on OK: exit() code, or LR-return's r0 (see loader_core.c) */
  uint32_t unsupported_nr;  /* on UNSUPPORTED_SYSCALL: the syscall number (r7) that stopped it */
} IcebearLinuxLoadResult;

/* Loads `data`/`size` as an ELF32 ARM executable into a fixed-size (see
 * loader_core.c), zeroed guest memory image and runs it from its entry
 * point via `icebear_core`, thunking `exit`/`write` (EABI numbers 1/4, see
 * linux/README.md) to the real host syscalls and stopping cleanly, not
 * crashing, on anything else. Static, non-PIE binaries only. */
IcebearLinuxLoadResult icebear_linux_load_and_run(const uint8_t* data, uint32_t size);

#endif /* ICEBEAR_LINUX_LOADER_H_ */

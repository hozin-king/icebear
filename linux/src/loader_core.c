#include "icebear/linux_loader.h"

#include <string.h>
#include <unistd.h>

#include "icebear/cpu.h"
#include "icebear/elf32.h"
#include "icebear/interp.h"

#define ICEBEAR_LINUX_EM_ARM 40u
#define ICEBEAR_LINUX_GUEST_MEM_SIZE (1u * 1024 * 1024) /* fixed size for now, see linux/README.md */
#define ICEBEAR_LINUX_STOP_ADDR 0xFFFFFFFFu
#define ICEBEAR_LINUX_MAX_STEPS 10000u

/* ARM EABI syscall numbers, verified against arch/arm/include/uapi/asm/unistd.h,
 * not guessed: __NR_exit=1, __NR_write=4. */
#define ICEBEAR_LINUX_SYS_EXIT 1u
#define ICEBEAR_LINUX_SYS_WRITE 4u

/* static, not stack-allocated: 1 MiB is too much to put on a thread's
 * stack, and this only ever runs one guest at a time anyway. */
static uint8_t icebear_linux_guest_mem[ICEBEAR_LINUX_GUEST_MEM_SIZE];

/* The resume loop: icebear_interp_run stops (return 1) at every SVC, hands
 * back r7/r0-r6, and expects PC advanced past it before being called
 * again, see interp.h. This is that expectation, plus the two syscalls
 * linux/README.md promises: real exit and write, everything else stops
 * cleanly instead of guessing what the guest wanted. */
static IcebearLinuxLoadResult icebear_linux_run_loop(IcebearCpu* cpu, IcebearMemory* mem) {
  IcebearLinuxLoadResult result = {ICEBEAR_LINUX_LOAD_OK, 0, 0};
  for (;;) {
    int rc = icebear_interp_run(cpu, mem, ICEBEAR_LINUX_STOP_ADDR, ICEBEAR_LINUX_MAX_STEPS);
    if (rc == 0) {
      result.r0 = cpu->r[0]; /* returned via BX LR to our sentinel, not exit() */
      return result;
    }
    if (rc != 1) {
      result.status = ICEBEAR_LINUX_LOAD_INTERP_FAILED;
      return result;
    }

    uint32_t nr = cpu->r[7];
    if (nr == ICEBEAR_LINUX_SYS_EXIT) {
      result.r0 = cpu->r[0];
      return result;
    }
    if (nr == ICEBEAR_LINUX_SYS_WRITE) {
      uint32_t fd = cpu->r[0];
      uint32_t buf = cpu->r[1];
      uint32_t count = cpu->r[2];
      if ((uint64_t)buf + count > mem->size) {
        cpu->r[0] = (uint32_t)-1; /* not a real errno, just "failed", see linux/README.md */
      } else {
        cpu->r[0] = (uint32_t)write((int)fd, mem->bytes + buf, count);
      }
      cpu->r[ICEBEAR_REG_PC] += 4;
      continue;
    }

    result.status = ICEBEAR_LINUX_LOAD_UNSUPPORTED_SYSCALL;
    result.unsupported_nr = nr;
    return result;
  }
}

IcebearLinuxLoadResult icebear_linux_load_and_run(const uint8_t* data, uint32_t size) {
  IcebearLinuxLoadResult result = {ICEBEAR_LINUX_LOAD_BAD_ELF, 0, 0};
  IcebearElf32Image image;
  if (icebear_elf32_parse(data, size, ICEBEAR_LINUX_EM_ARM, &image) != 0) {
    return result;
  }

  memset(icebear_linux_guest_mem, 0, sizeof(icebear_linux_guest_mem));
  for (uint32_t i = 0; i < image.segment_count; i++) {
    const IcebearElf32Segment* seg = &image.segments[i];
    if ((uint64_t)seg->vaddr + seg->memsz > sizeof(icebear_linux_guest_mem)) {
      result.status = ICEBEAR_LINUX_LOAD_TOO_LARGE;
      return result;
    }
    memcpy(icebear_linux_guest_mem + seg->vaddr, data + seg->file_offset, seg->filesz);
  }

  IcebearMemory mem = {icebear_linux_guest_mem, sizeof(icebear_linux_guest_mem)};
  IcebearCpu cpu;
  memset(&cpu, 0, sizeof(cpu));
  cpu.r[ICEBEAR_REG_SP] = sizeof(icebear_linux_guest_mem) - 64; /* headroom at the very top */
  cpu.r[ICEBEAR_REG_LR] = ICEBEAR_LINUX_STOP_ADDR;
  cpu.r[ICEBEAR_REG_PC] = image.entry;

  return icebear_linux_run_loop(&cpu, &mem);
}

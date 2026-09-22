#include <stdio.h>
#include <string.h>

#include "icebear/elf32.h"

#define EM_ARM_FOR_TESTS 40u
#define EM_386_FOR_TESTS 3u

/*
 * A hand-built, minimal ELF32 shared object: one PT_LOAD segment plus a
 * .dynsym/.dynstr pair with two exported functions (icebear_add at 0x40,
 * icebear_answer at 0x50). Independently verified with `readelf` during
 * development (see native/README.md) before trusting it as a test fixture,
 * the same reason test_interp.c cross-checks its own hand-encoded words.
 */
static const uint8_t kSynthElf[] = {
    0x7Fu, 0x45u, 0x4Cu, 0x46u, 0x01u, 0x01u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x03u, 0x00u, 0x28u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x34u, 0x00u, 0x00u, 0x00u, 0xA0u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x34u, 0x00u, 0x20u, 0x00u, 0x01u, 0x00u, 0x28u, 0x00u,
    0x03u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x18u, 0x01u, 0x00u, 0x00u,
    0x18u, 0x01u, 0x00u, 0x00u, 0x05u, 0x00u, 0x00u, 0x00u, 0x00u, 0x10u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x40u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x12u, 0x00u, 0x01u, 0x00u, 0x0Du, 0x00u, 0x00u, 0x00u,
    0x50u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x12u, 0x00u, 0x01u, 0x00u,
    0x00u, 0x69u, 0x63u, 0x65u, 0x62u, 0x65u, 0x61u, 0x72u, 0x5Fu, 0x61u, 0x64u, 0x64u,
    0x00u, 0x69u, 0x63u, 0x65u, 0x62u, 0x65u, 0x61u, 0x72u, 0x5Fu, 0x61u, 0x6Eu, 0x73u,
    0x77u, 0x65u, 0x72u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x04u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x0Bu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x54u, 0x00u, 0x00u, 0x00u,
    0x54u, 0x00u, 0x00u, 0x00u, 0x30u, 0x00u, 0x00u, 0x00u, 0x02u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x04u, 0x00u, 0x00u, 0x00u, 0x10u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x03u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x84u, 0x00u, 0x00u, 0x00u, 0x84u, 0x00u, 0x00u, 0x00u, 0x1Cu, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x04u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u,
};

static int test_parses_a_valid_image(void) {
  IcebearElf32Image image;
  int rc = icebear_elf32_parse(kSynthElf, sizeof(kSynthElf), EM_ARM_FOR_TESTS, &image);
  if (rc != 0) {
    fprintf(stderr, "FAIL(parses_a_valid_image): icebear_elf32_parse returned %d\n", rc);
    return 1;
  }
  if (image.segment_count != 1) {
    fprintf(stderr, "FAIL(parses_a_valid_image): expected 1 PT_LOAD segment, got %u\n",
            image.segment_count);
    return 1;
  }
  if (image.segments[0].vaddr != 0 || image.segments[0].filesz != 0x118 || !image.segments[0].executable) {
    fprintf(stderr, "FAIL(parses_a_valid_image): segment fields wrong (vaddr=%u filesz=%u exec=%d)\n",
            image.segments[0].vaddr, image.segments[0].filesz, image.segments[0].executable);
    return 1;
  }
  printf("ok: parses a valid ELF32 image (1 PT_LOAD segment)\n");
  return 0;
}

static int test_finds_symbols_by_name(void) {
  IcebearElf32Image image;
  if (icebear_elf32_parse(kSynthElf, sizeof(kSynthElf), EM_ARM_FOR_TESTS, &image) != 0) {
    fprintf(stderr, "FAIL(finds_symbols_by_name): parse failed\n");
    return 1;
  }
  uint32_t add_addr = icebear_elf32_find_symbol(&image, "icebear_add");
  uint32_t answer_addr = icebear_elf32_find_symbol(&image, "icebear_answer");
  uint32_t missing = icebear_elf32_find_symbol(&image, "does_not_exist");
  if (add_addr != 0x40 || answer_addr != 0x50 || missing != 0) {
    fprintf(stderr, "FAIL(finds_symbols_by_name): add=0x%x answer=0x%x missing=0x%x\n", add_addr,
            answer_addr, missing);
    return 1;
  }
  printf("ok: finds icebear_add (0x%x) and icebear_answer (0x%x) by name, missing symbol is 0\n", add_addr,
         answer_addr);
  return 0;
}

static int test_rejects_wrong_machine(void) {
  IcebearElf32Image image;
  if (icebear_elf32_parse(kSynthElf, sizeof(kSynthElf), EM_386_FOR_TESTS, &image) == 0) {
    fprintf(stderr, "FAIL(rejects_wrong_machine): an ARM image was accepted as EM_386\n");
    return 1;
  }
  printf("ok: an ARM image is rejected when a different machine is expected\n");
  return 0;
}

static int test_rejects_truncated_buffer(void) {
  IcebearElf32Image image;
  for (uint32_t size = 0; size < 52; size++) {
    if (icebear_elf32_parse(kSynthElf, size, EM_ARM_FOR_TESTS, &image) == 0) {
      fprintf(stderr, "FAIL(rejects_truncated_buffer): a %u-byte buffer was accepted\n", size);
      return 1;
    }
  }
  printf("ok: buffers shorter than an ELF32 header are all rejected\n");
  return 0;
}

static int test_rejects_bad_magic(void) {
  uint8_t corrupted[sizeof(kSynthElf)];
  memcpy(corrupted, kSynthElf, sizeof(kSynthElf));
  corrupted[0] = 0x00; /* was 0x7F, the start of the ELF magic */
  IcebearElf32Image image;
  if (icebear_elf32_parse(corrupted, sizeof(corrupted), EM_ARM_FOR_TESTS, &image) == 0) {
    fprintf(stderr, "FAIL(rejects_bad_magic): a corrupted magic number was accepted\n");
    return 1;
  }
  printf("ok: a corrupted ELF magic number is rejected\n");
  return 0;
}

int main(void) {
  int failures = 0;
  failures += test_parses_a_valid_image();
  failures += test_finds_symbols_by_name();
  failures += test_rejects_wrong_machine();
  failures += test_rejects_truncated_buffer();
  failures += test_rejects_bad_magic();

  if (failures != 0) {
    fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
  }
  printf("all elf32 tests passed\n");
  return 0;
}

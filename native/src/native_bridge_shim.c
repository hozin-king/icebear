/* Must come before any system header, see icebear/native_bridge.h for why. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

/* Implements native/include/icebear/native_bridge.h, exported as "NativeBridgeItf". */
#include <elf.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "icebear/elf32.h"
#include "icebear/native_bridge.h"

static bool icebear_is_arm32_elf(const char* libpath) {
  int fd = open(libpath, O_RDONLY);
  if (fd < 0) {
    return false;
  }

  Elf32_Ehdr hdr;
  ssize_t n = read(fd, &hdr, sizeof(hdr));
  close(fd);

  if (n != (ssize_t)sizeof(hdr)) {
    return false;
  }
  if (memcmp(hdr.e_ident, ELFMAG, SELFMAG) != 0) {
    return false;
  }
  if (hdr.e_ident[EI_CLASS] != ELFCLASS32) {
    return false;
  }
  return hdr.e_machine == EM_ARM;
}

/* What loadLibrary actually hands getTrampoline: the whole file (elf32.c
 * reads straight out of it, doesn't copy) plus the guest memory its
 * PT_LOAD segments were copied into. Never freed, see unloadLibrary's
 * comment below for why that's a real, known, deliberate gap. */
typedef struct IcebearLoadedLibrary {
  uint8_t* file_data;
  uint8_t* guest_mem;
  IcebearElf32Image image;
} IcebearLoadedLibrary;

static bool icebear_initialize(const struct NativeBridgeRuntimeCallbacks* runtime_cbs,
                             const char* private_dir, const char* instruction_set) {
  (void)runtime_cbs;
  (void)private_dir;
  (void)instruction_set;
  return true; /* TODO: stash runtime_cbs for getTrampoline's JNI lookups */
}

static void* icebear_load_library(const char* libpath, int flag) {
  (void)flag;
  if (!icebear_is_arm32_elf(libpath)) {
    return NULL;
  }

  int fd = open(libpath, O_RDONLY);
  if (fd < 0) {
    return NULL;
  }
  off_t end = lseek(fd, 0, SEEK_END);
  if (end <= 0 || (uint64_t)end > UINT32_MAX || lseek(fd, 0, SEEK_SET) != 0) {
    close(fd);
    return NULL;
  }
  uint32_t file_size = (uint32_t)end;

  uint8_t* file_data = malloc(file_size);
  if (!file_data) {
    close(fd);
    return NULL;
  }
  ssize_t n = read(fd, file_data, file_size);
  close(fd);
  if (n != (ssize_t)file_size) {
    free(file_data);
    return NULL;
  }

  IcebearElf32Image image;
  if (icebear_elf32_parse(file_data, file_size, EM_ARM, &image) != 0) {
    free(file_data); /* is_arm32_elf only checked the header; the rest of this one is malformed */
    return NULL;
  }

  uint64_t guest_mem_size = 0;
  for (uint32_t i = 0; i < image.segment_count; i++) {
    uint64_t seg_end = (uint64_t)image.segments[i].vaddr + image.segments[i].memsz;
    if (seg_end > guest_mem_size) {
      guest_mem_size = seg_end;
    }
  }
  if (guest_mem_size == 0 || guest_mem_size > UINT32_MAX) {
    free(file_data);
    return NULL;
  }

  uint8_t* guest_mem = calloc(1, (size_t)guest_mem_size);
  if (!guest_mem) {
    free(file_data);
    return NULL;
  }
  for (uint32_t i = 0; i < image.segment_count; i++) {
    memcpy(guest_mem + image.segments[i].vaddr, file_data + image.segments[i].file_offset,
           image.segments[i].filesz);
  }

  IcebearLoadedLibrary* lib = malloc(sizeof(IcebearLoadedLibrary));
  if (!lib) {
    free(file_data);
    free(guest_mem);
    return NULL;
  }
  lib->file_data = file_data;
  lib->guest_mem = guest_mem;
  lib->image = image;
  return lib;
}

static void* icebear_get_trampoline(void* handle, const char* name, const char* shorty,
                                  uint32_t len) {
  (void)shorty;
  (void)len;
  IcebearLoadedLibrary* lib = handle;
  uint32_t addr = icebear_elf32_find_symbol(&lib->image, name);
  if (addr == 0) {
    return NULL; /* not exported by this library at all */
  }
  /* addr is real now, an offset into lib->guest_mem ready for
   * icebear_interp_run. What's still missing: something ART can actually
   * CALL as a normal AArch64 function pointer that marshals its real JNI
   * arguments/return value and drives the interpreter underneath, a
   * generated trampoline (or a libffi closure), not written yet. */
  (void)addr;
  return NULL;
}

static bool icebear_is_supported(const char* libpath) { return icebear_is_arm32_elf(libpath); }

static const struct NativeBridgeRuntimeValues* icebear_get_app_env(const char* instruction_set) {
  (void)instruction_set;
  return NULL;
}

/* Governs far more than its name suggests: libnativebridge's own
 * LoadNativeBridge() calls THIS (via its wrapper, which defers to us for
 * any bridge_version >= 2) with NAMESPACE_VERSION(3) *unconditionally*,
 * at load time, before anything else runs, and rejects the whole bridge
 * outright if it gets false back (confirmed against AOSP's
 * libnativebridge/native_bridge.cc). Returning false here isn't "we
 * don't support namespaces", it's "don't load this bridge at all",
 * which used to be true here by accident, not by design. Namespace
 * features are separately, individually gated by their own
 * isCompatibleWith(NAMESPACE_VERSION) check at the point they're
 * actually used, so claiming version 3 here obligates every v3 function
 * below to be real (or safely inert), never NULL. Not claiming
 * VENDOR_NAMESPACE_VERSION(4): NativeBridgeGetVendorNamespace() checks
 * compatibility itself before ever touching our function pointer, so
 * declining it is safe, unlike NAMESPACE_VERSION. */
static bool icebear_is_compatible_with(uint32_t bridge_version) { return bridge_version <= 3; }

static NativeBridgeSignalHandlerFn icebear_get_signal_handler(int signal) {
  (void)signal;
  return NULL;
}

/* Real now: frees what loadLibrary allocated. Only reachable because
 * icebear_is_compatible_with(3) is honest above; it used to be dead code. */
static int icebear_unload_library(void* handle) {
  IcebearLoadedLibrary* lib = handle;
  if (!lib) {
    return -1;
  }
  free(lib->file_data);
  free(lib->guest_mem);
  free(lib);
  return 0;
}

/* No per-call error state tracked yet (loadLibrary/getTrampoline just
 * return NULL on failure); a fixed, generic string is honest about that
 * rather than inventing detail we don't actually have. */
static const char* icebear_get_error(void) { return "icebear: no detailed error reporting yet"; }

static bool icebear_is_path_supported(const char* library_path) { return icebear_is_arm32_elf(library_path); }

/* We don't do real linker-level namespace isolation (loadLibrary/
 * loadLibraryExt both just parse and load the guest .so directly, see
 * above), so every namespace-related call below is a safe no-op that
 * says "sure" rather than actually creating separate load domains. That
 * matches what we actually do today: one guest library loaded is exactly
 * like another, regardless of which "namespace" ART thinks it's in. */
static bool icebear_init_anonymous_namespace(const char* public_ns_sonames,
                                           const char* anon_ns_library_path) {
  (void)public_ns_sonames;
  (void)anon_ns_library_path;
  return true;
}

/* Not a real namespace object, just a distinct non-null token so callers
 * that check for NULL-on-failure don't treat this as having failed. */
static struct native_bridge_namespace_t* icebear_create_namespace(
    const char* name, const char* ld_library_path, const char* default_library_path,
    uint64_t type, const char* permitted_when_isolated_path,
    struct native_bridge_namespace_t* parent_ns) {
  (void)name;
  (void)ld_library_path;
  (void)default_library_path;
  (void)type;
  (void)permitted_when_isolated_path;
  (void)parent_ns;
  static int dummy_namespace_token;
  return (struct native_bridge_namespace_t*)&dummy_namespace_token;
}

static bool icebear_link_namespaces(struct native_bridge_namespace_t* from,
                                  struct native_bridge_namespace_t* to,
                                  const char* shared_libs_sonames) {
  (void)from;
  (void)to;
  (void)shared_libs_sonames;
  return true;
}

static void* icebear_load_library_ext(const char* libpath, int flag,
                                    struct native_bridge_namespace_t* ns) {
  (void)ns; /* no real namespace-scoped path resolution, see the note above */
  return icebear_load_library(libpath, flag);
}

__attribute__((visibility("default"))) struct NativeBridgeCallbacks NativeBridgeItf = {
    .version = 3,
    .initialize = icebear_initialize,
    .loadLibrary = icebear_load_library,
    .getTrampoline = icebear_get_trampoline,
    .isSupported = icebear_is_supported,
    .getAppEnv = icebear_get_app_env,
    .isCompatibleWith = icebear_is_compatible_with,
    .getSignalHandler = icebear_get_signal_handler,
    .unloadLibrary = icebear_unload_library,
    .getError = icebear_get_error,
    .isPathSupported = icebear_is_path_supported,
    .initAnonymousNamespace = icebear_init_anonymous_namespace,
    .createNamespace = icebear_create_namespace,
    .linkNamespaces = icebear_link_namespaces,
    .loadLibraryExt = icebear_load_library_ext,
    /* getVendorNamespace deliberately left NULL: we don't claim
     * VENDOR_NAMESPACE_VERSION(4), and that's checked before this would
     * ever be dereferenced, see icebear_is_compatible_with's comment. */
};

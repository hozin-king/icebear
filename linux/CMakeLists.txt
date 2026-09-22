cmake_minimum_required(VERSION 3.22)
project(icebear_linux C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Reuses native/'s portable decoder+interpreter unchanged (icebear_core is a
# plain host-buildable static lib, no NDK needed, see native/CMakeLists.txt)
# instead of forking a copy. See linux/README.md for what this directory
# adds on top: a standalone process, not Android's native bridge.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../native" "${CMAKE_CURRENT_BINARY_DIR}/native")

# Builds and runs on any host; proves icebear_core works with zero Android/JNI
# involvement. Not the real test suite (that's native/tests/test_interp.c).
add_executable(icebear_linux_selfcheck src/selfcheck.c)
target_link_libraries(icebear_linux_selfcheck PRIVATE icebear_core)

# The real (if still narrow: static, non-PIE only) ELF loading + execution
# logic, separate from the CLI so tests/test_loader.c can drive it directly.
add_library(icebear_linux_loader_core STATIC src/loader_core.c)
target_include_directories(icebear_linux_loader_core PUBLIC include)
target_link_libraries(icebear_linux_loader_core PUBLIC icebear_core)

add_executable(icebear_linux_loader src/loader.c)
target_link_libraries(icebear_linux_loader PRIVATE icebear_linux_loader_core)

enable_testing()
add_executable(icebear_linux_loader_tests tests/test_loader.c)
target_link_libraries(icebear_linux_loader_tests PRIVATE icebear_linux_loader_core)
add_test(NAME linux_loader_smoke_test COMMAND icebear_linux_loader_tests)

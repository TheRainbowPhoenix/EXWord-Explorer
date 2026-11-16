cmake_minimum_required(VERSION 3.15)
project(EXWordExplorerStandaloneUpdater C ASM)
include(GenerateUPDADN2)
add_executable(explorer_upd boot.c syscalls.S start.S)
target_compile_options(explorer_upd PRIVATE
  -mb -m4a-nofpu -Os -ffreestanding -fno-builtin
  -fno-stack-protector -fno-unwind-tables
  -fno-asynchronous-unwind-tables -Wall -Wextra)
target_link_options(explorer_upd PRIVATE
  -mb -m4a-nofpu -nostdlib -Wl,--build-id=none
  -Wl,--no-warn-rwx-segments
  "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/link.ld")
set_property(TARGET explorer_upd APPEND PROPERTY
  LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/link.ld")
generate_updadn2(TARGET explorer_upd
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/UPDADN2.BIN")

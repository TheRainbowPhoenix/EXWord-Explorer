cmake_minimum_required(VERSION 3.15)
project(EXWordExplorerFullBoot C ASM)
include(GenerateUPDADN2)
add_executable(fullboot fullboot.c start.S)
target_compile_options(fullboot PRIVATE
  -mb -m4a-nofpu -Os -ffreestanding -fno-builtin
  -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables
  -Wall -Wextra -fno-pic -fno-pie)
target_link_options(fullboot PRIVATE
  -mb -m4a-nofpu -nostdlib -no-pie -Wl,--build-id=none
  -Wl,--no-warn-rwx-segments
  "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/link.ld")
set_property(TARGET fullboot APPEND PROPERTY
  LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/link.ld")
generate_updadn2(TARGET fullboot
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fullboot.bin")

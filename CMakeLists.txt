cmake_minimum_required(VERSION 3.15)
project(EXWordExplorer VERSION 0.1 LANGUAGES C ASM)

# drv0/crd0/sys0 are EX-word filesystem mounts, not fx-CP HH3 volumes.
if(NOT FXSDK_PLATFORM_LONG STREQUAL "exword")
  message(FATAL_ERROR "EXWord Explorer must be built with EXWORD.cmake")
endif()

include(GenerateD01)
include(GenerateEXWORDPackage)
find_package(Gint 2.11 REQUIRED)

add_executable(explorer
  src/main.c
  src/explorer_fs.c
)
target_compile_options(explorer PRIVATE
  -Wall -Wextra -Os -flto=auto -fno-pic -fno-pie
  -fno-stack-protector -ffunction-sections -fdata-sections)
target_link_options(explorer PRIVATE
  -flto=auto -nostdlib -no-pie -Wl,--build-id=none
  -Wl,--no-warn-rwx-segments -Wl,-Map=explorer.map
  -Wl,--print-memory-usage)
target_link_libraries(explorer Gint::Gint)

generate_d01(TARGET explorer
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/explorer.d01"
  MODULE "explor")

if(NOT DEFINED EXWORD_METADATA_SOURCE)
  message(FATAL_ERROR "Set EXWORD_METADATA_SOURCE to the EX-word HTML metadata repository")
endif()
generate_exword_package(
  TARGET explorer
  D01 "${CMAKE_CURRENT_BINARY_DIR}/explorer.d01"
  APPID "EXPLR"
  TITLE "EXWord Explorer"
  HTML_DIR "${EXWORD_METADATA_SOURCE}/html"
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/packages"
  LOCALES ja cn)

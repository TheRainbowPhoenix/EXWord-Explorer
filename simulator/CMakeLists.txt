cmake_minimum_required(VERSION 3.15)
project(EXWordExplorerSimulator LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
find_package(SDL2 REQUIRED)
add_executable(explorer_sim
  ../src/main.c
  ../src/preview.c
  ../src/usb_screen.c
  ../src/readers/casio_bitmap.c
  ../src/readers/cjf.c
  src/simulator.cpp
  src/launcher.cpp
  src/mock_fs.c
  src/mock_usb.c)
target_include_directories(explorer_sim PRIVATE include ../src)
set_source_files_properties(../src/main.c PROPERTIES COMPILE_DEFINITIONS "main=explorer_app_main")
target_link_libraries(explorer_sim PRIVATE SDL2::SDL2)
if(MSVC)
  target_compile_options(explorer_sim PRIVATE /W3)
else()
  target_compile_options(explorer_sim PRIVATE -Wall -Wextra)
endif()

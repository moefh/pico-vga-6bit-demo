cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(vga_6bit_demo_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(vga_6bit_demo
  main.c
  vga_6bit.c
  vga_font.c
  vga_draw.c
)

pico_generate_pio_header(vga_6bit_demo ${CMAKE_CURRENT_LIST_DIR}/vga_6bit.pio)

pico_enable_stdio_usb(vga_6bit_demo 1)

target_link_libraries(vga_6bit_demo
  pico_stdlib
  pico_multicore
  hardware_pio
  hardware_dma
)

pico_add_extra_outputs(vga_6bit_demo)

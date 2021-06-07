#ifndef VGA_6BIT_H_FILE
#define VGA_6BIT_H_FILE

#include <stdbool.h>

#define VGA_ERROR_ALLOC     (-1)
#define VGA_ERROR_MULTICORE (-2)

#ifdef __cplusplus
extern "C" {
#endif

struct VGA_SCREEN {
  int width;
  int height;
  unsigned int sync_bits;
  unsigned int **framebuffer;
};
  
#if VGA_ENABLE_MULTICORE
extern void (*volatile vga_core1_func)(void);
#endif

int vga_init(unsigned int pin_out_base);
void vga_clear_screen(unsigned char color);
void vga_swap_buffers(bool wait_sync);

extern struct VGA_SCREEN vga_screen;

#ifdef __cplusplus
}
#endif

#endif /* VGA_6BIT_H_FILE */

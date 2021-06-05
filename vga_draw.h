#ifndef VGA_DRAW_H_FILE
#define VGA_DRAW_H_FILE

#include <stdbool.h>

#include "vga_6bit.h"

#ifdef __cplusplus
extern "C" {
#endif

struct SPRITE {
  int width;
  int height;
  unsigned int stride;  // number of words per line
  const unsigned int *data;
};

void draw_sprite(struct SPRITE *sprite, int spr_x, int spr_y, bool transparent);

#ifdef __cplusplus
}
#endif

#endif /* DRAW_H_FILE */

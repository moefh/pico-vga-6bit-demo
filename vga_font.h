#ifndef VGA_FONT_H_FILE
#define VGA_FONT_H_FILE

#define VGA_FONT_USE_STDARG 1

#include "vga_6bit.h"

#ifdef __cplusplus
extern "C" {
#endif

struct VGA_FONT {
  int w;
  int h;
  int first_char;
  int num_chars;
  const unsigned char *data;
};

enum FONT_ALIGNMENT {
  FONT_ALIGN_LEFT,
  FONT_ALIGN_CENTER,
  FONT_ALIGN_RIGHT
};

void font_set_font(const struct VGA_FONT *font);
void font_set_color(unsigned int color);
void font_move(unsigned int x, unsigned int y);
void font_align(enum FONT_ALIGNMENT alignment);

void font_draw_int(int num);
void font_draw_uint(unsigned int num);
void font_draw_float(float num);
void font_draw_text(const char *text);

#if VGA_FONT_USE_STDARG
void font_printf(const char *fmt, ...)
  __attribute__ ((format (printf, 1, 2)));
#endif
  
#ifdef __cplusplus
}
#endif
  
#endif /* VGA_FONT_H_FILE */

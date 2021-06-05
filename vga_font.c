#include <stdio.h>
#include <string.h>

#include "vga_font.h"

static char print_buf[32];
static const struct VGA_FONT *font;
static unsigned int font_x, font_y;
static enum FONT_ALIGNMENT font_alignment;
static unsigned char font_color;

#if VGA_FONT_USE_STDARG
#include <stdarg.h>

void font_printf(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(print_buf, sizeof(print_buf), fmt, ap);
  va_end(ap);

  font_draw_text(print_buf);
}
#endif

void font_set_font(const struct VGA_FONT *new_font)
{
  font = new_font;
}

void font_set_color(unsigned int color)
{
  font_color = vga_screen.sync_bits | (color & 0x3f);
}

void font_move(unsigned int x, unsigned int y)
{
  font_x = x;
  font_y = y;
}

void font_align(enum FONT_ALIGNMENT alignment)
{
  font_alignment = alignment;
}

void font_draw_int(int num)
{
  snprintf(print_buf, sizeof(print_buf), "%d", num);
  font_draw_text(print_buf);
}

void font_draw_uint(unsigned int num)
{
  snprintf(print_buf, sizeof(print_buf), "%u", num);
  font_draw_text(print_buf);
}

void font_draw_float(float num)
{
  snprintf(print_buf, sizeof(print_buf), "%f", num);
  font_draw_text(print_buf);
}

void font_draw_text(const char *text)
{
  if (text == NULL) return;
  
  switch (font_alignment) {
  case FONT_ALIGN_LEFT:   /* nothing to do */ break;
  case FONT_ALIGN_CENTER: font_x -= strlen(text) * font->w / 2; break;
  case FONT_ALIGN_RIGHT:  font_x -= strlen(text) * font->w; break;
  }
  
  int save_font_x = font_x;
  while (*text != '\0') {
    char ch = *text++;
    if (ch >= font->first_char && ch < font->first_char+font->num_chars) {
      ch -= font->first_char;
      for (int i = 0; i < font->h; i++) {
        uint8_t char_line = font->data[font->h*ch + i];
        uint8_t char_bit = 1;
        for (int j = 0; j < font->w; j++) {
          if ((char_line & char_bit) != 0 && font_y+i < vga_screen.height && font_x+j < vga_screen.width) {
            ((unsigned char *) vga_screen.framebuffer[font_y+i])[font_x+j] = font_color;
          }
          char_bit <<= 1;
        }
      }
    }
    font_x += font->w;
  }

  if (font_alignment == FONT_ALIGN_RIGHT) {
    font_x = save_font_x;
  }
}

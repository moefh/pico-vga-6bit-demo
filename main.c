
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/binary_info.h"

// for RNG:
#include "hardware/regs/rosc.h"
#include "hardware/regs/addressmap.h"

#include "vga_6bit.h"
#include "vga_font.h"
#include "vga_draw.h"

#include "data/font6x8.h"
#include "data/background.h"
#include "data/loserboy.h"

#define VGA_PIN_BASE  2  // first VGA output pin
#define NUM_SPRITES   8  // number of sprites to draw

struct CHARACTER {
  struct SPRITE *sprite;
  int message_index;
  int x;
  int y;
  int dx;
  int dy;
  int frame;
  int message_frame;
};

struct SPRITE background;
struct SPRITE char_frames[img_loserboy_num_spr];
struct CHARACTER characters[NUM_SPRITES];

#define loserboy_stand_frame        10
#define loserboy_mirror_frame_start 11
#define loserboy_walk_frame_delay   4
static const unsigned int loserboy_walk_cycle[] = {
  5, 6, 7, 8, 9, 8, 7, 6, 5, 0, 1, 2, 3, 4, 3, 2, 1, 0,
};

static const char *loserboy_messages[] = {
  "I'll get you!",
  "Come back here!",
  "Ayeeeee!",
  "You can't escape!",
  "Take this!",
};

static unsigned int rand(void)
{
  volatile unsigned int *reg = (unsigned int *)(ROSC_BASE + ROSC_RANDOMBIT_OFFSET);
  
  unsigned int ret = 0;
  for (int i = 0; i < 32; i++) {
    ret = (ret << 1) | (*reg & 1);
  }
  return ret;
}

static void blink_led(void)
{
  static int led_state = 1;
  static int frame_count = 0;
  if (frame_count++ >= 30) {
    frame_count = 0;
    led_state = !led_state;
    gpio_put(PICO_DEFAULT_LED_PIN, led_state);
  }
}

static int count_fps(void)
{
  static int last_fps;
  static int frame_count;
  static unsigned last_ms;

  unsigned int cur_ms = to_ms_since_boot(get_absolute_time());
  if (cur_ms/1000 != last_ms/1000) {
    last_fps = frame_count;
    frame_count = 0;
  }
  frame_count++;
  last_ms = cur_ms;
  return last_fps;
}

static void init_sprites(void)
{
  background.width  = img_background_width;
  background.height = img_background_height;
  background.stride = img_background_stride;
  background.data   = img_background_data;
  
  for (int i = 0; i < count_of(char_frames); i++) {
    struct SPRITE *spr = &char_frames[i];
    spr->width  = img_loserboy_width;
    spr->height = img_loserboy_height;
    spr->stride = img_loserboy_stride;
    spr->data   = &img_loserboy_data[i*img_loserboy_stride*img_loserboy_height];
  }

  for (int i = 0; i < NUM_SPRITES; i++) {
    struct CHARACTER *ch = &characters[i];
    ch->x = rand() % (vga_screen.width  - img_loserboy_width);
    ch->y = rand() % (vga_screen.height - img_loserboy_height);
    ch->dx = (1 + rand() % 3) * ((rand() & 1) ? -1 : 1);
    ch->dy = (1 + rand() % 2) * ((rand() & 1) ? -1 : 1);
    ch->frame = i + i*loserboy_walk_frame_delay;
    ch->sprite = NULL;
    ch->message_index = -1;
    ch->message_frame = 120 + 300 * i + rand() % 1200;
  }
}

static void move_character(struct CHARACTER *ch)
{
  ch->x += ch->dx;
  if (ch->x <  -ch->sprite->width/2)                   ch->dx =   1 + rand() % 3;
  if (ch->x >= vga_screen.width-ch->sprite->width/2)   ch->dx = -(1 + rand() % 3);

  ch->y += ch->dy;
  if (ch->y <  -ch->sprite->height/2)                  ch->dy =   1 + rand() % 2;
  if (ch->y >= vga_screen.height-ch->sprite->height/2) ch->dy = -(1 + rand() % 2);

  if (ch->message_frame-- < 0) {
    ch->message_index = -1;
    ch->message_frame = 600 + rand() % 1200;
  } else if (ch->message_frame == 180) {
    ch->message_index = rand() % count_of(loserboy_messages);
  }

  ch->frame++;
  if (ch->frame/loserboy_walk_frame_delay >= count_of(loserboy_walk_cycle)) {
    ch->frame = 0;
  }
  int frame_num = loserboy_walk_cycle[ch->frame/loserboy_walk_frame_delay];
  ch->sprite = &char_frames[frame_num + ((ch->dx < 0) ? loserboy_mirror_frame_start : 0)];
}

static void fancy_draw_text(int x, int y, unsigned int text_color, unsigned int border_color, const char *text)
{
  font_set_color(border_color);
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      if (dx != 0 || dy != 0) {
        font_move(x+dx, y+dy);
        font_draw_text(text);
      }
    }
  }
  font_set_color(text_color);
  font_move(x, y);
  font_draw_text(text);
}

int main(void)
{
  stdio_init_all();
  gpio_init(PICO_DEFAULT_LED_PIN);
  gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
  gpio_put(PICO_DEFAULT_LED_PIN, 1);

  //sleep_ms(5000); printf("Starting...\n");

  bi_decl_if_func_used(bi_pin_mask_with_name(3 << (VGA_PIN_BASE + 0), "Blue 0-1"));
  bi_decl_if_func_used(bi_pin_mask_with_name(3 << (VGA_PIN_BASE + 2), "Green 0-1"));
  bi_decl_if_func_used(bi_pin_mask_with_name(3 << (VGA_PIN_BASE + 4), "Red 0-1"));
  bi_decl_if_func_used(bi_1pin_with_name(VGA_PIN_BASE + 6, "H-Sync"));
  bi_decl_if_func_used(bi_1pin_with_name(VGA_PIN_BASE + 7, "V-Sync"));
  
  if (vga_init(VGA_PIN_BASE) < 0) {
    printf("ERROR initializing VGA\n");
    fflush(stdout);
    return 1;
  }

  font_set_font(&font6x8);
  init_sprites();

  while (true) {
    blink_led();

    for (int i = 0; i < NUM_SPRITES; i++) {
      move_character(&characters[i]);
    }

    // draw sprites
    draw_sprite(&background, 0, 0, false);
    bool has_message = false;
    for (int i = 0; i < NUM_SPRITES; i++) {
      struct CHARACTER *ch = &characters[i];
      draw_sprite(ch->sprite, ch->x, ch->y, true);
      if (ch->message_index >= 0) {
        if (has_message) {
          ch->message_index = -1;  // cancel message if already drawing one
        } else {
          font_align(FONT_ALIGN_CENTER);
          fancy_draw_text(ch->x + ch->sprite->width/2, ch->y - 10, 0x3f, 0, loserboy_messages[ch->message_index]);
          has_message = true;
        }
      }
    }

    // draw fps counter
    int fps = count_fps();
    font_align(FONT_ALIGN_LEFT);
    font_move(10, 10);
    font_set_color(0x3f);
    font_printf("%d fps", fps);

    // send prepared framebuffer to monitor and get a new one
    vga_swap_buffers(true);
  }
}

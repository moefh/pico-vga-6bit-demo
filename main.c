
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
#include "data/loserboy.h"
#include "data/tiles.h"

#define VGA_PIN_BASE  2  // first VGA output pin
#define NUM_SPRITES   30 // number of sprites to draw

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

struct SPRITE bg_tiles[img_tiles_num_spr];
struct SPRITE char_frames[img_loserboy_num_spr];
struct CHARACTER characters[NUM_SPRITES];

#define loserboy_stand_frame        10
#define loserboy_mirror_frame_start 11
#define loserboy_walk_frame_delay   4
static const unsigned int loserboy_walk_cycle[] = {
  5, 6, 7, 8, 9, 8, 7, 6, 5, 0, 1, 2, 3, 4, 3, 2, 1, 0,
};
static const unsigned char bg_map[20] = {
  0,0,0,0,0,
  0,0,1,0,0,
  0,1,0,1,0,
  0,0,0,0,0,
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
  for (int i = 0; i < count_of(bg_tiles); i++) {
    struct SPRITE *spr = &bg_tiles[i];
    spr->width  = img_tiles_width;
    spr->height = img_tiles_height;
    spr->stride = img_tiles_stride;
    spr->data   = &img_tiles_data[i*img_tiles_stride*img_tiles_height];
  }

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
    ch->message_frame = -1;
  }
}

static void move_character(struct CHARACTER *ch)
{
  if (ch->message_frame-- < 0) {
    ch->message_index = -1;
    ch->message_frame = 600 + rand() % 1200;
  } else if (ch->message_frame == 180) {
    ch->message_index = rand() % count_of(loserboy_messages);
  }

  if (ch->message_frame > 1500) {
    ch->sprite = &char_frames[loserboy_stand_frame + ((ch->dx < 0) ? loserboy_mirror_frame_start : 0)];
  } else {
    ch->x += ch->dx;
    if (ch->x <  -ch->sprite->width/2)                   ch->dx =   1 + rand() % 3;
    if (ch->x >= vga_screen.width-ch->sprite->width/2)   ch->dx = -(1 + rand() % 3);
    
    ch->y += ch->dy;
    if (ch->y <  -ch->sprite->height/2)                  ch->dy =   1 + rand() % 2;
    if (ch->y >= vga_screen.height-ch->sprite->height/2) ch->dy = -(1 + rand() % 2);
    
    ch->frame++;
    if (ch->frame/loserboy_walk_frame_delay >= count_of(loserboy_walk_cycle)) {
      ch->frame = 0;
    }
    int frame_num = loserboy_walk_cycle[ch->frame/loserboy_walk_frame_delay];
    ch->sprite = &char_frames[frame_num + ((ch->dx < 0) ? loserboy_mirror_frame_start : 0)];
  }
}

int main(void)
{
  stdio_init_all();
  gpio_init(PICO_DEFAULT_LED_PIN);
  gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
  gpio_put(PICO_DEFAULT_LED_PIN, 1);

  //sleep_ms(5000); printf("Starting...\n");

  bi_decl_if_func_used(bi_pin_mask_with_name(3 << (VGA_PIN_BASE + 0), "Red 0-1"));
  bi_decl_if_func_used(bi_pin_mask_with_name(3 << (VGA_PIN_BASE + 2), "Green 0-1"));
  bi_decl_if_func_used(bi_pin_mask_with_name(3 << (VGA_PIN_BASE + 4), "Blue 0-1"));
  bi_decl_if_func_used(bi_1pin_with_name(VGA_PIN_BASE + 6, "H-Sync"));
  bi_decl_if_func_used(bi_1pin_with_name(VGA_PIN_BASE + 7, "V-Sync"));
  
  if (vga_init(VGA_PIN_BASE) < 0) {
    printf("ERROR initializing VGA\n");
    fflush(stdout);
    return 1;
  }

  font_set_font(&font6x8);
  font_set_color(0x3f);
  init_sprites();

  while (true) {
    blink_led();

    for (int i = 0; i < NUM_SPRITES; i++) {
      move_character(&characters[i]);
    }

    // draw background
    vga_clear_screen(0x18);
    for (int ty = 0; ty < 4; ty++) {
      for (int tx = 0; tx < 5; tx++) {
        struct SPRITE *tile = &bg_tiles[bg_map[ty*5 + tx]];
        draw_sprite(tile, tx*tile->width, ty*tile->height, false);
      }
    }
    
    // draw sprites
    int msg_index = -1;
    int msg_x, msg_y;
    for (int i = 0; i < NUM_SPRITES; i++) {
      struct CHARACTER *ch = &characters[i];
      draw_sprite(ch->sprite, ch->x, ch->y, true);
      if (ch->message_index >= 0) {
        msg_x = ch->x + ch->sprite->width/2;
        msg_y = ch->y - 10;
        msg_index = ch->message_index;
      }
    }
    if (msg_index >= 0) {
      font_align(FONT_ALIGN_CENTER);
      font_move(msg_x, msg_y);
      font_print(loserboy_messages[msg_index]);
    }

    
    // draw fps counter
    int fps = count_fps();
    font_align(FONT_ALIGN_LEFT);
    font_move(10, 10);
    font_printf("%d fps", fps);

    // send prepared framebuffer to monitor and get a new one
    vga_swap_buffers(true);
  }
}

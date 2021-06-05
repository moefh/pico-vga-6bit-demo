/**
 * vga_6bit.c
 *
 * Copyright (C) 2021 MoeFH
 * Released under the MIT License
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"

#include "vga_6bit.h"
#include "vga_6bit.pio.h"

// VGA MODE
#define PIX_CLOCK_KHZ 12588
#define H_FRONT_PORCH 8
#define H_SYNC_PULSE  48
#define H_BACK_PORCH  24
#define H_PIXELS      320
#define V_FRONT_PORCH 10
#define V_SYNC_PULSE  2
#define V_BACK_PORCH  33
#define V_PIXELS      480
#define V_DIV         2
#define H_POLARITY    1  // polarity of sync pulse (0=positive, 1=negative)
#define V_POLARITY    1

#define H_FULL_LINE   (H_FRONT_PORCH+H_SYNC_PULSE+H_BACK_PORCH+H_PIXELS)
#define V_FULL_FRAME  (V_FRONT_PORCH+V_SYNC_PULSE+V_BACK_PORCH+V_PIXELS)

#define HSYNC_ON           (!H_POLARITY)
#define HSYNC_OFF          ( H_POLARITY)
#define VSYNC_ON           (!V_POLARITY)
#define VSYNC_OFF          ( V_POLARITY)
#define HBLANK_BUFFER_LEN  ((H_FRONT_PORCH+H_SYNC_PULSE+H_BACK_PORCH)/4)
#define HPIXELS_BUFFER_LEN (H_PIXELS/4)

#define SYNC_BITS     ((VSYNC_OFF<<7) | (HSYNC_OFF<<6))
#define SCREEN_WIDTH  H_PIXELS
#define SCREEN_HEIGHT (V_PIXELS/V_DIV)

static unsigned int hblank_buffer_vsync_on[HBLANK_BUFFER_LEN];
static unsigned int hblank_buffer_vsync_off[HBLANK_BUFFER_LEN];
static unsigned int hpixels_buffer_vsync_on[HPIXELS_BUFFER_LEN];
static unsigned int hpixels_buffer_vsync_off[HPIXELS_BUFFER_LEN];
static unsigned int *framebuffers[2];
static unsigned int *framebuffer_lines[2][SCREEN_HEIGHT];

struct DMA_BUFFER_INFO {
  uint32_t count;
  void *data;
};
static struct DMA_BUFFER_INFO dma_chain[2*V_FULL_FRAME+1];
static uint dma_control_chan;
static uint dma_data_chan;

static volatile uint frame_count;
static uint cur_framebuffer;
struct VGA_SCREEN vga_screen;

static void __isr __time_critical_func(dma_handler)(void)
{
  dma_hw->ints0 = 1u << dma_data_chan;
  dma_channel_set_read_addr(dma_control_chan, &dma_chain[0], true);
  frame_count++;
}

static void init_dma_irq(void)
{
  irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
  irq_set_priority(DMA_IRQ_0, 0xff);
  irq_set_enabled(DMA_IRQ_0, true);
}

#if VGA_ENABLE_MULTICORE

#include "pico/multicore.h"

#define MARK_CORE1_STARTED     0x12345678
#define MARK_CORE0_CONTINUING  0x87654321

void (*volatile vga_core1_func)(void) = NULL;

static void core1_main(void)
{
  multicore_fifo_push_blocking(MARK_CORE1_STARTED);
  
  uint32_t mark = multicore_fifo_pop_blocking();
  if (mark == MARK_CORE0_CONTINUING) {
    init_dma_irq();
  } else {
    printf("VGA MULTICORE ERROR: received invalid mark in core1: 0x%08x\n", (unsigned int) mark);
  }

  while (true) {
    if (vga_core1_func) {
      vga_core1_func();
    }
  }
}
#endif /* VGA_ENABLE_MULTICORE */

static int init_pio(unsigned int pin_out_base)
{
  PIO pio = pio0;
  uint sm = pio_claim_unused_sm(pio, true);

  // TODO: choose PIO clock divider based on the CPU clock and VGA
  // pixel clock.  We're currently assuming that the CPU clock is
  // 125MHz and VGA pixel clock is 12.5MHz (that is, 25MHz with pixel
  // doubling), so we just set the divider to 10.
  //uint f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
  float clock_div = 10.f;

  uint offset = pio_add_program(pio, &vga_program);
  vga_program_init(pio, sm, offset, pin_out_base, clock_div);

  dma_control_chan = dma_claim_unused_channel(true);
  dma_data_chan    = dma_claim_unused_channel(true);
  dma_channel_config cfg;

  // DMA control channel
  cfg = dma_channel_get_default_config(dma_control_chan);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
  channel_config_set_read_increment(&cfg, true);
  channel_config_set_write_increment(&cfg, true);
  channel_config_set_ring(&cfg, true, 3);    // loop write address every 1<<3 = 8 bytes
  
  dma_channel_configure(dma_control_chan,
                        &cfg,
                        &dma_hw->ch[dma_data_chan].al3_transfer_count,   // dest (we'll write to transfer_count and read_address)
                        &dma_chain[0],                                   // source (updated in irq)
                        sizeof(struct DMA_BUFFER_INFO)/sizeof(int32_t),  // num words for each transfer
                        false                                            // don't start now
                        );

  // DMA data channel
  cfg = dma_channel_get_default_config(dma_data_chan);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
  channel_config_set_read_increment(&cfg, true);
  channel_config_set_write_increment(&cfg, false);
  channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, true));
  channel_config_set_chain_to(&cfg, dma_control_chan);
  channel_config_set_irq_quiet(&cfg, true);  // raise irq when 0 is written to trigger register (end of chain)

  dma_channel_configure(dma_data_chan,
                        &cfg,
                        &pio->txf[sm],   // dest
                        NULL,            // source    (set by control channel)
                        0,               // num words (set by control channel)
                        false            // don't start now
                        );

  dma_channel_set_irq0_enabled(dma_data_chan, true);

#if VGA_ENABLE_MULTICORE
  multicore_launch_core1(core1_main);
  uint32_t mark = multicore_fifo_pop_blocking();
  if (mark != MARK_CORE1_STARTED) {
    return VGA_ERROR_MULTICORE;
  }
  multicore_fifo_push_blocking(MARK_CORE0_CONTINUING);
#else
  init_dma_irq();
#endif
  return 0;
}

static void set_dma_buffer_info(struct DMA_BUFFER_INFO *buf, void *data, uint32_t count)
{
  buf->data = data;
  buf->count = count;
}

static void clear_framebuffer(uint fb_num, uint8_t color)
{
  uint8_t val = SYNC_BITS | (color & 0x3f);
  memset(framebuffers[fb_num], val, SCREEN_WIDTH*SCREEN_HEIGHT);
}

static int init_buffers(int num_framebuffers)
{
  uint8_t sync_h0v0 = (VSYNC_OFF<<7) | (HSYNC_OFF<<6);
  uint8_t sync_h1v0 = (VSYNC_OFF<<7) | (HSYNC_ON <<6);
  uint8_t sync_h0v1 = (VSYNC_ON <<7) | (HSYNC_OFF<<6);
  uint8_t sync_h1v1 = (VSYNC_ON <<7) | (HSYNC_ON <<6);
  
  // hblank lines
  unsigned char *hblank_vsync_on   = (unsigned char *) hblank_buffer_vsync_on;
  unsigned char *hblank_vsync_off  = (unsigned char *) hblank_buffer_vsync_off;
  for (int i = 0; i < H_FRONT_PORCH+H_SYNC_PULSE+H_BACK_PORCH; i++) {
    if (i >= H_FRONT_PORCH && i < H_FRONT_PORCH+H_SYNC_PULSE) {
      hblank_vsync_on[i]  = sync_h1v1;
      hblank_vsync_off[i] = sync_h1v0;
    } else {
      hblank_vsync_on[i]  = sync_h0v1;
      hblank_vsync_off[i] = sync_h0v0;
    }
  }

  // vblank pixel lines
  memset(hpixels_buffer_vsync_on,  sync_h0v1, H_PIXELS);
  memset(hpixels_buffer_vsync_off, sync_h0v0, H_PIXELS);

  // framebuffers
  for (int fb_num = 0; fb_num < num_framebuffers; fb_num++) {
    framebuffers[fb_num] = malloc(SCREEN_WIDTH * SCREEN_HEIGHT);
    if (! framebuffers[fb_num]) {
      for (int i = 0; i < fb_num; i++)
        free(framebuffers[i]);
      return VGA_ERROR_ALLOC;
    }
    for (int line_num = 0; line_num < SCREEN_HEIGHT; line_num++) {
      framebuffer_lines[fb_num][line_num] = &framebuffers[fb_num][line_num*HPIXELS_BUFFER_LEN];
    }
    clear_framebuffer(fb_num, 0);
  }
  
  // setup DMA chain buffers
  struct DMA_BUFFER_INFO *buf = &dma_chain[0];
  for (int i = 0; i < V_FULL_FRAME; i++) {
    if (i < V_SYNC_PULSE) {
      // vblank with vsync active
      set_dma_buffer_info(buf++, hblank_buffer_vsync_on,  HBLANK_BUFFER_LEN);
      set_dma_buffer_info(buf++, hpixels_buffer_vsync_on, HPIXELS_BUFFER_LEN);
    } else if (i < V_SYNC_PULSE+V_BACK_PORCH || i >= V_SYNC_PULSE+V_BACK_PORCH+V_PIXELS) {
      // vblank with vsync inactive
      set_dma_buffer_info(buf++, hblank_buffer_vsync_off,  HBLANK_BUFFER_LEN);
      set_dma_buffer_info(buf++, hpixels_buffer_vsync_off, HPIXELS_BUFFER_LEN);
    } else {
      // pixel data
      set_dma_buffer_info(buf++, hblank_buffer_vsync_off, HBLANK_BUFFER_LEN);
      set_dma_buffer_info(buf++, framebuffer_lines[0][(i-V_SYNC_PULSE-V_BACK_PORCH)/V_DIV], HPIXELS_BUFFER_LEN);
    }
  }
  // chain terminator
  set_dma_buffer_info(buf, NULL, 0);

  return 0;
}

// === INTERFACE ====================================================

void vga_swap_buffers(bool wait_sync)
{
  if (wait_sync) {
    uint start_frame_count = frame_count;
    while (frame_count == start_frame_count) {
      //sleep_ms(1);  // should we remove this?
    }
  }
  
  // inject new framebuffer in DMA chain
  for (int i = 0; i < V_PIXELS; i++) {
    struct DMA_BUFFER_INFO *buf = &dma_chain[2*(V_SYNC_PULSE+V_BACK_PORCH+i) + 1];
    set_dma_buffer_info(buf, framebuffer_lines[cur_framebuffer][i/V_DIV], HPIXELS_BUFFER_LEN);
  }
  cur_framebuffer = !cur_framebuffer;
  vga_screen.framebuffer = framebuffer_lines[cur_framebuffer];
}

int vga_init(unsigned int pin_out_base)
{
  int err;
  //bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
  
  err = init_pio(pin_out_base);
  if (err < 0) return err;

  err = init_buffers(2);
  if (err < 0) return err;
  cur_framebuffer = 1; // start displaying framebuffer 0, drawing on 1

  vga_screen.width       = SCREEN_WIDTH;
  vga_screen.height      = SCREEN_HEIGHT;
  vga_screen.sync_bits   = SYNC_BITS;
  vga_screen.framebuffer = framebuffer_lines[cur_framebuffer];
  
  dma_channel_start(dma_control_chan);
  return 0;
}

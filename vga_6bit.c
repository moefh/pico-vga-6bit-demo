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
  uintptr_t read_addr;
  uintptr_t write_addr;
  uint32_t  transfer_count;
  uint32_t  ctrl_trig;
};
static struct DMA_BUFFER_INFO dma_chain[2*V_FULL_FRAME+1];
static void *dma_restart_buffer[1];
static uint dma_control_chan;
static uint dma_data_chan;

static volatile uint frame_count;
static uint cur_framebuffer;
struct VGA_SCREEN vga_screen;

static void __isr __time_critical_func(dma_handler)(void)
{
  dma_hw->ints0 = 1u << dma_data_chan;
  frame_count++;
}

static void set_dma_buffer_src(struct DMA_BUFFER_INFO *buf, volatile void *src, uint32_t count)
{
  buf->read_addr = (uintptr_t) src;
  buf->transfer_count = count;
}

static void set_dma_buffer_dst(struct DMA_BUFFER_INFO *buf, volatile void *dest, uint32_t ctrl)
{
  buf->write_addr = (uintptr_t) dest;
  buf->ctrl_trig = ctrl;
}

static int init_pio(unsigned int pin_out_base)
{
  PIO pio = pio0;
  uint sm = pio_claim_unused_sm(pio, true);
  uint pio_dreq = pio_get_dreq(pio, sm, true);

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

  // DMA control channel config
  dma_channel_config cfg = dma_channel_get_default_config(dma_control_chan);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
  channel_config_set_read_increment(&cfg, true);
  channel_config_set_write_increment(&cfg, true);
  channel_config_set_ring(&cfg, true, 4);    // loop write address every 1<<4 = 16 bytes
  
  dma_channel_configure(dma_control_chan,
                        &cfg,
                        &dma_hw->ch[dma_data_chan].read_addr,     // dest (update data channel and trigger it)
                        &dma_chain[0],                            // source
                        4,                                        // num words for each transfer
                        false                                     // don't start now
                        );

  // all blocks of dma_chain except last are set to trigger dma_data_chan to copy data to PIO
  for (int i = 0; i < count_of(dma_chain)-1; i++) {
    // src will be set by init_buffers() and vga_swap_buffers()
    set_dma_buffer_dst(&dma_chain[i],
                       &pio->txf[sm],                                              // write to PIO
                       DMA_CH0_CTRL_TRIG_INCR_READ_BITS                         |  // increment read ptr
                       (pio_dreq            << DMA_CH0_CTRL_TRIG_TREQ_SEL_LSB)  |  // as fast as PIO requires
                       (dma_control_chan    << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB)  |  // chain to dma_control_chan
                       (((uint)DMA_SIZE_32) << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB) |  // copy 32 bits per count
                       DMA_CH0_CTRL_TRIG_IRQ_QUIET_BITS                         |  // suppress IRQ
                       DMA_CH0_CTRL_TRIG_EN_BITS);
  }

  // last block of dma_chain is set to trigger dma_data_chan to copy the dma_chain start address to the control chain (restarting it)
  set_dma_buffer_src(&dma_chain[count_of(dma_chain)-1], dma_restart_buffer, 1);
  set_dma_buffer_dst(&dma_chain[count_of(dma_chain)-1],
                     &dma_hw->ch[dma_control_chan].al3_read_addr_trig,           // write to dma_control_chan read address trigger
                     (DREQ_FORCE          << DMA_CH0_CTRL_TRIG_TREQ_SEL_LSB)  |  // as fast as possible
                     (dma_data_chan       << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB)  |  // chain to itself (don't chain)
                     (((uint)DMA_SIZE_32) << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB) |  // copy 32 bits per count
                     0                                                        |  // trigger IRQ
                     DMA_CH0_CTRL_TRIG_EN_BITS);

  dma_channel_set_irq0_enabled(dma_data_chan, true);
  irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
  irq_set_priority(DMA_IRQ_0, 0xff);
  irq_set_enabled(DMA_IRQ_0, true);

  return 0;
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
      set_dma_buffer_src(buf++, hblank_buffer_vsync_on,  HBLANK_BUFFER_LEN);
      set_dma_buffer_src(buf++, hpixels_buffer_vsync_on, HPIXELS_BUFFER_LEN);
    } else if (i < V_SYNC_PULSE+V_BACK_PORCH || i >= V_SYNC_PULSE+V_BACK_PORCH+V_PIXELS) {
      // vblank with vsync inactive
      set_dma_buffer_src(buf++, hblank_buffer_vsync_off,  HBLANK_BUFFER_LEN);
      set_dma_buffer_src(buf++, hpixels_buffer_vsync_off, HPIXELS_BUFFER_LEN);
    } else {
      // pixel data
      set_dma_buffer_src(buf++, hblank_buffer_vsync_off, HBLANK_BUFFER_LEN);
      set_dma_buffer_src(buf++, framebuffer_lines[0][(i-V_SYNC_PULSE-V_BACK_PORCH)/V_DIV], HPIXELS_BUFFER_LEN);
    }
  }

  // setup DMA restart buffer
  dma_restart_buffer[0] = &dma_chain[0];

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
    set_dma_buffer_src(buf, framebuffer_lines[cur_framebuffer][i/V_DIV], HPIXELS_BUFFER_LEN);
  }
  cur_framebuffer = !cur_framebuffer;
  vga_screen.framebuffer = framebuffer_lines[cur_framebuffer];
}

void vga_clear_screen(unsigned char color)
{
  clear_framebuffer(cur_framebuffer, color);
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


#include "vga_draw.h"

#define GET_PIX0_TRANSP_MASK(block) ((((block) & 0x0000003f) != 0x0000000c) ? 0x000000ff : 0)
#define GET_PIX1_TRANSP_MASK(block) ((((block) & 0x00003f00) != 0x00000c00) ? 0x0000ff00 : 0)
#define GET_PIX2_TRANSP_MASK(block) ((((block) & 0x003f0000) != 0x000c0000) ? 0x00ff0000 : 0)
#define GET_PIX3_TRANSP_MASK(block) ((((block) & 0x3f000000) != 0x0c000000) ? 0xff000000 : 0)
#define GET_4PIX_TRANSP_MASK(block) (GET_PIX0_TRANSP_MASK(block) | \
                                     GET_PIX1_TRANSP_MASK(block) | \
                                     GET_PIX2_TRANSP_MASK(block) | \
                                     GET_PIX3_TRANSP_MASK(block))

static void draw_image_line0(unsigned int *screen, const unsigned int *image, int image_width)
{
  for (int x = 0; x < image_width/4; x++) {
    *screen++ = *image++;
  }
  switch (image_width%4) {
  case 0: break; // nothing to do
  case 1: *screen = (*screen&0xffffff00) | (*image & 0x000000ff); break;
  case 2: *screen = (*screen&0xffff0000) | (*image & 0x0000ffff); break;
  case 3: *screen = (*screen&0xff000000) | (*image & 0x00ffffff); break;
  }
}

static void draw_image_line1(unsigned int *screen, const unsigned int *image, int image_width, bool skip_first_block)
{
  unsigned int cur, old;

  // first block (3 pixels)
  cur = *image++;
  if (image_width >= 3 && ! skip_first_block) {
    image_width -= 3;
    *screen = (*screen & 0x000000ff) | ((cur << 8) & 0xffffff00);
    screen++;
  }

  // middle blocks (4 pixels)
  for (int x = 0; x < image_width/4; x++) {
    old = cur;
    cur = *image++;
    *screen = ((old >> 24) & 0x000000ff) | ((cur << 8) & 0xffffff00);
    screen++;
  }

  if (image_width % 4 == 0) return;

  // last block (1-3 pixels)
  old = cur;
  switch (image_width%4) {
  case 1: *screen = (*screen & 0xffffff00) | ((old >> 24) & 0x000000ff); break;
  case 2: *screen = (*screen & 0xffff0000) | ((old >> 24) & 0x000000ff) | ((*image << 8) & 0x0000ff00); break;
  case 3: *screen = (*screen & 0xff000000) | ((old >> 24) & 0x000000ff) | ((*image << 8) & 0x00ffff00); break;
  }
}

static void draw_image_line2(unsigned int *screen, const unsigned int *image, int image_width, bool skip_first_block)
{
  unsigned int cur, old;

  // first block (2 pixels)
  cur = *image++;
  if (image_width >= 2 && ! skip_first_block) {
    image_width -= 2;
    *screen = (*screen & 0x0000ffff) | ((cur << 16) & 0xffff0000);
    screen++;
  }

  // middle blocks (4 pixels)
  for (int x = 0; x < image_width/4; x++) {
    old = cur;
    cur = *image++;
    *screen = ((old >> 16) & 0x0000ffff) | ((cur << 16) & 0xffff0000);
    screen++;
  }

  if (image_width % 4 == 0) return;

  // last block (1-3 pixels)
  old = cur;
  switch (image_width%4) {
  case 1: *screen = (*screen & 0xffffff00) | ((old >> 16) & 0x000000ff); break;
  case 2: *screen = (*screen & 0xffff0000) | ((old >> 16) & 0x0000ffff); break;
  case 3: *screen = (*screen & 0xff000000) | ((old >> 16) & 0x0000ffff) | ((*image << 16) & 0x00ff0000); break;
  }
}

static void draw_image_line3(unsigned int *screen, const unsigned int *image, int image_width, bool skip_first_block)
{
  unsigned int cur, old;

  // first block (1 pixels)
  cur = *image++;
  if (image_width >= 1 && ! skip_first_block) {
    image_width -= 1;
    *screen = (*screen & 0x00ffffff) | ((cur << 24) & 0xff000000);
    screen++;
  }

  // middle blocks (4 pixels)
  for (int x = 0; x < image_width/4; x++) {
    old = cur;
    cur = *image++;
    *screen = ((old >> 8) & 0x00ffffff) | ((cur << 24) & 0xff000000);
    screen++;
  }

  if (image_width % 4 == 0) return;

  // last block (1-3 pixels)
  old = cur;
  switch (image_width%4) {
  case 1: *screen = (*screen & 0xffffff00) | ((old >> 8) & 0x000000ff); break;
  case 2: *screen = (*screen & 0xffff0000) | ((old >> 8) & 0x0000ffff); break;
  case 3: *screen = (*screen & 0xff000000) | ((old >> 8) & 0x00ffffff); break;
  }
}

static void draw_image_line_tr0(unsigned int *screen, const unsigned int *image, int image_width)
{
  for (int x = 0; x < image_width/4; x++) {
    unsigned int mask = GET_4PIX_TRANSP_MASK(*image);
    if (mask == 0xffffffff) {
      *screen++ = *image++;
    } else {
      *screen = (*screen & ~mask) | (*image++ & mask);
      screen++;
    }
  }

  if (image_width % 4 == 0) return;

  // TODO: optimize by calculating mask for the used pixels only?
  unsigned int mask = GET_4PIX_TRANSP_MASK(*image);
  switch (image_width%4) {
  case 1: *screen = (*screen & ((~mask) | 0xffffff00)) | (*image & mask); break;
  case 2: *screen = (*screen & ((~mask) | 0xffff0000)) | (*image & mask); break;
  case 3: *screen = (*screen & ((~mask) | 0xff000000)) | (*image & mask); break;
  }
}

static void draw_image_line_tr1(unsigned int *screen, const unsigned int *image, int image_width, bool skip_first_block)
{
  unsigned int cur, old;

  // first block (3 pixels)
  cur = *image++;
  if (image_width >= 3 && ! skip_first_block) {
    image_width -= 3;
    unsigned int block = ((cur << 8) & 0xffffff00);
    unsigned int mask  = GET_PIX1_TRANSP_MASK(block) | GET_PIX2_TRANSP_MASK(block) | GET_PIX3_TRANSP_MASK(block);
    *screen = (*screen & ~mask) | (block & mask);
    screen++;
  }

  // middle blocks (4 pixels)
  for (int x = 0; x < image_width/4; x++) {
    old = cur;
    cur = *image++;
    unsigned int block = ((old >> 24) & 0x000000ff) | ((cur << 8) & 0xffffff00);
    unsigned int mask  = GET_4PIX_TRANSP_MASK(block);
    if (mask == 0xffffffff) {
      *screen = block;
    } else {
      *screen = (*screen & ~mask) | (block & mask);
    }
    screen++;
  }

  if (image_width % 4 == 0) return;

  // last block (1-3 pixels)
  old = cur;
  switch (image_width%4) {
  case 1:
    {
      unsigned int block = ((old >> 24) & 0x000000ff);
      unsigned int mask  = GET_PIX0_TRANSP_MASK(block);
      *screen = (*screen & ~mask) | (block & mask);
    }
    break;
  case 2:
    {
      unsigned int block = ((old >> 24) & 0x000000ff) | ((*image << 8) & 0x0000ff00);
      unsigned int mask  = GET_PIX0_TRANSP_MASK(block) | GET_PIX1_TRANSP_MASK(block);
      *screen = (*screen & ~mask) | (block & mask);
    }
    break;
  case 3:
    {
      unsigned int block = ((old >> 24) & 0x000000ff) | ((*image << 8) & 0x00ffff00);
      unsigned int mask  = GET_PIX0_TRANSP_MASK(block) | GET_PIX1_TRANSP_MASK(block) | GET_PIX2_TRANSP_MASK(block);
      *screen = (*screen & ~mask) | (block & mask);
    }
    break;
  }
}

static void draw_image_line_tr2(unsigned int *screen, const unsigned int *image, int image_width, bool skip_first_block)
{
  unsigned int cur, old;

  // first block (2 pixels)
  cur = *image++;
  if (image_width >= 2 && ! skip_first_block) {
    image_width -= 2;
    unsigned int block = ((cur << 16) & 0xffff0000);
    unsigned int mask  = GET_PIX2_TRANSP_MASK(block) | GET_PIX3_TRANSP_MASK(block);
    *screen = (*screen & ~mask) | (block & mask);
    screen++;
  }

  // middle blocks (4 pixels)
  for (int x = 0; x < image_width/4; x++) {
    old = cur;
    cur = *image++;
    unsigned int block = ((old >> 16) & 0x0000ffff) | ((cur << 16) & 0xffff0000);
    unsigned int mask  = GET_4PIX_TRANSP_MASK(block);
    if (mask == 0xffffffff) {
      *screen = block;
    } else {
      *screen = (*screen & ~mask) | (block & mask);
    }
    screen++;
  }

  if (image_width % 4 == 0) return;

  // last block (1-3 pixels)
  old = cur;
  switch (image_width%4) {
  case 1:
    {
      unsigned int block = ((old >> 16) & 0x000000ff);
      unsigned int mask  = GET_PIX0_TRANSP_MASK(block);
      *screen = (*screen & ~mask) | (block & mask);
    }
    break;
  case 2:
    {
      unsigned int block = ((old >> 16) & 0x0000ffff);
      unsigned int mask  = GET_PIX0_TRANSP_MASK(block) | GET_PIX1_TRANSP_MASK(block);
      *screen = (*screen & ~mask) | (block & mask);
    }
    break;
  case 3:
    {
      unsigned int block = ((old >> 16) & 0x0000ffff) | ((*image << 16) & 0x00ff0000);
      unsigned int mask  = GET_PIX0_TRANSP_MASK(block) | GET_PIX1_TRANSP_MASK(block) | GET_PIX2_TRANSP_MASK(block);
      *screen = (*screen & ~mask) | (block & mask);
    }
    break;
  }
}

static void draw_image_line_tr3(unsigned int *screen, const unsigned int *image, int image_width, bool skip_first_block)
{
  unsigned int cur, old;

  // first block (1 pixel)
  cur = *image++;
  if (image_width >= 1 && ! skip_first_block) {
    image_width -= 1;
    unsigned int block = ((cur << 24) & 0xff000000);
    unsigned int mask  = GET_PIX3_TRANSP_MASK(block);
    *screen = (*screen & ~mask) | (block & mask);
    screen++;
  }

  // middle blocks (4 pixels)
  for (int x = 0; x < image_width/4; x++) {
    old = cur;
    cur = *image++;
    unsigned int block = ((old >> 8) & 0x00ffffff) | ((cur << 24) & 0xff000000);
    unsigned int mask  = GET_4PIX_TRANSP_MASK(block);
    if (mask == 0xffffffff) {
      *screen = block;
    } else {
      *screen = (*screen & ~mask) | (block & mask);
    }
    screen++;
  }

  if (image_width % 4 == 0) return;

  // last block (1-3 pixels)
  old = cur;
  switch (image_width%4) {
  case 1:
    {
      unsigned int block = ((old >> 8) & 0x000000ff);
      unsigned int mask  = GET_PIX0_TRANSP_MASK(block);
      *screen = (*screen & ~mask) | (block & mask);
    }
    break;
  case 2:
    {
      unsigned int block = ((old >> 8) & 0x0000ffff);
      unsigned int mask  = GET_PIX0_TRANSP_MASK(block) | GET_PIX1_TRANSP_MASK(block);
      *screen = (*screen & ~mask) | (block & mask);
    }
    break;
  case 3:
    {
      unsigned int block = ((old >> 8) & 0x00ffffff);
      unsigned int mask  = GET_PIX0_TRANSP_MASK(block) | GET_PIX1_TRANSP_MASK(block) | GET_PIX2_TRANSP_MASK(block);
      *screen = (*screen & ~mask) | (block & mask);
    }
    break;
  }
}

void draw_sprite(struct SPRITE *spr, int spr_x, int spr_y, bool transparent)
{
  const unsigned int *image_start = spr->data;

  int height = spr->height;
  if (spr_y < 0) {
    image_start += spr->stride * (-spr_y);
    height += spr_y;
    spr_y = 0;
  }
  if (height > vga_screen.height - spr_y) height = vga_screen.height - spr_y;
  if (height <= 0) return;

  bool skip_first_block = false;
  int width = spr->width;
  if (spr_x < 0) {
    image_start += (-spr_x) / 4;
    width += spr_x;
    spr_x = ((unsigned int) spr_x) % 4;
    skip_first_block = true;
  }
  if (width > vga_screen.width - spr_x) width = vga_screen.width - spr_x;
  if (width <= 0) return;

  unsigned int **line = vga_screen.framebuffer;
  if (transparent) {
    switch (spr_x % 4) {
    case 0: for (int y = 0; y < height; y++) draw_image_line_tr0(line[y+spr_y] + spr_x/4, image_start + spr->stride*y, width); break;
    case 1: for (int y = 0; y < height; y++) draw_image_line_tr1(line[y+spr_y] + spr_x/4, image_start + spr->stride*y, width, skip_first_block); break;
    case 2: for (int y = 0; y < height; y++) draw_image_line_tr2(line[y+spr_y] + spr_x/4, image_start + spr->stride*y, width, skip_first_block); break;
    case 3: for (int y = 0; y < height; y++) draw_image_line_tr3(line[y+spr_y] + spr_x/4, image_start + spr->stride*y, width, skip_first_block); break;
    }
  } else {
    switch (spr_x % 4) {
    case 0: for (int y = 0; y < height; y++) draw_image_line0(line[y+spr_y] + spr_x/4, image_start + spr->stride*y, width); break;
    case 1: for (int y = 0; y < height; y++) draw_image_line1(line[y+spr_y] + spr_x/4, image_start + spr->stride*y, width, skip_first_block); break;
    case 2: for (int y = 0; y < height; y++) draw_image_line2(line[y+spr_y] + spr_x/4, image_start + spr->stride*y, width, skip_first_block); break;
    case 3: for (int y = 0; y < height; y++) draw_image_line3(line[y+spr_y] + spr_x/4, image_start + spr->stride*y, width, skip_first_block); break;
    }
  }
}

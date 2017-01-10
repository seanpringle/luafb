/*
Copyright (c) 2017 Sean Pringle sean.pringle@gmail.com

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#define min(a,b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a < _b ? _a: _b; })
#define max(a,b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _a: _b; })

#define ensure(x) for ( ; !(x) ; exit(EXIT_FAILURE) )
#define errorf(...) fprintf(stderr, __VA_ARGS__)

static void
memset32 (void *ptr, uint32_t val, size_t num)
{
  if (
    ((val >> 24) & 0xFF) == (val & 0xFF) &&
    ((val >> 16) & 0xFF) == (val & 0xFF) &&
    ((val >>  8) & 0xFF) == (val & 0xFF) )
  {
    memset(ptr, 0, num * sizeof(uint32_t));
  }
  else
  {
    void *end = ptr + num * sizeof(uint32_t);
    for (; ptr < end; ptr += sizeof(uint32_t)) *((uint32_t*)ptr) = val;
  }
}

static uint32_t
rgba (uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
  return a << 24 | r << 16 | g << 8 | b;
}

void *fb = NULL;
int fbfd = 0;
struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;

typedef struct {
  int w;
  int h;
  uint32_t b[];
} canvas_t;

typedef struct _context_t {
  int32_t x, y;
  uint8_t r, g, b, a;
  char *font;
  double fontsize;
  canvas_t *canvas;
} context_t;

#define CONTEXTS 32
context_t contexts[CONTEXTS], *context = contexts;

lua_State *lua = NULL;
const char *script = "test.lua";

FT_Library library = NULL;

static void
push (canvas_t *canvas)
{
  ensure(context-contexts < CONTEXTS-1)
    errorf("context stack overflow");

  context_t *next = context+1;
  memcpy(next, context, sizeof(context_t));

  if (canvas)
  {
    next->canvas = canvas;
    next->x = 0;
    next->y = 0;
  }

  context = next;
}

static void
pop ()
{
  if (context > contexts)
  {
    context_t *old = context;
    context--;

    if (old->font && old->font != context->font)
      free(old->font);

    memset(old, 0, sizeof(context_t));
  }
}

static size_t
canvas_bytes (size_t w, size_t h)
{
  return sizeof(canvas_t) + max(1, w) * max(1, h) * sizeof(uint32_t);
}

static canvas_t*
canvas_alloc (int w, int h)
{
  canvas_t *c = calloc(1, canvas_bytes(w, h));
  ensure(c) errorf("canvas_alloc");
  c->w = w;
  c->h = h;
  return c;
}

static void
translate (double x, double y)
{
  context->x += x * context->canvas->w;
  context->y += y * context->canvas->h;
}

static void
color (double r, double g, double b, double a)
{
  context->r = r * 255;
  context->g = g * 255;
  context->b = b * 255;
  context->a = a * 255;
}

static void
font (const char *path, double size)
{
  context->font = strdup(path);
  context->fontsize = max(0.1, size);
}

static uint32_t
blend (uint32_t p1, uint32_t p2)
{
  uint8_t b1 = (p1      ) & 0xFF;
  uint8_t g1 = (p1 >>  8) & 0xFF;
  uint8_t r1 = (p1 >> 16) & 0xFF;
  uint8_t a1 = (p1 >> 24) & 0xFF;

  uint8_t b2 = (p2      ) & 0xFF;
  uint8_t g2 = (p2 >>  8) & 0xFF;
  uint8_t r2 = (p2 >> 16) & 0xFF;
  uint8_t a2 = (p2 >> 24) & 0xFF;

  uint8_t r = r2, g = g2, b = b2, a = a2;

  if (a1 > 0)
  {
    double blend = ((double)a1)/255 * ((double)a2)/255;
    r = (((double)r1)/255 + ((double)r2)/255) * blend * 255;
    g = (((double)g1)/255 + ((double)g2)/255) * blend * 255;
    b = (((double)b1)/255 + ((double)b2)/255) * blend * 255;
    a = blend;
  }

  return rgba(r, g, b, a);
}

static void
box (double x, double y, double w, double h)
{
  push(NULL);
  translate(x, y);

  int wp = min(w * context->canvas->w, context->canvas->w - context->x);
  int hp = min(h * context->canvas->h, context->canvas->h - context->y);

  uint32_t p32 = rgba(context->r, context->g, context->b, context->a);

  for (int j = context->y; j < context->y + hp; j++)
  {
    if (context->a == 255)
    {
      size_t location = context->x + (context->canvas->w * j);
      memset32(&context->canvas->b[location], p32, wp);
    }
    else
    {
      for (int i = context->x; i < context->x + wp; i++)
      {
        size_t location = i + (context->canvas->w * j);
        context->canvas->b[location] = blend(context->canvas->b[location], p32);
      }
    }
  }

  pop();
}

static void
blit (double x, double y, canvas_t *canvas)
{
  push(NULL);
  translate(x, y);

  int wp = min(canvas->w, context->canvas->w - context->x);
  int hp = min(canvas->h, context->canvas->h - context->y);

  for (int j = context->y; j < context->y + hp; j++)
  {
    for (int i = context->x; i < context->x + wp; i++)
    {
      size_t location = i + (context->canvas->w * j);

      context->canvas->b[location] = blend(
        context->canvas->b[location],
        canvas->b[canvas->w * (j-context->y) + (i-context->x)]
      );
    }
  }

  pop();
}

static void
clear (double r, double g, double b, double a)
{
  push(NULL);
  color(r, g, b, a);
  uint32_t p32 = rgba(context->r, context->g, context->b, context->a);
  memset32(context->canvas->b, p32, context->canvas->w * context->canvas->h);
  pop();
}

static void
render (canvas_t *frame)
{
  const size_t offset = vinfo.xoffset * sizeof(int);
  const size_t bytes = min(frame->w * sizeof(int), finfo.line_length);

  for (int j = 0; j < frame->h && j < vinfo.yres; j++)
  {
    size_t location = offset + ((j+vinfo.yoffset) * finfo.line_length);
    memcpy(fb + location, &frame->b[frame->w * j], bytes);
  }
}

static canvas_t*
text (const char *string)
{
  FT_Face       face;
  FT_GlyphSlot  slot;
  FT_Error      error;

  error = FT_New_Face(library, context->font, 0, &face);
  ensure(!error) errorf("FT_New_Face");

  error = FT_Set_Char_Size(face, context->fontsize * 24 * 64, 0, 0, 0);
  ensure(!error) errorf("FT_Set_Char_Size");

  slot = face->glyph;

  int total_width = 0, total_height = face->size->metrics.height/64, offset = 0;
  int width, height, top, ascent, descent, ascent_calc, advance_width, bearing_width, baseline;

  for (const char *s = string; *s; s++)
  {
    FT_Load_Char(face, *s, FT_LOAD_RENDER);
    advance_width = slot->advance.x / 64;
    total_width += advance_width;
  }

  canvas_t *can = canvas_alloc(total_width, total_height);

  push(can);

  baseline = total_height + (face->size->metrics.descender/64);

  for (const char *s = string; *s; s++)
  {
    char sc = *s;
    FT_Load_Char(face, sc, FT_LOAD_RENDER);

    ascent  = 0;
    descent = 0;
    width   = slot->bitmap.width;
    height  = slot->bitmap.rows;
    top     = slot->bitmap_top;
    bearing_width = slot->metrics.horiBearingX / 64;
    advance_width = slot->advance.x / 64;

    descent = (descent < (height - top)) ? height - top: descent;

    ascent_calc = (top < height) ? height: top;
    ascent = (ascent < (ascent_calc - descent)) ? ascent_calc - descent: ascent;

    canvas_t *c = canvas_alloc(width, height);

    push(c);

    for (int j = 0; j < height; j++)
    {
      for (int i = 0; i < width; i++)
      {
        uint8_t pixel = slot->bitmap.buffer[i + j * slot->bitmap.width];
        uint32_t p32 = rgba(context->r, context->g, context->b, pixel);
        c->b[i + j * c->w] = p32;
      }
    }

    pop();

    blit((double)(offset + bearing_width)/(double)total_width, (double)(baseline-ascent)/(double)total_height, c);

    free(c);

    offset += advance_width;
  }

  pop();

  FT_Done_Face(face);

  return can;
}

void
require_args (const char *func, int args)
{
  ensure( lua_gettop(lua) >= args) errorf("%s() expected %d arguments", func, args);
}

int
cb_canvas (lua_State *lua)
{
  require_args(__func__, 2);
  int w = lua_tonumber(lua, -2) * vinfo.xres;
  int h = lua_tonumber(lua, -1) * vinfo.yres;
  lua_pop(lua, 2);
  canvas_t *canvas = lua_newuserdata(lua, canvas_bytes(w, h));
  canvas->w = w;
  canvas->h = h;
  luaL_setmetatable(lua, "canvas_t");
  return 1;
}

int
cb_font (lua_State *lua)
{
  require_args(__func__, 2);
  const char *path = lua_tostring(lua, -2);
  double size = lua_tonumber(lua, -1);
  font(path, size);
  lua_pop(lua, 2);
  return 0;
}

int
cb_text (lua_State *lua)
{
  require_args(__func__, 1);
  const char *string = lua_tostring(lua, -1);
  canvas_t *t = text(string);
  lua_pop(lua, 1);

  canvas_t *canvas = lua_newuserdata(lua, canvas_bytes(t->w, t->h));
  memcpy(canvas, t, canvas_bytes(t->w, t->h));
  free(t);

  luaL_setmetatable(lua, "canvas_t");

  return 1;
}

int
cb_clear (lua_State *lua)
{
  require_args(__func__, 4);
  double r = lua_tonumber(lua, -4);
  double g = lua_tonumber(lua, -3);
  double b = lua_tonumber(lua, -2);
  double a = lua_tonumber(lua, -1);
  lua_pop(lua, 4);
  clear(r, g, b, a);
  return 0;
}

int
cb_push (lua_State *lua)
{
  canvas_t *canvas = NULL;
  if (lua_gettop(lua) > 0)
  {
    canvas = lua_touserdata(lua, -1);
    ensure(canvas) errorf("%s() expected canvas", __func__);
    lua_pop(lua, 1);
  }
  push(canvas);
  return 0;
}

int
cb_pop (lua_State *lua)
{
  pop();
  return 0;
}

int
cb_translate (lua_State *lua)
{
  require_args(__func__, 2);
  double x = lua_tonumber(lua, -2);
  double y = lua_tonumber(lua, -1);
  lua_pop(lua, 2);
  translate(x, y);
  return 0;
}

int
cb_color (lua_State *lua)
{
  require_args(__func__, 4);
  double r = lua_tonumber(lua, -4);
  double g = lua_tonumber(lua, -3);
  double b = lua_tonumber(lua, -2);
  double a = lua_tonumber(lua, -1);
  lua_pop(lua, 4);
  color(r, g, b, a);
  return 0;
}

int
cb_box (lua_State *lua)
{
  require_args(__func__, 4);
  double x = lua_tonumber(lua, -4);
  double y = lua_tonumber(lua, -3);
  double w = lua_tonumber(lua, -2);
  double h = lua_tonumber(lua, -1);
  lua_pop(lua, 4);
  box(x, y, w, h);
  return 0;
}

int cb_blit (lua_State *lua)
{
  require_args(__func__, 3);
  double x = lua_tonumber(lua, -3);
  double y = lua_tonumber(lua, -2);
  canvas_t *canvas = lua_touserdata(lua, -1);
  ensure(canvas) errorf("%s() expected canvas", __func__);
  blit(x, y, canvas);
  lua_pop(lua, 3);
  return 0;
}

int
cb_render (lua_State *lua)
{
  require_args(__func__, 1);
  canvas_t *canvas = lua_touserdata(lua, -1);
  ensure(canvas) errorf("%s() expected canvas", __func__);
  render(canvas);
  lua_pop(lua, 1);
  return 0;
}

int
cb_width (lua_State *lua)
{
  require_args(__func__, 1);
  canvas_t *canvas = lua_touserdata(lua, -1);
  ensure(canvas) errorf("%s() expected canvas", __func__);
  int width = canvas->w;
  lua_pop(lua, 1);
  lua_pushnumber(lua, (double)width/vinfo.xres);
  return 1;
}

int
cb_height (lua_State *lua)
{
  require_args(__func__, 1);
  canvas_t *canvas = lua_touserdata(lua, -1);
  ensure(canvas) errorf("%s() expected canvas", __func__);
  int height = canvas->h;
  lua_pop(lua, 1);
  lua_pushnumber(lua, (double)height/vinfo.yres);
  return 1;
}

int
cb_sleep (lua_State *lua)
{
  require_args(__func__, 1);
  int n = lua_tonumber(lua, -1);
  lua_pop(lua, 1);
  usleep(n*1000000);
  return 0;
}

int
cb_now (lua_State *lua)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  lua_pushnumber(lua, (double)(tv.tv_sec) + ((double)tv.tv_usec / 1000000));
  return 1;
}

int main (int argc, char *argv[])
{
  if (argc > 1) script = argv[1];

  fbfd = open("/dev/fb0", O_RDWR);
  ensure(fbfd != -1) errorf("failed open framebuffer device");

  ensure(ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) != -1) errorf("failed FBIOGET_FSCREENINFO");
  ensure(ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) != -1) errorf("failed FBIOGET_VSCREENINFO");
  ensure(vinfo.bits_per_pixel == 32) errorf("require 32bit color");

  int screensize = vinfo.xres * vinfo.yres * sizeof(int);
  fb = mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
  ensure(fb != MAP_FAILED) errorf("failed mmap framebuffer");

  ensure(FT_Init_FreeType(&library) == 0) errorf("FT_Init_FreeType");

  lua = luaL_newstate();
  luaL_openlibs(lua);

  lua_createtable(lua, 0, 0);

  lua_pushstring(lua, "clear");
  lua_pushcfunction(lua, cb_clear);
  lua_settable(lua, -3);

  lua_pushstring(lua, "canvas");
  lua_pushcfunction(lua, cb_canvas);
  lua_settable(lua, -3);

  lua_pushstring(lua, "font");
  lua_pushcfunction(lua, cb_font);
  lua_settable(lua, -3);

  lua_pushstring(lua, "text");
  lua_pushcfunction(lua, cb_text);
  lua_settable(lua, -3);

  lua_pushstring(lua, "blit");
  lua_pushcfunction(lua, cb_blit);
  lua_settable(lua, -3);

  lua_pushstring(lua, "push");
  lua_pushcfunction(lua, cb_push);
  lua_settable(lua, -3);

  lua_pushstring(lua, "pop");
  lua_pushcfunction(lua, cb_pop);
  lua_settable(lua, -3);

  lua_pushstring(lua, "translate");
  lua_pushcfunction(lua, cb_translate);
  lua_settable(lua, -3);

  lua_pushstring(lua, "color");
  lua_pushcfunction(lua, cb_color);
  lua_settable(lua, -3);

  lua_pushstring(lua, "box");
  lua_pushcfunction(lua, cb_box);
  lua_settable(lua, -3);

  lua_pushstring(lua, "render");
  lua_pushcfunction(lua, cb_render);
  lua_settable(lua, -3);

  lua_pushstring(lua, "canvas_t");
  luaL_newmetatable(lua, "canvas_t");

    lua_pushstring(lua, "width");
    lua_pushcfunction(lua, cb_width);
    lua_settable(lua, -3);

    lua_pushstring(lua, "height");
    lua_pushcfunction(lua, cb_height);
    lua_settable(lua, -3);

    lua_pushstring(lua, "__index");
    lua_pushvalue(lua, -2);
    lua_settable(lua, -3);

  // fb.canvas_t
  lua_settable(lua, -3);
  lua_setglobal(lua, "fb");

  lua_getglobal(lua, "os");
  // os.sleep
  lua_pushstring(lua, "sleep");
  lua_pushcfunction(lua, cb_sleep);
  lua_settable(lua, -3);
  // os.now
  lua_pushstring(lua, "now");
  lua_pushcfunction(lua, cb_now);
  lua_settable(lua, -3);
  lua_pop(lua, 1);

  if (luaL_dofile(lua, script) != 0)
  {
    perror("Error lua");
    perror(lua_tostring(lua, -1));
    exit(6);
  }

  munmap(fb, screensize);
  close(fbfd);

  return 0;
}

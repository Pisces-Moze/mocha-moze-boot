// SPDX-License-Identifier: GPL-2.0-or-later
/* Transparent real vidconsole output over the Moze boot artwork.
 * Serial output is untouched. No boot delay, frame sequence or fake log.
 */
#include <dm.h>
#include <cyclic.h>
#include <malloc.h>
#include <time.h>
#include <video.h>
#include <video_console.h>
#include <video_font.h>
#include "../../../drivers/video/vidconsole_internal.h"
#include "moze-boot-art.h"

struct moze_console {
	/* console_probe and console_simple_get_* require this first field. */
	struct video_fontdata *fontdata;
	struct udevice *dev;
	struct cyclic_info cyclic;
	u8 chars[MOZE_LOG_ROWS][MOZE_LOG_COLS];
	u32 colours[MOZE_LOG_ROWS][MOZE_LOG_COLS];
	u32 ring_palette[16];
	unsigned int phase;
	ulong started;
	bool registered;
};

static void moze_pixel(struct video_priv *v, int x, int y, u32 colour)
{
	int px = x, py = y;
	u8 *dst;

	if (px < 0 || px >= v->xsize || py < 0 || py >= v->ysize)
		return;
	dst = v->fb + py * v->line_length + px * VNBYTES(v->bpix);
	if (v->bpix == VIDEO_BPP16)
		*(u16 *)dst = moze_rgb565(colour);
	else
		*(u32 *)dst = colour;
}

static void moze_cell(struct moze_console *p, unsigned int col, unsigned int row)
{
	struct video_priv *v = dev_get_uclass_priv(p->dev->parent);
	struct video_fontdata *f = p->fontdata;
	u8 ch = p->chars[row][col];
	const u8 *glyph = f->video_fontdata + ch * f->char_pixel_bytes;
	int x, y;

	for (y = 0; y < f->height; y++)
		for (x = 0; x < f->width; x++) {
			int lx = MOZE_LOG_X + col * f->width + x;
			int ly = MOZE_LOG_Y + row * f->height + y;
			bool ink = ch && (glyph[y * f->byte_width + x / 8] & (0x80 >> (x % 8)));
			moze_pixel(v, lx, ly, ink ? p->colours[row][col] :
				   moze_art_pixel(lx, ly, p->phase, 0));
		}
}

static int moze_putc(struct udevice *dev, uint x_frac, uint y, int cp)
{
	struct moze_console *p = dev_get_priv(dev);
	unsigned int col = VID_TO_PIXEL(x_frac) / p->fontdata->width;
	unsigned int row = y / p->fontdata->height;
	struct video_priv *v = dev_get_uclass_priv(dev->parent);

	if (col >= MOZE_LOG_COLS || row >= MOZE_LOG_ROWS)
		return -EAGAIN;
	p->chars[row][col] = console_utf_to_cp437(cp);
	/* Preserve the console's actual ANSI foreground, including normal white. */
	p->colours[row][col] = v->bpix == VIDEO_BPP16 ?
		(((v->colour_fg >> 11) & 31) * 255 / 31 << 16) |
		(((v->colour_fg >> 5) & 63) * 255 / 63 << 8) |
		((v->colour_fg & 31) * 255 / 31) : v->colour_fg;
	moze_cell(p, col, row);
	return VID_TO_POS(p->fontdata->width);
}

static int moze_set_row(struct udevice *dev, uint row, int clr)
{
	struct moze_console *p = dev_get_priv(dev);
	unsigned int col;

	(void)clr;
	if (row >= MOZE_LOG_ROWS)
		return -EINVAL;
	memset(p->chars[row], 0, sizeof(p->chars[row]));
	for (col = 0; col < MOZE_LOG_COLS; col++)
		moze_cell(p, col, row);
	return 0;
}

static int moze_move_rows(struct udevice *dev, uint dst, uint src, uint count)
{
	struct moze_console *p = dev_get_priv(dev);
	unsigned int row, col;

	if (dst > MOZE_LOG_ROWS || src > MOZE_LOG_ROWS ||
	    count > MOZE_LOG_ROWS - dst || count > MOZE_LOG_ROWS - src)
		return -EINVAL;
	memmove(p->chars[dst], p->chars[src], count * sizeof(p->chars[0]));
	memmove(p->colours[dst], p->colours[src], count * sizeof(p->colours[0]));
	for (row = dst; row < dst + count; row++)
		for (col = 0; col < MOZE_LOG_COLS; col++)
			moze_cell(p, col, row);
	return 0;
}

static void moze_tick(struct cyclic_info *cyclic)
{
	struct moze_console *p = container_of(cyclic, struct moze_console, cyclic);
	struct video_priv *v = dev_get_uclass_priv(p->dev->parent);
	struct video_fontdata *f = p->fontdata;
	unsigned int ms = get_timer(p->started), x, y, frame, offset;

	if (ms - p->phase < MOZE_RING_FRAME_MS)
		return;
	p->phase = ms;
	{
		frame = (p->phase / MOZE_RING_FRAME_MS) % MOZE_RING_FRAMES;
		/* Decode the packed alpha directly. Avoid uncached palette reads,
		 * frame division and static-art tests for every individual pixel.
		 * Existing native console ink stays untouched above the ring.
		 */
		for (y = 0; y < MOZE_RING_H; y++) {
			unsigned int py = MOZE_RING_Y + y;
			u8 *dst = v->fb + py * v->line_length +
				  MOZE_RING_X * VNBYTES(v->bpix);

			offset = frame * MOZE_RING_W * MOZE_RING_H + y * MOZE_RING_W;
			for (x = 0; x < MOZE_RING_W; x++) {
				unsigned int px = MOZE_RING_X + x, index = offset + x;
				u8 ch = p->chars[py / f->height][px / f->width];
				u8 alpha = moze_ring_alpha4[index / 2];
				const u8 *glyph = f->video_fontdata + ch * f->char_pixel_bytes;

				if (ch && (glyph[(py % f->height) * f->byte_width +
						(px % f->width) / 8] & (0x80 >> (px % 8))))
					continue;
				alpha = index & 1 ? alpha & 15 : alpha >> 4;
				if (v->bpix == VIDEO_BPP16)
					((u16 *)dst)[x] = p->ring_palette[alpha];
				else
					((u32 *)dst)[x] = p->ring_palette[alpha];
			}
		}
		/* One bounded 64x64 update, made visible as a complete frame.
		 * Force this small damage through the ordinary sync path rather
		 * than waiting for stdout traffic or the 100ms default sync cap.
		 */
#ifdef __arm__
		asm volatile("dsb sy" ::: "memory");
#endif
		video_damage(p->dev->parent, MOZE_RING_X, MOZE_RING_Y,
			     MOZE_RING_W, MOZE_RING_H);
		video_sync(p->dev->parent, true);
	}
}

static int moze_probe(struct udevice *dev)
{
	struct moze_console *p = dev_get_priv(dev);
	struct video_priv *v = dev_get_uclass_priv(dev->parent);
	struct vidconsole_priv *vc = dev_get_uclass_priv(dev);
	unsigned int x, y;
	int ret = console_probe(dev);

	if (ret)
		return ret;
	if (v->xsize != 1536 || v->ysize != 2048 ||
	    (v->bpix != VIDEO_BPP16 && v->bpix != VIDEO_BPP32) ||
	    p->fontdata->width != 16 || p->fontdata->height != 32)
		return -EINVAL;
	p->dev = dev;
	for (x = 0; x < 16; x++) {
		u32 value = x * 17, rgb = (value << 16) | (value << 8) | value;

		p->ring_palette[x] = v->bpix == VIDEO_BPP16 ? moze_rgb565(rgb) : rgb;
	}
	p->started = get_timer(0);
	vc->cols = MOZE_LOG_COLS;
	vc->rows = MOZE_LOG_ROWS;
	vc->xsize_frac = VID_TO_POS(MOZE_LOG_COLS * 16);
	vc->xstart_frac = 0;
	vc->xcur_frac = 0;
	vc->ycur = 0;
	v->colour_bg = v->bpix == VIDEO_BPP16 ? moze_rgb565(MOZE_BG) : MOZE_BG;
	video_clear(dev->parent);
	for (y = MOZE_ART_Y; y < MOZE_ART_Y + MOZE_ART_H; y++)
		for (x = MOZE_ART_X; x < MOZE_ART_X + MOZE_ART_W; x++)
			moze_pixel(v, x, y, moze_art_pixel(x, y, 0, 0));
	for (y = MOZE_BRAND_Y; y < MOZE_BRAND_Y + MOZE_BRAND_H; y++)
		for (x = MOZE_BRAND_X; x < MOZE_BRAND_X + MOZE_BRAND_W; x++)
			moze_pixel(v, x, y, moze_art_pixel(x, y, 0, 0));
	for (y = MOZE_FOOTER_Y; y < MOZE_FOOTER_Y + MOZE_FOOTER_H; y++)
		for (x = MOZE_FOOTER_X; x < MOZE_FOOTER_X + MOZE_FOOTER_W; x++)
			moze_pixel(v, x, y, moze_art_pixel(x, y, 0, 0));
	cyclic_register(&p->cyclic, moze_tick, MOZE_RING_FRAME_MS * 1000,
			"moze-progress-ring");
	p->registered = true;
	return 0;
}

static int moze_remove(struct udevice *dev)
{
	struct moze_console *p = dev_get_priv(dev);
	if (p->registered)
		cyclic_unregister(&p->cyclic);
	return 0;
}

static struct vidconsole_ops moze_ops = {
	.putc_xy = moze_putc,
	.move_rows = moze_move_rows,
	.set_row = moze_set_row,
	.get_font_size = console_simple_get_font_size,
	.get_font = console_simple_get_font,
};

U_BOOT_DRIVER(vidconsole_moze) = {
	.name = "vidconsole_moze",
	.id = UCLASS_VIDEO_CONSOLE,
	.ops = &moze_ops,
	.probe = moze_probe,
	.remove = moze_remove,
	.priv_auto = sizeof(struct moze_console),
};

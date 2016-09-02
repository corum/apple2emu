/*

MIT License

Copyright (c) 2016 Mark Allender


Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#define GLEW_STATIC

#include <GL/glew.h>
#include "SDL.h"
#include "SDL_opengl.h"
#include "SDL_image.h"
#include "6502/memory.h"
#include "6502/video.h"
#include "6502/font.h"
#include "ui/interface.h"

// always render to default size - SDL can scale it up
const static int Video_native_width = 280;
const static int Video_native_height = 192;
const int Video_cell_width = Video_native_width / 40;
const int Video_cell_height = Video_native_height / 24;

static int Video_scale_factor = 4;
static SDL_Rect Video_native_size;
static SDL_Rect Video_window_size;

// information about internally built textures
const int Num_lores_colors = 16;
const int Num_hires_mono_patterns = 128;
const int Num_hires_color_patterns = 256;
const int Hires_texture_width = 8;
const int Num_hires_pattern_variations = 4;
const int Hires_color_texture_width = Hires_texture_width * Num_hires_pattern_variations * 2;  // mult by 2 for odd/even

// textures and pixel storage for mono mode.  There
// are only 128 patterns since we don't need to worry
// about the color bit (high bit)
static GLuint Hires_mono_textures[Num_hires_mono_patterns];
static uint8_t *Hires_mono_pixels[Num_hires_mono_patterns];

// textures and pixel patterns for color mode. there are
// 256 * 3 patterns for color because we have patterns
// that follow the regular pattern.  A complete set
// with left most bit set to white for when this is adjacent
// to another pixel and a set with the right bit as white
// when it is adjacent to another pixel
static GLuint Hires_color_texture;
static uint8_t *Hires_color_pixels;

GLuint Video_framebuffer;
GLuint Video_framebuffer_texture;

SDL_Window *Video_window = nullptr;
SDL_GLContext Video_context;

SDL_TimerID Video_flash_timer;
bool Video_flash = false;
font Video_font, Video_inverse_font;

uint8_t Video_mode = VIDEO_MODE_TEXT;

uint16_t       Video_primary_text_map[MAX_TEXT_LINES];
uint16_t       Video_secondary_text_map[MAX_TEXT_LINES];
uint16_t       Video_hires_map[MAX_TEXT_LINES];
uint16_t       Video_hires_secondary_map[MAX_TEXT_LINES];

static GLfloat Mono_colors[static_cast< uint8_t >(video_display_types::NUM_MONO_TYPES)][3] =
{
	{ 1.0f, 1.0f, 1.0f },
	{ 1.0f, 0.5f, 0.0f },
	{ 0.0f, 0.75f, 0.0f },
};
static GLfloat *Mono_color;
static video_display_types Video_display_mode = video_display_types::MONO_WHITE;

// values for lores colors
// see http://mrob.com/pub/xapple2/colors.html
// for values.  Good enough for a start
static GLubyte Lores_colors[Num_lores_colors][3] =
{
	{ 0x00, 0x00, 0x00 },                 // black
	{ 0xe3, 0x1e, 0x60 },                 // red
	{ 0x96, 0x4e, 0xbd },                 // dark blue
	{ 0xff, 0x44, 0xfd },                 // purple
	{ 0x00, 0xa3, 0x96 },                 // dark green
	{ 0x9c, 0x9c, 0x9c },                 // gray
	{ 0x14, 0xcf, 0xfd },                 // medium blue
	{ 0xd0, 0xce, 0xff },                 // light blue
	{ 0x60, 0x72, 0x03 },                 // brown
	{ 0xff, 0x6a, 0x32 },                 // orange
	{ 0x9c, 0x9c, 0x9c },                 // gray
	{ 0xff, 0xa0, 0xd0 },                 // pink
	{ 0x14, 0xf5, 0x3c },                 // light green
	{ 0xd0, 0xdd, 0x8d },                 // yellow
	{ 0x72, 0xff, 0xd0 },                 // aqua
	{ 0xff, 0xff, 0xff },                 // white
};

static GLubyte Hires_colors[4][3] = {
	//{ 255, 68, 253 },     // violet
	//{ 20, 245, 60 },      // green
	//{ 20, 207, 253 },     // blue
	//{ 255, 106, 60 },     // orange
	{ 227, 20, 255 },     // violet
	{ 27, 211, 79 },      // green
	{ 24, 115, 228 },     // blue
	{ 247, 64, 30 },      // orange
};
//SETFRAMECOLOR(HGR_BLUE, 24, 115, 229); // HCOLOR=6 BLUE    3000: 81 00 D5 AA // 0x00,0x80,0xFF -> Linards Tweaked 0x0D,0xA1,0xFF
//SETFRAMECOLOR(HGR_ORANGE, 247, 64, 30); // HCOLOR=5 ORANGE  2C00: 82 00 AA D5 // 0xF0,0x50,0x00 -> Linards Tweaked 0xF2,0x5E,0x00 
//SETFRAMECOLOR(HGR_GREEN, 27, 211, 79); // HCOLOR=1 GREEN   2400: 02 00 2A 55 // 0x20,0xC0,0x00 -> Linards Tweaked 0x38,0xCB,0x00
//SETFRAMECOLOR(HGR_VIOLET, 227, 20, 255); // HCOLOR=2 VIOLET  2800: 01 00 55 2A // 0xA0,0x00,0xFF -> Linards Tweaked 0xC7,0x34,0xFF

static char character_conv[] = {

	'@', 'A', 'B', 'C', 'D', 'E', 'F', 'G',		/* $00	*/
	'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',		/* $08	*/
	'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',		/* $10	*/
	'X', 'Y', 'Z', '[', '\\',']', '^', '_',		/* $18	*/
	' ', '!', '"', '#', '$', '%', '&', '\'',	   /* $20	*/
	'(', ')', '*', '+', ',', '-', '.', '/',		/* $28	*/
	'0', '1', '2', '3', '4', '5', '6', '7',		/* $30	*/
	'8', '9', ':', ';', '<', '=', '>', '?',		/* $38	*/

	'@', 'A', 'B', 'C', 'D', 'E', 'F', 'G',		/* $40	*/
	'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',		/* $48	*/
	'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',		/* $50	*/
	'X', 'Y', 'Z', '[', '\\',']', '^', '_',		/* $58	*/
	' ', '!', '"', '#', '$', '%', '&', '\'',	   /* $60	*/
	'(', ')', '*', '+', ',', '-', '.', '/',		/* $68	*/
	'0', '1', '2', '3', '4', '5', '6', '7',		/* $70	*/
	'8', '9', ':', ';', '<', '=', '>', '?',		/* $78	*/

	'@', 'A', 'B', 'C', 'D', 'E', 'F', 'G',		/* $80	*/
	'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',		/* $88	*/
	'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',		/* $90	*/
	'X', 'Y', 'Z', '[', '\\',']', '^', '_',		/* $98	*/
	' ', '!', '"', '#', '$', '%', '&', '\'',	   /* $A0	*/
	'(', ')', '*', '+', ',', '-', '.', '/',		/* $A8	*/
	'0', '1', '2', '3', '4', '5', '6', '7',		/* $B0	*/
	'8', '9', ':', ';', '<', '=', '>', '?',		/* $B8	*/

	'@', 'A', 'B', 'C', 'D', 'E', 'F', 'G',		/* $C0	*/
	'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',		/* $C8	*/
	'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',		/* $D0	*/
	'X', 'Y', 'Z', '[', '\\',']', '^', '_',		/* $D8	*/
	' ', 'a', 'b', 'c', 'd', 'e', 'f', 'g',		/* $E0	*/
	'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',		/* $E8	*/
	'p', 'q', 'r', 's', 't', 'u', 'v', 'w',		/* $F0	*/
	'x', 'y', 'z', ';', '<', '=', '>', '?',		/* $F8	*/
};

// callback for flashing characters
static uint32_t timer_flash_callback(uint32_t interval, void *param)
{
	Video_flash = !Video_flash;
	return interval;
}

// create internal texture which will be used for blitting hires pixel
// patterns in high res mode
static bool video_create_hires_textures()
{
	// create monochrome pattern first
	glGenTextures(Num_hires_mono_patterns, Hires_mono_textures);
	for (auto y = 0; y < 128; y++) {
		Hires_mono_pixels[y] = new uint8_t[3 * Hires_texture_width];
		memset(Hires_mono_pixels[y], 0, 3 * Hires_texture_width);
		uint8_t *src = Hires_mono_pixels[y];
		for (auto x = 0; x < Hires_texture_width - 1; x++) {
			if ((y >> x) & 1) {
				*src++ = 0xff;
				*src++ = 0xff;
				*src++ = 0xff;
			} else {
				src += 3;
			}
		}

		// create the texture
		glBindTexture(GL_TEXTURE_2D, Hires_mono_textures[y]);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, Hires_texture_width, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, Hires_mono_pixels[y]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	// create the color patterns.  There are essentially four patterns
	// that we have to worry about.  The color of hires pixels
	// depends on it's neighbors.  Neighbors include pixels from
	// the bytes that are next to the current byte (i.e. byte 0
	// for the right edge of a neighboring pixel an byte 6 for
	// the left edge of a neighboring pixel).  Create 4 sets
	// of 256 textures that take into account the 4 different
	// possible patterns for the neighboring pixels.  Additionally
	// we have to create the same set of textures for even/odd
	// scanline columns. 

	// texture information for color texture for hires pixel patternsi
	// pixel array is:
	// 3 bytes - color information
	// 8 bytes per pattern (really only 7)
	// 4 versions of pattern depending on left/right neighbor
	// 2 versions for odd/even scanline
	// 256 total patterns
	glGenTextures(1, &Hires_color_texture);
	uint32_t texture_size = 3 * Hires_color_texture_width * Num_hires_color_patterns;
	Hires_color_pixels = new uint8_t[texture_size];
	memset(Hires_color_pixels, 0, texture_size);
	uint8_t *pixel = Hires_color_pixels;
	GLubyte *even_color;
	GLubyte *odd_color;

	// 256 patterns total
	for (auto pattern = 0; pattern < Num_hires_color_patterns; pattern++) {

		// loop through potential neighbors of the byte.  We will
		// need left neighbor and right neighbor.
		for (auto column = 0; column < 2; column++) {
			uint8_t even_index = ((pattern & 0x80) ? 2 : 0) + column;
			uint8_t odd_index = ((pattern & 0x80) ? 2 : 0) + ((even_index + 1) % 2);
			even_color = Hires_colors[even_index];
			odd_color = Hires_colors[odd_index];

			// previous bit will come from neighbor to the right
			// of the current byte.  Next bit will be the bit to the
			// right of the current bit, and will eventually
			// extend to the left of the current byte to the neighbor
			// to the left
			for (auto i = 0; i < 4; i++) {
				uint8_t right_neighbor = (i & 1);
				uint8_t left_neighbor = (i >> 1) & 1;

				uint8_t prev_bit = left_neighbor;
				uint8_t next_bit;

				// loop through 7 pixels 
				for (auto x = 0; x < Hires_texture_width - 1; x++) {
					uint8_t cur_bit = (pattern >> x) & 1;
					if (x < Hires_texture_width - 2) {
						next_bit = (pattern >> (x + 1)) & 1;
					} else {
						next_bit = right_neighbor;
					}

					// if current bit is on and either the previous
					// or next bit is on, then this is a white pixel
					if (cur_bit) {
						if (prev_bit || next_bit) {
							pixel[0] = 0xff;
							pixel[1] = 0xff;
							pixel[2] = 0xff;
						} else {
							// check column position of current bit
							if (x & 1) {
								memcpy(pixel, odd_color, 3);
							} else {
								memcpy(pixel, even_color, 3);
							}
						}
					} else if (prev_bit && next_bit) {
						if (((x>0)?(x-1):(x+1)) & 1) {
							memcpy(pixel, odd_color, 3);
						} else {
							memcpy(pixel, even_color, 3);
						}
					}

					pixel += 3;
					prev_bit = cur_bit;
				}

				pixel += 3;   // skip the 8th pixel
			}
		}
	}

	glBindTexture(GL_TEXTURE_2D, Hires_color_texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture_size / Num_hires_color_patterns / 3, Num_hires_color_patterns, 0, GL_RGB, GL_UNSIGNED_BYTE, Hires_color_pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	return true;
}

// renders a text page (primary or secondary)
static void video_render_text_page()
{
	// fow now, just run through the text memory and put out whatever is there
	int x_pixel, y_pixel;
	bool primary = (Video_mode & VIDEO_MODE_PRIMARY) ? true : false;

	y_pixel = 0;
	for (auto y = 0; y < 24; y++) {
		x_pixel = 0;
		font *cur_font;
		for (auto x = 0; x < 40; x++) {
			uint16_t addr = primary ? Video_primary_text_map[y] + x : Video_secondary_text_map[y] + x;  // m_screen_map[row] + col;
			uint8_t c = memory_read(addr);

			// get normal or inverse font
			if (c <= 0x3f) {
				cur_font = &Video_inverse_font;
			} else if ((c <= 0x7f) && (Video_flash == true)) {
				// set inverse if flashing is true
				cur_font = &Video_inverse_font;
			} else {
				cur_font = &Video_font;
			}

			// get character in memory, and then convert to ASCII.  We get character
			// value from memory and then subtract out the first character in our
			// font (as we need to be 0-based from that point).  Then we can get
			// the row/col in the bitmap sheet where the character is
			c = character_conv[c] - cur_font->m_header.m_char_offset;

			glBindTexture(GL_TEXTURE_2D, cur_font->m_texture_id);
			glColor3f(1.0f, 1.0f, 1.0f);
			glBegin(GL_QUADS);
			glTexCoord2f(cur_font->m_char_u[c], cur_font->m_char_v[c]); glVertex2i(x_pixel, y_pixel);
			glTexCoord2f(cur_font->m_char_u[c] + cur_font->m_header.m_cell_u, cur_font->m_char_v[c]);  glVertex2i(x_pixel + Video_cell_width, y_pixel);
			glTexCoord2f(cur_font->m_char_u[c] + cur_font->m_header.m_cell_u, cur_font->m_char_v[c] + cur_font->m_header.m_cell_v); glVertex2i(x_pixel + Video_cell_width, y_pixel + Video_cell_height);
			glTexCoord2f(cur_font->m_char_u[c], cur_font->m_char_v[c] + cur_font->m_header.m_cell_v); glVertex2i(x_pixel, y_pixel + Video_cell_height);
			glEnd();
			glBindTexture(GL_TEXTURE_2D, 0);

			x_pixel += Video_cell_width;
		}
		y_pixel += Video_cell_height;
	}
}

// renders lores graphics mode
static void video_render_lores_mode()
{
	// fow now, just run through the text memory and put out whatever is there
	int x_pixel, y_pixel;
	bool primary = (Video_mode & VIDEO_MODE_PRIMARY) ? true : false;

	y_pixel = 0;
	for (auto y = 0; y < 20; y++) {
		x_pixel = 0;
		for (auto x = 0; x < 40; x++) {
			// get the 2 nibble value of the color.  Blit the corresponding colors
			// to the screen
			uint16_t addr = primary ? Video_primary_text_map[y] + x : Video_secondary_text_map[y] + x;  // m_screen_map[row] + col;
			uint8_t c = memory_read(addr);

			// render top cell
			uint8_t color = c & 0x0f;
			glColor3ub(Lores_colors[color][0], Lores_colors[color][1], Lores_colors[color][2]);
			glBegin(GL_QUADS);
			glVertex2i(x_pixel, y_pixel);
			glVertex2i(x_pixel + Video_cell_width, y_pixel);
			glVertex2i(x_pixel + Video_cell_width, y_pixel + (Video_cell_height / 2));
			glVertex2i(x_pixel, y_pixel + (Video_cell_height / 2));
			glEnd();

			// render bottom cell
			color = (c & 0xf0) >> 4;
			glColor3ub(Lores_colors[color][0], Lores_colors[color][1], Lores_colors[color][2]);
			glBegin(GL_QUADS);
			glVertex2i(x_pixel, y_pixel + (Video_cell_height / 2));
			glVertex2i(x_pixel + Video_cell_width, y_pixel + (Video_cell_height / 2));
			glVertex2i(x_pixel + Video_cell_width, y_pixel + Video_cell_height);
			glVertex2i(x_pixel, y_pixel + Video_cell_height);
			glEnd();

			x_pixel += Video_cell_width;
		}
		y_pixel += Video_cell_height;
	}

	// deal with the rest of the display
	for (auto y = 20; y < 24; y++) {
		x_pixel = 0;
		font *cur_font;
		for (auto x = 0; x < 40; x++) {
			uint16_t addr = primary ? Video_primary_text_map[y] + x : Video_secondary_text_map[y] + x;  // m_screen_map[row] + col;
			uint8_t c = memory_read(addr);

			// get normal or inverse font
			if (c <= 0x3f) {
				cur_font = &Video_inverse_font;
			} else if ((c <= 0x7f) && (Video_flash == true)) {
				// set inverse if flashing is true
				cur_font = &Video_inverse_font;
			} else {
				cur_font = &Video_font;
			}

			// get character in memory, and then convert to ASCII.  We get character
			// value from memory and then subtract out the first character in our
			// font (as we need to be 0-based from that point).  Then we can get
			// the row/col in the bitmap sheet where the character is
			c = character_conv[c] - cur_font->m_header.m_char_offset;

			glBindTexture(GL_TEXTURE_2D, cur_font->m_texture_id);
			glColor3f(1.0f, 1.0f, 1.0f);
			glBegin(GL_QUADS);
			glTexCoord2f(cur_font->m_char_u[c], cur_font->m_char_v[c]); glVertex2i(x_pixel, y_pixel);
			glTexCoord2f(cur_font->m_char_u[c] + cur_font->m_header.m_cell_u, cur_font->m_char_v[c]);  glVertex2i(x_pixel + Video_cell_width, y_pixel);
			glTexCoord2f(cur_font->m_char_u[c] + cur_font->m_header.m_cell_u, cur_font->m_char_v[c] + cur_font->m_header.m_cell_v); glVertex2i(x_pixel + Video_cell_width, y_pixel + Video_cell_height);
			glTexCoord2f(cur_font->m_char_u[c], cur_font->m_char_v[c] + cur_font->m_header.m_cell_v); glVertex2i(x_pixel, y_pixel + Video_cell_height);
			glEnd();
			glBindTexture(GL_TEXTURE_2D, 0);

			x_pixel += Video_cell_width;
		}
		y_pixel += Video_cell_height;
	}
}

static void video_render_hires_mode()
{
	bool primary = (Video_mode & VIDEO_MODE_PRIMARY) ? true : false;
	uint16_t offset;

	bool mixed = (Video_mode & VIDEO_MODE_MIXED) ? true : false;
	int y_end = 0;
	if (mixed == true) {
		y_end = 20;
	} else {
		y_end = 24;
	}

	// do the stupid easy thing first.  Just plow through
	// the memory and put the pixels on the screen
	int x_pixel;
	int y_pixel;

	for (int y = 0; y < y_end; y++) {
		offset = primary ? Video_hires_map[y] : Video_hires_secondary_map[y];
		uint32_t odd = 0;
		for (int x = 0; x < 40; x++) {
			y_pixel = y * 8;
			for (int b = 0; b < 8; b++) {
				x_pixel = x * 7;
				uint32_t memory_loc = offset + (1024 * b) + x;
				uint8_t byte = memory_read(memory_loc);
				uint8_t left_neighbor = ((x > 0 ? memory_read(memory_loc - 1) : 0) >> 6) & 1;
				uint8_t right_neighbor = (x < 39 ? memory_read(memory_loc + 1) : 0) & 1;
				if ((Video_display_mode != video_display_types::COLOR) && byte) {
					// mono mode - we don't care about the high bit in the display byte
					byte &= 0x7f;
					glBindTexture(GL_TEXTURE_2D, Hires_mono_textures[byte]);
					glColor3f(1.0f, 1.0f, 1.0f);
					glBegin(GL_QUADS);
					glTexCoord2f(0.0f, 0.0f); glVertex2i(x_pixel, y_pixel);
					glTexCoord2f(1.0f, 0.0f); glVertex2i(x_pixel + 8, y_pixel);
					glTexCoord2f(1.0f, 1.0f); glVertex2i(x_pixel + 8, y_pixel + 1);
					glTexCoord2f(0.0f, 1.0f); glVertex2i(x_pixel, y_pixel + 1);
					glEnd();
				} else if (byte) {
					float u, v;

					uint32_t column =  ((odd&1) ? 4 : 0) + (left_neighbor * 2 + right_neighbor);
					u = (column * 8.0f) / (float)Hires_color_texture_width;
					v = byte / (float)Num_hires_color_patterns;
					glBindTexture(GL_TEXTURE_2D, Hires_color_texture);
					glColor3f(1.0f, 1.0f, 1.0f);
					glBegin(GL_QUADS);
					glTexCoord2f(u, v); glVertex2i(x_pixel, y_pixel);
					glTexCoord2f(u + (7.0f / (float)Hires_color_texture_width), v); glVertex2i(x_pixel + 7, y_pixel);
					glTexCoord2f(u + (7.0f / (float)Hires_color_texture_width), v + (1.0f / (float)Num_hires_color_patterns)); glVertex2i(x_pixel + 7, y_pixel + 1);
					glTexCoord2f(u, v + (1.0f / (float)Num_hires_color_patterns)); glVertex2i(x_pixel, y_pixel + 1);
					glEnd();
				}
				y_pixel++;
			}
			odd++;
		}
	}

	// deal with the rest of the display
	glColor3f(1.0f, 1.0f, 1.0f);
	if (mixed) {
		for (auto y = 20; y < 24; y++) {
			x_pixel = 0;
			font *cur_font;
			for (auto x = 0; x < 40; x++) {
				uint16_t addr = primary ? Video_primary_text_map[y] + x : Video_secondary_text_map[y] + x;  // m_screen_map[row] + col;
				uint8_t c = memory_read(addr);

				// get normal or inverse font
				if (c <= 0x3f) {
					cur_font = &Video_inverse_font;
				} else if ((c <= 0x7f) && (Video_flash == true)) {
					// set inverse if flashing is true
					cur_font = &Video_inverse_font;
				} else {
					cur_font = &Video_font;
				}

				// get character in memory, and then convert to ASCII.  We get character
				// value from memory and then subtract out the first character in our
				// font (as we need to be 0-based from that point).  Then we can get
				// the row/col in the bitmap sheet where the character is
				c = character_conv[c] - cur_font->m_header.m_char_offset;

				glBindTexture(GL_TEXTURE_2D, cur_font->m_texture_id);
				glBegin(GL_QUADS);
				glTexCoord2f(cur_font->m_char_u[c], cur_font->m_char_v[c]); glVertex2i(x_pixel, y_pixel);
				glTexCoord2f(cur_font->m_char_u[c] + cur_font->m_header.m_cell_u, cur_font->m_char_v[c]);  glVertex2i(x_pixel + Video_cell_width, y_pixel);
				glTexCoord2f(cur_font->m_char_u[c] + cur_font->m_header.m_cell_u, cur_font->m_char_v[c] + cur_font->m_header.m_cell_v); glVertex2i(x_pixel + Video_cell_width, y_pixel + Video_cell_height);
				glTexCoord2f(cur_font->m_char_u[c], cur_font->m_char_v[c] + cur_font->m_header.m_cell_v); glVertex2i(x_pixel, y_pixel + Video_cell_height);
				glEnd();
				glBindTexture(GL_TEXTURE_2D, 0);

				x_pixel += Video_cell_width;
			}
			y_pixel += Video_cell_height;
		}
	}
}

static uint8_t video_read_handler(uint16_t addr)
{
	uint8_t a = addr & 0xff;

	// switch based on the address to set the video modes
	switch (a) {
	case 0x50:
		Video_mode &= ~VIDEO_MODE_TEXT;
		break;
	case 0x51:
		Video_mode |= VIDEO_MODE_TEXT;
		break;
	case 0x52:
		Video_mode &= ~VIDEO_MODE_MIXED;
		break;
	case 0x53:
		Video_mode |= VIDEO_MODE_MIXED;
		break;
	case 0x54:
		Video_mode |= VIDEO_MODE_PRIMARY;
		break;
	case 0x55:
		Video_mode &= ~VIDEO_MODE_PRIMARY;
		break;
	case 0x56:
		Video_mode &= ~VIDEO_MODE_HIRES;
		break;
	case 0x57:
		Video_mode |= VIDEO_MODE_HIRES;
		break;
	}

	return 0;
}

static void video_write_handler(uint16_t addr, uint8_t value)
{
	uint8_t a = addr & 0xff;

	// switch based on the address to set the video modes
	switch (a) {
	case 0x50:
		Video_mode &= ~VIDEO_MODE_TEXT;
		break;
	case 0x51:
		Video_mode |= VIDEO_MODE_TEXT;
		break;
	case 0x52:
		Video_mode &= ~VIDEO_MODE_MIXED;
		break;
	case 0x53:
		Video_mode |= VIDEO_MODE_MIXED;
		break;
	case 0x54:
		Video_mode |= VIDEO_MODE_PRIMARY;
		break;
	case 0x55:
		Video_mode &= ~VIDEO_MODE_PRIMARY;
		break;
	case 0x56:
		Video_mode &= ~VIDEO_MODE_HIRES;
		break;
	case 0x57:
		Video_mode |= VIDEO_MODE_HIRES;
		break;
	}
}

bool video_create()
{
	// set the rect for the window itself
	Video_window_size.x = 0;
	Video_window_size.y = 0;
	Video_window_size.w = ( int )(Video_scale_factor * Video_native_size.w);
	Video_window_size.h = ( int )(Video_scale_factor * Video_native_size.h);

	// create SDL window
	if (Video_window != nullptr) {
		SDL_DestroyWindow(Video_window);
	}

	// set attributes for GL
	uint32_t sdl_window_flags = SDL_WINDOW_RESIZABLE;
	//SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	//SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	sdl_window_flags |= SDL_WINDOW_OPENGL;

	Video_window = SDL_CreateWindow("Apple2Emu", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, ( int )(Video_native_size.w * Video_scale_factor), ( int )(Video_native_size.h * Video_scale_factor), sdl_window_flags);
	if (Video_window == nullptr) {
		printf("Unable to create SDL window: %s\n", SDL_GetError());
		return false;
	}

	// create openGL context
	if (Video_context != nullptr) {
		SDL_GL_DeleteContext(Video_context);
	}
	Video_context = SDL_GL_CreateContext(Video_window);

	if (Video_context == nullptr) {
		printf("Unable to create GL context: %s\n", SDL_GetError());
		return false;
	}
	glewExperimental = GL_TRUE;
	glewInit();

	// framebuffer and render to texture
	glGenFramebuffers(1, &Video_framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, Video_framebuffer);

	glGenTextures(1, &Video_framebuffer_texture);
	glBindTexture(GL_TEXTURE_2D, Video_framebuffer_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, Video_native_width, Video_native_height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// attach texture to framebuffer
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Video_framebuffer_texture, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("Unable to create framebuffer for render to texture.\n");
		return false;
	}

	if (Video_font.load("apple2.tga") == false) {
		return false;
	}
	if (Video_inverse_font.load("apple2_inverted.tga") == false) {
		return false;
	}

	if (video_create_hires_textures() == false) {
		return false;
	}

	return true;
}

// intialize the SDL system
bool video_init()
{
	if (Video_window == nullptr) {
		Video_native_size.x = 0;
		Video_native_size.y = 0;
		Video_native_size.w = Video_native_width;
		Video_native_size.h = Video_native_height;

		if (video_create() == false) {
			return false;
		}

		// set up a timer for flashing cursor
		Video_flash = false;
		Video_flash_timer = SDL_AddTimer(250, timer_flash_callback, nullptr);

		// set up screen map for video output.  This per/row
		// table gets starting memory address for that row of text
		//
		// algorithm here pulled from Apple 2 Monitors Peeled, pg 15
		for (int i = 0; i < MAX_TEXT_LINES; i++) {
			Video_primary_text_map[i] = 1024 + 256 * ((i / 2) % 4) + (128 * (i % 2)) + 40 * ((i / 8) % 4);
			Video_secondary_text_map[i] = 2048 + 256 * ((i / 2) % 4) + (128 * (i % 2)) + 40 * ((i / 8) % 4);

			// set up the hires map at the same time -- same number of lines just offset by fixed amount
			Video_hires_map[i] = 0x1c00 + Video_primary_text_map[i];
			Video_hires_secondary_map[i] = Video_hires_map[i] + 0x2000;
		}

		video_set_mono_type(video_display_types::MONO_WHITE);

		// initialize UI here since we need the window handle for imgui
		ui_init(Video_window);

	}

	for (auto i = 0x50; i <= 0x57; i++) {
		memory_register_c000_handler(i, video_read_handler, video_write_handler);
	}

	return true;
}

void video_shutdown()
{
	ui_shutdown();
	// free the pixels used for the hires texture patterns
	for (auto i = 0; i < Num_hires_mono_patterns; i++) {
		delete Hires_mono_pixels[i];
	}
	delete Hires_color_pixels;
	SDL_GL_DeleteContext(Video_context);
	SDL_DestroyWindow(Video_window);
}

bool Debug_show_bitmap = false;

void video_render_frame()
{
	// render to the framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, Video_framebuffer);
	glViewport(0, 0, Video_native_width, Video_native_height);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	glDisable(GL_DEPTH_TEST);
	glDepthMask(false);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0f, ( float )Video_native_width, ( float )Video_native_height, 0.0f, 0.0f, 1.0f);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glEnable(GL_TEXTURE_2D);

	if (Video_mode & VIDEO_MODE_TEXT) {
		video_render_text_page();
	} else if (!(Video_mode & VIDEO_MODE_HIRES)) {
		video_render_lores_mode();
	} else if (Video_mode & VIDEO_MODE_HIRES) {
		video_render_hires_mode();
	}


	// back to main framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, Video_window_size.w, Video_window_size.h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	if (Video_display_mode != video_display_types::COLOR) {
		glColor3fv(Mono_color);
	}

	// render texture to window.  Just render the texture to the 
	// full screen (using normalized device coordinates)
	glBindTexture(GL_TEXTURE_2D, Video_framebuffer_texture);
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
	glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, -1.0f);
	glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
	glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, 1.0f);
	glEnd();
	glBindTexture(GL_TEXTURE_2D, 0);

	if (Debug_show_bitmap) {
		glBindTexture(GL_TEXTURE_2D, Hires_color_texture);
		glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
		glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, -1.0f);
		glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
		glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, 1.0f);
		glEnd();
	}

	ui_do_frame(Video_window);

	SDL_GL_SwapWindow(Video_window);
}

void video_set_mono_type(video_display_types type)
{
	Video_display_mode = type;
	Mono_color = Mono_colors[static_cast< uint8_t >(type)];
}


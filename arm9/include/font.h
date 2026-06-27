#pragma once
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Maps a contiguous Unicode range to generated glyph indices.
 */
typedef struct {
    uint16_t start;
    uint16_t end;
    uint16_t first_glyph;
} FontRange;

/**
 * @brief Describes one generated bitmap font and its lookup tables.
 */
typedef struct {
    uint8_t cell_w;
    uint8_t cell_h;
    uint8_t bytes_per_glyph;
    int8_t tracking;
    uint16_t glyph_count;
    uint16_t fallback_glyph;
    const uint8_t *metrics;
    const uint8_t *bits;
    const FontRange *ranges;
    uint16_t range_count;
    const uint16_t *direct;
    const uint16_t *direct_glyphs;
    uint16_t direct_count;
} Font;

#define FONT_LEFT       0u
#define FONT_CENTER     1u
#define FONT_RIGHT      2u
#define FONT_ALIGN_MASK 3u

extern const Font font_8x8;

/**
 * @brief Resolves a Unicode codepoint to a glyph index in a generated font.
 *
 * @return Glyph index, or the font fallback glyph when no direct mapping exists.
 */
int font_find_glyph(const Font *font, uint32_t codepoint);

/** @brief Returns the horizontal advance for one glyph including tracking. */
uint16_t font_glyph_advance(const Font *font, uint16_t glyph, unsigned flags);

/**
 * @brief Draws one glyph into a 16-bit RGB555 framebuffer.
 *
 * @return Advance in pixels, or 0 if the font/framebuffer arguments are invalid.
 */
int font_draw_glyph_rgb555(uint16_t *fb, int fb_w, int fb_h, int x, int y,
                         const Font *font, uint16_t glyph, uint16_t color,
                         unsigned flags);

/**
 * @brief Draws a UTF-8 string into a 16-bit RGB555 framebuffer.
 *
 * Invalid UTF-8 sequences are rendered with the font fallback glyph.
 *
 * @return Width advanced in pixels.
 */
int font_draw_utf8_rgb555(uint16_t *fb, int fb_w, int fb_h, int x, int y,
                        const Font *font, const char *utf8, uint16_t color,
                        unsigned flags);

/** @brief Measures a UTF-8 string using the same glyph selection as drawing. */
int font_measure_utf8(const Font *font, const char *utf8, unsigned flags);

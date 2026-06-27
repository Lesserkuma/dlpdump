/**
 * @file font.c
 * @brief Looks up generated glyphs and renders UTF-8 text to RGB555 framebuffers.
 */
#include "font.h"
#include "text.h"

#include <string.h>

/**
 * @brief Returns the replacement codepoint used for unsupported glyphs.
 */
static uint32_t replacement_cp(void) {
    return TEXT_REPLACEMENT_CODEPOINT;
}

/**
 * @brief Finds the bitmap glyph for a Unicode codepoint.
 */
int font_find_glyph(const Font *font, uint32_t codepoint) {
    if (!font) return -1;
    if (codepoint > 0xffffu) codepoint = replacement_cp();
    uint16_t cp = (uint16_t)codepoint;
    for (uint16_t i = 0; i < font->range_count; i++) {
        const FontRange *r = &font->ranges[i];
        if (cp >= r->start && cp <= r->end) return (int)r->first_glyph + (int)(cp - r->start);
    }
    uint16_t lo = 0, hi = font->direct_count;
    while (lo < hi) {
        uint16_t mid = (uint16_t)(lo + ((hi - lo) >> 1));
        uint16_t v = font->direct[mid];
        if (cp < v) hi = mid;
        else if (cp > v) lo = (uint16_t)(mid + 1);
        else {
            uint16_t glyph = font->direct_glyphs ? font->direct_glyphs[mid] : font->glyph_count;
            if (glyph < font->glyph_count) return (int)glyph;
            break;
        }
    }
    if (cp != (uint16_t)replacement_cp()) {
        int g = font_find_glyph(font, replacement_cp());
        if (g >= 0) return g;
    }
    return font->fallback_glyph < font->glyph_count ? (int)font->fallback_glyph : -1;
}

/**
 * @brief Returns the horizontal advance for a rendered glyph.
 */
uint16_t font_glyph_advance(const Font *font, uint16_t glyph, unsigned flags) {
    (void)flags;
    if (!font || glyph >= font->glyph_count) return 0;
    const uint8_t *m = font->metrics + glyph * 2u;
    int advance = (int)m[1] + (int)font->tracking;
    return advance > 0 ? (uint16_t)advance : 0;
}

/**
 * @brief Draws one font glyph into an RGB555 framebuffer.
 */
int font_draw_glyph_rgb555(uint16_t *fb, int fb_w, int fb_h, int x, int y,
                         const Font *font, uint16_t glyph, uint16_t color,
                         unsigned flags) {
    (void)flags;
    if (!fb || !font || glyph >= font->glyph_count || fb_w <= 0 || fb_h <= 0) return 0;
    const uint8_t *m = font->metrics + glyph * 2u;
    const int src_left = m[0];
    int width = m[1];
    if (width <= 0 || src_left >= font->cell_w) return font_glyph_advance(font, glyph, flags);
    if (src_left + width > font->cell_w) width = font->cell_w - src_left;
    if (width <= 0) return font_glyph_advance(font, glyph, flags);

    int sx0 = 0, sx1 = width, sy0 = 0, sy1 = font->cell_h;
    int draw_x = x;
    if (draw_x < 0) sx0 = -draw_x;
    if (draw_x + sx1 > fb_w) sx1 = fb_w - draw_x;
    if (y < 0) sy0 = -y;
    if (y + sy1 > fb_h) sy1 = fb_h - y;
    if (sx0 < sx1 && sy0 < sy1) {
        const uint8_t *glyph_bits = font->bits + (unsigned)glyph * font->bytes_per_glyph;
        for (int row = sy0; row < sy1; row++) {
            uint16_t *dst = fb + (y + row) * fb_w + draw_x + sx0;
            unsigned bitpos = (unsigned)row * font->cell_w + (unsigned)src_left + (unsigned)sx0;
            for (int col = sx0; col < sx1; col++, bitpos++, dst++) {
                const uint8_t src = glyph_bits[bitpos >> 3];
                const uint8_t mask = (uint8_t)(1u << (bitpos & 7));
                if ((src & mask) == 0) *dst = color;
            }
        }
    }
    return font_glyph_advance(font, glyph, flags);
}

/**
 * @brief Draws a UTF-8 string into an RGB555 framebuffer.
 */
int font_draw_utf8_rgb555(uint16_t *fb, int fb_w, int fb_h, int x, int y,
                        const Font *font, const char *utf8, uint16_t color,
                        unsigned flags) {
    if (!font || !utf8) return x;
    int origin_x = x;
    unsigned align = flags & FONT_ALIGN_MASK;
    if (align == FONT_CENTER) x -= font_measure_utf8(font, utf8, flags) / 2;
    else if (align == FONT_RIGHT) x -= font_measure_utf8(font, utf8, flags);
    origin_x = x;
    const char *s = utf8; uint32_t cp;
    while (text_utf8_next(&s, &cp)) {
        if (cp == '\n') break;
        if (cp == '\r') continue;
        if (cp == '\t') cp = ' ';
        int glyph = font_find_glyph(font, cp);
        if (glyph >= 0) x += font_draw_glyph_rgb555(fb, fb_w, fb_h, x, y, font, (uint16_t)glyph, color, flags);
    }
    return x - origin_x;
}

/**
 * @brief Measures the rendered width of a UTF-8 string.
 */
int font_measure_utf8(const Font *font, const char *utf8, unsigned flags) {
    if (!font || !utf8) return 0;
    int x = 0, max_x = 0; const char *s = utf8; uint32_t cp;
    while (text_utf8_next(&s, &cp)) {
        if (cp == '\n') {
            if (x > max_x) max_x = x;
            x = 0;
            continue;
        }
        if (cp == '\r') continue;
        if (cp == '\t') cp = ' ';
        int glyph = font_find_glyph(font, cp);
        if (glyph >= 0) x += font_glyph_advance(font, (uint16_t)glyph, flags);
    }
    if (x > max_x) max_x = x;
    return max_x;
}

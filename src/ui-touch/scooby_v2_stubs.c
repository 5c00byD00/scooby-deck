// Scooby Deck v2: stand-ins for the WadaMesh font and emoji data that v2 leaves
// out of the build (platformio.ini build_src_filter). The UI is unscii-8 text;
// the Noto fallbacks (accents, Cyrillic, Greek, Arabic, symbols) and the colour
// emoji glyphs are gone, so these "fonts" have no glyphs at all and the emoji
// lookups find nothing. The large Montserrat sizes are aliased to the 16 px one
// in platformio.ini (-Wl,--defsym). GPL-3.0-or-later.
#if defined(SCOOBY_V2)
#include "lvgl.h"
#include "emoji_data.h"

static bool v2NoGlyph(const lv_font_t* f, lv_font_glyph_dsc_t* d, uint32_t cp, uint32_t next) {
  (void)f; (void)d; (void)cp; (void)next;
  return false;
}
static const uint8_t* v2NoBitmap(const lv_font_t* f, uint32_t cp) { (void)f; (void)cp; return NULL; }

#define V2_EMPTY_FONT(nm, lh)                                                  \
  const lv_font_t nm = {                                                       \
    .get_glyph_dsc = v2NoGlyph, .get_glyph_bitmap = v2NoBitmap,                \
    .line_height = lh, .base_line = 3, .subpx = LV_FONT_SUBPX_NONE,            \
    .underline_position = -1, .underline_thickness = 1,                        \
    .dsc = NULL, .fallback = NULL, .user_data = NULL };

V2_EMPTY_FONT(extras_12, 15)
V2_EMPTY_FONT(extras_14, 17)
V2_EMPTY_FONT(extras_16, 19)
V2_EMPTY_FONT(extras_20, 24)
V2_EMPTY_FONT(extras_24, 28)
V2_EMPTY_FONT(extras_lat_18, 21)
V2_EMPTY_FONT(extras_lat_20, 24)
V2_EMPTY_FONT(extras_lat_24, 28)
V2_EMPTY_FONT(extras_lat_28, 32)

const lv_img_dsc_t* emojiGlyphLookup(uint32_t cp) { (void)cp; return NULL; }
const lv_img_dsc_t* emojiGlyphSequenceLookup(const char* utf8) { (void)utf8; return NULL; }
const uint16_t kEmojiGlyphCount = 0;
#endif

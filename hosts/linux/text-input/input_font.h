#ifndef POCKET_INPUT_FONT_H
#define POCKET_INPUT_FONT_H
#include <stddef.h>
#include <stdint.h>
/* Private glyph references, never public engine handles. Slot 1 is loaded only
 * for a package that explicitly admits offline input and supplies this atlas. */
#define POCKET_INPUT_GLYPH_BASE 65536u
#define POCKET_INPUT_GLYPH_MAX 131071u
#define POCKET_INPUT_FONT_MAX (13u*1024u*1024u)
int pocket_input_font_valid(const void *data,size_t bytes);
int pocket_input_font_has(const void *data,size_t bytes,uint32_t codepoint);
#endif

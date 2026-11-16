/* Standalone CJF font decoder for EX-word. No gint dependency.
 * Big-endian headers, linked ranges, raw/indexed glyph data, nibble/byte RLE.
 * Based on the supplied cjf.py; no Unicode character mapping required. */
#ifndef CASIO_CJF_H
#define CASIO_CJF_H
#include <stdint.h>
#include <stddef.h>

/* Independent of bitmap reader: CJF can be upstreamed alone. */
typedef int (*cjf_read_at)(void *context, uint32_t offset,
    void *buffer, size_t length);
typedef struct {
    cjf_read_at read;
    void *user;
    uint32_t size;
} cjf_stream_t;
#define CJF_MAX_RANGES 24

typedef struct {
    uint32_t offset, first, last, data_offset, count; /* physical glyph slots */
    uint16_t width, height, glyph_bytes, variant;
    uint8_t encoding, mapping, indexed;
} cjf_range_t;
typedef struct {
    cjf_stream_t file;
    uint16_t nominal_size;
    int range_count;
    cjf_range_t ranges[CJF_MAX_RANGES];
} cjf_font_t;

/* Return 0 on success, -1 malformed/out of bounds, -2 I/O.
 * Glyph previews enumerate physical slots independently of codepage mapping.
 * Encoding may be unknown (eg encoding 9 in sb24f.cjf). */
int cjf_open(cjf_stream_t const *file, cjf_font_t *font);
int cjf_read_glyph(cjf_font_t const *font, int range_index,
    uint32_t glyph_index, uint8_t *bitmap, size_t capacity);
#endif

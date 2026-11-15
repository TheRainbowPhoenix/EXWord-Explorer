/* Standalone CJF font decoder for EX-word. No gint dependency.
 * Big-endian headers, linked ranges, raw/indexed glyph data, nibble/byte RLE.
 * Based on the supplied cjf.py; no Unicode character mapping required. */
#ifndef CASIO_CJF_H
#define CASIO_CJF_H
#include "casio_bitmap.h"
#include <stdint.h>
#include <stddef.h>
#define CJF_MAX_RANGES 24

typedef struct {
    uint32_t offset, first, last, data_offset, count;
    uint16_t width, height, glyph_bytes, variant;
    uint8_t encoding, mapping, indexed;
} cjf_range_t;
typedef struct {
    casio_stream_t file;
    uint16_t nominal_size;
    int range_count;
    cjf_range_t ranges[CJF_MAX_RANGES];
} cjf_font_t;

/* Return 0 on success, -1 malformed/out of bounds, -2 I/O, -3 unknown
 * character encoding (explicit glyph indices can be used instead). */
int cjf_open(casio_stream_t const *file, cjf_font_t *font);
int cjf_read_glyph(cjf_font_t const *font, int range_index,
    uint32_t glyph_index, uint8_t *bitmap, size_t capacity);
#endif

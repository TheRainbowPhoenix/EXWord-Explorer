/* Standalone Casio packed bitmap decoder. No gint dependency.
 * Matches bitmap.py (Casio BIN, NOT Windows BMP).
 * All fields BE; pixels output as RGB565. Caller owns all buffers. */
#ifndef CASIO_BITMAP_H
#define CASIO_BITMAP_H
#include <stdint.h>
#include <stddef.h>

typedef int (*casio_read_at)(void *user, uint32_t offset,
    void *buffer, size_t length); /* 0 success; nonzero failure */
typedef void (*casio_plot565)(void *user, uint16_t x, uint16_t y,
    uint16_t color);

typedef struct {
    casio_read_at read;
    void *user;
    uint32_t size;
} casio_stream_t;

typedef struct {
    uint16_t width, height, palette[16];
    uint8_t format, colors;
    uint32_t data_offset, stride;
} casio_bitmap_t;

/* 0 success; -1 invalid/truncated; -2 I/O or insufficient row buffer. */
int casio_bitmap_open(casio_stream_t const *file, casio_bitmap_t *bmp);
int casio_bitmap_render(casio_stream_t const *file,
    casio_bitmap_t const *bmp, uint8_t *row, size_t row_size,
    casio_plot565 plot, void *user);
/* Factory test format: repeated (u8 count, BE RGB565), explicit dims. */
int casio_bitmap_render_rle565(casio_stream_t const *file,
    uint16_t width, uint16_t height, casio_plot565 plot, void *user);
#endif

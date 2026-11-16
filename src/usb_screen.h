#ifndef EXPLORER_USB_SCREEN_H
#define EXPLORER_USB_SCREEN_H
#include <stdint.h>
#include "explorer_fs.h"
typedef struct {
    char path[EX_PATH_MAX];
    uint32_t total_bytes, completed, chunk, chunks;
    int is_write, active, total_known;
} usb_screen_transfer_t;
void usb_screen_draw(const char *detail, int served,
    usb_screen_transfer_t const *transfer);
#endif

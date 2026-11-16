#ifndef EXPLORER_USB_H
#define EXPLORER_USB_H

#include <stdint.h>
#include "explorer_fs.h"
/* USB and local previews are mutually exclusive. They share one workspace
 * so the EX-word linker can preserve its mandatory 4 KiB heap. */
#define EXFS_DATA_MAX 2048
#define EXFS_PACKET (EXFS_DATA_MAX + EX_PATH_MAX + 16)
extern uint8_t ex_shared_io[EXFS_PACKET];

/* Modal, native EX-word USB remote filesystem service.
   Returns to the volume selector when ESC or AC/ON is pressed. */
void ex_usb_remote_screen(void);

#endif

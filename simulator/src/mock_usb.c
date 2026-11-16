/* Actual USB page renderer, driven by mock progress instead of hardware. */
#include "usb_screen.h"
#include "explorer_usb.h"
uint8_t ex_shared_io[EXFS_PACKET];
#include <stdio.h>
extern void sim_capture_named(const char *name);
void ex_usb_remote_screen(void)
{
    usb_screen_transfer_t demo={0};
    demo.active=1;
    demo.is_write=0;
    demo.total_known=1;
    demo.total_bytes=61440;
    demo.completed=24576;
    demo.chunk=12;
    demo.chunks=30;
    snprintf(demo.path,sizeof demo.path,"\\\\drv0\\Documents\\big_file.bin");
    usb_screen_draw("Connected to computer",173,&demo);
    sim_capture_named("usb-remote");
}

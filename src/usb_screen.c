#include "usb_screen.h"
#include <gint/display.h>
#include <stdio.h>
#include <string.h>

/* Version-3, error-correction M QR for http://pho3.be/go/exxplorer,
   generated offline. One 29-bit scanline per uint32_t. */
static const uint32_t qr_rows[29] = {
    0x1fcc4e7fu,
    0x10411c41u,
    0x17426c5du,
    0x174d975du,
    0x174e0c5du,
    0x10583f41u,
    0x1fd5557fu,
    0x000dbc00u,
    0x12d053a0u,
    0x04a7bdc9u,
    0x17c5ef0eu,
    0x03b393feu,
    0x107a6acbu,
    0x1930fba4u,
    0x0ec9c9c7u,
    0x16902f2au,
    0x00f18b2au,
    0x0b14f4c1u,
    0x157d7cabu,
    0x05894b13u,
    0x16d02ff4u,
    0x00156317u,
    0x1fc61952u,
    0x10516b15u,
    0x174e11f8u,
    0x1758ab71u,
    0x174aff11u,
    0x1040df22u,
    0x1fd2b5cau
};
static void qr_draw(void)
{
    /* 3 px/modules with a quiet-zone margin; merge horizontal runs so
       displaying the QR needs far fewer draw calls. */
    drect(18,57,149,191,C_WHITE);
    for(int y=0;y<29;y++) {
        int x=0;
        while(x<29) {
            if(!(qr_rows[y] & (1u << (28-x)))) { x++; continue; }
            int first=x;
            do { x++; } while(x<29 && (qr_rows[y] & (1u << (28-x))));
            drect(41+3*first,81+3*y,41+3*x-1,83+3*y,C_BLACK);
        }
    }
}
void usb_screen_draw(const char *detail, int count,
    usb_screen_transfer_t const *current)
{
    char label[96], shown[57];
    dclear(C_WHITE);
    drect(0,0,527,38,C_RGB(4,12,23));
    dtext(14,13,C_WHITE,"USB REMOTE");
    snprintf(label,sizeof label,"%d req",count);
    dtext(447,13,C_WHITE,label);

    qr_draw();
    dtext(25,203,C_DARK,"Scan to set up");

    dtext(171,53,C_DARK,"CONNECTION");
    dtext(171,73,C_BLACK,detail);
    dtext(171,105,C_DARK,"PC TFTP ADDRESS");
    dtext(171,125,C_BLACK,"127.0.0.1:6969");
    dtext(171,151,C_BLUE,"http://pho3.be/go/exxplorer");

    drect(169,181,518,275,C_RGB(28,30,31));
    dtext(181,190,C_DARK,current->active ?
        (current->is_write ? "RECEIVING FILE" : "SENDING FILE") :
        "TRANSFER");
    if(current->active) {
        /* Native paths are often Shift-JIS; show a bounded safe ASCII tail. */
        size_t n=strlen(current->path), start=n>43 ? n-43 : 0;
        int j=0;
        if(start) { shown[j++]='.'; shown[j++]='.'; }
        for(size_t i=start;i<n && j<(int)sizeof shown-1;i++) {
            unsigned char c=(unsigned char)current->path[i];
            shown[j++]=(c>=32 && c<127) ? c : '?';
        }
        shown[j]=0;
        dtext(181,209,C_BLACK,shown);
    }
    else dtext(181,209,C_DARK,"Waiting for a file transfer");

    drect(181,235,506,249,C_LIGHT);
    if(current->active && current->total_known && current->total_bytes) {
        uint32_t done=current->completed;
        if(done>current->total_bytes) done=current->total_bytes;
        int filled=(int)((uint64_t)done*326u/current->total_bytes);
        if(filled>0) drect(181,235,180+filled,249,C_BLUE);
    }
    if(current->active) {
        if(current->chunks)
            snprintf(label,sizeof label,"%lu / %lu chunks",
                (unsigned long)current->chunk,(unsigned long)current->chunks);
        else snprintf(label,sizeof label,"Chunk %lu / ?",
                (unsigned long)current->chunk);
        dtext(181,258,C_DARK,label);
    }
    drect(0,281,527,319,C_RGB(4,12,23));
    dtext(12,296,C_WHITE,"ESC / AC-ON / SOUND");
    drect(370,285,518,315,C_RGB(24,28,31));
    dtext(391,296,C_WHITE,"STOP USB");
    dupdate();
}


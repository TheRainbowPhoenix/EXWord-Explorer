#include "explorer_usb.h"
#include "explorer_fs.h"

#include <gint/display.h>
#include <gint/keyboard.h>
#include <gint/usb.h>
#include <gint/usb-ff-bulk.h>
#include <gint/drivers/touch.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* EXFS/1: fxlink app=exfs, types req and resp. All integers LE.
   Request: id:u32, offset:u32, length:u16, pathlen:u16, cmd:u8, flags:u8,
            path[pathlen], data[length] (only WRITE/RENAME).
   Response: id:u32, status:i32, total:u32, data[...] .
   LIST entries: kind:u8 (1=dir), namelen:u8, raw name[namelen].
   No host-supplied path is used without validation. */
enum {
    EXFS_LIST = 1, EXFS_READ, EXFS_CREATE, EXFS_WRITE, EXFS_DELETE,
    EXFS_RENAME, EXFS_STAT, EXFS_PING
};
#define EXFS_PACKET 1024
#define EXFS_DATA_MAX 768

static usb_interface_t const *usb_interfaces[] = { &usb_ff_bulk, NULL };
static uint8_t request[EXFS_PACKET];
static uint8_t answer[EXFS_PACKET];

static uint16_t read16(uint8_t const *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t read32(uint8_t const *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void write32(uint8_t *p, uint32_t n)
{
    p[0] = n; p[1] = n>>8; p[2] = n>>16; p[3] = n>>24;
}

static void screen(const char *detail, int count)
{
    char text[64];
    dclear(C_WHITE);
    drect(0, 0, 527, 32, C_RGB(3, 9, 16));
    dtext(8, 10, C_WHITE, "USB REMOTE - EXFS/1");
    dtext(12, 59, C_BLACK, detail);
    dtext(12, 91, C_DARK, "Connect the host bridge using libfxlink.");
    dtext(12, 113, C_DARK, "Local TFTP + a desktop filesystem GUI.");
    dtext(12, 150, C_RED, "Remote writes may alter system files.");
    dtext(12, 180, C_BLACK, "Only expose the host TFTP service on 127.0.0.1.");
    snprintf(text, sizeof text, "%d requests served", count);
    dtext(12, 215, C_BLACK, text);
    drect(0, 291, 527, 319, C_RGB(3, 9, 16));
    dtext(8, 299, C_WHITE, "ESC / AC-ON : disconnect and return");
    dupdate();
}

/* Validate a native EX-word path without ever accepting a relative path,
   dot traversal, or slash aliases. Keep Shift-JIS trailing bytes opaque. */
static int valid_path(char const *path)
{
    if(path[0] != '\\' || path[1] != '\\') return 0;
    const char *vols[] = { "drv0", "sys0", "data0", "data1", "data2",
        "crd0", "crd1", "crd2" };
    const char *component=NULL;
    for(unsigned i=0; i<sizeof vols/sizeof vols[0]; i++) {
        size_t n=strlen(vols[i]);
        if(!strncmp(path+2, vols[i], n) &&
            (path[2+n]==0 || path[2+n]=='\\')) {
            component=path+2+n;
            break;
        }
    }
    if(!component) return 0;
    if(*component=='\\') component++;
    if(!*component) return 1;
    for(const char *p=component;;p++) {
        unsigned char c=(unsigned char)*p;
        if(c==0 || c=='\\') {
            size_t n=(size_t)(p-component);
            if(n==0 || (n==1 && component[0]=='.') ||
                (n==2 && component[0]=='.' && component[1]=='.'))
                return 0;
            if(c==0) break;
            component=p+1;
            continue;
        }
        if(c<32 || c=='/') return 0;
        if(((c>=0x81 && c<=0x9f) || (c>=0xe0 && c<=0xfc)) &&
            p[1]) p++;
    }
    return 1;
}
static void send_reply(uint32_t id, int status, uint32_t total,
    const uint8_t *body, int body_size)
{
    if(body_size<0 || body_size>EXFS_DATA_MAX) { status=-2; body_size=0; }
    write32(answer, id);
    write32(answer+4, (uint32_t)status);
    write32(answer+8, total);
    if(body_size) memcpy(answer+12, body, body_size);

    usb_fxlink_header_t h;
    if(!usb_fxlink_fill_header(&h, "exfs", "resp", 12+body_size))
        return;
    int pipe=usb_ff_bulk_output();
    if(pipe<0) return;
    usb_write_sync(pipe, &h, sizeof h, false);
    usb_write_sync(pipe, answer, 12+body_size, false);
    usb_commit_sync(pipe);
}

static int dispatch(uint8_t const *data, int size)
{
    if(size < 14) return 0;
    uint32_t id=read32(data);
    uint32_t offset=read32(data+4);
    int amount=read16(data+8);
    int path_size=read16(data+10);
    int cmd=data[12], flags=data[13];
    int status=-2, total=0, body_size=0;
    uint8_t body[EXFS_DATA_MAX];
    char path[EX_PATH_MAX], to[EX_PATH_MAX];

    if(path_size<=0 || path_size>=EX_PATH_MAX ||
        size<14+path_size) {
        send_reply(id, -2, 0, NULL, 0);
        return 1;
    }
    if(memchr(data+14, 0, path_size)) {
        send_reply(id, -2, 0, NULL, 0);
        return 1;
    }
    memcpy(path, data+14, path_size);
    path[path_size]=0;
    if(!valid_path(path)) {
        send_reply(id, -3, 0, NULL, 0);
        return 1;
    }
    uint8_t const *payload=data+14+path_size;
    int remaining=size-14-path_size;

    if(cmd==EXFS_PING) {
        status=0;
        memcpy(body, "EXFS/1", 6);
        body_size=6;
    }
    else if(cmd==EXFS_LIST && offset<=0x7fffffff) {
        ex_entry_t rows[EX_ROWS];
        status=ex_list(path, (int)offset, rows);
        if(status==-1) status=0; /* Native API: empty directory */
        if(status>=0) {
            total=status;
            status=0;
            /* Each page starts at offset, the host requests successive pages. */
            int visible = (int)total-(int)offset;
            if(visible > EX_ROWS) visible=EX_ROWS;
            for(int i=0; i<visible; i++) {
                size_t n=strlen(rows[i].name);
                if(n>=EX_NAME_MAX) n=EX_NAME_MAX-1;
                if(n>255 || (int)n+2 > EXFS_DATA_MAX-body_size) break;
                body[body_size++]=rows[i].is_dir ? 1 : 0;
                body[body_size++]=(uint8_t)n;
                memcpy(body+body_size, rows[i].name, n);
                body_size+=n;
            }
        }
    }
    else if(cmd==EXFS_READ && offset<=0x7fffffff) {
        if(amount>512) amount=512;
        int n=ex_read(path, (int)offset, body, amount, &total);
        if(n<0) status=n;
        else { status=0; body_size=n; }
    }
    else if(cmd==EXFS_STAT) {
        uint8_t dummy=0;
        status=ex_read(path, 0, &dummy, 0, &total);
        if(status>=0) status=0;
    }
    else if(cmd==EXFS_CREATE && remaining==0) {
        status=ex_create(path, !!(flags&1));
    }
    else if(cmd==EXFS_WRITE && amount<=512 && remaining==amount &&
        offset<=0x7fffffff && offset+(uint32_t)amount<=0x7fffffff) {
        status=ex_remote_write(path, (int)offset, payload, amount);
        if(status==amount) status=0;
        else if(status>=0) status=-1;
    }
    else if(cmd==EXFS_DELETE && remaining==0) {
        status=ex_delete(path);
    }
    else if(cmd==EXFS_RENAME && amount>0 && amount<EX_PATH_MAX &&
        remaining==amount && !memchr(payload, 0, amount)) {
        memcpy(to, payload, amount);
        to[amount]=0;
        if(valid_path(to)) status=ex_rename(path, to);
    }
    send_reply(id, status, total, body, body_size);
    return 1;
}

void ex_usb_remote_screen(void)
{
    screen("Starting USB...", 0);
    int rc=usb_open(usb_interfaces, GINT_CALL_NULL);
    if(rc!=0) {
        char msg[80];
        snprintf(msg, sizeof msg, "USB open failed: %d", rc);
        screen(msg, 0);
        while(1) {
            key_event_t ev=getkey();
            if(ev.key==KEY_EXIT || ev.key==KEY_ACON) return;
        }
    }

    int served=0, active=-1;
    ex_fs_usb_mode(1);
    screen("Waiting for USB connection...", 0);
    for(;;) {
        int now=usb_is_open();
        if(active!=now) {
            screen(now ? "USB connected - remote filesystem ready" :
                "Waiting for USB connection...", served);
            active=now;
        }
        key_event_t ev=pollevent();
        if(ev.type==KEYEV_DOWN &&
           (ev.key==KEY_EXIT || ev.key==KEY_ACON)) break;
        if(!now) continue;

        usb_fxlink_header_t h;
        if(!usb_fxlink_handle_messages(&h)) continue;
        /* Keep each request bounded and always drain unknown transactions. */
        if(strncmp(h.application, "exfs", 16) ||
           strncmp(h.type, "req", 16) ||
           h.size<14 || h.size>EXFS_PACKET) {
            usb_fxlink_drop_transaction();
            continue;
        }
        int got=usb_read_sync(usb_ff_bulk_input(), request, h.size, false);
        if(got!=(int)h.size) continue;
        if(dispatch(request, got)) {
            served++;
            screen("USB connected - remote filesystem ready", served);
        }
    }
    usb_close();
    ex_fs_usb_mode(0);
}

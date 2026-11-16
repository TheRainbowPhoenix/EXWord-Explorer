#include "explorer_usb.h"
#include "explorer_fs.h"

#include <gint/display.h>
#include <gint/keyboard.h>
#include <gint/usb.h>
#include <gint/usb-ff-bulk.h>
#include <gint/defs/timeout.h>
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
    EXFS_RENAME, EXFS_STAT, EXFS_PING, EXFS_BEGIN
};
/* USB chunks can be larger than RFC 1350's fixed 512-byte TFTP block.
   2048 balances fewer round trips with the EX-word's limited RAM. */
/* Shared workspace is available to preview.c whenever USB is off. */
uint8_t ex_shared_io[EXFS_PACKET];
#define request ex_shared_io
#define answer ex_shared_io
static usb_interface_t const *usb_interfaces[] = { &usb_ff_bulk, NULL };

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
typedef struct {
    char path[EX_PATH_MAX];
    uint32_t total_bytes, completed, chunk, chunks;
    int is_write, active, total_known;
} transfer_status_t;
static transfer_status_t current;
static int screen_dirty;

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
static void screen(const char *detail, int count)
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
    dtext(181,190,C_DARK,current.active ?
        (current.is_write ? "RECEIVING FILE" : "SENDING FILE") :
        "TRANSFER");
    if(current.active) {
        /* Native paths are often Shift-JIS; show a bounded safe ASCII tail. */
        size_t n=strlen(current.path), start=n>43 ? n-43 : 0;
        int j=0;
        if(start) { shown[j++]='.'; shown[j++]='.'; }
        for(size_t i=start;i<n && j<(int)sizeof shown-1;i++) {
            unsigned char c=(unsigned char)current.path[i];
            shown[j++]=(c>=32 && c<127) ? c : '?';
        }
        shown[j]=0;
        dtext(181,209,C_BLACK,shown);
    }
    else dtext(181,209,C_DARK,"Waiting for a file transfer");

    drect(181,235,506,249,C_LIGHT);
    if(current.active && current.total_known && current.total_bytes) {
        uint32_t done=current.completed;
        if(done>current.total_bytes) done=current.total_bytes;
        int filled=(int)((uint64_t)done*326u/current.total_bytes);
        if(filled>0) drect(181,235,180+filled,249,C_BLUE);
    }
    if(current.active) {
        if(current.chunks)
            snprintf(label,sizeof label,"%lu / %lu chunks",
                (unsigned long)current.chunk,(unsigned long)current.chunks);
        else snprintf(label,sizeof label,"Chunk %lu / ?",
                (unsigned long)current.chunk);
        dtext(181,258,C_DARK,label);
    }
    drect(0,281,527,319,C_RGB(4,12,23));
    dtext(12,296,C_WHITE,"ESC / AC-ON / SOUND");
    drect(370,285,518,315,C_RGB(24,28,31));
    dtext(391,296,C_WHITE,"STOP USB");
    dupdate();
}

static void transfer_begin(const char *path, int write,
    uint32_t total, int total_known)
{
    snprintf(current.path,sizeof current.path,"%s",path);
    current.is_write=write;
    current.active=1;
    current.total_bytes=total;
    current.total_known=total_known;
    current.completed=0;
    current.chunk=0;
    current.chunks=total_known ?
        (total+EXFS_DATA_MAX-1)/EXFS_DATA_MAX : 0;
    screen_dirty=1;
}
static void transfer_update(const char *path, int write,
    uint32_t offset, uint32_t transferred, uint32_t file_size)
{
    if(!current.active || strcmp(current.path,path) ||
       current.is_write!=write) {
        transfer_begin(path,write,write ? 0 : file_size,!write);
    }
    current.chunk=offset/EXFS_DATA_MAX+1;
    current.completed=offset+transferred;
    if(!write && !current.total_known) {
        current.total_bytes=file_size;
        current.total_known=1;
        current.chunks=(file_size+EXFS_DATA_MAX-1)/EXFS_DATA_MAX;
    }
    if(current.chunk==1 || (current.chunk&3)==0 ||
       (current.chunks && current.chunk>=current.chunks))
        screen_dirty=1;
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
    /* The reply can be built in-place in ex_shared_io. Shift the payload
       FIRST so the response header cannot overwrite its first bytes. */
    if(body_size) memmove(answer+12, body, body_size);
    write32(answer, id);
    write32(answer+4, (uint32_t)status);
    write32(answer+8, total);

    usb_fxlink_header_t h;
    if(!usb_fxlink_fill_header(&h, "exfs", "resp", 12+body_size))
        return;
    int pipe=usb_ff_bulk_output();
    if(pipe<0) return;
    /* Never block indefinitely on a host which stopped accepting data. */
    timeout_t deadline=timeout_make_ms(1000);
    if(usb_write_sync_timeout(pipe,&h,sizeof h,false,&deadline)<0) return;
    deadline=timeout_make_ms(1000);
    if(usb_write_sync_timeout(pipe,answer,12+body_size,false,&deadline)<0)
        return;
    deadline=timeout_make_ms(1000);
    usb_commit_sync_timeout(pipe,&deadline);
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
    /* Reuse the request workspace as response body to avoid a 2-KiB
       stack allocation. Necessary with the EX-word's 8-KiB stack. */
    uint8_t *body=ex_shared_io;
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

    if(cmd==EXFS_BEGIN && remaining==0 && amount==0 &&
        offset<=0x7fffffff) {
        transfer_begin(path,!!(flags&1),offset,offset!=0);
        status=0;
    }
    else if(cmd==EXFS_PING) {
        status=0;
        memcpy(body, "EXFS/1", 6);
        body_size=6;
    }
    else if(cmd==EXFS_LIST && offset<=0x7fffffff) {
        ex_entry_t rows[EX_ROWS];
        /* For subsequent pages, amount carries the previously returned
           directory count. Avoid recounting 100+ entries each page. */
        status=amount>0 ? ex_list_window(path,(int)offset,amount,rows) :
            ex_list(path,(int)offset,rows);
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
        if(amount>EXFS_DATA_MAX) amount=EXFS_DATA_MAX;
        int n=ex_read(path, (int)offset, body, amount, &total);
        if(n<0) status=n;
        else {
            status=0;
            body_size=n;
            if(n>0) transfer_update(path,0,offset,n,total);
        }
    }
    else if(cmd==EXFS_STAT) {
        uint8_t dummy=0;
        status=ex_read(path, 0, &dummy, 0, &total);
        if(status>=0) status=0;
    }
    else if(cmd==EXFS_CREATE && remaining==0) {
        status=ex_create(path, !!(flags&1));
    }
    else if(cmd==EXFS_WRITE && amount<=EXFS_DATA_MAX && remaining==amount &&
        offset<=0x7fffffff && offset+(uint32_t)amount<=0x7fffffff) {
        status=ex_remote_write(path, (int)offset, payload, amount);
        if(status==amount) {
            if(amount) transfer_update(path,1,offset,amount,0);
            status=0;
        }
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
    memset(&current,0,sizeof current);
    screen_dirty=0;
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
        if((ev.type==KEYEV_DOWN &&
           (ev.key==KEY_EXIT || ev.key==KEY_ACON ||
            ev.key==KEY_EX_AUDIO)) ||
           (ev.type==KEYEV_TOUCH_DOWN &&
            ev.x>=367 && ev.y>=281)) break;
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
        /* A stalled or unplugged host must not trap the app forever inside
           the synchronous payload read. Return to key/touch polling. */
        int got=0;
        /* fxlink bulk transfer_size is 2048: a request with 2048 data
           plus its EXFS path/header spans *multiple* USB transactions.
           The gint sync reader never reads across transaction borders. */
        while(got<(int)h.size) {
            timeout_t deadline=timeout_make_ms(600);
            int n=usb_read_sync_timeout(usb_ff_bulk_input(),
                request+got, (int)h.size-got, false, &deadline);
            if(n<=0) break;
            got+=n;
        }
        if(got!=(int)h.size) {
            usb_fxlink_drop_transaction();
            continue;
        }
        if(dispatch(request, got)) {
            served++;
            /* Render transfer milestones, not every USB packet. */
            if(screen_dirty || (served&31)==0) {
                screen("Connected to computer",served);
                screen_dirty=0;
            }
        }
    }
    screen("Stopping USB...", served);
    usb_close();
    ex_fs_usb_mode(0);
}

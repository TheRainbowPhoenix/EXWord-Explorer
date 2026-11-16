/* Deterministic in-memory EX-word filesystem for the SDL simulator.
 * This is NOT a host filesystem adapter. No paths leave this process.
 * The real Explorer UI is compiled unmodified against these mock calls. */
#include "explorer_fs.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#define FILE_COUNT 96
#define CJF_SIZE 0x4200
#define IMAGE_W 240
#define IMAGE_H 160
typedef struct {
    char path[EX_PATH_MAX];
    int directory, used;
    int length;
    uint8_t data[2048];
} demo_file_t;
static demo_file_t files[FILE_COUNT];
static int initialized;
static uint8_t font[CJF_SIZE];
static uint8_t picture[4+IMAGE_W*IMAGE_H*2];
static unsigned char notes[256];

static void be16(uint8_t *p, unsigned n)
{
    p[0]=(uint8_t)(n>>8); p[1]=(uint8_t)n;
}
static void be32(uint8_t *p, uint32_t n)
{
    p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;
}
static void put(const char *p,int folder,const uint8_t *bytes,int size)
{
    for(int i=0;i<FILE_COUNT;i++) if(!files[i].used) {
        demo_file_t *f=&files[i];
        memset(f,0,sizeof *f);
        f->used=1;
        f->directory=folder;
        snprintf(f->path,sizeof f->path,"%s",p);
        if(bytes && size>0) {
            if(size>(int)sizeof f->data) size=sizeof f->data;
            memcpy(f->data,bytes,size);
            f->length=size;
        }
        return;
    }
}
static void init_files(void)
{
    if(initialized) return;
    initialized=1;
    memcpy(notes,"This is a mock file in EXWord Explorer.\r\n",41);
    memcpy(notes+41,"No real storage is modified in the simulator.\r\n",47);
    notes[88]=0;
    picture[0]=0x20; picture[1]=IMAGE_W;
    be16(picture+2,IMAGE_H);
    for(int y=0;y<IMAGE_H;y++)
        for(int x=0;x<IMAGE_W;x++) {
            int r=(x*31)/IMAGE_W;
            int g=(y*63)/IMAGE_H;
            int b=((x+y)*31)/(IMAGE_W+IMAGE_H);
            unsigned color=(r<<11)|(g<<5)|b;
            be16(picture+4+(y*IMAGE_W+x)*2,color);
        }
    memcpy(font,"CJF",3);
    be32(font+4,0x10);
    be16(font+8,24);
    for(int range=0;range<2;range++) {
        unsigned r=range==0 ? 0x10 : 0x60;
        be32(font+r+4,range==0 ? 0x60 : 0xffffffffu);
        be32(font+r+8,range==0 ? 0x80200000 : 0xa0200000);
        be32(font+r+12,range==0 ? 0x80ff0000 : 0xa0ff0000);
        be16(font+r+0x18,16);
        be16(font+r+0x1a,12);
        be16(font+r+0x1c,32);
        font[r+0x2e]=9;
        font[r+0x38]=255;
        unsigned base=range==0 ? 0x200 : 0x2200;
        be32(font+r+0x34,base);
        for(int index=(range==0?34:65);index<256;index++) {
            uint8_t *glyph=font+base+index*32;
            for(int y=0;y<16;y++) for(int x=0;x<12;x++) {
                int on = x==0 || y==15 || ((x+y+index)%9)==0 ||
                    (((index>>((x/2)%8))&1) && y>=5 && y<=10 && x>=3 && x<=9);
                if(on) glyph[y*2+x/8] |= (uint8_t)(0x80u>>(x&7));
            }
        }
    }

    put("\\drv0\\README.TXT",0,notes,88);
    put("\\drv0\\sample.cjf",0,NULL,0);
    put("\\drv0\\colors.bin",0,NULL,0);
    put("\\drv0\\Documents",1,NULL,0);
    put("\\drv0\\Pictures",1,NULL,0);
    put("\\drv0\\Documents\\hello.txt",0,notes,88);
    put("\\drv0\\Pictures\\colors.bin",0,NULL,0);
    for(int i=1;i<=29;i++) {
        char path[EX_PATH_MAX];
        snprintf(path,sizeof path,"\\drv0\\EXAMPLE%02d.TXT",i);
        put(path,0,notes,88);
    }
    const char *volumes[]={"sys0","data0","data1","data2","crd0","crd1","crd2"};
    for(size_t i=0;i<sizeof volumes/sizeof *volumes;i++) {
        char path[EX_PATH_MAX];
        snprintf(path,sizeof path,"\\%s\\ABOUT.TXT",volumes[i]);
        put(path,0,notes,88);
        snprintf(path,sizeof path,"\\%s\\DemoFolder",volumes[i]);
        put(path,1,NULL,0);
    }
}
static const char *normalized(const char *name)
{
    /* EX-word OS paths in the real app start with TWO slashes. */
    return name && name[0]=='\\' && name[1]=='\\' ? name+1 : name;
}
static demo_file_t *lookup(const char *name)
{
    name=normalized(name);
    for(int i=0;i<FILE_COUNT;i++)
        if(files[i].used && !strcmp(name,files[i].path)) return &files[i];
    return NULL;
}
int ex_join(char out[EX_PATH_MAX], const char *parent,const char *name)
{
    if(!parent || !name || !*name || strchr(name,'\\') ||
       strchr(name,'/') || !strcmp(name,".") || !strcmp(name,".."))
        return -1;
    if(strlen(parent)+strlen(name)+2>=EX_PATH_MAX) return -1;
    snprintf(out,EX_PATH_MAX,"%s\\%s",parent,name);
    return 0;
}
static int list(const char *path,int first,ex_entry_t rows[EX_ROWS])
{
    init_files();
    const char *prefix=normalized(path);
    size_t n=strlen(prefix);
    int total=0;
    if(rows) memset(rows,0,EX_ROWS*sizeof *rows);
    for(int i=0;i<FILE_COUNT;i++) {
        if(!files[i].used || strncmp(files[i].path,prefix,n) ||
           files[i].path[n]!='\\') continue;
        const char *base=files[i].path+n+1;
        if(strchr(base,'\\')) continue;
        if(rows && total>=first && total<first+EX_ROWS) {
            ex_entry_t *e=&rows[total-first];
            snprintf(e->name,sizeof e->name,"%s",base);
            e->is_dir=files[i].directory;
        }
        total++;
    }
    return total;
}
int ex_list(const char *p,int first,ex_entry_t rows[EX_ROWS])
{
    return list(p,first,rows);
}
int ex_list_window(const char *p,int first,int known_total,
    ex_entry_t rows[EX_ROWS])
{
    (void)known_total;
    return list(p,first,rows);
}
int ex_exists_in(const char *directory,const char *name)
{
    char path[EX_PATH_MAX];
    return ex_join(path,directory,name)<0 ? -1 : lookup(path)!=NULL;
}
static const uint8_t *data_for(const char *path,int *length)
{
    demo_file_t *f=lookup(path);
    if(!f || f->directory) return NULL;
    if(strstr(path,"sample.cjf")) {
        *length=sizeof font; return font;
    }
    if(strstr(path,"colors.bin")) {
        *length=sizeof picture; return picture;
    }
    *length=f->length;
    return f->data;
}
int ex_read(const char *path,int offset,uint8_t *out,int max,int *size)
{
    init_files();
    if(!size || offset<0 || max<0) return -1;
    int total=0;
    const uint8_t *bytes=data_for(path,&total);
    if(!bytes) return -1;
    *size=total;
    if(offset>=total || !max) return 0;
    int len=total-offset;
    if(len>max) len=max;
    if(out) memcpy(out,bytes+offset,len);
    return len;
}
int ex_write_byte(const char *path,int offset,uint8_t value)
{
    init_files();
    demo_file_t *f=lookup(path);
    if(!f || f->directory || offset<0 || offset>=f->length ||
       strstr(path,".cjf") || strstr(path,".bin")) return -1;
    f->data[offset]=value;
    return 0;
}
int ex_create(const char *path,int directory)
{
    init_files();
    if(lookup(path)) return -1;
    put(normalized(path),directory,NULL,0);
    return lookup(path) ? 0 : -1;
}
int ex_delete(const char *path)
{
    init_files();
    demo_file_t *f=lookup(path);
    if(!f) return -1;
    memset(f,0,sizeof *f);
    return 0;
}
int ex_rename(const char *a,const char *b)
{
    init_files();
    demo_file_t *f=lookup(a);
    if(!f || lookup(b)) return -1;
    snprintf(f->path,sizeof f->path,"%s",normalized(b));
    return 0;
}
int ex_copy_file(const char *a,const char *b)
{
    init_files();
    demo_file_t *f=lookup(a);
    if(!f || f->directory || lookup(b) || strstr(a,".cjf") ||
       strstr(a,".bin")) return -1;
    put(normalized(b),0,f->data,f->length);
    return lookup(b) ? 0 : -1;
}
void ex_fs_usb_mode(int on) { (void)on; }
int ex_remote_write(const char *path,int offset,const uint8_t *data,int n)
{
    (void)path;(void)offset;(void)data;(void)n;return -1;
}

#include "casio_bitmap.h"
#include <string.h>

static const uint16_t casio_palette[16] = {
    0x0000,0x001f,0x07e0,0x07ff,0xf800,0xf81f,0xffe0,0xffff,
    0xffff,0x0000,0x07e0,0x001f,0xffff,0x0000,0x0000,0xffff
};

static uint16_t be16(uint8_t const *p)
{
    return ((uint16_t)p[0]<<8) | p[1];
}
static int read_bytes(casio_stream_t const *file, uint32_t pos,
    void *buffer, size_t n)
{
    if(!file || !file->read || pos > file->size ||
        n > file->size-pos) return -1;
    return file->read(file->user,pos,buffer,n) ? -2 : 0;
}
int casio_bitmap_open(casio_stream_t const *file, casio_bitmap_t *bmp)
{
    if(!bmp) return -1;
    uint8_t h[6];
    int rc=read_bytes(file,0,h,4);
    if(rc) return rc;
    uint16_t packed=be16(h), height=be16(h+2);
    int kind=packed>>12, width=packed&0x0fff;
    if(!width || !height || kind>3) return -1;
    memset(bmp,0,sizeof *bmp);
    bmp->width=width;
    bmp->height=height;
    memcpy(bmp->palette,casio_palette,sizeof casio_palette);
    bmp->format=kind;
    bmp->colors=16;
    bmp->data_offset=4;
    if(kind==3) {
        rc=read_bytes(file,4,h,2);
        if(rc) return rc;
        int n=be16(h);
        if(n<1 || n>16) return -1;
        uint8_t colors[32];
        rc=read_bytes(file,6,colors,n*2);
        if(rc) return rc;
        for(int i=0;i<n;i++) bmp->palette[i]=be16(colors+2*i);
        bmp->colors=n;
        bmp->format=n==2 ? 0 : 1;
        bmp->data_offset=6+n*2;
    }
    bmp->stride=bmp->format==0 ? (width+7)/8 :
        bmp->format==1 ? (width+1)/2 : width*2;
    uint64_t end=(uint64_t)bmp->data_offset+
        (uint64_t)bmp->stride*height;
    return end <= file->size ? 0 : -1;
}
int casio_bitmap_render(casio_stream_t const *file,
    casio_bitmap_t const *bmp, uint8_t *row, size_t row_size,
    casio_plot565 plot, void *user)
{
    if(!bmp || !plot || !row || row_size<bmp->stride) return -2;
    for(uint32_t y=0; y<bmp->height; y++) {
        int rc=read_bytes(file,bmp->data_offset+y*bmp->stride,
            row,bmp->stride);
        if(rc) return rc;
        for(uint32_t x=0; x<bmp->width; x++) {
            uint16_t color=0;
            if(bmp->format==0) {
                int on=!!(row[x/8] & (0x80>>(x&7)));
                color=(bmp->colors==2) ? bmp->palette[on] :
                    (on ? 0x0000 : 0xffff);
            }
            else if(bmp->format==1) {
                int idx=(row[x/2]>>(x&1 ? 0 : 4)) & 15;
                if(idx>=bmp->colors) return -1;
                color=bmp->palette[idx];
            }
            else color=be16(row+x*2);
            plot(user,x,y,color);
        }
    }
    return 0;
}

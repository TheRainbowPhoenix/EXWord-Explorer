/* Self-contained fixtures ported from supplied bitmap.py, cjf.py,
 * and test_readers.py; executable with ordinary host C compiler. */
#include "../src/readers/casio_bitmap.h"
#include "../src/readers/cjf.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { const uint8_t *data; size_t length; } mem_t;
static int read_mem(void *opaque, uint32_t pos, void *dst, size_t n)
{
    mem_t const *m=opaque;
    if(pos>m->length || n>m->length-pos) return -1;
    memcpy(dst,m->data+pos,n);
    return 0;
}
typedef struct { uint16_t pixels[10]; int count; } output_t;
static void plot(void *ctx, uint16_t x, uint16_t y, uint16_t color)
{
    (void)x; (void)y;
    output_t *o=ctx;
    assert(o->count<10);
    o->pixels[o->count++]=color;
}
static void test_bitmap(uint8_t const *bytes, size_t n,
    unsigned width, unsigned height, uint16_t a, uint16_t b)
{
    mem_t m={bytes,n};
    casio_stream_t source={read_mem,&m,n};
    casio_bitmap_t bmp;
    assert(casio_bitmap_open(&source,&bmp)==0);
    assert(bmp.width==width && bmp.height==height);
    uint8_t scratch[40];
    output_t out={{0},0};
    assert(casio_bitmap_render(&source,&bmp,scratch,sizeof scratch,
        plot,&out)==0);
    assert(out.count==(int)(width*height));
    assert(out.pixels[0]==a && out.pixels[1]==b);
}
static void be32(uint8_t *p, uint32_t v)
{
    p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;
}
static void be16(uint8_t *p, uint16_t v)
{
    p[0]=v>>8;p[1]=v;
}
static void font_fixture(void)
{
    uint8_t data[0x60]={0};
    memcpy(data,"CJF",3);
    be32(data+4,0x10);
    be16(data+8,8);
    be32(data+0x14,0xffffffff);
    be32(data+0x18,0x41000000);
    be32(data+0x1c,0x43000000);
    be16(data+0x28,1); /* height */
    be16(data+0x2a,4); /* width */
    be16(data+0x2c,1); /* bytes per glyph */
    data[0x3e]=8;
    be32(data+0x44,0x80000050);
    be32(data+0x50,0x8000000c);
    be32(data+0x54,0x0000000d);
    be32(data+0x58,0x4000000e);
    data[0x5c]=0xf0; /* raw */
    data[0x5d]=0xb3; /* nibble RLE of 11110000 */
    data[0x5e]=0x83; /* byte RLE */
    data[0x5f]=0x03;
    mem_t m={data,sizeof data};
    cjf_font_t font;
    cjf_stream_t cjfs={read_mem,&m,sizeof data};
    assert(cjf_open(&cjfs,&font)==0);
    assert(font.nominal_size==8);
    assert(font.range_count==1 && font.ranges[0].count==3);
    uint8_t glyph[1];
    for(unsigned i=0;i<3;i++) {
        assert(cjf_read_glyph(&font,0,i,glyph,sizeof glyph)==0);
        assert(glyph[0]==0xf0);
    }
    assert(cjf_read_glyph(&font,0,3,glyph,sizeof glyph)<0);
    be32(data+0x14,0x10); /* cyclic linked list */
    assert(cjf_open(&cjfs,&font)<0);
}
int main(void)
{
    static const uint8_t mono[]={0x00,0x09,0x00,0x02,0x80,0x80,0x40,0x00};
    static const uint8_t rgb[]={0x20,0x02,0x00,0x01,0xf8,0x00,0x07,0xe0};
    static const uint8_t idx[]={0x10,0x02,0x00,0x01,0x14};
    static const uint8_t pal[]={0x30,0x02,0x00,0x01,0x00,0x02,
                                0xf8,0x00,0x07,0xe0,0x40};
    /* MONO is 9*2 pixels; use a dedicated plot buffer below instead. */
    mem_t m={mono,sizeof mono};
    casio_stream_t source={read_mem,&m,sizeof mono};
    casio_bitmap_t bmp;
    assert(casio_bitmap_open(&source,&bmp)==0);
    assert(bmp.width==9 && bmp.height==2 && bmp.stride==2);
    test_bitmap(rgb,sizeof rgb,2,1,0xf800,0x07e0);
    test_bitmap(idx,sizeof idx,2,1,0x001f,0xf800);
    test_bitmap(pal,sizeof pal,2,1,0xf800,0x07e0);
    static const uint8_t bad[]={0x40,0x01,0x00,0x01};
    m.data=bad;m.length=sizeof bad;
    source.size=m.length;
    assert(casio_bitmap_open(&source,&bmp)<0);
    static const uint8_t rle[]={0x02,0xf8,0x00,0x01,0x07,0xe0};
    mem_t rm={rle,sizeof rle};
    casio_stream_t rs={read_mem,&rm,sizeof rle};
    output_t out={{0},0};
    assert(casio_bitmap_render_rle565(&rs,3,1,plot,&out)==0);
    assert(out.count==3 && out.pixels[0]==0xf800 &&
           out.pixels[1]==0xf800 && out.pixels[2]==0x07e0);
    font_fixture();
    puts("All standalone Casio bitmap/CJF parser tests passed");
    return 0;
}

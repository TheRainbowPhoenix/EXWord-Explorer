#include "preview.h"
#include "explorer_fs.h"
#include "readers/casio_bitmap.h"
#include "readers/cjf.h"
#include <gint/display.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static char path[EX_PATH_MAX];
static casio_stream_t stream;
static casio_bitmap_t bitmap;
static cjf_font_t font;
static int type, range_no;
static uint32_t first_glyph;
/* Bitmaps and fonts never render simultaneously, so reuse their scratch RAM. */
static union { uint8_t glyph[2048]; uint8_t row[1100]; } scratch;

/* The OS does not expose a persistent gint-world descriptor.
 * Cache adjacent reads, particularly the per-byte CJF run tokens and rows. */
static uint8_t cache[2048];
static uint32_t cache_start;
static int cache_size;

static int read_at(void *ctx, uint32_t offset, void *buffer, size_t count)
{
    (void)ctx;
    if(offset >= (uint32_t)stream.size ||
       count > (size_t)stream.size - offset) return -1;
    if(count==0) return 0;
    if(offset<cache_start || offset-cache_start+count>(size_t)cache_size) {
        int total=0;
        int n=(int)(stream.size-offset);
        if(n>(int)sizeof cache) n=sizeof cache;
        int rc=ex_read(path,(int)offset,cache,n,&total);
        if(rc<n) return -1;
        cache_start=offset;
        cache_size=rc;
    }
    memcpy(buffer,cache+offset-cache_start,count);
    return 0;
}
static void plot565(void *ctx, uint16_t x, uint16_t y, uint16_t color)
{
    (void)ctx;
    int px=264-(int)bitmap.width/2+x;
    int py=160-(int)bitmap.height/2+y;
    if(px>=0 && px<528 && py>=0 && py<320)
        dpixel(px,py,color);
}
int preview_open(char const *filename, int format)
{
    uint8_t marker=0;
    int size=0;
    int rc=ex_read(filename,0,&marker,1,&size);
    if(rc<0 || size<=0) return -1;
    snprintf(path,sizeof path,"%s",filename);
    stream.read=read_at;
    stream.user=NULL;
    stream.size=(uint32_t)size;
    cache_start=0;
    cache_size=0;
    first_glyph=0;
    range_no=0;
    type=format;
    if(format==PREVIEW_BITMAP) {
        rc=casio_bitmap_open(&stream,&bitmap);
        if(rc || bitmap.stride>sizeof scratch.row) return -1;
    }
    else if(format==PREVIEW_CJF) {
        rc=cjf_open(&stream,&font);
        if(rc) return rc;
        if(!font.range_count) return -1;
        if(font.ranges[0].glyph_bytes>sizeof scratch.glyph) return -1;
    }
    else return -1;
    return 0;
}
static int columns(void)
{
    cjf_range_t const *r=&font.ranges[range_no];
    int cell_w=r->width+9;
    if(cell_w<38) cell_w=38;
    int n=528/cell_w;
    if(n<1) n=1;
    return n;
}
static int page_count(void)
{
    cjf_range_t const *r=&font.ranges[range_no];
    int cell_h=r->height+16;
    int rows=289/cell_h;
    if(rows<1) rows=1;
    return columns()*rows;
}
void preview_page(int delta)
{
    if(type!=PREVIEW_CJF) return;
    uint32_t count=font.ranges[range_no].count;
    int step=page_count();
    if(delta>0) {
        uint32_t next=first_glyph + step;
        if(next<count) first_glyph=next;
    }
    else if(first_glyph>0)
        first_glyph=first_glyph>(uint32_t)step ?
            first_glyph-step : 0;
}
void preview_range(int delta)
{
    if(type!=PREVIEW_CJF) return;
    int next=range_no+delta;
    if(next<0 || next>=font.range_count) return;
    range_no=next;
    first_glyph=0;
}
int preview_render(void)
{
    dclear(C_WHITE);
    if(type==PREVIEW_BITMAP) {
        int rc=casio_bitmap_render(&stream,&bitmap,scratch.row,sizeof scratch.row,plot565,NULL);
        if(rc) return rc;
        dupdate();
        return 0;
    }
    if(type!=PREVIEW_CJF) return -1;
    cjf_range_t const *r=&font.ranges[range_no];
    int cw=r->width+9;
    if(cw<38) cw=38;
    int ch=r->height+16;
    int cols=columns(), per=page_count();
    if(r->glyph_bytes>sizeof scratch.glyph) return -1;
    for(int pos=0;pos<per;pos++) {
        uint32_t index=first_glyph+(uint32_t)pos;
        if(index>=r->count) break;
        if(cjf_read_glyph(&font,range_no,index,scratch.glyph,sizeof scratch.glyph))
            return -1;
        int ox=pos%cols*cw;
        int oy=pos/cols*ch;
        int stride=(r->width+7)/8;
        for(int y=0;y<r->height;y++) {
            if(oy+y>=290) break;
            for(int x=0;x<r->width && ox+x<528;x++) {
                if(scratch.glyph[y*stride+x/8] & (0x80>>(x&7)))
                    dpixel(ox+x,oy+y,C_BLACK);
            }
        }
        char label[16];
        snprintf(label,sizeof label,"%03X",(unsigned)index);
        if(oy+r->height+12<296)
            dtext(ox,oy+r->height+1,C_DARK,label);
    }
    drect(0,297,527,319,C_RGB(3,9,16));
    char footer[90];
    snprintf(footer,sizeof footer,"CJF %d/%d   %lu/%lu  PgUp/Dn browse  ESC back",
        range_no+1,font.range_count,(unsigned long)first_glyph+1,
        (unsigned long)r->count);
    dtext(5,302,C_WHITE,footer);
    dupdate();
    return 0;
}

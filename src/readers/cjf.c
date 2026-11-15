#include "cjf.h"
#include <string.h>

static uint16_t be16(uint8_t const *p)
{
    return ((uint16_t)p[0]<<8) | p[1];
}
static uint32_t be32(uint8_t const *p)
{
    return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) |
           ((uint32_t)p[2]<<8) | p[3];
}
static int read_bytes(casio_stream_t const *file, uint32_t pos,
    void *buffer, size_t n)
{
    if(!file || !file->read || pos>file->size ||
        n>file->size-pos) return -1;
    return file->read(file->user,pos,buffer,n) ? -2 : 0;
}
static uint32_t normalize(uint32_t v)
{
    while(v && !(v&255)) v>>=8;
    return v;
}
static int32_t code_rank(cjf_range_t const *r, uint32_t code)
{
    uint32_t lead=code>>8, trail=code&255;
    if(r->encoding==0 || r->encoding==8) return (int32_t)code;
    if(r->encoding==1 || r->encoding==10 ||
       r->encoding==11 || r->encoding==12) {
        if(trail==0x7f) return -1;
        if(lead>=0xa0) lead-=0x40;
        if(trail>0x7f) trail--;
        return (int32_t)(lead*188+trail-0x40);
    }
    if(r->encoding==6) {
        if(r->mapping==1) {
            int stride=(r->last&255)-(r->first&255)+1;
            if(stride<=0) return -1;
            return (int32_t)(lead*stride+trail);
        }
        return (int32_t)(lead*190+trail-0x40-(trail>0x7f));
    }
    return -1;
}
int cjf_open(casio_stream_t const *file, cjf_font_t *font)
{
    uint8_t head[10];
    if(!font) return -1;
    int rc=read_bytes(file,0,head,sizeof head);
    if(rc) return rc;
    if(memcmp(head,"CJF",3)) return -1;
    memset(font,0,sizeof *font);
    font->file=*file;
    font->nominal_size=be16(head+8);
    uint32_t pos=be32(head+4);
    while(pos!=0xffffffff) {
        if(font->range_count>=CJF_MAX_RANGES) return -1;
        for(int i=0;i<font->range_count;i++)
            if(font->ranges[i].offset==pos) return -1;
        uint8_t raw[0x39];
        rc=read_bytes(file,pos,raw,sizeof raw);
        if(rc) return rc;
        cjf_range_t *r=&font->ranges[font->range_count];
        r->offset=pos;
        r->first=normalize(be32(raw+8));
        r->last=normalize(be32(raw+12));
        r->height=be16(raw+0x18);
        r->width=be16(raw+0x1a);
        r->glyph_bytes=be16(raw+0x1c);
        r->variant=be16(raw+0x1e);
        uint32_t d=be32(raw+0x34);
        r->data_offset=d&0x7fffffff;
        r->indexed=!!(d&0x80000000);
        r->encoding=raw[0x2e];
        r->mapping=raw[0x38];
        if(!r->width || !r->height || !r->glyph_bytes ||
            r->glyph_bytes < ((r->width+7)/8)*r->height ||
            r->data_offset>=file->size ||
            r->last<r->first) return -1;
        int32_t first=code_rank(r,r->first);
        int32_t last=code_rank(r,r->last);
        if(first<0 || last<first) return -3;
        r->count=(uint32_t)(last-first)+1;
        font->range_count++;
        pos=be32(raw+4);
    }
    return font->range_count>0 ? 0 : -1;
}
int cjf_read_glyph(cjf_font_t const *font, int range_index,
    uint32_t glyph_index, uint8_t *bitmap, size_t capacity)
{
    if(!font || !bitmap || range_index<0 ||
        range_index>=font->range_count) return -1;
    cjf_range_t const *r=&font->ranges[range_index];
    if(glyph_index>=r->count || capacity<r->glyph_bytes) return -1;
    uint64_t pos=(uint64_t)r->data_offset+
        (uint64_t)glyph_index*(r->indexed ? 4 : r->glyph_bytes);
    if(pos>0xffffffff) return -1;
    uint32_t kind=0x80000000;
    if(r->indexed) {
        uint8_t entry[4];
        int rc=read_bytes(&font->file,(uint32_t)pos,entry,4);
        if(rc) return rc;
        uint32_t value=be32(entry);
        kind=value&0xc0000000;
        pos=(uint64_t)r->data_offset +
            (value & (value&0x80000000 ? 0x7fffffff : 0x3fffffff));
    }
    if(pos>0xffffffff) return -1;
    if(kind&0x80000000)
        return read_bytes(&font->file,(uint32_t)pos,bitmap,r->glyph_bytes);
    memset(bitmap,0,r->glyph_bytes);
    uint32_t bit=0, total=(uint32_t)r->glyph_bytes*8;
    int nibble=!(kind&0x40000000);
    while(bit<total) {
        uint8_t byte;
        int rc=read_bytes(&font->file,(uint32_t)pos++,&byte,1);
        if(rc) return rc;
        for(int k=0;k<(nibble?2:1) && bit<total;k++) {
            unsigned value=nibble ? (k==0 ? byte>>4 : byte&15) : byte;
            unsigned mask=nibble ? 7 : 127;
            unsigned count=(value&mask)+1;
            unsigned on=value&(nibble?8:128);
            while(count-- && bit<total) {
                if(on) bitmap[bit>>3] |= (uint8_t)(0x80u>>(bit&7));
                bit++;
            }
        }
    }
    return 0;
}

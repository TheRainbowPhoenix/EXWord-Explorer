/* Adapted from NCInput_gint simulator display shim for EX-word landscape. */
#ifndef MOCK_DISPLAY_H
#define MOCK_DISPLAY_H
#include <stdint.h>
typedef uint16_t color_t;
#define C_RGB(r,g,b) (color_t)((((r)&31)<<11)|(((g)&63)<<5)|((b)&31))
#define C_WHITE 0xffff
#define C_BLACK 0x0000
#define C_LIGHT 0xce79
#define C_DARK 0x4210
#define C_BLUE 0x001f
#define C_RED 0xf800
#define C_NONE 0x0001
#define SCREEN_W 528
#define SCREEN_H 320
#ifdef __cplusplus
extern "C" {
#endif
void dclear(color_t color);
void drect(int x1,int y1,int x2,int y2,color_t color);
void dline(int x1,int y1,int x2,int y2,color_t color);
void dtext(int x,int y,color_t color,const char *text);
void dpixel(int x,int y,color_t color);
void dupdate(void);
#ifdef __cplusplus
}
#endif
#endif

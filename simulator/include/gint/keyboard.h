/* NCInput_gint SDL event API, with EX-word keys. */
#ifndef MOCK_KEYBOARD_H
#define MOCK_KEYBOARD_H
#ifdef __cplusplus
extern "C" {
#endif
enum { KEYEV_NONE=0, KEYEV_DOWN=1, KEYEV_UP=2, KEYEV_HOLD=3,
       KEYEV_TOUCH_DOWN=4, KEYEV_TOUCH_UP=5, KEYEV_TOUCH_DRAG=6 };
typedef struct { int type,key,x,y,shift,alpha; } key_event_t;
enum { KEY_UP=1, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_EXE, KEY_EXIT,
       KEY_DEL, KEY_PAGEUP, KEY_PAGEDOWN, KEY_ACON, KEY_EX_AUDIO,
       KEY_ALPHA, KEY_SHIFT };
/* The 26 physical character keys are contiguous in QWERTY order. */
enum { KEY_EX_Q=100, KEY_EX_W, KEY_EX_E, KEY_EX_R, KEY_EX_T,
 KEY_EX_Y, KEY_EX_U, KEY_EX_I, KEY_EX_O, KEY_EX_P, KEY_EX_A, KEY_EX_S,
 KEY_EX_D, KEY_EX_F, KEY_EX_G, KEY_EX_H, KEY_EX_J, KEY_EX_K, KEY_EX_L,
 KEY_EX_Z, KEY_EX_X, KEY_EX_C, KEY_EX_V, KEY_EX_B, KEY_EX_N, KEY_EX_M };
enum { KEY_0=200, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5,
 KEY_6, KEY_7, KEY_8, KEY_9 };
void clearevents(void);
key_event_t pollevent(void);
key_event_t waitevent(void *unused);
key_event_t getkey(void);
int keydown(int key);
#ifdef __cplusplus
}
#endif
#endif

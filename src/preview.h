#ifndef EXPLORER_PREVIEW_H
#define EXPLORER_PREVIEW_H
/* Application-specific drawing adapter. Decoders in readers/ remain
   standalone C with no gint dependency. */
enum { PREVIEW_BITMAP=1, PREVIEW_CJF=2, PREVIEW_RLE565=3 };
int preview_open(char const *path, int type);
int preview_render(void); /* redraw from current page */
void preview_page(int delta);
void preview_range(int delta);
#endif

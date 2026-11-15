#ifndef EXPLORER_FS_H
#define EXPLORER_FS_H

#include <stdint.h>

/* Keep directory listings bounded: the EX-word loader has little RAM. */
#define EX_NAME_MAX 160
#define EX_PATH_MAX 320
#define EX_ROWS 18

typedef struct {
    char name[EX_NAME_MAX];
    int is_dir;
    int truncated;
} ex_entry_t;

/* Paths use the native EX-word spelling, eg "\\drv0\CASIO". */
int ex_join(char out[EX_PATH_MAX], const char *parent, const char *name);
int ex_list(const char *dir, int first, ex_entry_t rows[EX_ROWS]);
/* Use a known count to stop enumeration after the visible window. */
int ex_list_window(const char *dir, int first, int known_total,
    ex_entry_t rows[EX_ROWS]);
void ex_fs_usb_mode(int on);
int ex_remote_write(const char *path, int offset, const uint8_t *data, int length);
int ex_read(const char *path, int offset, uint8_t *data, int max,
    int *size);
int ex_write_byte(const char *path, int offset, uint8_t byte);
int ex_create(const char *path, int directory);
int ex_delete(const char *path);
int ex_rename(const char *from, const char *to);
int ex_copy_file(const char *from, const char *to);

/* Returns 1 if a sibling of this exact name exists; 0 if not; <0 on error. */
int ex_exists_in(const char *directory, const char *name);

#endif

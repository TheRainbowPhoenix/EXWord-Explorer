#include "explorer_fs.h"
#include <gint/exword.h>
#include <gint/gint.h>
#include <string.h>
#include <stdio.h>

/* All native EX-word filesystem calls run in the OS world, never while
   gint has installed its own interrupt handlers. */
#define EX_FILE 1
#define EX_DIRECTORY 5
#define EX_READ_ONLY 1
#define EX_WRITE_ONLY 2
#define EX_READ_WRITE 3

int ex_join(char out[EX_PATH_MAX], const char *parent, const char *name)
{
    if(!parent || !name || !*name || strchr(name, '\\') || strchr(name, '/')
       || !strcmp(name, ".") || !strcmp(name, ".."))
        return -1;
    size_t n = strlen(parent), m = strlen(name);
    if(n + 1 + m >= EX_PATH_MAX) return -1;
    memcpy(out, parent, n);
    out[n++] = '\\';
    memcpy(out+n, name, m+1);
    return 0;
}

static const char *leaf(const char *name)
{
    const char *back = strrchr(name, '\\');
    const char *slash = strrchr(name, '/');
    const char *p = back > slash ? back : slash;
    return p ? p + 1 : name;
}

typedef struct {
    const char *directory;
    int first, total;
    ex_entry_t *rows;
    const char *lookfor;
    int found;
} search_t;

static int search_native(search_t *q)
{
    char pattern[EX_PATH_MAX + 4];
    size_t n = strlen(q->directory);
    if(n + 3 >= sizeof pattern) return -3;
    memcpy(pattern, q->directory, n);
    pattern[n++] = '\\';
    pattern[n++] = '*';
    pattern[n] = '\0';

    char filename[512] = {0};
    unsigned long kind = 0;
    int handle = -1, rc = exword_sys_findfirst(pattern, &handle, filename, &kind);
    if(rc < 0) {
        /* The native API also uses -1 for an empty directory. */
        return (rc == -1 || rc == -16) ? 0 : rc;
    }

    int index = 0;
    do {
        filename[sizeof filename - 1] = '\0';
        const char *name = leaf(filename);
        if(*name && strcmp(name, ".") && strcmp(name, "..")) {
            if(q->lookfor && !strcmp(name, q->lookfor)) q->found = 1;
            if(q->rows && index >= q->first && index < q->first + EX_ROWS) {
                ex_entry_t *e = &q->rows[index - q->first];
                size_t length = strlen(name);
                e->truncated = length >= sizeof e->name;
                if(length >= sizeof e->name) length = sizeof e->name-1;
                memcpy(e->name, name, length);
                e->name[length] = '\0';
                e->is_dir = kind == EX_DIRECTORY;
            }
            index++;
        }
        filename[0] = '\0';
        kind = 0;
        rc = exword_sys_findnext(handle, filename, &kind);
    } while(rc >= 0);

    int close_rc = exword_sys_findclose(handle);
    q->total = index;
    if(rc != -16 && rc != -1) return rc;
    if(close_rc < 0) return close_rc;
    return index;
}

int ex_list(const char *directory, int first, ex_entry_t rows[EX_ROWS])
{
    search_t q = { .directory = directory, .first = first, .rows = rows };
    if(rows) memset(rows, 0, EX_ROWS * sizeof *rows);
    return gint_world_switch(GINT_CALL(search_native, (void *)&q));
}

int ex_exists_in(const char *directory, const char *name)
{
    search_t q = { .directory = directory, .lookfor = name };
    int rc = gint_world_switch(GINT_CALL(search_native, (void *)&q));
    return rc < 0 ? rc : q.found;
}

typedef struct {
    const char *path;
    int offset, limit, size;
    uint8_t *bytes;
} io_t;

static int read_native(io_t *q)
{
    int fd = exword_sys_open(q->path, EX_READ_ONLY);
    if(fd < 0) return fd;
    int size = exword_sys_get_filesize(fd);
    if(size < 0) {
        exword_sys_close(fd);
        return size;
    }
    q->size = size;
    int rc = 0;
    if(q->offset < 0) rc = -1;
    else if(q->offset < size && q->limit > 0) {
        rc = exword_sys_seek(fd, q->offset, 0);
        if(rc >= 0) {
            int length = size-q->offset;
            if(length > q->limit) length = q->limit;
            rc = exword_sys_read(fd, q->bytes, length);
            if(rc > length) rc = -1;
        }
    }
    int closed = exword_sys_close(fd);
    if(rc >= 0 && closed < 0) return closed;
    return rc;
}

int ex_read(const char *path, int offset, uint8_t *data, int max, int *size)
{
    io_t q = { .path = path, .offset = offset, .limit = max, .bytes = data };
    if(max < 0 || !size) return -1;
    int rc = gint_world_switch(GINT_CALL(read_native, (void *)&q));
    if(rc >= 0) *size = q.size;
    return rc;
}

static int write_native(io_t *q)
{
    int fd = exword_sys_open(q->path, EX_READ_WRITE);
    if(fd < 0) return fd;
    int rc = exword_sys_seek(fd, q->offset, 0);
    if(rc >= 0) {
        rc = exword_sys_write(fd, q->bytes, 1);
        if(rc == 1) rc = 0;
        else if(rc >= 0) rc = -1;
    }
    int closed = exword_sys_close(fd);
    return rc >= 0 && closed < 0 ? closed : rc;
}

int ex_write_byte(const char *path, int offset, uint8_t byte)
{
    io_t q = { .path=path, .offset=offset, .bytes=&byte };
    return gint_world_switch(GINT_CALL(write_native, (void *)&q));
}

typedef struct {
    const char *a, *b;
    int type;
} op_t;

static int create_native(op_t *q)
{
    return exword_sys_create(q->a, q->type);
}
int ex_create(const char *path, int directory)
{
    op_t q = { .a = path, .type = directory ? EX_DIRECTORY : EX_FILE };
    return gint_world_switch(GINT_CALL(create_native, (void *)&q));
}
static int delete_native(op_t *q)
{
    return exword_sys_delete(q->a);
}
int ex_delete(const char *path)
{
    op_t q = { .a = path };
    return gint_world_switch(GINT_CALL(delete_native, (void *)&q));
}
static int rename_native(op_t *q)
{
    return exword_sys_rename(q->a, q->b);
}
int ex_rename(const char *from, const char *to)
{
    op_t q = { .a = from, .b = to };
    return gint_world_switch(GINT_CALL(rename_native, (void *)&q));
}

static int copy_native(op_t *q)
{
    uint8_t buffer[512];
    int source = exword_sys_open(q->a, EX_READ_ONLY);
    if(source < 0) return source;
    int length = exword_sys_get_filesize(source);
    if(length < 0) {
        exword_sys_close(source);
        return length;
    }
    int rc = exword_sys_create(q->b, EX_FILE);
    if(rc < 0) {
        exword_sys_close(source);
        return rc;
    }
    int destination = exword_sys_open(q->b, EX_WRITE_ONLY);
    if(destination < 0) {
        exword_sys_close(source);
        exword_sys_delete(q->b);
        return destination;
    }

    rc = 0;
    int processed = 0;
    while(processed < length) {
        int n = length-processed;
        if(n > (int)sizeof buffer) n = sizeof buffer;
        int count = exword_sys_read(source, buffer, n);
        if(count <= 0 || count > n) { rc = count < 0 ? count : -1; break; }
        int written = 0;
        while(written < count) {
            int w = exword_sys_write(destination, buffer+written, count-written);
            if(w <= 0 || w > count-written) {
                rc = w < 0 ? w : -1;
                break;
            }
            written += w;
        }
        if(rc < 0) break;
        processed += count;
    }

    int close_dest = exword_sys_close(destination);
    int close_source = exword_sys_close(source);
    if(rc >= 0 && close_dest < 0) rc = close_dest;
    if(rc >= 0 && close_source < 0) rc = close_source;
    if(rc < 0) exword_sys_delete(q->b);
    return rc;
}

int ex_copy_file(const char *from, const char *to)
{
    if(!strcmp(from, to)) return -1;
    op_t q = { .a = from, .b = to };
    return gint_world_switch(GINT_CALL(copy_native, (void *)&q));
}

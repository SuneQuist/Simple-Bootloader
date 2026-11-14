#ifndef FS_H
#define FS_H

enum { FS_BLOCK_SIZE = 512 };

/* A flat RAM filesystem. File names are at most 29 characters. */
void fs_init(void);
unsigned fs_put(const char *name, unsigned char type, const char *data, unsigned len);

/* Read-only views into the RAM filesystem. NULL means not found. */
const char *fs_read(const char *name, unsigned *len);
const char *fs_entry(unsigned slot, unsigned *ino);

unsigned fs_absolute(const char *path);
void fs_goto(const char *path);

#endif


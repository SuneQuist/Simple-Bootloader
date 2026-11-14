#ifndef FS_H
#define FS_H

enum { FS_BLOCK_SIZE = 512 };

/* A flat RAM filesystem. File names are at most 29 characters. */
void fs_init(void);

/* Read-only views into the RAM filesystem. NULL means not found. */
const char *fs_read(const char *name, unsigned *len);
const char *fs_entry(unsigned dir_ino, unsigned slot, unsigned *ino);

unsigned fs_absolute(const char *path);
const char *fs_read_exec(const char *path, unsigned *len);
void fs_goto(const char *path);

#endif


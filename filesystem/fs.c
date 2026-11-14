#include <stdlib.h>
#include "../console/console.h"

#include "fs.h"

enum { MAX_FILES = 15, NAME_CAP = 30, ROOT_INO = 1 }; 
enum { INODE_FILE = 1, INODE_DIR = 2 };

unsigned DIR_INO = ROOT_INO;

// File refernce (inode)
struct inode {
    unsigned type, size, block;
};

/*
* An INODE_DIR simply contains the ino of a differenty entry into inodes[MAX_FILEs + 2];
* A INODE_FILE is not saved inside the DIR, but in its own array and the dir refers
* to a specific index in that array.
*/
struct dirent {
    unsigned short ino;     // 2 bytes
    char name[NAME_CAP];    // 30 bytes
};

/* A block holds either file byte sor diretory entires  */
union block {
    // bytes is the field containing things like text/file content
    char bytes[FS_BLOCK_SIZE];
    /* block is also used for fiel and not just dir, but entries
       is never used for .type INODE_FILE */
    struct dirent entries[MAX_FILES]; // total size of each file entry is 32 bytes  
};

// Compile to Error if directory entries does not fit in one block;
_Static_assert(sizeof(struct dirent) * MAX_FILES <= FS_BLOCK_SIZE,
               "Directory entries must fit in one block");

static struct inode inodes[MAX_FILES + 2]; // 0 = invalid, 1 = root
static union block blocks[MAX_FILES + 1];  // block 0 = root contents

void fs_init(void) {
    inodes[DIR_INO] = (struct inode){ INODE_DIR, 0, 0 };
} 

static int same_name(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

const char *fs_entry(unsigned slot, unsigned *ino) {
    const struct inode *dir = &inodes[DIR_INO];
    if (slot >= dir->size / sizeof(struct dirent))
        return 0;

    const struct dirent *entry = &blocks[dir->block].entries[slot];
    *ino = entry->ino;
    return entry->name;
}

static unsigned fs_lookup(const char *name, unsigned ino) {
    for (unsigned i = 0;; ++i) {
        const char *entry = fs_entry(i, &ino);
        if (!entry)
            return 0;
        if (same_name(name, entry))
            return ino;
    }
}

unsigned fs_put(const char *name, unsigned char type, const char *data, unsigned len) {
    unsigned n = 0;
    while (n < NAME_CAP && name[n] && name[n] != '/')
        ++n;
    if (!n || n == NAME_CAP || name[n] != '\0' || len > FS_BLOCK_SIZE)
        return 0;

    unsigned ino = fs_lookup(name, DIR_INO);
    if (!ino) {
        struct inode *dir = &inodes[DIR_INO];
        unsigned slot = dir->size / sizeof(struct dirent);
        if (slot == MAX_FILES)
            return 0;
        ino = slot + 2;
        inodes[ino] = (struct inode){ type == 1 ? INODE_FILE : INODE_DIR, 0, slot + 1 };
        struct dirent *entry = &blocks[dir->block].entries[slot];
        entry->ino = ino;
        for (unsigned i = 0; i <= n; ++i)
            entry->name[i] = name[i];
        dir->size += sizeof(*entry);
    }

    struct inode *file = &inodes[ino];
    if (type == 1) {
        for (unsigned i = 0; i < len; ++i)
            blocks[file->block].bytes[i] = data[i];
        file->size = len;
    }

    return ino;
}

const char *fs_read(const char *name, unsigned *len) {
    unsigned ino = fs_lookup(name, DIR_INO);
    if (!ino || inodes[ino].type != INODE_FILE)
        return 0;
    *len = inodes[ino].size;
    return blocks[inodes[ino].block].bytes;
}

unsigned fs_absolute(const char *path) {
    if (path[0] != '/')
        return 0;  /* This function only handles absolute paths. */

    unsigned ino = DIR_INO;

    for (;;) {
        while (*path == '/')
            ++path;

        if (*path == '\0')
            return ino;  /* "/" returns the root. */

        char name[NAME_CAP];
        unsigned len = 0;

        while (*path && *path != '/') {
            if (len == sizeof(name) - 1)
                return 0;  /* Leave space for '\0'. */
            name[len++] = *path++;
        }
        name[len] = '\0';

        ino = fs_lookup(name, ino);
        if (!ino)
            return 0;

        /* A slash after a component requires it to be a directory. */
        if (*path == '/' && inodes[ino].type != INODE_DIR)
            return 0;
    }
}

void fs_goto(const char *path) {
    unsigned ino = fs_absolute(path);
    if (!ino) {
        console_print("Path not found\n");
        return;
    }
    
    struct inode *node = &inodes[ino];
    if (node->type == 2) {
        DIR_INO = ino;
        console_print("Entered folder:" + *path);
        return;
    }

    if (node->type == 1) {
        console_print("This is a file, not a folder");
        return;    
    }
}

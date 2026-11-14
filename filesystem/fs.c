#include "../console/console.h"
#include "../account/account.h"
#include "fs.h"

enum { MAX_FILES = 15, NAME_CAP = 30, ROOT_INO = 1 };
enum { INODE_FILE = 1, INODE_DIR = 2 };
enum { FS_X = 1u, FS_W = 2u, FS_R = 4u };

unsigned DIR_INO = ROOT_INO;  /* Current directory, not always root. */
static unsigned next_ino;

struct inode {
    unsigned type, size, block;
    unsigned owner, mode;
};

struct dirent {
    unsigned short ino;
    char name[NAME_CAP];
};

union block {
    char bytes[FS_BLOCK_SIZE];
    struct dirent entries[MAX_FILES];
};

_Static_assert(sizeof(struct dirent) * MAX_FILES <= FS_BLOCK_SIZE,
               "Directory entries must fit in one block");

static struct inode inodes[MAX_FILES + 2];
static union block blocks[MAX_FILES + 1];

static int same_name(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int fs_allowed(const struct inode *node, unsigned uid,
                      unsigned required) {
    if (uid == ACCOUNT_NONE)
        return 0;
    if (uid == UID_ROOT)
        return 1;

    unsigned bits = uid == node->owner ? node->mode >> 6 : node->mode;
    return (bits & required) == required;
}

/* Look up one name inside the directory we were actually given. */
static unsigned fs_lookup(const char *name, unsigned dir_ino) {
    const struct inode *dir = &inodes[dir_ino];
    const struct dirent *entries = blocks[dir->block].entries;

    for (unsigned i = 0; i < dir->size / sizeof(*entries); ++i)
        if (same_name(name, entries[i].name))
            return entries[i].ino;

    return 0;
}

/* If leaf is null, return the target inode.
   Otherwise, put the last name in leaf and return its parent inode. */
static unsigned walk(const char *path, unsigned uid, char *leaf) {
    if (!path || !*path || uid == ACCOUNT_NONE)
        return 0;

    unsigned ino = *path == '/' ? ROOT_INO : DIR_INO;

    while (*path == '/')
        ++path;
    if (!*path)
        return leaf ? 0 : ino;  /* "/" names root, not a new entry. */

    for (;;) {
        if (inodes[ino].type != INODE_DIR ||
            !fs_allowed(&inodes[ino], uid, FS_X))
            return 0;

        char name[NAME_CAP];
        unsigned n = 0;
        while (*path && *path != '/') {
            if (n == NAME_CAP - 1)
                return 0;
            name[n++] = *path++;
        }
        name[n] = '\0';

        /* Parent navigation isn't implemented yet. */
        if (same_name(name, ".") || same_name(name, ".."))
            return 0;

        int had_slash = *path == '/';
        while (*path == '/')
            ++path;

        if (!*path && leaf) {
            if (had_slash)
                return 0;  /* No trailing slash when saving or creating. */
            for (unsigned i = 0; i <= n; ++i)
                leaf[i] = name[i];
            return ino;
        }

        ino = fs_lookup(name, ino);
        if (!ino)
            return 0;

        if (!*path)
            return had_slash && inodes[ino].type != INODE_DIR ? 0 : ino;
    }
}

/* Private helper so boot setup can seed files before anyone logs in. */
static unsigned put_as(const char *path, unsigned type, const char *data,
                       unsigned len, unsigned uid, unsigned mode) {
    if ((type != INODE_FILE && type != INODE_DIR) ||
        len > FS_BLOCK_SIZE || (len && (!data || type == INODE_DIR)))
        return 0;

    char leaf[NAME_CAP];
    unsigned parent = walk(path, uid, leaf);
    if (!parent)
        return 0;

    struct inode *dir = &inodes[parent];
    unsigned ino = fs_lookup(leaf, parent);

    if (ino) {
        if (type != INODE_FILE || inodes[ino].type != INODE_FILE ||
            !fs_allowed(&inodes[ino], uid, FS_W))
            return 0;
    } else {
        unsigned slot = dir->size / sizeof(struct dirent);
        if (!fs_allowed(dir, uid, FS_W | FS_X) ||
            slot >= MAX_FILES || next_ino >= MAX_FILES + 2)
            return 0;

        ino = next_ino++;
        inodes[ino] = (struct inode){ type, 0, ino - 1, uid, mode };

        struct dirent *entry = &blocks[dir->block].entries[slot];
        entry->ino = ino;
        for (unsigned i = 0; (entry->name[i] = leaf[i]) != '\0'; ++i)
            ;
        dir->size += sizeof(*entry);
    }

    if (type == INODE_FILE) {
        struct inode *file = &inodes[ino];
        for (unsigned i = 0; i < len; ++i)
            blocks[file->block].bytes[i] = data[i];
        file->size = len;
    }
    return ino;
}

void fs_init(void) {
    DIR_INO = ROOT_INO;
    next_ino = ROOT_INO + 1;
    inodes[ROOT_INO] = (struct inode){ INODE_DIR, 0, 0, UID_ROOT, 0755 };

    put_as("/bin", INODE_DIR, 0, 0, UID_ROOT, 0755);

    static const char ls[] = "listdir \"$@\"\n";
    static const char hello[] = "echo 'HELLO WORLD!'\n";
    put_as("/bin/ls", INODE_FILE, ls, sizeof ls - 1, UID_ROOT, 0755);
    put_as("/bin/hello", INODE_FILE, hello, sizeof hello - 1, UID_ROOT, 0755);
}

unsigned fs_put(const char *path, unsigned char type,
                const char *data, unsigned len) {
    return put_as(path, type, data, len, account_uid(),
                  type == INODE_DIR ? 0755 : 0644);
}

static const char *read_as(const char *path, unsigned *len,
                           unsigned required) {
    unsigned uid = account_uid();
    unsigned ino = walk(path, uid, 0);
    if (!ino || inodes[ino].type != INODE_FILE ||
        !fs_allowed(&inodes[ino], uid, required))
        return 0;

    *len = inodes[ino].size;
    return blocks[inodes[ino].block].bytes;
}

const char *fs_read(const char *path, unsigned *len) {
    return read_as(path, len, FS_R);
}

const char *fs_read_exec(const char *path, unsigned *len) {
    return read_as(path, len, FS_R | FS_X);
}

const char *fs_entry(unsigned dir_ino, unsigned slot, unsigned *ino) {
    if (dir_ino == 0 || dir_ino >= next_ino)
        return 0;

    const struct inode *dir = &inodes[dir_ino];
    if (dir->type != INODE_DIR ||
        !fs_allowed(dir, account_uid(), FS_R) ||
        slot >= dir->size / sizeof(struct dirent))
        return 0;

    const struct dirent *entry = &blocks[dir->block].entries[slot];
    *ino = entry->ino;
    return entry->name;
}

unsigned fs_absolute(const char *path) {
    if (!path || *path != '/')
        return 0;
    return walk(path, account_uid(), 0);
}

void fs_goto(const char *path) {
    unsigned uid = account_uid();
    unsigned ino = walk(path, uid, 0);

    if (!ino || inodes[ino].type != INODE_DIR ||
        !fs_allowed(&inodes[ino], uid, FS_X)) {
        console_print("Cannot enter directory\n");
        return;
    }

    DIR_INO = ino;
}

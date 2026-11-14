enum { BLOCK_SIZE = 512, MAX_FILES = 15, NAME_CAP = 30 }; 
enum { INODE_FILE = 1, INODE_DIR = 2 };

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
    char bytes[BLOCK_SIZE];
    /* block is also used for fiel and not just dir, but entries
       is never used for .type INODE_FILE */
    struct dirent entries[MAX_FILES]; // total size of each file entry is 32 bytes  
};

static struct inode inodes[MAX_FILES + 2]; // 0 = invalid, 1 = root
static union block blocks[MAX_FILES + 1];  // block 0 = root contents

static void fs_init(void) {
    inodes[1] = (struct inode){ .type = INODE_DIR, .size = 0, .block = 0 };
} 

static int same_name(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static unsigned fs_lookup(const char *name) {
    const struct inode *root = &inodes[1];
    const struct dirent *entries = blocks[root->block].entries;

    for (unsigned i = 0; i < root->size / sizeof(*entries); i++) {
       if (same_name(entries[i].name, name))
            return entries[i].ino;
    }

    return 0;
}

// Create or replace a file. Return its inode number, or 0 on failure
static unsigned fs_put(const char *name, const char *data, unsigned len) {
    unsigned n = 0;
    while (n < NAME_CAP && name[n] && name[n] != '/')
        ++n;
    if (!n || n == NAME_CAP || name[n] != '\0' || len > BLOCK_SIZE)
        return 0;

    unsigned ino = fs_lookup(name);
    if (!ino) {
        struct inode *root = &inodes[1];
        unsigned slot = root->size / sizeof(struct dirent);
        if (slot == MAX_FILES)
            return 0;

        ino = slot + 2;
        inodes[ino] = (struct inode){ .type = INODE_FILE, .block = slot + 1 };
        
        struct dirent *entry = &blocks[root->block].entries[slot];
        entry->ino = ino;
        for (unsigned i = 0; i <= n; ++i)
            entry->name[i] = name[i];
        root->size += sizeof(*entry);
    }

    struct inode *file = &inodes[ino];
    for (unsigned i = 0; i < len; i++)
        blocks[file->block].bytes[i] = data[i];
    file->size = len;
    return ino;
}

static void vga_write(unsigned pos, const char *text, unsigned len) {
    volatile unsigned short *vga = (volatile unsigned short *)0xB8000;
    for (unsigned i = 0; i < len; i++)
        vga[pos + i] = 0x0F00u | (unsigned char)text[i];
}

static void ls(void) {
    const struct inode *root = &inodes[1];
    const struct dirent *entries = blocks[root->block].entries;
    // 2 entries would mean roo->size(64) / sizeof(*entires)(32) [64/32] = 2 entries;
    unsigned count = root->size / sizeof(*entries);

    for (int unsigned i = 0; i < count + 0; i++) {
        const struct dirent *entry = &entries[i];
        // +1 otherwise it starts at row 0 and intersects with QEMUs extension text.
        unsigned row = (i+1) * 80; // One row per entry in 80-column VGA mode
        
        // inode number from dirent fits into 2 bytes
        /* ino < 10 then its number is 0-9, else we divide by 1, 
         * so e.g. 16/10 = 1;
         * Modulo gives us the remainder, so 16/10 = 1, and 16 % 10 = 6,
         * combined it gives 16;
         * The first leading space ' ' mean is we get 2, then it won't write 02, 
         * but just 2 instead. The '0' on the second entry turns a number into
         * into its display charcter, meaning it goes from '0' + 6 to '6';
        */
        char number[2] = {
            entry->ino < 10 ? ' ' : '0' + entry->ino / 10,
            '0' + entry->ino % 10
        };

        unsigned len = 0;
        while (entry->name[len] != '\0')
            ++len;

        vga_write(row, number, 2);
        vga_write(row + 3, entry->name, len);
    }
}

static void cat(const char *name) {
    unsigned ino = fs_lookup(name);
    if (!ino) {
        vga_write(0, "File not found", sizeof("File not found") - 1);
        return;
    }
    
    struct inode *file = &inodes[ino];
    char *file_contents = blocks[file->block].bytes;    
    
    unsigned row = 0;
    unsigned col  = 0;
    for (unsigned i = 0; i < file->size && row < 25; i++) {
        // Line end by null terminator or \n;
        if (file_contents[i] == '\0' || file_contents[i] == '\n' || col == 80) {
            ++row;
            col = 0;
            if (row == 25)
                break;
            continue;
        }
    
        vga_write(row * 80 + col, &file_contents[i], 1);
        col++;
    }
}

void kernel_main(void) {
    static const char msg[] = "Hello from notes.txt";
    static const char msg2[] = "Hello World!\nThis is a new line!";

    fs_init();
    fs_put("notes.txt", msg, sizeof(msg) - 1);
    fs_put("test.txt", msg2, sizeof(msg2) - 1);

    //unsigned ino = fs_lookup("notes.txt");
    ls();
    cat("test.txt");

    for (;;)
        __asm__ volatile ("cli; hlt");
}


#include "history.h"
#include "../filesystem/fs.h"

#define HISTORY_PATH "/vars/history"
#define HISTORY_SIZE FS_BLOCK_SIZE

static unsigned str_length(const char *s)
{
    unsigned n = 0;

    while (s[n])
        n++;

    return n;
}

void history_add(const char *command)
{
    if (!command || !command[0])
        return;

    unsigned old_len = 0;
    const char *old = fs_read(HISTORY_PATH, &old_len);

    if (!old)
        return;

    unsigned cmd_len = str_length(command);

    if (cmd_len == 0 || cmd_len >= HISTORY_SIZE)
        return;

    char buffer[HISTORY_SIZE];

    /*
     * Keep only enough old history to fit
     * the new command plus its newline.
     */
    unsigned start = 0;

    while (old_len - start + cmd_len + 1 > HISTORY_SIZE) {
        while (start < old_len && old[start] != '\n')
            start++;

        if (start < old_len)
            start++;
    }

    unsigned len = 0;

    for (unsigned i = start; i < old_len; i++)
        buffer[len++] = old[i];

    for (unsigned i = 0; i < cmd_len; i++)
        buffer[len++] = command[i];

    buffer[len++] = '\n';

    fs_put(HISTORY_PATH, FS_FILE, buffer, len);
}

unsigned history_count(void)
{
    unsigned len = 0;
    const char *text = fs_read(HISTORY_PATH, &len);

    if (!text)
        return 0;

    unsigned count = 0;

    for (unsigned i = 0; i < len; i++) {
        if (text[i] == '\n')
            count++;
    }

    return count;
}

int history_get(unsigned index, char *out, unsigned capacity)
{
    if (!out || capacity == 0)
        return 0;

    unsigned len = 0;
    const char *text = fs_read(HISTORY_PATH, &len);

    if (!text)
        return 0;

    unsigned current = 0;
    unsigned start = 0;

    for (unsigned i = 0; i < len; i++) {
        if (text[i] != '\n')
            continue;

        if (current == index) {
            unsigned size = i - start;

            if (size >= capacity)
                return 0;

            for (unsigned j = 0; j < size; j++)
                out[j] = text[start + j];

            out[size] = '\0';

            return 1;
        }

        current++;
        start = i + 1;
    }

    return 0;
}
#ifndef COMMON_H
#define COMMON_H

#include <windows.h>

#define CHUNK 4096

typedef struct {
    DWORD len;
    char data[CHUNK];
} Shared;

static void say(HANDLE h, const char *text, DWORD len)
{
    DWORD written = 0;
    WriteFile(h, text, len, &written, NULL);
}

static void make_name(char *out, const char *base, const char *suffix)
{
    wsprintfA(out, "%s_%s", base, suffix);
}

#endif
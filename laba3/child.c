#include "common.h"

typedef struct {
    HANDLE mem;         
    Shared *shared;    
    HANDLE sem_empty;   
    HANDLE sem_full;    
    HANDLE proc;        
} Channel;

int read_name(HANDLE in, char *name, int max_len)
{
    int len = 0;
    char c;
    DWORD got = 0;

    while (ReadFile(in, &c, 1, &got, NULL) && got == 1) {
        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            continue;
        }
        if (len < max_len - 1) {
            name[len] = c;
            len++;
        }
    }
    name[len] = '\0';
    return len;
}

int start_child(const char *file_name, int index, Channel *ch)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    char base[64];
    char mem_name[96];
    char empty_name[96];
    char full_name[96];
    char cmd[512];
    const char msg_mem[] = "error: failed to create shared memory\r\n";
    const char msg_map[] = "error: failed to map shared memory\r\n";
    const char msg_sem[] = "error: failed to create semaphore\r\n";
    const char msg_proc[] = "error: failed to start child.exe\r\n";

    wsprintfA(base, "lab3_%lu_%d", GetCurrentProcessId(), index);
    make_name(mem_name, base, "mem");
    make_name(empty_name, base, "empty");
    make_name(full_name, base, "full");

    ch->mem = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                 0, sizeof(Shared), mem_name);
    if (ch->mem == NULL) {
        say(err, msg_mem, sizeof(msg_mem) - 1);
        return 0;
    }

    ch->shared = (Shared *)MapViewOfFile(ch->mem, FILE_MAP_ALL_ACCESS,
                                         0, 0, sizeof(Shared));
    if (ch->shared == NULL) {
        say(err, msg_map, sizeof(msg_map) - 1);
        return 0;
    }
    ch->shared->len = 0;

    ch->sem_empty = CreateSemaphoreA(NULL, 1, 1, empty_name);
    ch->sem_full = CreateSemaphoreA(NULL, 0, 1, full_name);
    if (ch->sem_empty == NULL || ch->sem_full == NULL) {
        say(err, msg_sem, sizeof(msg_sem) - 1);
        return 0;
    }

    wsprintfA(cmd, "child.exe %s \"%s\"", base, file_name);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        say(err, msg_proc, sizeof(msg_proc) - 1);
        return 0;
    }

    CloseHandle(pi.hThread);
    ch->proc = pi.hProcess;
    return 1;
}

int send_chunk(Channel *ch, const char *data, DWORD len)
{
    DWORD i;
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    const char msg[] = "error: semaphore failure\r\n";

    if (WaitForSingleObject(ch->sem_empty, INFINITE) != WAIT_OBJECT_0) {
        say(err, msg, sizeof(msg) - 1);
        return 0;
    }

    for (i = 0; i < len; i++) {
        ch->shared->data[i] = data[i];
    }
    ch->shared->len = len;

    if (!ReleaseSemaphore(ch->sem_full, 1, NULL)) {
        say(err, msg, sizeof(msg) - 1);
        return 0;
    }
    return 1;
}

void close_channel(Channel *ch)
{
    UnmapViewOfFile(ch->shared);
    CloseHandle(ch->mem);
    CloseHandle(ch->sem_empty);
    CloseHandle(ch->sem_full);
    CloseHandle(ch->proc);
}

int main(void)
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE procs[2];
    Channel ch1;
    Channel ch2;
    Channel *target;
    char name1[256];
    char name2[256];
    char piece[CHUNK];
    DWORD n = 0;
    char c;
    DWORD got = 0;

    const char ask1[] = "File name for child1: ";
    const char ask2[] = "File name for child2: ";
    const char hint[] = "Enter lines (end of input: Ctrl+Z, then Enter):\r\n";
    const char msg_empty[] = "error: empty file name\r\n";

    say(out, ask1, sizeof(ask1) - 1);
    if (read_name(in, name1, sizeof(name1)) == 0) {
        say(err, msg_empty, sizeof(msg_empty) - 1);
        return 1;
    }

    say(out, ask2, sizeof(ask2) - 1);
    if (read_name(in, name2, sizeof(name2)) == 0) {
        say(err, msg_empty, sizeof(msg_empty) - 1);
        return 1;
    }

    if (!start_child(name1, 1, &ch1)) {
        return 1;
    }
    if (!start_child(name2, 2, &ch2)) {
        return 1;
    }

    say(out, hint, sizeof(hint) - 1);
    target = &ch1;

    while (ReadFile(in, &c, 1, &got, NULL) && got == 1) {
        if (c == '\r') {
            continue;
        }

        piece[n] = c;
        n++;

        if (c == '\n' || n == CHUNK) {
            if (!send_chunk(target, piece, n)) {
                return 1;
            }
            n = 0;
        }

        if (c == '\n') {
            if (target == &ch1) {
                target = &ch2;
            } else {
                target = &ch1;
            }
        }
    }

    if (n > 0) {
        if (!send_chunk(target, piece, n)) {
            return 1;
        }
    }

    if (!send_chunk(&ch1, piece, 0) || !send_chunk(&ch2, piece, 0)) {
        return 1;
    }

    procs[0] = ch1.proc;
    procs[1] = ch2.proc;
    WaitForMultipleObjects(2, procs, TRUE, INFINITE);

    close_channel(&ch1);
    close_channel(&ch2);

    return 0;
}
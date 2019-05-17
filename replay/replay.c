#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>

#define OP_ALLOC   1
#define OP_FREE    2
#define OP_REALLOC 3

typedef struct Data Data;
typedef struct Op Op;

struct Op {
    uint64_t kind;
    union {
        struct {
            uint64_t slot;
            uint64_t timestamp;
            uint64_t size;
        } alloc;
        struct {
            uint64_t slot;
            uint64_t timestamp;
        } free;
        struct {
            uint64_t slot;
            uint64_t timestamp;
            uint64_t size;
        } realloc;
    };
};

struct Data {
    uint64_t slot_count;
    uint64_t operation_count;
    Op operations[];
};

void * mmap_file(const char * path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open failed");
        exit(1);
    }

    struct stat sb;
    fstat(fd, &sb);
    void * result = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (result == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    return result;
}

void * mmap_anonymous(size_t size) {
    void * result = mmap(NULL, (size + 4096) & ~4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (result == MAP_FAILED) {
        perror("anonymous mmap failed");
        exit(1);
    }
    return result;
}

void default_set_marker(uint32_t marker) {
}

void default_override_next_timestamp(uint64_t timestamp) {
}

typedef void (*override_next_timestamp_t)(uint64_t timestamp);
typedef void (*set_marker_t)(uint32_t marker);

int main(int argc, char * argv[]) {
    if (argc != 2) {
        fprintf(stderr, "syntax: replay <replay.dat>\n");
        return 1;
    }

    set_marker_t set_marker = dlsym(RTLD_DEFAULT, "memory_profiler_set_marker");
    override_next_timestamp_t override_next_timestamp = dlsym(RTLD_DEFAULT, "memory_profiler_override_next_timestamp");

    if (!set_marker) {
        set_marker = default_set_marker;
    }

    if (!override_next_timestamp) {
        override_next_timestamp = default_override_next_timestamp;
    }

    Data * data = (Data *)mmap_file(argv[1]);
    void ** slots = (void **)mmap_anonymous(data->slot_count * sizeof(void *));
    size_t count = 0;

    for (size_t i = 0; i < data->operation_count; ++i) {
        const Op * op = &data->operations[i];
        switch (op->kind) {
            case OP_ALLOC:
                count++;
                if (slots[op->alloc.slot] != NULL) {
                    abort();
                }
                override_next_timestamp(op->alloc.timestamp);
                slots[op->alloc.slot] = malloc(op->alloc.size);
                break;
            case OP_FREE:
                override_next_timestamp(op->free.timestamp);
                free(slots[op->free.slot]);
                slots[op->free.slot] = NULL;
                break;
            case OP_REALLOC:
                count++;
                override_next_timestamp(op->realloc.timestamp);
                slots[op->realloc.slot] = realloc(slots[op->realloc.slot], op->realloc.size);
                break;
        }
    }

    printf("free: %i\n", mallinfo().fordblks);
    printf("fast free: %i\n", mallinfo().fsmblks);
    printf("fast free blocks: %i\n", mallinfo().smblks);

    return 0;
}

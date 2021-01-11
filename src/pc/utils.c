#include "utils.h"
#include "game/object_list_processor.h"
#include "game/object_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

//
// C dynamic array, vector-like struct
//

static void __dynamic_array_realloc(struct __DynamicArray *da, int newcapacity) {
    if (newcapacity > da->capacity) {
        void *newbuffer = calloc(newcapacity, da->itemsize);
        if (da->buffer) {
            memcpy(newbuffer, da->buffer, da->count * da->itemsize);
            free(da->buffer);
        }
        da->buffer = newbuffer;
        da->capacity = newcapacity;
    }
}

void __dynamic_array_init(struct __DynamicArray *da, int itemsize) {
    da->buffer   = NULL;
    da->count    = 0;
    da->capacity = 0;
    da->itemsize = itemsize;
}

void __dynamic_array_add(struct __DynamicArray *da, void *item) {
    __dynamic_array_realloc(da, da->count + 1);
    memcpy((void *) ((size_t) da->buffer + (size_t) da->count * (size_t) da->itemsize), item, (size_t) da->itemsize);
    da->count++;
}

void __dynamic_array_rem(struct __DynamicArray *da, int index) {
    if (index == -1) {
        da->count = 0;
    } else if (index >= da->count - 1) {
        da->count--;
    } else {
        memmove((void *) ((size_t) da->buffer + (size_t) (index + 0) * (size_t) da->itemsize),
                (void *) ((size_t) da->buffer + (size_t) (index + 1) * (size_t) da->itemsize),
                                    (size_t) (da->count - index - 1) * (size_t) da->itemsize);
        da->count--;
    }
}

void __dynamic_array_clr(struct __DynamicArray *da) {
    if (da->buffer) {
        free(da->buffer);
    }
    da->buffer   = NULL;
    da->count    = 0;
    da->capacity = 0;
}

void *__dynamic_array_get(struct __DynamicArray *da, int index) {
    return (void *) ((size_t) da->buffer + (size_t) index * (size_t) da->itemsize);
}

void __dynamic_array_set(struct __DynamicArray *da, int index, void *item) {
    memcpy((void *) ((size_t) da->buffer + (size_t) index * (size_t) da->itemsize), item, (size_t) da->itemsize);
}

int __dynamic_array_find(struct __DynamicArray *da, void *item) {
    void *curr = da->buffer;
    void *end = (void *) ((size_t) da->buffer + (size_t) da->count * (size_t) da->itemsize);
    for (int i = 0; curr != end; ++i) {
        if (memcmp(curr, item, (size_t) da->itemsize) == 0) {
            return i;
        }
        curr = (void *) ((size_t) curr + (size_t) da->itemsize);
    }
    return -1;
}

int __dynamic_array_find_eq(struct __DynamicArray *da, void *item, bool (*eqfunc)(void *, void *)) {
    void *curr = da->buffer;
    void *end = (void *) ((size_t) da->buffer + (size_t) da->count * (size_t) da->itemsize);
    for (int i = 0; curr != end; ++i) {
        if (eqfunc(curr, item)) {
            return i;
        }
        curr = (void *) ((size_t) curr + (size_t) da->itemsize);
    }
    return -1;
}

//
// C String to SM64 String conversion
//

static const struct { const char *str; unsigned char c64; int w; } sSm64CharMap[] = {
    { "0",   0x00, 7 }, { "1",  0x01,  7 }, { "2",   0x02, 7 }, { "3",   0x03, 7 }, { "4",   0x04,  7 }, { "5",   0x05,  7 },
    { "6",   0x06, 7 }, { "7",  0x07,  7 }, { "8",   0x08, 7 }, { "9",   0x09, 7 }, { "A",   0x0A,  6 }, { "B",   0x0B,  6 },
    { "C",   0x0C, 6 }, { "D",  0x0D,  6 }, { "E",   0x0E, 6 }, { "F",   0x0F, 6 }, { "G",   0x10,  6 }, { "H",   0x11,  6 },
    { "I",   0x12, 5 }, { "J",  0x13,  6 }, { "K",   0x14, 6 }, { "L",   0x15, 5 }, { "M",   0x16,  8 }, { "N",   0x17,  8 },
    { "O",   0x18, 6 }, { "P",  0x19,  6 }, { "Q",   0x1A, 6 }, { "R",   0x1B, 6 }, { "S",   0x1C,  6 }, { "T",   0x1D,  5 },
    { "U",   0x1E, 6 }, { "V",  0x1F,  6 }, { "W",   0x20, 8 }, { "X",   0x21, 7 }, { "Y",   0x22,  6 }, { "Z",   0x23,  6 },
    { "a",   0x24, 6 }, { "b",  0x25,  5 }, { "c",   0x26, 5 }, { "d",   0x27, 6 }, { "e",   0x28,  5 }, { "f",   0x29,  5 },
    { "g",   0x2A, 6 }, { "h",  0x2B,  5 }, { "i",   0x2C, 4 }, { "j",   0x2D, 5 }, { "k",   0x2E,  5 }, { "l",   0x2F,  3 },
    { "m",   0x30, 7 }, { "n",  0x31,  5 }, { "o",   0x32, 5 }, { "p",   0x33, 5 }, { "q",   0x34,  6 }, { "r",   0x35,  5 },
    { "s",   0x36, 5 }, { "t",  0x37,  5 }, { "u",   0x38, 5 }, { "v",   0x39, 5 }, { "w",   0x3A,  7 }, { "x",   0x3B,  7 },
    { "y",   0x3C, 5 }, { "z",  0x3D,  5 }, { "\'",  0x3E, 4 }, { ".",   0x3F, 4 }, { "^",   0x50,  8 }, { "|",   0x51,  8 },
    { "<",   0x52, 8 }, { ">",  0x53,  8 }, { "[A]", 0x54, 7 }, { "[B]", 0x55, 7 }, { "[C]", 0x56,  6 }, { "[Z]", 0x57,  7 },
    { "[R]", 0x58, 7 }, { ",",  0x6F,  4 }, { " ",   0x9E, 5 }, { "-",   0x9F, 6 }, { "/",   0xD0, 10 }, { "[%]", 0xE0,  7 },
    { "(",   0xE1, 5 }, { ")(", 0xE2, 10 }, { ")",   0xE3, 5 }, { "+",   0xE4, 9 }, { "&",   0xE5,  8 }, { ":",   0xE6,  4 },
    { "!",   0xF2, 5 }, { "%",  0xF3,  7 }, { "?",   0xF4, 7 }, { "~",   0xF7, 8 }, { "$",   0xF9,  8 }, { "@",   0xFA, 10 },
    { "*",   0xFB, 6 }, { "¤",  0xFD, 10 }, { "\n",  0xFE, 0 }, { "\0",  0xFF, 0 },
};
static const int sSm64CharCount = sizeof(sSm64CharMap) / sizeof(sSm64CharMap[0]);

static const char *__sm64_add_char(unsigned char *str64, const char *str, int *i) {
    for (int k = 0; k != sSm64CharCount; ++k) {
        if (strstr(str, sSm64CharMap[k].str) == str) {
            str64[(*i)++] = sSm64CharMap[k].c64;
            return str + strlen(sSm64CharMap[k].str);
        }
    }
    return str + 1;
}

#define STRING_MAX_LENGTH   2048
unsigned char *__sm64_string(bool heapAlloc, const char *fmt, ...) {

    // Format
    char buffer[STRING_MAX_LENGTH];
    va_list arg;
    va_start(arg, fmt);
    vsnprintf(buffer, STRING_MAX_LENGTH, fmt, arg);
    va_end(arg);

    // Allocation
    static unsigned char sStringBuffer[8][STRING_MAX_LENGTH];
    static unsigned int sStringBufferIndex = 0;
    unsigned char *str64;
    if (heapAlloc) {
        str64 = calloc(STRING_MAX_LENGTH, sizeof(unsigned char));
    } else {
        str64 = sStringBuffer[sStringBufferIndex];
        sStringBufferIndex = (sStringBufferIndex + 1) % 8;
    }

    // Conversion
    memset(str64, 0xFF, STRING_MAX_LENGTH);
    const char *str = &buffer[0];
    for (int i = 0; *str != 0 && i < STRING_MAX_LENGTH - 1;) {
        str = __sm64_add_char(str64, str, &i);
    }
    return str64;
}

unsigned char *__sm64_string_decapitalize(unsigned char *str64) {
    bool wasSpace = true;
    for (unsigned char *p = str64; *p != 255; p++) {
        if (*p >= 10 && *p <= 35) {
            if (wasSpace) wasSpace = false;
            else *p += 26;
        } else if (*p >= 63) {
            wasSpace = true;
        }
    }
    return str64;
}

int __sm64_strlen(const unsigned char *str64) {
    int len = 0;
    for (; str64 && *str64 != 255; str64++, len++);
    return len;
}

int __sm64_cwidth(unsigned char c64) {
    for (int k = 0; k != sSm64CharCount; ++k) {
        if (sSm64CharMap[k].c64 == c64) {
            return sSm64CharMap[k].w;
        }
    }
    return 0;
}

//
// Dynamic Object Graph Node allocation
//

#define ALLOC_SIZE 0x10000
static DA sLoadedGraphNodes = da_type(void *);

void *get_graph_node_from_geo(const void *geoLayout) {
    int i = da_find(sLoadedGraphNodes, geoLayout);
    if (i != -1) {
        return da_get(sLoadedGraphNodes, i + 1, void *);
    }

    struct AllocOnlyPool *pool = calloc(1, ALLOC_SIZE);
    pool->totalSpace = ALLOC_SIZE;
    pool->usedSpace  = 0;
    pool->startPtr   = (u8 *) pool + sizeof(struct AllocOnlyPool);
    pool->freePtr    = (u8 *) pool + sizeof(struct AllocOnlyPool);
    void *graphNode  = process_geo_layout(pool, (void *) geoLayout);
    if (graphNode) {
        da_add(sLoadedGraphNodes, geoLayout);
        da_add(sLoadedGraphNodes, graphNode);
    }
    return graphNode;
}

void *obj_spawn_with_geo(void *parent, const void *geoLayout, const void *behavior) {
    struct Object *obj = spawn_object(parent, 0, behavior);
    obj->header.gfx.sharedChild = (struct GraphNode *) get_graph_node_from_geo(geoLayout);
    return obj;
}

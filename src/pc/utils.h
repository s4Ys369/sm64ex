#ifndef PC_UTILS_H
#define PC_UTILS_H

#include <stdbool.h>

/* C dynamic array, vector-like struct */
struct __DynamicArray {
    void *buffer;
    int count;
    int capacity;
    int itemsize;
};
void  __dynamic_array_init(struct __DynamicArray *da, int itemsize);
void  __dynamic_array_add(struct __DynamicArray *da, void *item);
void  __dynamic_array_rem(struct __DynamicArray *da, int index);
void  __dynamic_array_clr(struct __DynamicArray *da);
void *__dynamic_array_get(struct __DynamicArray *da, int index);
void  __dynamic_array_set(struct __DynamicArray *da, int index, void *item);
int   __dynamic_array_find(struct __DynamicArray *da, void *item);
int   __dynamic_array_find_eq(struct __DynamicArray *da, void *item, bool (*eqfunc)(void *, void *));

#define DA                              struct __DynamicArray
#define da_type(type)                   { NULL, 0, 0, sizeof(type) }
#define da_init(da, type)               __dynamic_array_init(&(da), sizeof(type))
#define da_add(da, item)                __dynamic_array_add(&(da), (void *) &(item))
#define da_rem(da, index)               __dynamic_array_rem(&(da), (index))
#define da_rem_all(da)                  __dynamic_array_rem(&(da), -1)
#define da_clr(da)                      __dynamic_array_clr(&(da))
#define da_get(da, index, type)         (*((type *) __dynamic_array_get(&(da), (index))))
#define da_getp(da, index, type)        ((type *) __dynamic_array_get(&(da), (index)))
#define da_set(da, index, item)         __dynamic_array_set(&(da), (index), (void *) &(item))
#define da_item(ptr, type)              (*((type *) ptr))
#define da_find(da, item)               __dynamic_array_find(&(da), (void *) &(item))
#define da_find_eq(da, item, eqfunc)    __dynamic_array_find_eq(&(da), (void *) &(item), eqfunc)

/* String conversion */
unsigned char *__sm64_string(bool heapAlloc, const char *fmt, ...);
unsigned char *__sm64_string_decapitalize(unsigned char *str64);
int __sm64_strlen(const unsigned char *str64);
int __sm64_cwidth(unsigned char c64);

#define str64s(...)    __sm64_string(false, __VA_ARGS__)  /* String allocated on stack (circular buffer of static strings) */
#define str64h(...)    __sm64_string(true,  __VA_ARGS__)  /* String allocated on heap */
#define str64l(str64)  __sm64_strlen(str64)               /* String length */
#define str64d(str64)  __sm64_string_decapitalize(str64)  /* String decapitalization (does not alloc a new string, return the modified string) */
#define str64w(c64)    __sm64_cwidth(c64)                 /* SM64 Char width */

/* Dynamically allocated graph nodes */
void *get_graph_node_from_geo(const void *geoLayout);
void *obj_spawn_with_geo(void *parent, const void *geoLayout, const void *behavior);

#endif
#ifndef __alloc_h__
#define __alloc_h__

#ifdef USE_SHACO_MALLOC
#include <stdlib.h>
void *shaco_malloc(size_t size);
void *shaco_realloc(void *ptr, size_t size);
void *shaco_calloc(size_t nmemb, size_t size);
void  shaco_free(void *ptr);
#define malloc shaco_malloc
#define realloc shaco_realloc
#define calloc shaco_calloc
#define free shaco_free
#else
#include <stdlib.h>
#endif

#endif

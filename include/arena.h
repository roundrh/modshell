#ifndef ARENA_H
#define ARENA_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REGION_DEF_CAP (512 * 1024)

typedef struct s_reg {
  struct s_reg *next;
  size_t cap;
  size_t off;
  char data[];
} t_region;

typedef struct s_arena {
  t_region *head;
  t_region *curr;
} t_arena;

void *arena_realloc(t_arena *a, void *optr, size_t nsize, size_t osize);
void *arena_alloc(t_arena *a, size_t s);
void arena_reset(t_arena *a);
void arena_free(t_arena *a);
void arena_init(t_arena *a);

#endif // ARENA_H

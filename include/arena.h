#ifndef ARENA_H
#define ARENA_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define ARENA_GROWTH_FACTOR 2
#define ARENA_INITIAL_CAP (512 * 1024)

typedef struct s_arena {
  void *mem;
  size_t cap;
  size_t off;
} t_arena;

void *arena_alloc(t_arena *a, size_t s);
void arena_sect_reset(t_arena *a, void *mark);
void *arena_mark(t_arena *a);
void arena_realloc(t_arena *a);
void arena_reset(t_arena *a);
void arena_init(t_arena *a);

#endif // ARENA_H

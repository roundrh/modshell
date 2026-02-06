#include "arena.h"

void *arena_alloc(t_arena *a, size_t s) {
  s = (s + 7) & ~7;

  while (a->off + s > a->cap) {
    arena_realloc(a);
  }

  void *ptr = (char *)a->mem + a->off;
  a->off += s;

  return ptr;
}

void *arena_mark(t_arena *a) { return (char *)a->mem + a->off; }

void arena_sect_reset(t_arena *a, void *mark) {

  assert(mark >= a->mem);
  assert((char *)mark <= (char *)a->mem + a->cap);

  a->off = (size_t)((char *)mark - (char *)a->mem);
}

void arena_realloc(t_arena *a) {

  size_t nc = a->cap * ARENA_GROWTH_FACTOR;
  void *m = (void *)realloc(a->mem, nc);
  if (!m) {
    perror("realloc");
    exit(12);
  }

  a->mem = m;
  a->cap = nc;
}

void arena_reset(t_arena *a) { a->off = 0; }

void arena_init(t_arena *a) {
  a->mem = malloc(ARENA_INITIAL_CAP);
  if (!a->mem) {
    perror("malloc");
    exit(12);
  }

  a->cap = ARENA_INITIAL_CAP;
  a->off = 0;
}

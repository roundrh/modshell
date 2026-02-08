#include "arena.h"

static t_region *region_create(size_t cap) {
  size_t s = sizeof(t_region) + cap;
  t_region *r = malloc(s);
  if (!r) {
    perror("malloc");
    exit(12);
  }

  r->next = NULL;
  r->cap = cap;
  r->off = 0;
  return r;
}

void *arena_alloc(t_arena *a, size_t s) {

  s = (s + 7) & ~7;
  if (!a->curr || a->curr->off + s > a->curr->cap) {
    size_t nc = (s > REGION_DEF_CAP) ? s : REGION_DEF_CAP;
    t_region *nr = region_create(nc);

    if (!a->head)
      a->head = nr;
    else
      a->curr->next = nr;

    a->curr = nr;
  }

  void *ptr = a->curr->data + a->curr->off;
  a->curr->off += s;
  return ptr;
}

void arena_init(t_arena *a) {
  a->curr = NULL;
  a->head = NULL;
}

void arena_reset(t_arena *a) {
  t_region *r = a->head;
  while (r) {
    r->off = 0;
    r = r->next;
  }

  a->curr = a->head;
}

void arena_free(t_arena *a) {
  t_region *r = a->head;
  while (r) {
    t_region *n = r->next;
    free(r);
    r = n;
  }
}

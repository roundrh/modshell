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

void *arena_realloc(t_arena *a, void *optr, size_t nsize, size_t osize) {

  if (optr == NULL)
    return arena_alloc(a, nsize);

  if (nsize <= osize)
    return optr;

  void *nptr = arena_alloc(a, nsize);
  if (!nptr)
    return NULL;
  memcpy(nptr, optr, osize);

  return nptr;
}

void arena_rollback(t_arena *a, t_region *p, size_t off) {
  if (!p) {
    arena_reset(a);
    return;
  }
  t_region *r = p->next;
  while (r) {
    t_region *next = r->next;
    free(r);
    r = next;
  }

  a->curr = p;
  a->curr->off = off;
  a->curr->next = NULL;
}

void arena_get_mark(t_arena *a, t_region **p, size_t *off) {
  *p = a->curr;
  *off = (*p)->off;
}

void *arena_alloc(t_arena *a, size_t s) {

  if (s == 0)
    return NULL;

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
  if (!a || !a->head)
    return;

  t_region *r = a->head->next;
  while (r) {
    t_region *next = r->next;
    free(r);
    r = next;
  }
  a->head->next = NULL;
  a->head->off = 0;
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

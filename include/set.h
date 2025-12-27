#ifndef FSAUTOPROC_SET_H
#define FSAUTOPROC_SET_H

#define DEFINE_SET(typ, name)                                                  \
  struct name##_s {                                                            \
    int count;                                                                 \
    typ* items;                                                                \
  };                                                                           \
  typedef struct name##_s name##_t;

#define DECLARE_STATIC_SET_FREE(typ, name)                                     \
  static void name##_free(name##_t* set) {                                     \
    if (set != NULL) {                                                         \
      je_free(set->items);                                                     \
      set->items = NULL;                                                       \
      set->count = 0;                                                          \
      je_free(set);                                                            \
    }                                                                          \
  }

#define DECLARE_STATIC_SET_ALLOC(typ, name)                                    \
  static name##_t* name##_alloc(int count) {                                   \
    void* items = je_calloc(count, sizeof(typ));                               \
    if (!items) return NULL;                                                   \
    name##_t* set = je_malloc(sizeof(name##_t));                               \
    if (!set) {                                                                \
      je_free(items);                                                          \
      return NULL;                                                             \
    }                                                                          \
    set->count = count;                                                        \
    set->items = (typ*) items;                                                 \
    return set;                                                                \
  }

#define SET_AT(name, index)                                                    \
  (name != NULL && name->items != NULL && index >= 0 && index < name->count    \
           ? &(name)->items[index]                                             \
           : NULL)

#endif//FSAUTOPROC_SET_H

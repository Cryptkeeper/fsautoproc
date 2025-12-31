#ifndef FSAUTOPROC_SET_H
#define FSAUTOPROC_SET_H

/// @def DEFINE_SET_TYPE
/// @brief Defines a set type containing an array of the specified inner type.
/// @param typ The name of the set type to define
/// @param inner The inner item type contained in the set
#define DEFINE_SET_TYPE(typ, inner)                                            \
  typedef inner typ##_inner_item;                                              \
  typedef struct {                                                             \
    int count;               /* number of elements in the set */               \
    typ##_inner_item* items; /* array of elements or NULL     */               \
  } typ;

/// @def DEFINE_SET_FREE_STATIC
/// @brief Defines a static function to free a set of the specified type.
/// @param typ The name of the set type
#define DEFINE_SET_FREE_STATIC(typ)                                            \
  static void typ##_free(typ* set) {                                           \
    if (!set) return;                                                          \
    je_free(set->items);                                                       \
    je_free(set);                                                              \
  }

/// @def DEFINE_SET_ALLOC_STATIC
/// @brief Defines a static function to allocate a set of the specified type
/// and count of inner items.
/// @param typ The name of the set type
/// @param inner The inner item type contained in the set. If the count is
/// less than or equal to zero, the items pointer will be set to NULL but a set
/// will still be allocated and returned.
#define DEFINE_SET_ALLOC_STATIC(typ)                                           \
  static typ* typ##_alloc(int count) {                                         \
    void* items = NULL;                                                        \
    if (count > 0) {                                                           \
      items = je_calloc(count, sizeof(typ##_inner_item));                      \
      if (!items) return NULL;                                                 \
    }                                                                          \
    typ* set = je_malloc(sizeof(typ));                                         \
    if (!set) {                                                                \
      je_free(items);                                                          \
      return NULL;                                                             \
    }                                                                          \
    set->count = count;                                                        \
    set->items = items;                                                        \
    return set;                                                                \
  }

/// @def DEFINE_SET_FOR_EACH_STATIC
/// @brief Defines a static function to iterate over each item in a set of the
/// provided type, invoking the specified function on each contained item.
/// @param typ The name of the set type
#define DEFINE_SET_FOR_EACH_STATIC(typ)                                        \
  static void typ##_for_each(typ* set, void (*func)(typ##_inner_item*)) {      \
    for (int i = 0; set && set->items && i < set->count; i++)                  \
      func(&set->items[i]);                                                    \
  }

/// @def SET_AT
/// @brief Safely retrieves a pointer to the item at the specified index in the
/// set. If the set is NULL, the items pointer is NULL, or the index is out of
/// bounds, NULL is returned.
/// @param name The name of the set variable
/// @param index The index of the item to retrieve
#define SET_AT(name, index)                                                    \
  (name != NULL && name->items != NULL && index >= 0 && index < name->count    \
           ? &(name)->items[index]                                             \
           : NULL)

#endif//FSAUTOPROC_SET_H

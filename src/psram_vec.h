#pragma once
// BigVec<T>: a std::vector whose storage lives in PSRAM on boards that have
// it (the NM-CYD-C5), for the large lists (earthquakes, eruption reports).
// On the classic CYD it is a plain std::vector.
#include <stddef.h>
#include <stdlib.h>
#include <new>
#include <vector>
#include <esp_heap_caps.h>

#if defined(BOARD_NM_CYD_C5)
template <class T>
struct PsAlloc {
  using value_type = T;
  PsAlloc() = default;
  template <class U>
  PsAlloc(const PsAlloc<U> &) {}
  T *allocate(size_t n) {
    void *p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(n * sizeof(T));
    if (!p) abort();
    return static_cast<T *>(p);
  }
  void deallocate(T *p, size_t) { heap_caps_free(p); }
};
template <class T, class U>
bool operator==(const PsAlloc<T> &, const PsAlloc<U> &) { return true; }
template <class T, class U>
bool operator!=(const PsAlloc<T> &, const PsAlloc<U> &) { return false; }
template <class T>
using BigVec = std::vector<T, PsAlloc<T>>;
#else
template <class T>
using BigVec = std::vector<T>;
#endif

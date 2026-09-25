#pragma once

// Test assertion that stays active in every build type. Plain assert() compiles
// away under NDEBUG, which silently skips both the check and any engine call
// written inside it.

#include <cstdio>
#include <cstdlib>

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                   #condition);                                             \
      std::abort();                                                         \
    }                                                                       \
  } while (false)

#pragma once

#if !defined(_WIN32)
#include "libco.h"

namespace Fiber {

inline void* CreateHostFiber(void (*entry)()) {
    // libco allocates a fixed stack, unlike CreateFiber's initial commit size.
    return co_create(16 * 1024 * 1024, entry);
}

}  // namespace Fiber
#endif

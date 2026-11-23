#include "host_fiber.h"

#include <cstdio>

namespace {
cothread_t scheduler;
volatile unsigned checksum = 0;

__attribute__((noinline)) void DeepCall(unsigned depth) {
    volatile unsigned char frame[4096];
    for (unsigned i = 0; i < sizeof(frame); ++i) frame[i] = static_cast<unsigned char>(depth);
    if (depth) DeepCall(depth - 1);
    checksum = checksum + frame[depth];
}

void GuestThread() {
    DeepCall(128);
    co_switch(scheduler);
}
}  // namespace

int main() {
    scheduler = co_active();
    auto fiber = Fiber::CreateHostFiber(GuestThread);
    if (!fiber) return 1;
    co_switch(fiber);
    co_delete(fiber);
    if (checksum != 8256) return 1;
    std::puts("Guest fiber survived 129 nested 4 KiB frames.");
}

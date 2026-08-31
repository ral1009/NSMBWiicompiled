#include "hle_stubs.h"

#include <cstdint>

extern "C" int32_t KPAD__Read_HLE(uint32_t chan, uint32_t statusPtr, uint32_t count)
{
    (void)chan;
    (void)statusPtr;
    (void)count;
    return 0;
}
// 0x80197380 is MKW's KPAD__Read address only. NSMBW's own compiled binary places an unrelated
// function there (confirmed by reading its real translated body: a tiny r3-adjust-then-tail-call
// trampoline into 0x80194D90, nothing resembling a 3-argument KPAD read) - registering this
// override unconditionally duplicate-symbols the NSMBW link once that address is reached, so it's
// MKW-only. See NsmbwProduct.cmake for MKW_RUNTIME_PRODUCT_NSMBW.
#ifndef MKW_RUNTIME_PRODUCT_NSMBW
PPC_NATIVE_OVERRIDE(80197380, KPAD__Read_HLE, int32_t, (uint32_t chan, uint32_t statusPtr, uint32_t count),
         (chan, statusPtr, count));
#endif

extern "C" int32_t KPAD__GetUnifiedWpadStatus_HLE(uint32_t chan, uint32_t statusPtr, uint32_t count)
{
    (void)chan;
    (void)statusPtr;
    (void)count;
    return 0;
}
PPC_NATIVE_OVERRIDE(8019812C, KPAD__GetUnifiedWpadStatus_HLE, int32_t,
         (uint32_t chan, uint32_t statusPtr, uint32_t count), (chan, statusPtr, count));

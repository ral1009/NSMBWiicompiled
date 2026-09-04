#ifndef DOLPHIN_GXMANAGE_H
#define DOLPHIN_GXMANAGE_H

#include <dolphin/gx/GXFifo.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*GXDrawSyncCallback)(u16 token);
typedef void (*GXDrawDoneCallback)(void);

GXFifoObj* GXInit(void* base, u32 size);
// Seeds the persistent GX shadow-register-ID bytes GXInit() normally seeds internally. Exposed so
// native product bootstrap can call it once, unconditionally, for games whose own GXInit never
// routes through this native override (see GXManage.cpp for the full explanation).
void GXInitShadowRegisterIds(void);
GXDrawSyncCallback GXSetDrawSyncCallback(GXDrawSyncCallback cb);
GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback cb);
void GXDrawDone(void);
void GXSetDrawDone(void);
void GXFlush(void);
void GXPixModeSync(void);
void GXTexModeSync(void);
void GXSetMisc(GXMiscToken token, u32 val);
void GXAbortFrame(void);

#ifdef __cplusplus
}
#endif

#endif

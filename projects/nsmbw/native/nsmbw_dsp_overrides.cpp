// NSMBW address bindings for the DSP HLE in runtime/src/hle/audio/audio.cpp, which registers
// at Mario Kart Wii's addresses (DSPInit 0x8015D444, __DSP_boot_task 0x8015DC60, ...). Same
// situation as the IOS, WPAD and AI families. These bodies keep their state in AxDspHle and
// touch no title-specific guest globals, so this is a pure rebinding.
//
// Identified from the DSP hardware register block at 0xCC005000 (mailbox pair at +0x00/+0x02
// out, +0x04/+0x06 in, DSPCSR at +0x0A). The five leaves sit in the same order and at nearly
// the same spacing as MKW's:
//
//   0x801D5820  DSPCheckMailToDSP   lhz 0x5000, return bit 15
//   0x801D5830  DSPCheckMailFromDSP lhz 0x5004, return bit 15
//   0x801D5840  DSPReadMailFromDSP  (lhz 0x5004 << 16) | lhz 0x5006
//   0x801D5860  DSPSendMailToDSP    sth hi -> 0x5000, sth lo -> 0x5002
//   0x801D5880  DSPInit             init-flag guard at r13-0x4C88, OSSetInterruptHandler(7,
//                                   0x801D5AD0), __OSUnmaskInterrupts(0x01000000), DSPCSR setup
//   0x801D5940  DSPCheckInit        returns the r13-0x4C88 flag
//   0x801D5950  DSPAddTask          links the task, then calls 0x801D60B0 when it becomes head
//   0x801D59C0  DSPAssertTask       compares task->4 priorities and sets the pending-task globals
//   0x801D60B0  __DSP_boot_task     contains the "DSP is booting task: 0x%08X" debug string
//
// Without these, boot hangs inside 0x801D60B0 polling the DSP mailbox at 0xCC005000 for a reply
// that runtime/src/hle/dsp.cpp's flat register storage never produces.
//
// Only the five MKW overrides as MKW registers them; DSPCheckInit/DSPAddTask/DSPCheckMailFromDSP/
// DSPReadMailFromDSP stay translated here, as they do in MKW.
//
// Lives here, not in the shared runtime tree, because the translator's override-skip detection
// only scans this project's native_registration_root (projects/nsmbw/native).
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

#include <cstdint>

extern "C" void DSPInit_8015d444();
extern "C" void __DSP_boot_task_8015dc60(uint32_t taskPtr);
extern "C" void DSPSendMailToDSP_8015d430(uint32_t mail);
extern "C" uint32_t DSPCheckMailToDSP_8015d3fc();
extern "C" uint32_t DSPAssertTask_8015d57c(uint32_t taskPtr);

PPC_NATIVE_OVERRIDE_VOID(801D5880, DSPInit_8015d444, (), ());
PPC_NATIVE_OVERRIDE_VOID(801D60B0, __DSP_boot_task_8015dc60, (uint32_t taskPtr), (taskPtr));
PPC_NATIVE_OVERRIDE_VOID(801D5860, DSPSendMailToDSP_8015d430, (uint32_t mail), (mail));
PPC_NATIVE_OVERRIDE(801D5820, DSPCheckMailToDSP_8015d3fc, uint32_t, (), ());
PPC_NATIVE_OVERRIDE(801D59C0, DSPAssertTask_8015d57c, uint32_t, (uint32_t taskPtr), (taskPtr));

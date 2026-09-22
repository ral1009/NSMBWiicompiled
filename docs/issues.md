# Issues log

One entry per bug that changed observable behaviour, in the order it was fixed. Compiled on 2026-09-21 from every session transcript, the git history and the override-file headers; kept current from then on (see CLAUDE.md, "Documentation policy").

**Scope** is the field that matters. It is what lets a "reusable runtime bug" be split from "this game's own weirdness" later without re-investigating:

- **General** — a defect in the shared translator / runtime / aurora that any non-MKW title would hit. The fix (or its pattern) is reusable as-is.
- **NSMBW-specific** — an NSMBW address binding, memory-map constant, or game-side behaviour. The *class* is often general (noted where so) but the fix itself is not portable.
- **Unconfirmed** — the cause was not proven, or the fix is a stub/guess that has not been validated against real behaviour.

Fields: **Symptom** (what was observed) / **Root cause** (what was actually wrong, with the evidence) / **Fix** (what changed, where) / **Scope**.

The recurring classes, for reference (details in CLAUDE.md):
1. **MKW-address binding** — a runtime HLE is registered at Mario Kart Wii's address, so for NSMBW it never fires and the SDK function runs as translated code.
2. **Inlined leaf** — the translator inlines a small leaf into its callers, bypassing an override at that leaf's address.
3. **Dropped bookkeeping** — an override replaces an SDK body without the stores to `__gx`/`__OSData` that other still-translated SDK code reads.
4. **Raw MMIO** — translated guest code reads/writes a hardware register through the unchecked flat-memory path and faults or spins.
5. **Never-completing wait** — the guest waits for a flag that an IOS thread / DSP / hardware interrupt would set on a console and nothing sets here.

---

## 2026-08-30 — translator bring-up (Phases 2–5)

### Translator refuses to start without SDA bases
- **Symptom:** `translate-recursive` threw from `RequireSdaBases()` before touching any code; the README said the bases were only needed with `RuntimeConfig.h`.
- **Root cause:** the manifest loader demands `_SDA_BASE_`/`_SDA2_BASE_` unconditionally.
- **Fix:** read the `lis`/`ori` pair in `__init_registers` (0x80004234) and put `0x8042F980` / `0x80433360` in `projects/nsmbw/nsmbw.yml`.
- **Scope:** General (documentation gap in upstream README; the requirement itself is correct).

### `syms.txt` format mismatch
- **Symptom:** NSMBW-Decomp's `syms.txt` (`name=0xADDR`) rejected as a function map.
- **Root cause:** the translator wants `hexaddr name`, no `0x` prefix.
- **Fix:** conversion script → `projects/nsmbw/function_map.txt`.
- **Scope:** NSMBW-specific (tooling).

### `bl 0x60` — translator crashed decoding low memory
- **Symptom:** discovery queued "function at 0x00000060" at depth 4 and crashed reading it.
- **Root cause:** a real, absolute `bl 0x60` (opcode 18, AA=1) from 0x801AB150 into the IPL/OS-provided low-memory block, which no dumped file contains. Two problems stacked: the translator checked native-override exclusions only *after* attempting to decode the target, and nothing existed at 0x60 to decode.
- **Fix:** `baseTranslationExclusions` check moved ahead of decode in the discovery loop (`translator/src/Translator.Cli/Program.cs`); `NsmbwBootStub_00000060` in `projects/nsmbw/native/nsmbw_boot_stubs.cpp` logs and returns.
- **Scope:** General (translator) for the ordering fix. **Unconfirmed** for the stub — nobody has verified what the real 0x60 routine does; no symptom has been traced to it since.

### One manifest per REL
- **Symptom:** the manifest schema takes a DOL plus at most one REL (`ProjectInputs(Dol, Rel?)`).
- **Root cause:** upstream only ever needed MKW's single REL.
- **Fix:** four manifests (`nsmbw-<rel>.yml`), each seeded from the REL header's `_prolog`.
- **Scope:** NSMBW-specific (tooling).

### REL function seeding: thin coverage, then a consistent 240-byte offset
- **Symptom:** `d_en_bossNP` translated only 6 functions (its `.plf` exposes 5 named symbols); after seeding from the REL's own relocation table (~1,967 candidates), the seeds were later found to be shifted by ~240 bytes, so each decoded a function *near* the intended one.
- **Root cause:** the seeding script computed `load + addend` and omitted `Sections[target].FileOffset`. The validation (checking for `stwu r1,-N(r1)` prologues) sliced into the extracted `.text` array, which includes that offset, so it silently checked the right bytes while writing the wrong addresses.
- **Fix:** regenerated the map with the section offset, re-ran translation for all four RELs.
- **Scope:** NSMBW-specific (own tooling bug). Lesson: validate against the same address the consumer will use.

### Upstream compile errors when building outside MKW's configuration
- **Symptom:** `gx_texture.cpp` referenced a non-existent `g_TlutObjMeta` map; `nand_path.h` lacked an include; `music_attenuation.cpp` needs the optional C++/WinRT SDK.
- **Root cause:** copy-paste bug (should be `GetTlutObjMeta(oa)`, as nine lines above); missing include; optional dependency.
- **Fix:** first two fixed in place; the third is stubbed for NSMBW in `nsmbw_runtime_shims.cpp` ("feature not running").
- **Scope:** General (upstream bugs).

### NSMBW product build graph
- **Symptom:** MKW's CMake is a single-product build; NSMBW's five-module output did not fit. Then, in order: missing `-march` flag on the new targets, duplicate/missing native-override symbols, missing DLL-copy step, and `Memory::Init()` never called before guest memory was touched.
- **Root cause:** `runtime/src/main.cpp` hardcodes MKW's entry address and cannot be linked, so NSMBW needs its own `main()` and the generic symbols `main.cpp` happened to provide.
- **Fix:** `emit-nsmbw-build-shards` / `generate-nsmbw-data-init` commands, `NsmbwProduct.cmake` (off by default, `MKW_BUILD_NSMBW`), `runtime/src/product/nsmbw_product.cpp`, `projects/nsmbw/native/nsmbw_runtime_shims.cpp`.
- **Scope:** NSMBW-specific (product plumbing).

### State-free ABI mismatch across merged modules dropped whole callers
- **Symptom:** "43 excluded callers" per shard emit; later `conflicting types for 'func_801C9BA0_statefree_v1'` at compile time; `missing_target` on a dropped function.
- **Root cause:** each REL's `translate-recursive` decides independently whether a shared target can use the narrowed "state-free" calling convention, and the five runs disagree. The emitter treated a caller whose forward declaration mismatched (or whose callee had no surviving definition) by dropping the caller entirely. A first patch (2026-09-06) rewrote only the call site but left the orphaned `extern "C"` declaration, which collided when shard packing bundled it with a correct declaration.
- **Fix:** `NsmbwMultiModuleShardEmitter.cs` `DowngradeStateFreeCallSiteToSafePath`: rewrite only the mismatched call site(s) to `InvokeDirectCpu` and remove the stale `_statefree`/`_statefree_vN` declarations. Recovered exactly the 43 functions (40,919 → 40,962).
- **Scope:** General (translator, multi-module merge).

### Cross-module relocations resolved against the wrong module
- **Symptom:** suspected garbage in `d_en_bossNP`'s `.ctors`.
- **Root cause:** `RelFile.ApplyRelocations` special-cased only `ModuleId == 0` (DOL); a relocation into a *different* REL fell through to the self-module branch and used the wrong `baseAddress`/`Sections`. (It turned out not to be the trigger for the `.ctors` symptom — see next entry — but it is a real bug.)
- **Fix:** `RelFile.ModuleId` + a module registry threaded into `ApplyRelocations`.
- **Scope:** General (translator).

### REL load ranges overlapped in guest memory
- **Symptom:** `d_en_bossNP`'s constructor table read back as `0x00040104`-style garbage; running its `_prolog` crashed. Read back correctly right after its own `memcpy`, wrong by the time constructors ran.
- **Root cause:** `RelFile.cs` sized each REL's resident footprint from the raw on-disc file length, which includes the relocation/import tables that `OSLink` discards (`do_link()` shrinks the block to `fixSize + bssSize`). `d_en_bossNP` was sized 624,156 instead of 463,140 bytes, so its range ran into `d_enemiesNP`'s and the later copy overwrote it.
- **Fix:** resident size = max section extent (aligned) + BSS. All four modules now non-overlapping with plausible allocator gaps.
- **Scope:** General (translator).

## 2026-08-31 — Phase 6 runtime bring-up begins

### Vtable-only function never translated (`0x802E4860`)
- **Symptom:** `missing_target 0x802E4860` via `InvokeIndirectCpu` from `func_802DE84C` (sound-archive reader refill callback).
- **Root cause:** `translate-recursive` follows only direct `bl`; this function has no ELF symbol either (26 KB gap in `wiimj2d.elf`'s symtab).
- **Fix:** new CLI command `extract-data-function-pointers` scans data sections for aligned words that fall in executable ranges and decode as function starts (6,045 candidates; 19,246 rejected by the existing `LooksLikeFunctionStart` safety net). Extended to REL data on 2026-09-20 (see below).
- **Scope:** General (translator).

### Raw MMIO access from translated code faults
- **Symptom:** hardware access violations at `0xCC003004` (PI_INTMR), `0xCC00401C` (MI), `0xCD000000` (IPC), `0xCC00500A` (DSP_CSR), `0xCD006800` (EXI), `0xCD006C00` (AI), `0xCD006024` (DI_CONFIG).
- **Root cause:** guest-visible MMIO pages are `PAGE_NOACCESS` by design; translated *stores* route through a checked policy path but *loads* use the unchecked `FlatRead*` for speed, and a value cannot be recovered from a caught fault (no way to know which x86 register an inlined memcpy targeted).
- **Fix:** flat register-block backing on the checked path for PI/MI/IPC/DSP/EXI/AI/DI (`runtime/src/hle/{pi,mi,dsp,exi,ai,di}.cpp`); guest functions that legitimately *read* MMIO get byte-for-byte native ports with only the register accesses swapped to `Memory::Read/Write`: `__OSMaskInterrupts` (0x801B13F0), `EXIInit` (0x801B9CE0), `EXISelect` (0x801B9700), `EXIDeselect` (0x801B9830), the EXI immediate-transfer start/wait pair (0x801B8C60 / 0x801B9090), the `DI_CONFIG` probe leaf (0x801AB2F0, also inlined into both callers so it needs a function-level override). All under `projects/nsmbw/native/nsmbw_exi_*.cpp`, `nsmbw_os_interrupt_mask.cpp`, `nsmbw_di_config_probe.cpp`.
- **Scope:** General (register backing, class 4). NSMBW-specific for the per-function ports (MKW's `vi.cpp` does the same thing at MKW addresses).

### `EXIProbe` full-protocol port hangs
- **Symptom:** porting the 1,900-line chip-select / command / ID-readback chain produced a genuine hang ("Not Responding"), not a crash.
- **Root cause:** the exit conditions of the device-identification protocol could not be verified without unbounded reverse engineering.
- **Fix:** override at the documented `BOOL EXIProbe(chan)` boundary (0x801BA0C0) returning "not found" — the function's own early exit for r4==0, and the correct answer for an empty bus (no memory card / BBA / AD16). Keeps the (chan 0, dev 2) result cache. `nsmbw_exi_probe.cpp`.
- **Scope:** NSMBW-specific. Justified rather than guessed, but unverified against a game path that expects a device.

### EXI `TSTART` never clears (immediate mode, then DMA mode)
- **Symptom:** `func_801B16D0` / `EXIImmWait` spun forever polling CR.TSTART; later the same spin from `func_801B3F60` (SRAM/RTC read on chan 0 dev 1).
- **Root cause:** `exi.cpp`'s register backing stored the CR write but nothing completed the transfer. The first fix cleared TSTART synchronously only when `DMA==0` ("nothing exercises that path yet"); `EXIDma` (0x801B8F90) writes `TSTART|DMA` and was left unhandled.
- **Fix:** both modes complete synchronously on the CR write; a DMA read zero-fills `length` bytes at `mar` (blank SRAM, which the SDK's checksum fallback handles). `runtime/src/hle/exi.cpp`.
- **Scope:** General (class 5).

### DSP bring-up: CSR reset bit and 32-bit register access
- **Symptom:** `func_801AC2C0` (DSP init) spun in `while (DSP_CSR & 1)`; then a 32-bit `stw` to `AR_DMA_MMADDR_H/L` faulted.
- **Root cause:** `dsp.cpp` was a passive store: bit 0 of CSR (a reset pulse the hardware clears) never cleared. Separately only 16-bit accessors were registered while the SDK writes adjacent 16-bit registers as one word.
- **Fix:** CSR bit-0 self-clears; `DSP_HLE_TryRead32/TryWrite32` added. `runtime/src/hle/dsp.cpp`.
- **Scope:** General.

### `DCZeroRange(0xFFFFFFE0)` — uninitialised low-memory field
- **Symptom:** access violation at `0xFFFFFFE0` after DSP init; chased through a dozen frames (sound-archive reader, OSReport banner) before the source was found.
- **Root cause:** `func_801AA940` (OSInit) reads `USABLE_MEM2_START` at `0x80003124`; the seed table set its neighbours (`0x80003120`, `0x80003128`) but not it, so the arena-lo global stayed at the never-written default `0xFFFFFFFF`, and `& ~31` gave the crash address. On a console the IPL fills the `0x80003100` block before the entry point.
- **Fix:** `{0x80003124, kMem2CachedBase}` in `SeedLowMemDefaults()` (`nsmbw_product.cpp`); also `OSBootInfo.consoleType` (0x8000002C). Note: forcing `PHYSICAL_MEM2_SIZE` to the retail 64 MB was tried and reverted — the `0x10000000` "anomaly" bit the 128 MB size produces is what lets `func_80227DB0` continue, so it is not an error flag.
- **Scope:** General in principle (every title needs the IPL block); the table itself lives in NSMBW product code.

### `__OSInitSTM` sleeps forever on an IOS open
- **Symptom:** boot hung with the main thread asleep after `Invoking entry point`.
- **Root cause:** NSMBW's `__OSInitSTM` (0x801B6960) opens `/dev/stm/immediate` and `/dev/stm/eventhook` through a generic "post to work queue at 0x803DA180, `OSSleepThread`" helper; the IOS thread that would drain it and `OSWakeupThread` does not exist here (class 5). MKW's HLE for the same SDK function is at MKW's address.
- **Fix:** `nsmbw_os_init_stm.cpp` mirrors MKW's `__OSInitSTM_HLE`: fake non-zero handles, set the init flag, never create the watcher thread.
- **Scope:** NSMBW-specific (class 1).

### Missing IOS HLE cases: `/dev/di` open, ES ioctls
- **Symptom:** hangs/failures in the same open-and-wait family after the STM fix.
- **Root cause:** `nand_isfs.cpp` had no `/dev/di` fd and no `ES_IOCTL_DIGETTICKETVIEW` (0x1B) / `ES_IOCTL_GETCONSUMPTION` (0x16) cases.
- **Fix:** `DI_DEV_FD` handle; zero-filled ticket view; zero parental-control entries. `runtime/src/hle/storage/nand_isfs.cpp`.
- **Scope:** General (HLE gaps MKW never exercised).

### SCInit poll-flag stall (`0x8042AD30` stuck at 1)
- **Symptom:** RIP oscillating in a two-instruction loop after SCInit; `kThreadListHead/Tail/RunningContext` never changed.
- **Root cause:** `SCInit → func_801DBB40 → 0x801D9890` enqueues the system-config read into an EGG task registry (32-entry array at 0x803DA2C0) that a worker thread would drain; nothing drains it. An early hand-edit forcing the flag in `func_801DB980` to 3 was lost with shard regeneration and superseded.
- **Fix:** `nsmbw_sc_deferred_write_sync.cpp` overrides 0x801D9890 and invokes the request's own completion callback (`func_801DBC60`) immediately with success; `func_801DBEB0` still owns the flag write. Plus the IOS IPC bindings below, which let the underlying `IOS_Open` of `/dev/fs` complete.
- **Scope:** NSMBW-specific (class 5).

### Disk-ID check and a second async op never complete (`func_801D0E30`)
- **Symptom:** spin on `-0x6048(r13)` after the SC fix; then on `-0x6044(r13)`.
- **Root cause:** `0x801D2D60` kicks off a check whose completion flag is set by a callback path never reached; `0x801D07E0` starts an operation via `func_801D3530` with callback `func_801D0D60` that nothing completes.
- **Fix:** `nsmbw_disk_id_check_sync.cpp`: set the flag / invoke the callback immediately with result 1.
- **Scope:** NSMBW-specific. **Unconfirmed** that skipping `0x801D2D60`'s table-entry validation is harmless (no symptom traced to it yet).

### VI register writes fault
- **Symptom:** faults on `VI_DCR` then `HTR0` and the rest of the sequence as boot drove `VI_HW_REGS` directly.
- **Root cause:** `vi.cpp` only special-cased a few registers.
- **Fix:** flat 64-register `VI_HW_REGS` block, `VICLK`/`DTVSTATUS` kept as live reads. `runtime/src/hle/vi.cpp`.
- **Scope:** General.

### Tick-wait spins never yield (`func_801AF710`, `func_801AF900`)
- **Symptom:** `bl 0x801BE010` twice, spin until the delta ≥ 1 — forever. Calling `VI_HLE_PollRetrace` from inside the loop made it worse (287 M calls in 15 s, counter unchanged).
- **Root cause:** the retrace counter at `r13-0x4E34` only advances from SelectThread's idle loop; a tight spin never reaches it.
- **Fix:** override the one-line accessor 0x801BE010 to pump one `VI_HLE_PollRetrace()` per read (`nsmbw_tick_read_pump.cpp`). Fixed all three wait sites at once.
- **Scope:** NSMBW-specific (address); the pattern is general.

### `func_801A9CE0` mistranslated as an infinite self-branch
- **Symptom:** deterministic native SIGSEGV in the next function (`func_801A9D00`, `mtspr 952`) with `rcx=0xFFFFFFFF` as the `CpuContext*`; localised with lldb.
- **Root cause:** the translator emitted `loc_801A9CE4: { r3 = 0; goto loc_801A9CE4; }` — a side-effect-free infinite loop (almost certainly a misread `blr`), which is UB in C++, so clang's optimiser produced garbage register state in the inlined callers.
- **Fix:** override returning `r3 = 0` (`nsmbw_func_801a9ce0.cpp`).
- **Scope:** General (translator decode bug). **Unconfirmed** — the decode bug itself was never fixed in the translator, only worked around.

### Five callers of the overridden state-free variant lost their bodies
- **Symptom:** `undefined symbol: func_801AF900` etc. at link, then `missing_target` for each at boot.
- **Root cause:** overriding `func_801A9CE0` removes its `_statefree_v0` definition, and the emitter excluded every caller that forward-declared it. Hand-appended bodies in a generated shard vanished on the next regeneration (shard names are content-hashed).
- **Fix:** `nsmbw_func_801af900_registration.cpp`: `func_801AF900` (colour/curve table builder) is a no-op, `func_801B8B20` (init-callback table walker) is reimplemented, `func_801AC980` / `func_801AD620` / `func_801AD9E0` abort loudly if reached. Re-translating with the override removed still did not emit them.
- **Scope:** **Unconfirmed / open** — the no-op is a cosmetic risk; the second cause for the missing translation was never diagnosed.

## 2026-09-01 → 2026-09-02 — thread scheduler

### Thread-lifecycle HLE bound at MKW addresses; "Entry point returned"
- **Symptom:** boot ended with the entry point returning; `main` created the game thread, resumed it, suspended itself and fell through. Then, once creation worked, every `SelectThread` bailed early.
- **Root cause:** `runtime/src/hle/os/*` addresses are MKW's: `SelectThread` 0x801A9C08 → real 0x801B4FE0, `OSCreateThread` 0x801A9E84 → 0x801B5270, `OSSuspendThread` 0x801AA6A8 → 0x801B5C40, `OSResumeThread` 0x801AA58C → 0x801B59A0, `OSInitContext` 0x801A20BC → 0x801AD0F0, VI retrace queue 0x80386BC0 → 0x8042AB28; five `os_internal.h` scheduler constants likewise. Each real address was confirmed by matching field offsets in the disassembly. `Fiber::GuestFiberManager::Initialize()` was also never called for the NSMBW product.
- **Fix:** `nsmbw_{select,create,suspend,resume}_thread_override.cpp`, per-product constants in `os_internal.h` / `vi.cpp`, fiber init in `nsmbw_product.cpp`. Overrides must live under `projects/nsmbw/native/` — the translator's override-skip scan only reads that root, and a registration in `runtime/src` produced a duplicate symbol.
- **Scope:** NSMBW-specific (class 1). The lesson that the shared HLE headers hardcode one title's layout is general.

### Bogus `OSSaveContext` call masked blocking; alarm pump walked garbage
- **Symptom:** removing the `0x801A1ED8` call from `SelectThread` moved the stall *earlier* and produced ~1,850 access violations per 30 s.
- **Root cause:** 0x801A1ED8 is not `OSSaveContext`; its non-zero return made `SelectThread` early-return, so guest waits that should have blocked did not (the apparent progress to `GXInitTexObj` was an artefact). The violations came from the OSAlarm pump walking `r13-0x6360`, which no NSMBW instruction references (reads as ASCII "iceo"); `OSSetAlarm` at 0x801A0870 is a different leaf.
- **Fix:** call removed for good; alarm pump inert for NSMBW with the evidence recorded in `os_internal.h`.
- **Scope:** NSMBW-specific. **Open:** the OSAlarm HLE is still unbound for NSMBW.

### CP / PE register blocks
- **Symptom:** raw writes to 0xCC000000 (Command Processor) and 0xCC001000 (Pixel Engine) faulted inside `dSys_c::create()`.
- **Fix:** store-only backing, `runtime/src/hle/cp.cpp`, `pe.cpp`.
- **Scope:** General.

### `VIWaitForRetrace` HLE bound to a padding gap; retrace callbacks never registered
- **Symptom:** the guest's own `VIWaitForRetrace` (0x801BCE30) spun forever (`g_vi.retraceCount` stayed 0; breakpoint on `VI_HLE_PollRetrace` never hit). Later, EGG's `endRender` slept on its queue forever.
- **Root cause:** the HLE was registered at 0x801B99EC, which in NSMBW is padding before an unrelated function. The pre/post-retrace callback setters were likewise unbound, so `AdvanceRetrace` had nothing to invoke and the waker (0x802BBAA0) never ran.
- **Fix:** `nsmbw_vi_wait_for_retrace.cpp` binds the existing HLE and the two setters.
- **Scope:** NSMBW-specific (class 1).

### IOS IPC family bound at MKW addresses
- **Symptom:** SCInit's `IOS_Open("/dev/fs")` chain parked forever in `OSSleepThread`.
- **Root cause:** `ios.cpp` / `nand_isfs.cpp` register at 0x801937E0–0x801945E0 (MKW). NSMBW's 14 `IOS_*` entry points (0x80224C90–0x80225AE0) all tail-call the submitter at 0x80224A50 and were identified by the command id each writes.
- **Fix:** `nsmbw_ios_ipc_overrides.cpp`.
- **Scope:** NSMBW-specific (class 1).

### DSP task boot hangs polling the mailbox
- **Symptom:** boot stuck in `__DSP_boot_task` (0x801D60B0) polling 0xCC005000.
- **Root cause:** `audio.cpp`'s DSP HLE is at MKW's addresses; the flat DSP register store never answers.
- **Fix:** `nsmbw_dsp_overrides.cpp` binds the five leaves MKW binds.
- **Scope:** NSMBW-specific (class 1).

### `__AXOutInitDSP` spins waiting for the DSP task's init callback
- **Symptom:** spin on `*(r13-0x5100) == 0`.
- **Root cause:** nothing executes DSP microcode, so the callback that sets the flag never fires.
- **Fix:** bind 0x801A1E20 to `AxDspHle::InitForAXOut` (`nsmbw_ax_overrides.cpp`); per-product guest globals in `ax_internal.h`.
- **Scope:** NSMBW-specific (class 1).

### `AIInit` sample-rate calibration never terminates
- **Symptom:** boot hung at 0x8019F644 in `do { s = AISCNT } while (s == prev)`.
- **Root cause:** flat AI register storage; the sample counter never advances.
- **Fix:** bind `AIInit` (0x8019F330) to the HLE (`nsmbw_ai_overrides.cpp`).
- **Scope:** NSMBW-specific (class 1).

## 2026-09-03 — boot ordering, arena, disc

### Constructors ran before the entry point; OSInit wiped the REL images
- **Symptom:** `fProfListMg_c::m_data_p` (0x8042A698) read null; `fBase_make` virtual-called through a null list; the game looped drawing nothing.
- **Root cause:** (1) static ctors and REL `_prolog`s ran before `__start`, whose `__init_data` (0x80004250 → memset at 0x80004600) then zeroed every BSS word they had written. (2) `InitializeDataSections()` places the REL blobs at 0x807685A0+, inside MEM1's arena, which OSInit (0x801AA940) clears — measured at `profileList` (0x8076A828): correct after `__init_data`, zero the instant OSInit returned. On a console RELs load from disc *after* OSInit.
- **Fix:** hook 0x80004040 (a two-instruction leaf `__start` calls exactly once, after BSS clear and OS bring-up, before `main`) restores the four REL images and runs ctors + prologs there (`nsmbw_guest_ctor_hook.cpp`).
- **Scope:** General pattern for any pre-seeded-REL runtime; the hook address is NSMBW-specific.

### Boot stack placed inside the arena; OSInit zeroed its own frame
- **Symptom:** `DVDReadDiskID(block=0x40, buf=0x20)`; `r30 == 0` inside OSInit although the translation of `func_801AA280` saves/restores it correctly.
- **Root cause:** `mem1ArenaLo` was seeded as the DOL BSS end (0x8042FF1C); OSInit's `DCZeroRange(arenaLo, arenaHi−arenaLo)` therefore covered the live stack (`_stack_addr` = 0x8043FF20 from `__init_registers`) and wiped the saved-r30 slot at 0x8043FEE0. The zero propagated into DVDInit.
- **Fix:** `mem1ArenaLo = 0x8043FF20`, above the stack (`nsmbw_product.cpp`). NSMBW-Decomp's `ARENA_HI 0x817f0000` confirmed the upper bound.
- **Scope:** NSMBW-specific (memory-map constant). Lesson general: the arena must not contain the boot stack.

### DI commands through `/dev/di` unserviced
- **Symptom:** DVDInit's first command failed and the DVD state machine took its error path; ReadDiskID never issued.
- **Root cause:** NSMBW reaches the drive via `IOS_IoctlAsync` on `/dev/di` (only two DOL functions touch DI registers); `NAND_IOS_Ioctl_HLE` had no `DI_DEV_FD` case. Commands identified from the functions' own error strings: 0x86 `DVDLowClearCoverInterrupt` (0x801D3840), 0x12 `DVDLowInquiry` (0x801D25B0), 0x70 `DVDLowReadDiskID` (0x801D15E0); plus `DVDReadAbsAsyncPrio` (0x801CEE00) identified by its command-block stores.
- **Fix:** `nsmbw_dvd_overrides.cpp` binds them to MKW's existing HLE (cover-interrupt returns TRUE — with no lid there is never one pending).
- **Scope:** NSMBW-specific (class 1).

### No FST in guest memory — every path lookup failed
- **Symptom:** black screen; `dDvd::loader_c::request` called 4,188 times; `DVDConvertPathToEntrynum("/WIIMJ2DNP.str")` = −1 for a file that exists.
- **Root cause:** `__DVDFSInit` is *inlined* into `DVDInit` (0x801CAE70) — a breakpoint on 0x801CA790 never fired — and latches `OSBootInfo.FSTLocation` (0x80000038) once, early-returning on zero. MKW's `DVDInit_8015EA1C()` publishes the FST but is bound at MKW's address; `SystemBridge::Initialize()` (which reserves 2 MiB for it) is MKW-only. Loading the disc's raw `sys/fst.bin` instead was wrong: the read HLE resolves extents against its own generated FST.
- **Fix:** `SeedLowMemDefaults` makes the 2 MiB reservation below `ipcBufLo` and lowers MEM2 arena hi; `DVDInit_8015EA1C()` runs pre-entry (`nsmbw_product.cpp`).
- **Scope:** NSMBW-specific (class 1); the ordering constraint is general.

### DVD reads rounded to 32 bytes overran indexed files
- **Symptom:** "range extends beyond the indexed DVD file" (218 vs 224, 8789 vs 8800, …).
- **Root cause:** the SDK rounds every read length up to 32 bytes; real discs store files padded.
- **Fix:** `dvd.cpp` allows the overrun and zero-fills the padding.
- **Scope:** General.

### VAT never pushed to aurora on the raw-FIFO path
- **Symptom:** aurora sized vertex format 5 at 3 bytes/vertex; FIFO desynced at byte 3855 of the second draw; ~322 "Ignoring nested GX_CMD_CALL_DL" per run (a symptom of parsing mid-vertex, not a nested list).
- **Root cause:** `gx_dl.cpp` `SyncAppliedVtxStateFromHleReal()` memcpy'd the HLE VAT into the "applied" mirror and marked it applied without calling aurora's `GXSetVtxAttrFmt`, so `SameVtxAttrFmt` compared the value with a copy of itself. MKW's display-list path always pushed first; NSMBW's raw-FIFO route never had.
- **Fix:** clear the mirror row so the push cannot be skipped.
- **Scope:** General (raw-FIFO path).

### Zero-vertex draw wedged the FIFO decoder
- **Symptom:** only two draws ever rasterised; `g_hleGxState.inBegin == true` 24 s in, stuck on vtxFmt 7 — the `fmt=7 n=0` draw NSMBW issues during GX bring-up.
- **Root cause:** the draw handler set `inBegin = true` and only cleared it inside `if (vertsRemaining > 0)`; the command loop runs `while (!inBegin)`, so every later byte was eaten as vertex data.
- **Fix:** a zero-count draw does not enter begin mode (`gx_fifo.cpp`).
- **Scope:** General.

### Guest re-loaded the pre-seeded RELs and zeroed `profileList[1..8]`
- **Symptom:** after the save screen, black; `dScCrsin_c::m_isDispOff` stuck true; a child process stuck in CREATING; `dScene_c::createNextScene()` could not build RESTART_CRSIN; later "BAD PTMF" and `unmapped guest touch` warnings. (First misdiagnosed as a cross-module relocation bug; the translator's blob and `RestoreRelImages()` were verified correct.)
- **Root cause:** `DynamicModuleControlBase::link()` (0x80160080) runs `do_load()`/`do_link()` — and `load_async()` runs `do_load_async()` — for the four RELs, which exist on disc only as `.LZ` the DVD layer cannot decompress; the failed load allocates from `sDylinkHeap` over memory the pre-seed already filled and zeroes it.
- **Fix:** `nsmbw_preseeded_rel_link.cpp` skips `do_load`/`do_link`/`load_async` for exactly those four module names and reports success.
- **Scope:** NSMBW-specific.

## 2026-09-04 — first pixels, input

### GX HLE bound only at MKW addresses (texture load, PE state)
- **Symptom:** `GXLoadTexObj` (0x801C7600) hit 7,000+ times as translated code; `GXSetBlendMode`/`SetColorUpdate`/`SetAlphaUpdate`/`SetZMode` (0x801C8F00/8F50/8F80/8FB0) likewise.
- **Fix:** bound in `nsmbw_gx_overrides.cpp`.
- **Scope:** NSMBW-specific (class 1). Real fixes, but not the black-screen cause at the time.

### `GXTexObj` format decoded from the wrong word
- **Symptom:** the full-screen background quad's texture decoded as `GX_TF_Z24X8` (22) and read back transparent white.
- **Root cause:** `ExtractTexObjMetaFromGuest` trusted `word5` whenever it decoded to *any* valid enum; `word2` (hardware-packed with width/height) said RGBA8.
- **Fix:** prefer `word2` when they disagree (`gx_objects.cpp`).
- **Scope:** General.

### GX BP register-ID table unseeded (commit `0a4d08a`)
- **Symptom:** colour writes masked off, cull mode corrupted; black screen under a correct pipeline.
- **Root cause:** the table `GX_WRITE_RAS_REG` uses to route state writes to BP registers is seeded inside the native `GXInit` override, bound at MKW's address. Unseeded, `GXSetColorUpdate`/`GXSetBlendMode` landed on register 0x00 (genMode) instead of 0x41 (cmode0).
- **Fix:** seed at boot regardless of the binding.
- **Scope:** NSMBW-specific trigger (class 1); the seeding itself is general.

### Startup overlay never dismissed (commit `0a4d08a`, then 2026-09-05)
- **Symptom:** opaque "WiiCompiled" card over correctly rendering output.
- **Root cause:** dismiss hook at MKW's `StrapScene::CheckInput` (0x800077C8).
- **Fix:** NSMBW hook at `dScBoot_c::finalizeState_WiiStrapDispEndWait` (0x8015CFB0); then `settings_overlay::DisableStartupScreen()` per product, since NSMBW's strap screen is itself the first thing drawn.
- **Scope:** NSMBW-specific (class 1).

### Input never wired (PAD/WPAD at MKW addresses; `_wpdcb[0]` null)
- **Symptom:** the save-confirm dialog waited for A forever; keyboard and controller did nothing.
- **Root cause:** `pad.cpp`/`wpad.cpp` bind at addresses that do not exist in NSMBW; `WPADProbe_HLE` returns "no controller" unconditionally; no `WPADRead` equivalent exists. NSMBW's real `WPADProbe` (0x801E1080) reads the `_wpdcb[chan]` control block directly, and `_wpdcb[0]` was null because `WPAD::Init` (0x801DFB90) is replaced wholesale and the linking loop at 0x801DF930 never ran. Wholesale-replacing `mPad::beginPad()` crashed (`guest address 0x0`): it constructs `g_core[]` on first call.
- **Fix, in three generations:** (1) 2026-09-04 vtable patch of `beginFrame` slot 12 + trampoline; (2) 2026-09-05 seed `_wpdcb[0]` as a synced remote (`nsmbw_wpad_overrides.cpp`, kept) + `EGG::CoreController::downTrigger` override (edges only); (3) 2026-09-20 `KPADRead`/`KPADReadEx` internal 0x801ED500 returns one real sample per VI frame (`nsmbw_kpad_overrides.cpp`, commit `33702c4`). Inject where the SDK *samples* input, not where the game consumes it.
- **Scope:** NSMBW-specific (class 1).

## 2026-09-05 → 2026-09-18 — uncommitted stretch

### Wipe-circle diagnostic recursed into itself (self-inflicted)
- **Symptom:** process vanished without output at the STAGE wipe; 27,551 back-to-back `ENTER` lines; Windows reported `STATUS_STACK_OVERFLOW` (the crash handler only catches access violations).
- **Root cause:** the diagnostic override called `InvokeIndirectCpu` on its own address (0x8001B580); once overridden, the translated body is excluded, so "call the real calc()" recursed.
- **Fix:** reimplement the three-line dispatch and call only the non-overridden sub-parts (`nsmbw_wipecircle_calc_diag.cpp`).
- **Scope:** N/A (tooling lesson): an override cannot call through to the function it replaces.

### Strap panes at alpha 0 — chased as a TEV-colour animation bug
- **Symptom:** `GX_TEVREG0` source (0x8043FB08) animated RGB every frame with alpha fixed at 0 across 267 samples; strap text/illustration invisible.
- **Outcome:** the panes were sampling a transparent sliver of their texture — the SU texture-size bug below. The alpha reading was true but not the cause.
- **Scope:** superseded (see 2026-09-20 `GXLoadTexObj` entry).

## 2026-09-19 — first 3D frame (commit `590ae0a`)

### Texture copies clobbered the display-copy registers
- **Symptom:** 2D content as a 143-pixel-wide strip.
- **Root cause:** BP 0x4D (copy stride) and 0x49/0x4A (copy source rect) are shared by display and texture copies; `GXSetTexCopySrc` (0x801C5AA0), `GXSetTexCopyDst` (0x801C5B10), `GXCopyTex` (0x801C63D0) ran translated and overwrote them before the display copy read them.
- **Fix:** `nsmbw_gx_texcopy_overrides.cpp`.
- **Scope:** NSMBW-specific (class 1).

### Indexed matrix loads dead
- **Root cause:** `GXLoadPosMtxIndx` (0x801C9AD0), `GXLoadNrmMtxIndx3x3` (0x801C9B60) unbound — nw4r::g3d loads node matrices by palette index.
- **Fix:** `nsmbw_gx_mtxindx_overrides.cpp`.
- **Scope:** NSMBW-specific (class 1).

### 3D projection cached in `__gx`, never flushed by the display-list HLE
- **Symptom:** projection set but never reached aurora before the scene's display lists ran.
- **Root cause:** NSMBW's `GXSetProjection`/`GXSetCurrentMtx` cache into `__gx` and set a dirty bit at `__gx+0x5FC` that `__GXSetDirtyState` (0x801C5430) flushes from `GXBegin`/`GXCallDisplayList`; the runtime's `GXCallDisplayList` HLE knew nothing of it.
- **Fix:** `NsmbwCallDisplayList_801C9720` invokes 0x801C5430 when the bit is set (`nsmbw_gx_overrides.cpp`).
- **Scope:** NSMBW-specific (class 3).

### Locked-cache DMA never happened — the actual "no 3D" cause
- **Symptom:** every 3D vertex collapsed; view-matrix palettes in main RAM stayed zero.
- **Root cause:** nw4r::g3d computes node matrices in the locked L1 cache (0xE0000000) and DMAs them back with `LCLoadBlocks`/`LCStoreBlocks`/`LCStoreData`. Binding those (`nsmbw_os_lockedcache_overrides.cpp`) was insufficient: the translator inlines them (`inline leaf 0x…` in the shards), so callers wrote SPR 922/923 (DMAU/DMAL) directly and nothing honoured the writes (class 2).
- **Fix:** `PPC_WriteSpr` in `runtime/src/ppc_helpers.cpp` performs the DMA on the SPR 923 trigger bit and calls `GxNotifyGuestRamDmaWrite`.
- **Scope:** General (any title using the locked cache).

### Pipeline cache WAL corruption after an abort
- **Symptom:** after one killed run, every launch aborted `0xC0000409` right after "Using framebuffer size".
- **Root cause:** half-written SQLite WAL in `nsmbw_data/Cache/{pipeline_cache,dawn_cache}.db*`; the prewarm crashed reading it. Config and NAND save ruled out first.
- **Fix:** `ResetPersistentStateForCleanRun()` writes `.last_run_unclean` at start, removes it in `atexit`, and wipes `Cache/` only if the marker survived (`nsmbw_product.cpp`; `NSMBW_KEEP_STATE=1` opts out).
- **Scope:** General.

## 2026-09-20 — cutscene, file select, audio, input, world map, 1-1

### Display-list recording was a dead override (commit `5c4baa9`)
- **Symptom:** cutscene crash `[aurora] unmapped vtx attr 9`; ~98 `GX guest pointer: memory error` lines per run; 10+ FIFO-desync events. The `NSMBW_LOG_FIFO_DESYNC` ring showed draws with `LR=0x801C9690`, inside NSMBW's `GXEndDisplayList`.
- **Root cause:** the host diverts gather-pipe writes into the guest's buffer only while `g_dlRecordState.active`, which only MKW's bound `GXBeginDisplayList` ever set. NSMBW's Begin/End (0x801C95B0 / 0x801C9670) ran translated, so recorded commands executed live and the buffer replayed stale memory. The record state also published wrap/count to MKW's fifo-object address, which is live NSMBW `.data`.
- **Fix:** `nsmbw_gx_displaylist_overrides.cpp` mirrors the guest bookkeeping (dirty flush, `__gx+0x5F8/0x5F9`, state save at 0x80390850) and starts/stops host recording; record state carries the guest fifo-object address (NSMBW 0x803907D0). Memory errors 98 → 0, desyncs → 0.
- **Scope:** NSMBW-specific (classes 1 and 3); the parameterised fifo address is a general runtime change.

### GAME_SETUP stuck in `VoiceEndWait` — audio never advanced (commit `b4f0f8d`)
- **Symptom:** after player-count select nothing happened; `NSMBW_LOG_STATE_CHANGES` showed the last transition `LowBatteryCheck → VoiceEndWait`.
- **Root cause:** only `AIInit` was bound. `AIRegisterDMACallback`/`AIInitDMA`/`AIStartDMA`/getters (0x8019F1F0/F240/F2C0/F2E0/F2F0/F310) ran translated against flat storage, so `__AXNextFrame` never ran and no sound ever "finished". `AIStartDMA` and the getters are inlined into their AX call sites (class 2). The four AX DSP-task callbacks (0x801A1D90/1DA0/1E00/1E10) are reached only through a struct of pointers and were never translated. With audio live, the scheduler's idle loop broke out right after `Audio_HLE_Poll` on every pass and starved `VI_HLE_PollRetrace`, leaving the main thread WAITING in `EGG::AsyncDisplay::beginFrame`.
- **Fix:** bind the AI DMA family (`nsmbw_ai_overrides.cpp`); `dsp.cpp` serves the AI DMA registers (0xCC005030–3A) and starts DMA on the CTRL_LEN bit-15 write; callbacks seeded in `function_map.txt`; early break removed from the idle loop (`os_scheduler.cpp`).
- **Scope:** NSMBW-specific for the bindings; General for the register-level AI DMA and the idle-loop fix. Pitfall: re-seeding from 0x80004000 instead of the entry 0x80004050 drops the entry point.

### `GXLoadTexObj` override dropped the SU texture-size cache (commit `0db7c99`)
- **Symptom:** every 2D layout screen a smeared gradient with a hard band; strap and title elements invisible; `NSMBW_LOG_DRAW_TEXGEN` showed `suScale=1/1` against 628×96 / 198×164 textures while decoded textures and TEV/blend/projection were correct.
- **Root cause:** SU_TS0/1 (BP 0x30/0x31) are written by the guest's `__GXSetSUTexRegs` (0x801C7A10) from the image0/mode0 words the SDK's `GXLoadTexObj` caches at `__gx+0x564` / `+0x584`; the native override never stored them, so the guest computed 1×1. MKW HLEs `__GXSetSUTexRegs` itself.
- **Fix:** the override writes both words under `MKW_RUNTIME_PRODUCT_NSMBW`.
- **Scope:** NSMBW-specific (class 3).

### Raw-FIFO staging buffer reset on overflow (commit `5ffe822`)
- **Symptom:** cutscene item rain: FPS to 1–2, `GX guest pointer memory error 0x58000004`, XF PosMtx warnings, access violation.
- **Root cause:** `g_hleGxState.fifoBytes` was a fixed 4,096-byte array; a single raw draw exceeded it, `pushBytes` reset the buffer and the parser resumed mid-packet.
- **Fix:** growable `std::vector` (observed 16,384 bytes).
- **Scope:** General.

### World-map crashes: cross-module calls, REL `.bss`, REL vtables (commit `b571273`)
- **Symptom:** `missing_target 0x80B6E7A0`, `0x80B73330`; `missing_target 0x0` from 0x808DC34C at the level-entry wipe (a world-map object's vtable word read 0x000254E8 at boot).
- **Root causes:** (1) calls between RELs go through relocations the recursive translator does not follow — 77 targets never translated. (2) `RelFile.BuildImage` copied the REL file's tail (relocation tables) over the `.bss` range instead of leaving it zero, as `OSLink` does. (3) `extract-data-function-pointers` scanned only main.dol's data, so REL vtables yielded no seeds.
- **Fix:** `cross_module_call_target_*` seeds (re-translating a REL needs `--rel-projects` with all four ymls); `BuildImage` stops at `bssOffset` (570 translator tests pass); REL data scanned via `CollectDataSectionFunctionPointerTargets()` (21 `rel_vtable_target_func_*` seeds).
- **Scope:** General (translator) for (2) and (3); (1) is a general limitation with NSMBW-specific seeds.

### Save re-created on every launch (commit `078a0f5`)
- **Symptom:** `wiimj2d.sav` on disk, yet every run replayed the intro; `NSMBW_LOG_NAND` showed `ExistFileCheck → NandSpaceCheck → CreateFile` after `IOS_Ioctlv(READDIR, '/title/…/wiimj2d.sav') -> -106`.
- **Root cause:** the SDK's `NANDGetType` classifies a path by `ISFS_ReadDir`'s *error code*: OK = directory, EINVAL (−101) = file, ENOENT (−106) = nothing. `HandleIsfsReadDir` returned ENOENT for any non-directory.
- **Fix:** return EINVAL for an existing file (`nand_isfs.cpp`); `ResetPersistentStateForCleanRun` keeps the save unless `NSMBW_RESET_SAVE=1`. (A slot is only marked started on first reaching the map, so a run stopped mid-intro still shows "NEW" — game behaviour.)
- **Scope:** General (error-code semantics are part of the HLE contract).

### Levels invisible: viewport/scissor bookkeeping never mirrored (commit `1a07f39`)
- **Symptom:** in 1-1 HUD/hills/clouds drew; sky black, ground, Mario and enemies missing. 351 logged level draws all had `vp=(0,0 0x0)` and `scissor=(-342,-342 1x1)`.
- **Root cause:** `nsmbw_gx_setters_diag.cpp` bound `GXSetViewport` (0x801C9D50) / `GXSetScissor` (0x801C9DA0) without the `__GXData` stores (six floats at `__gx+0x544`; `suScis0/1` at `+0x148/0x14C`); translated readers `GXGetViewportv`, `GXGetScissor` (inlined into `d2d::Multi_c::draw`, `LytBase_c::SetScissorMask`, 0x8008A230) and `__GXSetViewport` restored zeros after each render-to-texture pass. Scissor −342/1×1 is the fingerprint of raw-zero BP registers.
- **Fix:** overrides mirror the SDK stores exactly.
- **Scope:** NSMBW-specific (class 3).

### Regression: 2 px alternation from re-setting `GX_DIRTY_VIEWPORT` (commit `a2642e8`)
- **Symptom:** wrong/black patches on the map, title and levels right after the fix above (black grass on part of the world map); `NSMBW_LOG_VIEWPORT` showed `ox=660` and `ox=662` alternating per frame.
- **Root cause:** mirroring the dirty bit made the guest's `__GXSetViewport` re-emit XF 0x101A–0x101F on every flush with the SDK's +342 centre offset, while aurora encodes/decodes its own viewport with +340.
- **Fix:** store the floats (for `GXGetViewportv`) without the dirty bit; the HLE call already applied the viewport. Mirror the *stores* other guest code reads, not side effects the host already performed.
- **Scope:** NSMBW-specific. **Open:** aurora's +340 vs hardware +342 means guest-written XF viewports (display lists, `GXSetViewportJitter`) are 2 px off.

## 2026-09-21 — texture cache across levels

### Stale tileset after a level change: BP 0x66 (texture invalidate) ignored
- **Symptom:** 1-1 renders correctly; 1-2 (and 1-3 / tower) draw every level-specific tile with a slice of 1-1's grass tileset. Geometry, the shared Pa0 tiles (? blocks, bricks, pipes), sprites and the HUD are all right — only the Pa1 tileset is wrong, and it is the *previous* level's.
- **Root cause:** aurora's static texture cache keys a source on (guest pointer, width, height, format, mips) and only re-checks the bytes after its validation revision is bumped, which only `GX_LOAD_AURORA_INVALIDATE_TEX_ALL` did — the private sub-command the host `GXInvalidateTexAll` emits. NSMBW's `GXInvalidateTexAll` (0x801C7800, not bound; MKW's is bound at 0x80171110) runs as translated code and writes the hardware register instead: BP `0x66001000` / `0x66001100`, which `handle_bp` in `aurora-main/lib/gx/command_processor.cpp` dropped. The level heap is rebuilt at the same addresses between levels, so Pa1_chika's 1024×256 RGB5A3 texture landed exactly where Pa1_nohara's had been, matched the key and was served from cache.
- **Fix:** `handle_bp` treats any BP 0x66 write as `invalidate_static_texture_cache()` (checked before the register value dedup, since the write is a command, not state). Guest write-tracking hooks are installed, so the bump costs a generation compare per texture, not a re-hash. Developer confirmed 1-2 renders with the right tileset; 1-1 self-test run unchanged.
- **Scope:** General (aurora) — any title whose SDK `GXInvalidateTexAll`/`GXInvalidateTexRegion` runs as guest code hits this; MKW only avoided it through its native binding. Class 1 (override at MKW's address is dead for NSMBW), fixed at the register level so it doesn't depend on a per-title binding.

---

## Open / unconfirmed items

Not fixed, or fixed by a guess. Listed so the scope split later does not miss them.

- `NsmbwBootStub_00000060` — unknown low-memory routine, log-and-return.
- `func_801AF900` no-op (colour/curve table), `func_801AC980` / `func_801AD620` / `func_801AD9E0` abort stubs; second cause for their non-translation undiagnosed.
- `func_801A9CE0` decode bug worked around by override; not fixed in the translator.
- OSAlarm HLE unbound for NSMBW (`os_internal.h`).
- `EXIProbe` always "not found"; `nsmbw_disk_id_check_sync.cpp` skips 0x801D2D60's own bookkeeping.
- Strap screen never auto-advances without input (decomp's 1200-frame `mAutoAdvanceTimer` path).
- aurora viewport offset 340 vs hardware 342.
- Leftover `[debug]` print for target 0x60 in `Program.cs` discovery loop.
- `*_diag.cpp` overrides whose areas are now stable (each removal needs the shard manifest regenerated).

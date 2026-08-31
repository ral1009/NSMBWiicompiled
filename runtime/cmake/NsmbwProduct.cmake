# Experimental NSMBW recompiled product.
#
# This is NOT another entry in PublicProducts.cmake's WiiCompiled/RetroRewind pair: those are
# alternate builds of the *same* game (a shared base plus a selectable mod overlay - you build
# one or the other). NSMBW's four .rel modules are not alternatives to main.dol; the real game
# loads all four into memory alongside it at once, so this graph is "translate everything, merge
# it into one object graph, link one executable" - see docs/MASTER_PLAN.md Phase 4/5's notes on
# why WiiCompiled's product model didn't fit before this file existed.
#
# Off by default so a normal MKW configure/build is completely unaffected.
option(MKW_BUILD_NSMBW "Build the experimental NSMBW recompiled product" OFF)

if(MKW_BUILD_NSMBW)
    set(NSMBW_SHARD_MANIFEST
        "${MKW_RUNTIME_SOURCE_DIR}/../generated_nsmbw/build_shards/shards.cmake"
        CACHE FILEPATH "Translator-owned NSMBW combined shard manifest")
    if(NOT EXISTS "${NSMBW_SHARD_MANIFEST}")
        message(FATAL_ERROR
            "Missing NSMBW shard manifest: ${NSMBW_SHARD_MANIFEST}. "
            "Run 'Translator.Cli emit-nsmbw-build-shards --modules-file projects/nsmbw/modules.txt "
            "--native-source-dir projects/nsmbw/native' first.")
    endif()
    include("${NSMBW_SHARD_MANIFEST}")
    message(STATUS "NSMBW translator graph: ${NSMBW_FUNCTION_COUNT} unique functions across main.dol + 4 RELs")

    if(NOT NSMBW_SHARDS)
        message(FATAL_ERROR "NSMBW shard manifest contains no shards")
    endif()

    add_library(nsmbw_translated OBJECT ${NSMBW_SHARDS} ${NSMBW_REGISTRATION_SOURCES})
    mkw_configure_translated_target(nsmbw_translated)
    target_precompile_headers(nsmbw_translated PRIVATE "${MKW_RUNTIME_SOURCE_DIR}/include/mkw_pch.h")

    # Hand-written native overrides for this project (projects/nsmbw/native/, e.g. the
    # 0x00000060 unknown-boot-call stub). Ordinary C++, not translated PPC, so it gets the
    # normal object-target settings instead of the translated-code compile options.
    file(GLOB NSMBW_NATIVE_SOURCES CONFIGURE_DEPENDS
        "${MKW_RUNTIME_SOURCE_DIR}/../projects/nsmbw/native/*.cpp")
    if(NSMBW_NATIVE_SOURCES)
        add_library(nsmbw_native OBJECT ${NSMBW_NATIVE_SOURCES})
        mkw_configure_object_target(nsmbw_native)
        mkw_apply_common_compile_options(nsmbw_native)
    endif()

    # One combined initializer for main.dol + all 4 RELs (Translator.Cli generate-nsmbw-data-init
    # --rel-projects ...) - not per-module: each project's own generate-data-init only knows
    # about a single dol+rel pair, and every one of those separately-generated files defines the
    # same extern "C" InitializeDataSections()/IsDataSectionsInitialized() symbols, which would
    # collide at link time the moment more than one was included.
    set(NSMBW_DATA_INIT_FILE "${MKW_RUNTIME_SOURCE_DIR}/../generated_nsmbw/data_sections_init.cpp")
    set(NSMBW_DATA_INIT_BLOB_ASM "${MKW_RUNTIME_SOURCE_DIR}/../generated_nsmbw/data_sections_init_blobs.S")
    set(NSMBW_GUEST_SYMBOL_TABLE_FILE "${MKW_RUNTIME_SOURCE_DIR}/../generated_nsmbw/guest_symbol_table.cpp")
    if(NOT EXISTS "${NSMBW_DATA_INIT_FILE}")
        message(FATAL_ERROR
            "Missing combined NSMBW data initializer: ${NSMBW_DATA_INIT_FILE}. Run "
            "'Translator.Cli generate-nsmbw-data-init --project projects/nsmbw/nsmbw.yml "
            "--rel-projects projects/nsmbw/nsmbw-d_basesNP.yml,projects/nsmbw/nsmbw-d_en_bossNP.yml,"
            "projects/nsmbw/nsmbw-d_enemiesNP.yml,projects/nsmbw/nsmbw-d_profileNP.yml' first.")
    endif()
    set(NSMBW_DATA_INIT_SOURCES "${NSMBW_DATA_INIT_FILE}")
    if(EXISTS "${NSMBW_DATA_INIT_BLOB_ASM}")
        enable_language(ASM)
        set_source_files_properties("${NSMBW_DATA_INIT_BLOB_ASM}" PROPERTIES
            LANGUAGE ASM SKIP_UNITY_BUILD_INCLUSION ON)
        list(APPEND NSMBW_DATA_INIT_SOURCES "${NSMBW_DATA_INIT_BLOB_ASM}")
    endif()
    if(EXISTS "${NSMBW_GUEST_SYMBOL_TABLE_FILE}")
        list(APPEND NSMBW_DATA_INIT_SOURCES "${NSMBW_GUEST_SYMBOL_TABLE_FILE}")
    else()
        list(APPEND NSMBW_DATA_INIT_SOURCES "${MKW_RUNTIME_SOURCE_DIR}/cmake/guest_symbol_table_stub.cpp")
    endif()
    add_library(nsmbw_data_init OBJECT ${NSMBW_DATA_INIT_SOURCES})
    mkw_configure_object_target(nsmbw_data_init)
    mkw_apply_common_compile_options(nsmbw_data_init)

    # NSMBW's own copy of the generic runtime (memory model, PPC/CPU-context dispatch, HLE OS
    # stubs) - reusing mkw_runtime_common directly is not an option, for two reasons found while
    # wiring this up:
    #   1. It already contains runtime/src/main.cpp, which defines main()/WinMain() - linking it
    #      alongside this file's own entry point (below) would be a duplicate-entry-point error.
    #   2. runtime/src/abi_bridge.cpp and hle/os/os_alarm.cpp hardcode
    #      #include "generated/RuntimeConfig.h" resolved against the workspace root, which for an
    #      already-compiled mkw_runtime_common object means MKW's own SDA bases are baked in, not
    #      NSMBW's - r2/r13 would seed wrong and corrupt every small-data access. The BEFORE
    #      include directory below points that same #include at NSMBW's own generated copy
    #      instead, without touching either shared source file.
    # This does mean recompiling most of runtime/src a second time - a real build-time cost, but
    # not a correctness risk, and not worth avoiding by forking those two files just for this.
    file(GLOB_RECURSE NSMBW_RUNTIME_COMMON_SOURCES CONFIGURE_DEPENDS "${MKW_RUNTIME_SOURCE_DIR}/src/*.cpp")
    list(REMOVE_ITEM NSMBW_RUNTIME_COMMON_SOURCES
        "${MKW_RUNTIME_SOURCE_DIR}/src/main.cpp"
        "${MKW_RUNTIME_SOURCE_DIR}/src/product/base_product.cpp"
        "${MKW_RUNTIME_SOURCE_DIR}/src/product/retro_rewind_product.cpp"
        "${MKW_RUNTIME_SOURCE_DIR}/src/product/nsmbw_product.cpp"
        "${MKW_RUNTIME_SOURCE_DIR}/src/host_cpu_baseline.cpp")
    if(NOT WIN32)
        list(REMOVE_ITEM NSMBW_RUNTIME_COMMON_SOURCES "${MKW_RUNTIME_SOURCE_DIR}/src/wup028_adapter.cpp")
    endif()
    # music_attenuation.cpp needs a C++/WinRT SDK (MKW_CPPWINRT_INCLUDE_DIR) not configured for
    # this build; the two symbols other files actually call from it are stubbed in
    # projects/nsmbw/native/nsmbw_runtime_shims.cpp instead (see that file's own comment).
    # (hle/gx/gx_texture.cpp's real g_TlutObjMeta bug and hle/storage/riivolution.cpp's missing
    # #include, both found the same way - compiling each file standalone instead of merged into
    # mkw_runtime_common's unity build, which happened to paper over both - are fixed directly in
    # the shared source instead of excluded; see nand_path.h and gx_texture.cpp.)
    #
    # ax_effects.cpp (audio DSP reverb) is excluded outright: it hardcodes a direct C++ call into
    # one specific MKW dol address with no corresponding NSMBW function, and has no override worth
    # preserving in exchange (unlike hle/net/network_config.cpp, hle/os/os_alarm.cpp,
    # hle/os/os_init.cpp and hle/os/os_scheduler.cpp - all four also hardcode a callback/chain
    # call into one specific MKW address, but each registers other generic Wii overrides too -
    # IOS network ioctls, thread scheduling, OSFatal, SIInit - worth keeping, so they stay
    # included and their one dangling callback address is stubbed in nsmbw_runtime_shims.cpp
    # instead of losing the whole file).
    list(REMOVE_ITEM NSMBW_RUNTIME_COMMON_SOURCES
        "${MKW_RUNTIME_SOURCE_DIR}/src/music_attenuation.cpp"
        "${MKW_RUNTIME_SOURCE_DIR}/src/hle/audio/ax_effects.cpp")
    add_library(nsmbw_runtime_common OBJECT ${NSMBW_RUNTIME_COMMON_SOURCES})
    mkw_configure_object_target(nsmbw_runtime_common)
    target_include_directories(nsmbw_runtime_common BEFORE PRIVATE
        "${MKW_RUNTIME_SOURCE_DIR}/../generated_nsmbw")
    target_compile_features(nsmbw_runtime_common PRIVATE cxx_std_20)
    # MKW_RUNTIME_PRODUCT_NSMBW: distinguishes this compile of the shared runtime/src tree from
    # mkw_runtime_common's. Needed where a guest-code-address native override in a shared file is
    # only valid for MKW's link layout - NSMBW's own compiled binary can (and does) place an
    # unrelated real function at that same absolute address, so building the override in
    # unconditionally would either shadow real NSMBW behavior or duplicate-symbol the link. See its
    # use in hle/input/kpad.cpp, hle/ios.cpp, hle/os/os_alarm.cpp, hle/storage/nand_isfs.cpp.
    target_compile_definitions(nsmbw_runtime_common PRIVATE
        SDL_MAIN_HANDLED _DISABLE_STRING_ANNOTATION _DISABLE_VECTOR_ANNOTATION
        MKW_RUNTIME_PRODUCT_NSMBW)
    target_link_libraries(nsmbw_runtime_common PRIVATE
        aurora::gx aurora::pad aurora::si aurora::vi aurora::mtx
        mkw::pugixml mkw::toml11 mkw::cryptopp)
    if(WIN32)
        target_link_libraries(nsmbw_runtime_common PRIVATE shell32 windowsapp)
    else()
        target_link_libraries(nsmbw_runtime_common PRIVATE mkw::libco)
    endif()

    # PublicProducts.cmake applies -march=x86-64-v3 (AVX2/FMA/BMI2) to its own targets in one
    # foreach right at the end of that file - which has already run by the time this file is
    # included, so it never sees these targets. Without it, the always-inline x86 SIMD intrinsics
    # in runtime/include/isa/ppc_isa_float.h and ppc_isa_quantized.h (_mm_fmadd_ps, _mm_shuffle_epi8)
    # fail to compile: clang refuses to inline a function requiring FMA/SSSE3 into a caller that
    # was compiled without those target features enabled.
    foreach(nsmbw_target IN ITEMS nsmbw_translated nsmbw_native nsmbw_data_init nsmbw_runtime_common)
        if(TARGET ${nsmbw_target})
            target_compile_options(${nsmbw_target} PRIVATE -march=x86-64-v3)
        endif()
    endforeach()

    # Debug info for interactive debugging (Visual Studio, etc.) of the boot-sequence crash this
    # is currently stuck on. Deliberately not on nsmbw_translated - 30,000+ generated functions'
    # worth of debug info would bloat the build a lot for no benefit here, since the crash lives
    # in our own hand-written code (nsmbw_product.cpp, nsmbw_runtime_shims.cpp), not translated
    # PPC. This is a Release build (-O3), so optimization can still make some locals/steps hard to
    # inspect - if that turns out to matter, the next step would be a non-optimized build of just
    # these two targets, not this project-wide.
    foreach(nsmbw_debug_target IN ITEMS nsmbw_native nsmbw_runtime_common)
        if(TARGET ${nsmbw_debug_target})
            target_compile_options(${nsmbw_debug_target} PRIVATE -g)
        endif()
    endforeach()

    # The Phase 5 milestone executable: no window, no aurora frame, no input - just prove
    # decode -> lift -> emit -> compile -> link works by initializing guest memory and jumping
    # into the real dol entry point. See runtime/src/product/nsmbw_product.cpp's own header
    # comment for why this isn't runtime/src/main.cpp, and for what happens next (an
    # unimplemented HLE call will hit RuntimeCrash::FatalMissingGuestTarget - expected, and
    # Phase 6's actual starting point).
    add_executable(NSMBWCompiled "${MKW_RUNTIME_SOURCE_DIR}/src/product/nsmbw_product.cpp")
    mkw_configure_object_target(NSMBWCompiled)
    target_compile_features(NSMBWCompiled PRIVATE cxx_std_20)
    target_compile_options(NSMBWCompiled PRIVATE -march=x86-64-v3 -g)
    # -g alone only affects compiling; the linker strips debug sections unless told to keep them.
    target_link_options(NSMBWCompiled PRIVATE -g)
    target_sources(NSMBWCompiled PRIVATE
        $<TARGET_OBJECTS:nsmbw_translated>
        $<TARGET_OBJECTS:nsmbw_native>
        $<TARGET_OBJECTS:nsmbw_data_init>
        $<TARGET_OBJECTS:nsmbw_runtime_common>
        $<TARGET_OBJECTS:mkw_cpu_baseline>)
    target_link_libraries(NSMBWCompiled PRIVATE
        aurora::gx aurora::pad aurora::si aurora::vi aurora::mtx
        mkw::pugixml mkw::toml11 mkw::cryptopp)
    if(WIN32)
        target_link_libraries(NSMBWCompiled PRIVATE
            dbghelp user32 winmm ws2_32 iphlpapi secur32 crypt32 windowsapp setupapi winusb shell32)
    else()
        target_link_libraries(NSMBWCompiled PRIVATE mkw::libco)
    endif()

    # Same runtime-DLL copy PublicProducts.cmake's mkw_configure_product() does for
    # WiiCompiled/RetroRewind: aurora/SDL3/Dawn are shared libraries the exe loads at startup, not
    # linked in statically, so without this the exe fails immediately with STATUS_DLL_NOT_FOUND
    # (confirmed the hard way - it printed nothing and exited 0xC0000135) before ever reaching
    # main(). NSMBWCompiled doesn't call mkw_configure_product() (that function also wires up
    # MKW's wii_bootstrap/DSP-coefficient/pipeline-cache asset copying, none of which NSMBW has or
    # needs yet), so this piece is pulled in on its own instead.
    if(EXISTS "${MKW_AURORA_DIR}/cmake/AuroraCopyRuntimeDLLs.cmake")
        include("${MKW_AURORA_DIR}/cmake/AuroraCopyRuntimeDLLs.cmake")
        aurora_copy_runtime_dlls(NSMBWCompiled)
    endif()
    if(WIN32)
        foreach(nsmbw_runtime_dll libc++.dll libunwind.dll)
            execute_process(
                COMMAND "${CMAKE_CXX_COMPILER}" "--print-file-name=${nsmbw_runtime_dll}"
                OUTPUT_VARIABLE nsmbw_runtime_dll_path
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT EXISTS "${nsmbw_runtime_dll_path}")
                get_filename_component(nsmbw_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
                set(nsmbw_runtime_dll_path "${nsmbw_compiler_bin}/${nsmbw_runtime_dll}")
            endif()
            if(NOT EXISTS "${nsmbw_runtime_dll_path}")
                message(FATAL_ERROR "llvm-mingw runtime DLL not found: ${nsmbw_runtime_dll}")
            endif()
            add_custom_command(TARGET NSMBWCompiled POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${nsmbw_runtime_dll_path}" $<TARGET_FILE_DIR:NSMBWCompiled>)
        endforeach()
    endif()

    message(STATUS "NSMBWCompiled executable target ready.")
endif()

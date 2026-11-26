cmake_minimum_required(VERSION 3.25)
project(nsmbw_linux_launcher_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.4/SDL3-3.4.4.tar.gz
    URL_HASH SHA256=ee712dbe6a89bb140bbfc2ce72358fb5ee5cc2240abeabd54855012db30b3864
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(SDL3)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b-docking.tar.gz
    URL_HASH SHA256=466fdef9b18de15f0bb6e288e3d00ffa3d82200ec458ce5e4f724a161d9528a5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)
set(runtime "${CMAKE_CURRENT_LIST_DIR}/../..")
add_executable(NSMBWCompiled
    "${runtime}/src/product/nsmbw_launcher_linux.cpp"
    "${runtime}/src/host_cpu_baseline.cpp")
target_compile_definitions(NSMBWCompiled PRIVATE
    MKW_RUNTIME_PRODUCT_NSMBW MKW_CPU_BASELINE_NO_CTOR)
target_include_directories(NSMBWCompiled PRIVATE
    "${runtime}/include" "${runtime}/third_party/toml11")
include("${runtime}/../projects/nsmbw/linux/launcher.cmake")
nsmbw_configure_linux_launcher()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/test_game.cpp"
    "#include <cstdlib>\nextern \"C\" int NsmbwGameMain() { return std::getenv(\"MKW_FORCE_UNSUPPORTED_CPU\") ? 99 : 37; }\n")
add_library(test_game SHARED "${CMAKE_CURRENT_BINARY_DIR}/test_game.cpp")
set_target_properties(test_game PROPERTIES PREFIX "")

# Compile the actual product glue even when no disc is available for translation.
add_library(nsmbw_product_compile OBJECT "${runtime}/src/product/nsmbw_product.cpp")
target_include_directories(nsmbw_product_compile PRIVATE
    "${runtime}/include" "${runtime}/src" "${runtime}/../aurora-main/include"
    "${runtime}/third_party/toml11" "${imgui_SOURCE_DIR}")
target_compile_definitions(nsmbw_product_compile PRIVATE TARGET_PC MKW_RUNTIME_PRODUCT_NSMBW)
target_compile_options(nsmbw_product_compile PRIVATE -march=x86-64-v3 -fPIC)
target_link_libraries(nsmbw_product_compile PRIVATE SDL3::SDL3)

add_library(libco STATIC "${runtime}/third_party/libco/libco.c")
target_include_directories(libco PUBLIC "${runtime}/third_party/libco")
add_executable(fiber_stack fiber_stack.cpp)
target_include_directories(fiber_stack PRIVATE "${runtime}/include")
target_link_libraries(fiber_stack PRIVATE libco)
add_executable(memory_fault memory_fault.cpp)
target_include_directories(memory_fault PRIVATE "${runtime}/include")

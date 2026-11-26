# Keep setup and its UI usable on a v2 CPU before choosing the native game library.
function(nsmbw_configure_linux_launcher)
    FetchContent_GetProperties(imgui SOURCE_DIR imgui_dir)
    target_include_directories(NSMBWCompiled PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}" "${imgui_dir}")
    target_sources(NSMBWCompiled PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/setup.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/disc_setup.cpp"
        "${imgui_dir}/imgui.cpp"
        "${imgui_dir}/imgui_draw.cpp"
        "${imgui_dir}/imgui_tables.cpp"
        "${imgui_dir}/imgui_widgets.cpp"
        "${imgui_dir}/backends/imgui_impl_sdl3.cpp"
        "${imgui_dir}/backends/imgui_impl_sdlrenderer3.cpp")
    target_link_libraries(NSMBWCompiled PRIVATE SDL3::SDL3 ${CMAKE_DL_LIBS})
endfunction()

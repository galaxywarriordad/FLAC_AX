set(_flac_ax_persist_src "${CMAKE_CURRENT_SOURCE_DIR}/src/persist/ax_persist_callers.cpp")
if(EXISTS "${_flac_ax_persist_src}")
    add_library(flac_ax_persist STATIC
        "${_flac_ax_persist_src}"
    )
    flac_ax_configure_target(flac_ax_persist)

    if(TARGET ax::core)
        target_link_libraries(flac_ax_persist PUBLIC ax::core)
    elseif(TARGET ax_core)
        target_link_libraries(flac_ax_persist PUBLIC ax_core)
    endif()

    target_include_directories(flac_ax_persist PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    foreach(_flac_ax_persist_consumer IN ITEMS ax_av_studio ax_video_upscaler flac_ax)
        if(TARGET "${_flac_ax_persist_consumer}")
            target_link_libraries("${_flac_ax_persist_consumer}" PRIVATE flac_ax_persist)
        endif()
    endforeach()
endif()

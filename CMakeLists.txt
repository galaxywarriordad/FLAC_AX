add_library(flac_ax_persist STATIC
    src/persist/ax_persist_callers.cpp
)
flac_ax_configure_target(flac_ax_persist)

# Link flac_ax_persist into studio, upscaler, and host targets that save presets/playlists:
# if(TARGET ax_av_studio)
#     target_link_libraries(ax_av_studio PRIVATE flac_ax_persist)
# endif()
# if(TARGET ax_video_upscaler)
#     target_link_libraries(ax_video_upscaler PRIVATE flac_ax_persist)
# endif()

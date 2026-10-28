# marigold-channeldir: the channel directory the app carries
# (tools/channeldir/channeldir.cpp). CI runs it; the TV never fetches it.
add_executable(marigold-channeldir channeldir.cpp)
marigold_warnings(marigold-channeldir)
target_link_libraries(marigold-channeldir PRIVATE marigold_core)

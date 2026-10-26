# Trimmed Legacy C (see UPSTREAM.md). Built without our warning flags.
add_library(legacy STATIC
  src/chsegura.c
  src/dados.c
  src/entrada_texto.c
  src/js.c
  src/lsregistro.c
  src/ponteiro.c
  src/proxyts.c
  src/rede.c
  src/redeurl.c
  src/video.c
  src/video_modo.c
  src/webosver.c
  shim/marco.c
)
target_include_directories(legacy PUBLIC src shim)
target_compile_definitions(legacy PUBLIC NV_WEBOS)
target_compile_options(legacy PRIVATE -w)
find_package(Threads REQUIRED)
target_link_libraries(legacy PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

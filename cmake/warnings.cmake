# Strict warnings for our own code. Vendored Legacy C is built without them:
# it is kept close to upstream and warnings there are not ours to fix.
option(MARIGOLD_WERROR "Treat warnings in our code as errors" ON)

function(marigold_warnings target)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
    -Wunused -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion
    -Wformat=2 -Wimplicit-fallthrough)
  if(MARIGOLD_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()

function(marigold_warnings_c target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wformat=2)
  if(MARIGOLD_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()

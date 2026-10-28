# PCRE2 10.44 (BSD licence, see LICENCE), library sources only, from
# https://github.com/PCRE2Project/pcre2/releases/tag/pcre2-10.44
# (tarball sha256 86b9cb0aa3bcb7994faa88018292bc704cdbb708e785f7c74352ff6ea7d3175b).
# 8-bit, Unicode, no JIT. config.h and pcre2.h are the upstream .generic files;
# the options are set here.
add_library(pcre2 STATIC
  src/pcre2_auto_possess.c src/pcre2_chartables.c src/pcre2_chkdint.c
  src/pcre2_compile.c src/pcre2_config.c src/pcre2_context.c src/pcre2_convert.c
  src/pcre2_dfa_match.c src/pcre2_error.c src/pcre2_extuni.c src/pcre2_find_bracket.c
  src/pcre2_jit_compile.c src/pcre2_maketables.c src/pcre2_match.c
  src/pcre2_match_data.c src/pcre2_newline.c src/pcre2_ord2utf.c
  src/pcre2_pattern_info.c src/pcre2_script_run.c src/pcre2_serialize.c
  src/pcre2_string_utils.c src/pcre2_study.c src/pcre2_substitute.c
  src/pcre2_substring.c src/pcre2_tables.c src/pcre2_ucd.c src/pcre2_valid_utf.c
  src/pcre2_xclass.c
)
# Users include <marigold-pcre2/pcre2.h>: a plain <pcre2.h> can find another
# PCRE2's header first (Homebrew's in /usr/local/include on Intel Macs),
# which doesn't match this library.
configure_file(src/pcre2.h ${CMAKE_CURRENT_BINARY_DIR}/include/marigold-pcre2/pcre2.h COPYONLY)
target_include_directories(pcre2 PRIVATE src)
target_include_directories(pcre2 SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
target_compile_definitions(pcre2
  PUBLIC PCRE2_CODE_UNIT_WIDTH=8 PCRE2_STATIC
  PRIVATE HAVE_CONFIG_H SUPPORT_PCRE2_8=1 SUPPORT_UNICODE=1 HAVE_MEMMOVE=1
          HAVE_STDINT_H=1 HAVE_INTTYPES_H=1 HAVE_LIMITS_H=1 HAVE_STRING_H=1
          HAVE_STDLIB_H=1)
target_compile_options(pcre2 PRIVATE -w)

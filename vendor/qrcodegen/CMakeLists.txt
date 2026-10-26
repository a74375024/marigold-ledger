# QR Code generator library (C++) 1.8.0 by Project Nayuki (MIT licence, see
# LICENSE): the two files of its cpp/ folder, unchanged, from
# https://github.com/nayuki/QR-Code-generator/tree/v1.8.0/cpp
# (qrcodegen.cpp sha256 1f3b3fcdac6954c32cf583ccd02ec9b5901f756a38c461acedc70be4a77d3757,
#  qrcodegen.hpp sha256 b779c3b156cf7a57ce789d6fee4fc991ccc2913774d26c909d22bb8f26b2a793).
# Used only through src/core/report/qr.hpp.
add_library(qrcodegen STATIC src/qrcodegen.cpp)
target_include_directories(qrcodegen SYSTEM PUBLIC src)
target_compile_options(qrcodegen PRIVATE -w)

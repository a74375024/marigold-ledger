// tvOS: the Metal side of the window (gfx_metal.mm). The app thread draws a
// frame between gfx::Renderer::beginFrame() and Window::swap(), which
// presents it.
#pragma once

#include <string>

#ifdef __OBJC__
@class CAMetalLayer;
#else
using CAMetalLayer = void;
#endif

namespace marigold::tvos::metal {

bool open(CAMetalLayer* layer);
void close();
std::string deviceName();
// Ends the frame begun by beginFrame and shows it.
void present();

}  // namespace marigold::tvos::metal

// tvOS: the UI renderer over Metal (gfx.hpp), drawing exactly what the GLES2
// renderer draws (gfx.cpp): one quad and one shader for rounded fills and
// outlines, tinted text and icons, and images; scissor clips; and holes that
// clear the layer to transparent so the video view under it shows. The
// blending keeps the layer premultiplied, as Core Animation composites it.
// The shader is compiled when the app starts (no Metal tools at build time).
#include "platform/gfx.hpp"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

#include "core/util/log.hpp"
#include "platform/tvos/metal.hpp"

namespace marigold::gfx {
namespace {

const char* const kShader = R"(
#include <metal_stdlib>
using namespace metal;

struct VUniforms {
  float4 rect;
  float2 layout;
};

struct FUniforms {
  float4 rect;
  float4 color;
  float radius;
  float thickness;
  float pixel;
  int mode;
};

struct VOut {
  float4 pos [[position]];
  float2 local;
  float2 uv;
};

vertex VOut vmain(uint vid [[vertex_id]], constant VUniforms& u [[buffer(0)]]) {
  float2 a = float2(float(vid & 1u), float(vid >> 1u));
  float2 p = u.rect.xy + a * u.rect.zw;
  VOut o;
  o.local = a * u.rect.zw;
  o.uv = a;
  o.pos = float4(p.x / u.layout.x * 2.0 - 1.0, 1.0 - p.y / u.layout.y * 2.0, 0.0, 1.0);
  return o;
}

float roundedBox(float2 p, float2 hs, float r) {
  float2 q = abs(p) - hs + float2(r);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

fragment float4 fmain(VOut in [[stage_in]], constant FUniforms& u [[buffer(0)]],
                      texture2d<float> tex [[texture(0)]], sampler s [[sampler(0)]]) {
  if (u.mode == 1) return float4(u.color.rgb, u.color.a * tex.sample(s, in.uv).a);
  if (u.mode == 3) return float4(0.0);
  float2 hs = u.rect.zw * 0.5;
  float2 p = in.local - hs;
  float r = min(u.radius, min(hs.x, hs.y));
  float cover = clamp(0.5 - roundedBox(p, hs, r) / u.pixel, 0.0, 1.0);
  if (u.thickness > 0.0) {
    float inner = roundedBox(p, hs - float2(u.thickness), max(r - u.thickness, 0.0));
    cover *= clamp(0.5 + inner / u.pixel, 0.0, 1.0);
  }
  if (u.mode == 2) {
    float4 t = tex.sample(s, in.uv);
    return float4(t.rgb, t.a * u.color.a * cover);
  }
  return float4(u.color.rgb, u.color.a * cover);
}
)";

// The layouts match the shader's structs (16-byte aligned float4s).
struct VUniforms {
  float rect[4];
  float layout[2];
  float pad[2];
};
struct FUniforms {
  float rect[4];
  float color[4];
  float radius;
  float thickness;
  float pixel;
  int32_t mode;
};
static_assert(sizeof(VUniforms) == 32 && sizeof(FUniforms) == 48, "shader uniform layout");

constexpr int kHole = 3;

struct State {
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> queue = nil;
  CAMetalLayer* layer = nil;
  id<MTLRenderPipelineState> blended = nil;  // every draw
  id<MTLRenderPipelineState> replace = nil;  // holes: written, not blended
  id<MTLSamplerState> sampler = nil;
  id<MTLTexture> blank = nil;                // bound when a draw samples nothing
  // The frame being drawn (nil when there is no drawable: in the background).
  id<CAMetalDrawable> drawable = nil;
  id<MTLCommandBuffer> commands = nil;
  id<MTLRenderCommandEncoder> encoder = nil;
  bool clipEmpty = false;
  // Textures by id (the app's unsigned handles).
  std::mutex texturesMu;
  std::unordered_map<unsigned, id<MTLTexture>> textures;
  unsigned nextTexture = 1;
};

State& state() {
  static State s;
  return s;
}

id<MTLRenderPipelineState> pipeline(id<MTLLibrary> library, MTLPixelFormat format, bool blend) {
  MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
  d.vertexFunction = [library newFunctionWithName:@"vmain"];
  d.fragmentFunction = [library newFunctionWithName:@"fmain"];
  MTLRenderPipelineColorAttachmentDescriptor* c = d.colorAttachments[0];
  c.pixelFormat = format;
  if (blend) {
    // As gfx.cpp: GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA for colour; GL_ONE,
    // GL_ONE_MINUS_SRC_ALPHA for alpha, so translucent draws never lower the
    // layer's alpha and punch through to the video.
    c.blendingEnabled = YES;
    c.rgbBlendOperation = MTLBlendOperationAdd;
    c.alphaBlendOperation = MTLBlendOperationAdd;
    c.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    c.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    c.sourceAlphaBlendFactor = MTLBlendFactorOne;
    c.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
  }
  NSError* error = nil;
  id<MTLRenderPipelineState> p = [library.device newRenderPipelineStateWithDescriptor:d error:&error];
  if (!p) MLOG_E("gfx", "pipeline failed: %s", error.localizedDescription.UTF8String ?: "?");
  return p;
}

MTLScissorRect scissor(int x0, int y0, int x1, int y1, int w, int h) {
  x0 = std::clamp(x0, 0, w);
  x1 = std::clamp(x1, 0, w);
  y0 = std::clamp(y0, 0, h);
  y1 = std::clamp(y1, 0, h);
  return MTLScissorRect{static_cast<NSUInteger>(x0), static_cast<NSUInteger>(y0),
                        static_cast<NSUInteger>(std::max(0, x1 - x0)), static_cast<NSUInteger>(std::max(0, y1 - y0))};
}

Renderer gRenderer;

}  // namespace

struct Renderer::Backend {
  int unused = 0;  // the Metal state is per process (state())
};

Renderer& renderer() { return gRenderer; }

bool Renderer::init(int drawableWidth, int drawableHeight) {
  drawableW_ = drawableWidth > 0 ? drawableWidth : 1920;
  drawableH_ = drawableHeight > 0 ? drawableHeight : 1080;
  State& s = state();
  if (!s.device) return false;
  NSError* error = nil;
  id<MTLLibrary> library = [s.device newLibraryWithSource:@(kShader) options:nil error:&error];
  if (!library) {
    MLOG_E("gfx", "shader compile failed: %s", error.localizedDescription.UTF8String ?: "?");
    return false;
  }
  s.blended = pipeline(library, s.layer.pixelFormat, true);
  s.replace = pipeline(library, s.layer.pixelFormat, false);
  if (!s.blended || !s.replace) return false;
  MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
  sd.minFilter = MTLSamplerMinMagFilterLinear;
  sd.magFilter = MTLSamplerMinMagFilterLinear;
  sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
  sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
  s.sampler = [s.device newSamplerStateWithDescriptor:sd];
  px::Image white;
  white.w = white.h = 1;
  white.rgba = {255, 255, 255, 255};
  unsigned blank = uploadTexture(white);
  {
    std::lock_guard<std::mutex> lock(s.texturesMu);
    s.blank = s.textures[blank];
  }
  if (!gpu_) gpu_ = new Backend;
  return true;
}

void Renderer::shutdown() {
  State& s = state();
  s.encoder = nil;
  s.commands = nil;
  s.drawable = nil;
  {
    std::lock_guard<std::mutex> lock(s.texturesMu);
    s.textures.clear();
  }
  s.blank = nil;
  s.blended = nil;
  s.replace = nil;
  s.sampler = nil;
  delete gpu_;
  gpu_ = nullptr;
}

void Renderer::beginFrame(Color clear) {
  clips_.clear();
  State& s = state();
  s.encoder = nil;
  s.commands = nil;
  s.drawable = nil;
  s.clipEmpty = false;
  if (!s.layer || !s.blended) return;
  @autoreleasepool {
    s.drawable = [s.layer nextDrawable];  // waits for one (at most a second)
    if (!s.drawable) return;
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = s.drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(static_cast<double>(clear.r), static_cast<double>(clear.g),
                                                            static_cast<double>(clear.b), static_cast<double>(clear.a));
    s.commands = [s.queue commandBuffer];
    s.encoder = [s.commands renderCommandEncoderWithDescriptor:pass];
  }
  [s.encoder setViewport:MTLViewport{0, 0, static_cast<double>(drawableW_), static_cast<double>(drawableH_), 0, 1}];
  [s.encoder setRenderPipelineState:s.blended];
  [s.encoder setFragmentSamplerState:s.sampler atIndex:0];
  [s.encoder setScissorRect:scissor(0, 0, drawableW_, drawableH_, drawableW_, drawableH_)];
}

void Renderer::draw(Rect r, Color c, float radius, float thickness, int mode, unsigned texture) {
  State& s = state();
  if (!s.encoder || s.clipEmpty || r.w <= 0 || r.h <= 0 || (c.a <= 0 && mode != kHole)) return;
  id<MTLTexture> t = s.blank;
  if (mode == 1 || mode == 2) {
    std::lock_guard<std::mutex> lock(s.texturesMu);
    auto it = s.textures.find(texture);
    if (it == s.textures.end()) return;
    t = it->second;
  }
  VUniforms vu{{r.x, r.y, r.w, r.h}, {kLayoutWidth, kLayoutHeight}, {0, 0}};
  FUniforms fu{{r.x, r.y, r.w, r.h}, {c.r, c.g, c.b, c.a}, radius, thickness,
               kLayoutWidth / static_cast<float>(drawableW_), mode};
  [s.encoder setVertexBytes:&vu length:sizeof vu atIndex:0];
  [s.encoder setFragmentBytes:&fu length:sizeof fu atIndex:0];
  [s.encoder setFragmentTexture:t atIndex:0];
  [s.encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
}

void Renderer::fill(Rect r, Color c, float radius) { draw(r, c, radius, 0.0f, 0, 0); }

void Renderer::stroke(Rect r, Color c, float radius, float thickness) {
  draw(r, c, radius, std::max(thickness, 0.5f), 0, 0);
}

void Renderer::textureAlpha(unsigned texture, Rect r, Color c) {
  if (texture) draw(r, c, 0.0f, 0.0f, 1, texture);
}

void Renderer::image(unsigned texture, Rect r, float opacity, float radius) {
  if (texture) draw(r, Color{1, 1, 1, opacity}, radius, 0.0f, 2, texture);
}

void Renderer::hole(Rect r) {
  State& s = state();
  if (!s.encoder) return;
  float sx = static_cast<float>(drawableW_) / kLayoutWidth;
  float sy = static_cast<float>(drawableH_) / kLayoutHeight;
  // The same pixels gfx.cpp clears (Metal counts rows from the top).
  int x0 = static_cast<int>(std::floor(r.x * sx + 0.5f));
  int x1 = static_cast<int>(std::floor((r.x + r.w) * sx + 0.5f));
  int y0 = static_cast<int>(std::floor(r.y * sy + 0.5f));
  int y1 = static_cast<int>(std::floor((r.y + r.h) * sy + 0.5f));
  MTLScissorRect sr = scissor(x0, y0, x1, y1, drawableW_, drawableH_);
  if (sr.width == 0 || sr.height == 0) return;
  // A hole ignores the clip, as a scissored glClear does.
  [s.encoder setScissorRect:sr];
  [s.encoder setRenderPipelineState:s.replace];
  bool wasEmpty = s.clipEmpty;
  s.clipEmpty = false;
  draw({0, 0, kLayoutWidth, kLayoutHeight}, Color{0, 0, 0, 0}, 0.0f, 0.0f, kHole, 0);
  s.clipEmpty = wasEmpty;
  [s.encoder setRenderPipelineState:s.blended];
  applyClip();
}

void Renderer::pushClip(Rect r) {
  if (!clips_.empty()) {
    const Rect& p = clips_.back();
    float x0 = std::max(r.x, p.x), y0 = std::max(r.y, p.y);
    float x1 = std::min(r.x + r.w, p.x + p.w), y1 = std::min(r.y + r.h, p.y + p.h);
    r = {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
  }
  clips_.push_back(r);
  applyClip();
}

void Renderer::popClip() {
  if (!clips_.empty()) clips_.pop_back();
  applyClip();
}

void Renderer::clearClips() {
  clips_.clear();
  applyClip();
}

void Renderer::applyClip() {
  State& s = state();
  s.clipEmpty = false;
  if (!s.encoder) return;
  if (clips_.empty()) {
    [s.encoder setScissorRect:scissor(0, 0, drawableW_, drawableH_, drawableW_, drawableH_)];
    return;
  }
  const Rect& r = clips_.back();
  float sx = static_cast<float>(drawableW_) / kLayoutWidth;
  float sy = static_cast<float>(drawableH_) / kLayoutHeight;
  int x0 = static_cast<int>(r.x * sx), y0 = static_cast<int>(r.y * sy);
  int x1 = x0 + static_cast<int>(std::ceil(r.w * sx)), y1 = y0 + static_cast<int>(std::ceil(r.h * sy));
  MTLScissorRect sr = scissor(x0, y0, x1, y1, drawableW_, drawableH_);
  // Metal refuses an empty scissor: nothing is drawn until the clip changes.
  if (sr.width == 0 || sr.height == 0) {
    s.clipEmpty = true;
    return;
  }
  [s.encoder setScissorRect:sr];
}

unsigned uploadTexture(const px::Image& im, unsigned replace) {
  State& s = state();
  if (!s.device || im.w <= 0 || im.h <= 0) return replace;
  id<MTLTexture> t = nil;
  {
    std::lock_guard<std::mutex> lock(s.texturesMu);
    auto it = s.textures.find(replace);
    if (it != s.textures.end() && it->second.width == static_cast<NSUInteger>(im.w) &&
        it->second.height == static_cast<NSUInteger>(im.h))
      t = it->second;
  }
  if (!t) {
    MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                 width:static_cast<NSUInteger>(im.w)
                                                                                height:static_cast<NSUInteger>(im.h)
                                                                             mipmapped:NO];
    d.usage = MTLTextureUsageShaderRead;
    t = [s.device newTextureWithDescriptor:d];
    if (!t) return replace;
  }
  [t replaceRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(im.w), static_cast<NSUInteger>(im.h))
       mipmapLevel:0
         withBytes:im.rgba.data()
       bytesPerRow:static_cast<NSUInteger>(im.w) * 4u];
  std::lock_guard<std::mutex> lock(s.texturesMu);
  unsigned handle = replace ? replace : s.nextTexture++;
  s.textures[handle] = t;
  return handle;
}

void releaseTexture(unsigned& handle) {
  if (handle) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.texturesMu);
    s.textures.erase(handle);
  }
  handle = 0;
}

}  // namespace marigold::gfx

namespace marigold::tvos::metal {

bool open(CAMetalLayer* layer) {
  gfx::State& s = gfx::state();
  s.layer = layer;
  s.device = layer.device ?: MTLCreateSystemDefaultDevice();
  if (!s.device) return false;
  s.queue = [s.device newCommandQueue];
  return s.queue != nil;
}

void close() {
  gfx::State& s = gfx::state();
  s.queue = nil;
  s.layer = nil;
  s.device = nil;
}

std::string deviceName() {
  gfx::State& s = gfx::state();
  return s.device ? std::string(s.device.name.UTF8String ?: "?") : std::string("?");
}

void present() {
  gfx::State& s = gfx::state();
  if (!s.encoder) return;
  [s.encoder endEncoding];
  [s.commands presentDrawable:s.drawable];
  [s.commands commit];
  static bool first = true;
  if (first) MLOG_I("gfx", "first frame shown");
  first = false;
  s.encoder = nil;
  s.commands = nil;
  s.drawable = nil;
}

}  // namespace marigold::tvos::metal

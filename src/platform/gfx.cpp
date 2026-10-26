#include "platform/gfx.hpp"

#include <GLES2/gl2.h>

#include <algorithm>
#include <cmath>

#include "core/util/log.hpp"
#include "gfx.h"  // vendor/legacy/shim: the C API ponteiro.c calls

namespace marigold::gfx {
namespace {

const char* kVertexShader = R"(
attribute vec2 aPos;
uniform vec4 uRect;
uniform vec2 uLayout;
varying vec2 vLocal;
varying vec2 vUv;
void main() {
  vec2 p = uRect.xy + aPos * uRect.zw;
  vLocal = aPos * uRect.zw;
  vUv = aPos;
  gl_Position = vec4(p.x / uLayout.x * 2.0 - 1.0, 1.0 - p.y / uLayout.y * 2.0, 0.0, 1.0);
}
)";

// Pixel coordinates up to 1920 need highp: mediump has ~10 bits of mantissa.
const char* kFragmentShader = R"(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform vec4 uRect;
uniform vec4 uColor;
uniform float uRadius;
uniform float uThickness;
uniform float uPixel;
uniform int uMode;
uniform sampler2D uTexture;
varying vec2 vLocal;
varying vec2 vUv;
float roundedBox(vec2 p, vec2 hs, float r) {
  vec2 q = abs(p) - hs + vec2(r);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
  if (uMode == 1) {
    gl_FragColor = vec4(uColor.rgb, uColor.a * texture2D(uTexture, vUv).a);
    return;
  }
  vec2 hs = uRect.zw * 0.5;
  vec2 p = vLocal - hs;
  float r = min(uRadius, min(hs.x, hs.y));
  float cover = clamp(0.5 - roundedBox(p, hs, r) / uPixel, 0.0, 1.0);
  if (uThickness > 0.0) {
    float inner = roundedBox(p, hs - vec2(uThickness), max(r - uThickness, 0.0));
    cover *= clamp(0.5 + inner / uPixel, 0.0, 1.0);
  }
  if (uMode == 2) {
    vec4 t = texture2D(uTexture, vUv);
    gl_FragColor = vec4(t.rgb, t.a * uColor.a * cover);
    return;
  }
  gl_FragColor = vec4(uColor.rgb, uColor.a * cover);
}
)";

GLuint compile(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char info[512] = "";
    glGetShaderInfoLog(shader, sizeof info, nullptr, info);
    MLOG_E("gfx", "shader compile failed: %s", info);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

Renderer gRenderer;

}  // namespace

Renderer& renderer() { return gRenderer; }

bool Renderer::init(int drawableWidth, int drawableHeight) {
  drawableW_ = drawableWidth > 0 ? drawableWidth : 1920;
  drawableH_ = drawableHeight > 0 ? drawableHeight : 1080;
  GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader);
  GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentShader);
  if (!vs || !fs) return false;
  program_ = glCreateProgram();
  glAttachShader(program_, vs);
  glAttachShader(program_, fs);
  glBindAttribLocation(program_, 0, "aPos");
  glLinkProgram(program_);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (!ok) {
    char info[512] = "";
    glGetProgramInfoLog(program_, sizeof info, nullptr, info);
    MLOG_E("gfx", "program link failed: %s", info);
    return false;
  }
  glUseProgram(program_);
  uRect_ = glGetUniformLocation(program_, "uRect");
  uLayout_ = glGetUniformLocation(program_, "uLayout");
  uColor_ = glGetUniformLocation(program_, "uColor");
  uRadius_ = glGetUniformLocation(program_, "uRadius");
  uThickness_ = glGetUniformLocation(program_, "uThickness");
  uMode_ = glGetUniformLocation(program_, "uMode");
  uTexture_ = glGetUniformLocation(program_, "uTexture");
  uPixel_ = glGetUniformLocation(program_, "uPixel");
  glUniform2f(uLayout_, kLayoutWidth, kLayoutHeight);
  glUniform1i(uTexture_, 0);
  glUniform1f(uPixel_, kLayoutWidth / static_cast<float>(drawableW_));

  // Every draw is this one quad, deformed by uRect in the vertex shader. It
  // lives in a buffer, not client memory (Legacy found WebGL rejects the
  // latter; GLES2 accepts both, the buffer is the portable choice).
  static const GLfloat quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
  glGenBuffers(1, &vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

  glEnable(GL_BLEND);
  // Separate alpha blend, and the GL_ONE matters (Legacy): with GL_SRC_ALPHA
  // on both channels every translucent draw would lower the surface alpha
  // and punch a dark patch through to whatever is behind the window.
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glViewport(0, 0, drawableW_, drawableH_);
  return true;
}

void Renderer::shutdown() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (program_) glDeleteProgram(program_);
  vbo_ = 0;
  program_ = 0;
}

void Renderer::beginFrame(Color clear) {
  clips_.clear();
  glDisable(GL_SCISSOR_TEST);
  glViewport(0, 0, drawableW_, drawableH_);
  glClearColor(clear.r, clear.g, clear.b, clear.a);
  glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::draw(Rect r, Color c, float radius, float thickness, int mode, unsigned texture) {
  if (r.w <= 0 || r.h <= 0 || c.a <= 0) return;
  glUniform4f(uRect_, r.x, r.y, r.w, r.h);
  glUniform4f(uColor_, c.r, c.g, c.b, c.a);
  glUniform1f(uRadius_, radius);
  glUniform1f(uThickness_, thickness);
  glUniform1i(uMode_, mode);
  if (mode != 0) glBindTexture(GL_TEXTURE_2D, texture);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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
  float sx = static_cast<float>(drawableW_) / kLayoutWidth;
  float sy = static_cast<float>(drawableH_) / kLayoutHeight;
  int x0 = static_cast<int>(std::floor(r.x * sx + 0.5f));
  int x1 = static_cast<int>(std::floor((r.x + r.w) * sx + 0.5f));
  int y0 = static_cast<int>(std::floor((kLayoutHeight - (r.y + r.h)) * sy + 0.5f));
  int y1 = static_cast<int>(std::floor((kLayoutHeight - r.y) * sy + 0.5f));
  if (x1 <= x0 || y1 <= y0) return;
  glEnable(GL_SCISSOR_TEST);
  glScissor(x0, y0, x1 - x0, y1 - y0);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);
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
  if (clips_.empty()) {
    glDisable(GL_SCISSOR_TEST);
    return;
  }
  const Rect& r = clips_.back();
  float sx = static_cast<float>(drawableW_) / kLayoutWidth;
  float sy = static_cast<float>(drawableH_) / kLayoutHeight;
  glEnable(GL_SCISSOR_TEST);
  glScissor(static_cast<int>(r.x * sx), static_cast<int>((kLayoutHeight - r.y - r.h) * sy),
            static_cast<int>(std::ceil(r.w * sx)), static_cast<int>(std::ceil(r.h * sy)));
}

}  // namespace marigold::gfx

// C entry points for the vendored ponteiro.c (vendor/legacy/shim/gfx.h).
extern "C" {

float gfx_opacidade_grupo = 1.0f;

float gfx_escala(void) { return 1.0f; }

void gfx_sem_recorte(void) { marigold::gfx::renderer().clearClips(); }

void gfx_cor(GfxRect r, float raio, float cr, float cg, float cb, float ca) {
  float radius = raio <= 0.5f ? raio * std::min(r.w, r.h) : raio;
  marigold::gfx::renderer().fill({r.x, r.y, r.w, r.h}, {cr, cg, cb, ca * gfx_opacidade_grupo},
                                 radius);
}

}  // extern "C"

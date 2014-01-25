// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_CLIENT_GL_HELPER_H_
#define CONTENT_COMMON_GPU_CLIENT_GL_HELPER_H_

#include "base/atomicops.h"
#include "base/basictypes.h"
#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "content/common/content_export.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace gfx {
class Rect;
class Size;
}

namespace gpu {
class ContextSupport;
struct Mailbox;
}

namespace media {
class VideoFrame;
};

class SkRegion;

namespace content {

class GLHelperScaling;

class ScopedGLuint {
 public:
  typedef void (gpu::gles2::GLES2Interface::*GenFunc)(GLsizei n, GLuint* ids);
  typedef void (gpu::gles2::GLES2Interface::*DeleteFunc)(GLsizei n,
                                                         const GLuint* ids);
  ScopedGLuint(gpu::gles2::GLES2Interface* gl,
               GenFunc gen_func,
               DeleteFunc delete_func)
      : gl_(gl), id_(0u), delete_func_(delete_func) {
    (gl_->*gen_func)(1, &id_);
  }

  operator GLuint() const { return id_; }

  GLuint id() const { return id_; }

  ~ScopedGLuint() {
    if (id_ != 0) {
      (gl_->*delete_func_)(1, &id_);
    }
  }

 private:
  gpu::gles2::GLES2Interface* gl_;
  GLuint id_;
  DeleteFunc delete_func_;

  DISALLOW_COPY_AND_ASSIGN(ScopedGLuint);
};

class ScopedBuffer : public ScopedGLuint {
 public:
  explicit ScopedBuffer(gpu::gles2::GLES2Interface* gl)
      : ScopedGLuint(gl,
                     &gpu::gles2::GLES2Interface::GenBuffers,
                     &gpu::gles2::GLES2Interface::DeleteBuffers) {}
};

class ScopedFramebuffer : public ScopedGLuint {
 public:
  explicit ScopedFramebuffer(gpu::gles2::GLES2Interface* gl)
      : ScopedGLuint(gl,
                     &gpu::gles2::GLES2Interface::GenFramebuffers,
                     &gpu::gles2::GLES2Interface::DeleteFramebuffers) {}
};

class ScopedTexture : public ScopedGLuint {
 public:
  explicit ScopedTexture(gpu::gles2::GLES2Interface* gl)
      : ScopedGLuint(gl,
                     &gpu::gles2::GLES2Interface::GenTextures,
                     &gpu::gles2::GLES2Interface::DeleteTextures) {}
};

template <GLenum Target>
class ScopedBinder {
 public:
  typedef void (gpu::gles2::GLES2Interface::*BindFunc)(GLenum target,
                                                       GLuint id);
  ScopedBinder(gpu::gles2::GLES2Interface* gl, GLuint id, BindFunc bind_func)
      : gl_(gl), bind_func_(bind_func) {
    (gl_->*bind_func_)(Target, id);
  }

  virtual ~ScopedBinder() { (gl_->*bind_func_)(Target, 0); }

 private:
  gpu::gles2::GLES2Interface* gl_;
  BindFunc bind_func_;

  DISALLOW_COPY_AND_ASSIGN(ScopedBinder);
};

template <GLenum Target>
class ScopedBufferBinder : ScopedBinder<Target> {
 public:
  ScopedBufferBinder(gpu::gles2::GLES2Interface* gl, GLuint id)
      : ScopedBinder<Target>(gl, id, &gpu::gles2::GLES2Interface::BindBuffer) {}
};

template <GLenum Target>
class ScopedFramebufferBinder : ScopedBinder<Target> {
 public:
  ScopedFramebufferBinder(gpu::gles2::GLES2Interface* gl, GLuint id)
      : ScopedBinder<Target>(gl,
                             id,
                             &gpu::gles2::GLES2Interface::BindFramebuffer) {}
};

template <GLenum Target>
class ScopedTextureBinder : ScopedBinder<Target> {
 public:
  ScopedTextureBinder(gpu::gles2::GLES2Interface* gl, GLuint id)
      : ScopedBinder<Target>(gl, id, &gpu::gles2::GLES2Interface::BindTexture) {
  }
};

class ScopedFlush {
 public:
  explicit ScopedFlush(gpu::gles2::GLES2Interface* gl) : gl_(gl) {}

  ~ScopedFlush() { gl_->Flush(); }

 private:
  gpu::gles2::GLES2Interface* gl_;

  DISALLOW_COPY_AND_ASSIGN(ScopedFlush);
};

class ReadbackYUVInterface;

// Provides higher level operations on top of the gpu::gles2::GLES2Interface
// interfaces.
class CONTENT_EXPORT GLHelper {
 public:
  GLHelper(gpu::gles2::GLES2Interface* gl,
           gpu::ContextSupport* context_support);
  ~GLHelper();

  enum ScalerQuality {
    // Bilinear single pass, fastest possible.
    SCALER_QUALITY_FAST = 1,

    // Bilinear upscale + N * 50% bilinear downscales.
    // This is still fast enough for most purposes and
    // Image quality is nearly as good as the BEST option.
    SCALER_QUALITY_GOOD = 2,

    // Bicubic upscale + N * 50% bicubic downscales.
    // Produces very good quality scaled images, but it's
    // 2-8x slower than the "GOOD" quality, so it's not always
    // worth it.
    SCALER_QUALITY_BEST = 3,
  };

  // Copies the block of pixels specified with |src_subrect| from |src_texture|,
  // scales it to |dst_size|, and writes it into |out|.
  // |src_size| is the size of |src_texture|. The result is of format GL_BGRA
  // and is potentially flipped vertically to make it a correct image
  // representation.  |callback| is invoked with the copy result when the copy
  // operation has completed.
  // Note that the src_texture will have the min/mag filter set to GL_LINEAR
  // and wrap_s/t set to CLAMP_TO_EDGE in this call.
  void CropScaleReadbackAndCleanTexture(
      GLuint src_texture,
      const gfx::Size& src_size,
      const gfx::Rect& src_subrect,
      const gfx::Size& dst_size,
      unsigned char* out,
      const SkBitmap::Config config,
      const base::Callback<void(bool)>& callback);

  // Copies the block of pixels specified with |src_subrect| from |src_mailbox|,
  // scales it to |dst_size|, and writes it into |out|.
  // |src_size| is the size of |src_mailbox|. The result is of format GL_BGRA
  // and is potentially flipped vertically to make it a correct image
  // representation.  |callback| is invoked with the copy result when the copy
  // operation has completed.
  // Note that the texture bound to src_mailbox will have the min/mag filter set
  // to GL_LINEAR and wrap_s/t set to CLAMP_TO_EDGE in this call. src_mailbox is
  // assumed to be GL_TEXTURE_2D.
  void CropScaleReadbackAndCleanMailbox(
      const gpu::Mailbox& src_mailbox,
      uint32 sync_point,
      const gfx::Size& src_size,
      const gfx::Rect& src_subrect,
      const gfx::Size& dst_size,
      unsigned char* out,
      const SkBitmap::Config config,
      const base::Callback<void(bool)>& callback);

  // Copies the texture data out of |texture| into |out|.  |size| is the
  // size of the texture.  No post processing is applied to the pixels.  The
  // texture is assumed to have a format of GL_RGBA with a pixel type of
  // GL_UNSIGNED_BYTE.  This is a blocking call that calls glReadPixels on the
  // current OpenGL context.
  void ReadbackTextureSync(GLuint texture,
                           const gfx::Rect& src_rect,
                           unsigned char* out,
                           SkBitmap::Config format);

  void ReadbackTextureAsync(GLuint texture,
                            const gfx::Size& dst_size,
                            unsigned char* out,
                            SkBitmap::Config config,
                            const base::Callback<void(bool)>& callback);

  // Creates a copy of the specified texture. |size| is the size of the texture.
  // Note that the src_texture will have the min/mag filter set to GL_LINEAR
  // and wrap_s/t set to CLAMP_TO_EDGE in this call.
  GLuint CopyTexture(GLuint texture, const gfx::Size& size);

  // Creates a scaled copy of the specified texture. |src_size| is the size of
  // the texture and |dst_size| is the size of the resulting copy.
  // Note that the src_texture will have the min/mag filter set to GL_LINEAR
  // and wrap_s/t set to CLAMP_TO_EDGE in this call.
  GLuint CopyAndScaleTexture(GLuint texture,
                             const gfx::Size& src_size,
                             const gfx::Size& dst_size,
                             bool vertically_flip_texture,
                             ScalerQuality quality);

  // Returns the shader compiled from the source.
  GLuint CompileShaderFromSource(const GLchar* source, GLenum type);

  // Copies all pixels from |previous_texture| into |texture| that are
  // inside the region covered by |old_damage| but not part of |new_damage|.
  void CopySubBufferDamage(GLuint texture,
                           GLuint previous_texture,
                           const SkRegion& new_damage,
                           const SkRegion& old_damage);

  // Simply creates a texture.
  GLuint CreateTexture();
  // Deletes a texture.
  void DeleteTexture(GLuint texture_id);

  // Insert a sync point into the GL command buffer.
  uint32 InsertSyncPoint();
  // Wait for the sync point before executing further GL commands.
  void WaitSyncPoint(uint32 sync_point);

  // Creates a mailbox that is attached to the given texture id, and a sync
  // point to wait on before using the mailbox. Returns an empty mailbox on
  // failure.
  // Note the texture is assumed to be GL_TEXTURE_2D.
  gpu::Mailbox ProduceMailboxFromTexture(GLuint texture_id, uint32* sync_point);

  // Creates a texture and consumes a mailbox into it. Returns 0 on failure.
  // Note the mailbox is assumed to be GL_TEXTURE_2D.
  GLuint ConsumeMailboxToTexture(const gpu::Mailbox& mailbox,
                                 uint32 sync_point);

  // Resizes the texture's size to |size|.
  void ResizeTexture(GLuint texture, const gfx::Size& size);

  // Copies the framebuffer data given in |rect| to |texture|.
  void CopyTextureSubImage(GLuint texture, const gfx::Rect& rect);

  // Copies the all framebuffer data to |texture|. |size| specifies the
  // size of the framebuffer.
  void CopyTextureFullImage(GLuint texture, const gfx::Size& size);

  // Check whether rgb565 readback is supported or not.
  bool CanUseRgb565Readback();

  // A scaler will cache all intermediate textures and programs
  // needed to scale from a specified size to a destination size.
  // If the source or destination sizes changes, you must create
  // a new scaler.
  class CONTENT_EXPORT ScalerInterface {
   public:
    ScalerInterface() {}
    virtual ~ScalerInterface() {}

    // Note that the src_texture will have the min/mag filter set to GL_LINEAR
    // and wrap_s/t set to CLAMP_TO_EDGE in this call.
    virtual void Scale(GLuint source_texture, GLuint dest_texture) = 0;
    virtual const gfx::Size& SrcSize() = 0;
    virtual const gfx::Rect& SrcSubrect() = 0;
    virtual const gfx::Size& DstSize() = 0;
  };

  // Note that the quality may be adjusted down if texture
  // allocations fail or hardware doesn't support the requtested
  // quality. Note that ScalerQuality enum is arranged in
  // numerical order for simplicity.
  ScalerInterface* CreateScaler(ScalerQuality quality,
                                const gfx::Size& src_size,
                                const gfx::Rect& src_subrect,
                                const gfx::Size& dst_size,
                                bool vertically_flip_texture,
                                bool swizzle);

  // Create a readback pipeline that will scale a subsection of the source
  // texture, then convert it to YUV422 planar form and then read back that.
  // This reduces the amount of memory read from GPU to CPU memory by a factor
  // 2.6, which can be quite handy since readbacks have very limited speed
  // on some platforms. All values in |dst_size| and |dst_subrect| must be
  // a multiple of two. If |use_mrt| is true, the pipeline will try to optimize
  // the YUV conversion using the multi-render-target extension. |use_mrt|
  // should only be set to false for testing.
  ReadbackYUVInterface* CreateReadbackPipelineYUV(ScalerQuality quality,
                                                  const gfx::Size& src_size,
                                                  const gfx::Rect& src_subrect,
                                                  const gfx::Size& dst_size,
                                                  const gfx::Rect& dst_subrect,
                                                  bool flip_vertically,
                                                  bool use_mrt);

  // Returns the maximum number of draw buffers available,
  // 0 if GL_EXT_draw_buffers is not available.
  GLint MaxDrawBuffers();

 protected:
  class CopyTextureToImpl;

  // Creates |copy_texture_to_impl_| if NULL.
  void InitCopyTextToImpl();
  // Creates |scaler_impl_| if NULL.
  void InitScalerImpl();

  gpu::gles2::GLES2Interface* gl_;
  gpu::ContextSupport* context_support_;
  scoped_ptr<CopyTextureToImpl> copy_texture_to_impl_;
  scoped_ptr<GLHelperScaling> scaler_impl_;
  bool initialized_565_format_check_;
  bool support_565_format_;

  DISALLOW_COPY_AND_ASSIGN(GLHelper);
};

// Similar to a ScalerInterface, a yuv readback pipeline will
// cache a scaler and all intermediate textures and frame buffers
// needed to scale, crop, letterbox and read back a texture from
// the GPU into CPU-accessible RAM. A single readback pipeline
// can handle multiple outstanding readbacks at the same time, but
// if the source or destination sizes change, you'll need to create
// a new readback pipeline.
class CONTENT_EXPORT ReadbackYUVInterface {
 public:
  ReadbackYUVInterface() {}
  virtual ~ReadbackYUVInterface() {}

  // Note that |target| must use YV12 format.
  virtual void ReadbackYUV(const gpu::Mailbox& mailbox,
                           uint32 sync_point,
                           const scoped_refptr<media::VideoFrame>& target,
                           const base::Callback<void(bool)>& callback) = 0;
  virtual GLHelper::ScalerInterface* scaler() = 0;
};

}  // namespace content

#endif  // CONTENT_COMMON_GPU_CLIENT_GL_HELPER_H_

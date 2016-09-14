//
//  Created by Bradley Austin Davis on 2016/05/15
//  Copyright 2013-2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "GLTexture.h"

#include <NumericalConstants.h>

#include "GLTextureTransfer.h"
#include "GLBackend.h"

using namespace gpu;
using namespace gpu::gl;

std::shared_ptr<GLTextureTransferHelper> GLTexture::_textureTransferHelper;

// FIXME placeholder for texture memory over-use
#define DEFAULT_MAX_MEMORY_MB 256

const GLenum GLTexture::CUBE_FACE_LAYOUT[6] = {
    GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
};

const GLenum GLTexture::WRAP_MODES[Sampler::NUM_WRAP_MODES] = {
    GL_REPEAT,                         // WRAP_REPEAT,
    GL_MIRRORED_REPEAT,                // WRAP_MIRROR,
    GL_CLAMP_TO_EDGE,                  // WRAP_CLAMP,
    GL_CLAMP_TO_BORDER,                // WRAP_BORDER,
    GL_MIRROR_CLAMP_TO_EDGE_EXT        // WRAP_MIRROR_ONCE,
};     

const GLFilterMode GLTexture::FILTER_MODES[Sampler::NUM_FILTERS] = {
    { GL_NEAREST, GL_NEAREST },  //FILTER_MIN_MAG_POINT,
    { GL_NEAREST, GL_LINEAR },  //FILTER_MIN_POINT_MAG_LINEAR,
    { GL_LINEAR, GL_NEAREST },  //FILTER_MIN_LINEAR_MAG_POINT,
    { GL_LINEAR, GL_LINEAR },  //FILTER_MIN_MAG_LINEAR,

    { GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },  //FILTER_MIN_MAG_MIP_POINT,
    { GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST },  //FILTER_MIN_MAG_POINT_MIP_LINEAR,
    { GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR },  //FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
    { GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR },  //FILTER_MIN_POINT_MAG_MIP_LINEAR,
    { GL_LINEAR_MIPMAP_NEAREST, GL_NEAREST },  //FILTER_MIN_LINEAR_MAG_MIP_POINT,
    { GL_LINEAR_MIPMAP_LINEAR, GL_NEAREST },  //FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
    { GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR },  //FILTER_MIN_MAG_LINEAR_MIP_POINT,
    { GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR },  //FILTER_MIN_MAG_MIP_LINEAR,
    { GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR }  //FILTER_ANISOTROPIC,
};

GLenum GLTexture::getGLTextureType(const Texture& texture) {
    switch (texture.getType()) {
    case Texture::TEX_2D:
        return GL_TEXTURE_2D;
        break;

    case Texture::TEX_CUBE:
        return GL_TEXTURE_CUBE_MAP;
        break;

    default:
        qFatal("Unsupported texture type");
    }
    Q_UNREACHABLE();
    return GL_TEXTURE_2D;
}


const std::vector<GLenum>& GLTexture::getFaceTargets(GLenum target) {
    static std::vector<GLenum> cubeFaceTargets {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
    };
    static std::vector<GLenum> faceTargets {
        GL_TEXTURE_2D
    };
    switch (target) {
    case GL_TEXTURE_2D:
        return faceTargets;
    case GL_TEXTURE_CUBE_MAP:
        return cubeFaceTargets;
    default:
        Q_UNREACHABLE();
        break;
    }
    Q_UNREACHABLE();
    return faceTargets;
}

float GLTexture::getMemoryPressure() {
    // Check for an explicit memory limit
    auto availableTextureMemory = Texture::getAllowedGPUMemoryUsage();

    // If no memory limit has been set, use a percentage of the total dedicated memory
    if (!availableTextureMemory) {
        auto totalGpuMemory = getDedicatedMemory();

        // If no limit has been explicitly set, and the dedicated memory can't be determined, 
        // just use a fallback fixed value of 256 MB
        if (!totalGpuMemory) {
            totalGpuMemory = MB_TO_BYTES(DEFAULT_MAX_MEMORY_MB);
        }

        // Allow 75% of all available GPU memory to be consumed by textures
        // FIXME overly conservative?
        availableTextureMemory = (totalGpuMemory >> 2) * 3;
    }

    // Return the consumed texture memory divided by the available texture memory.
    auto consumedGpuMemory = Context::getTextureGPUMemoryUsage();
    return (float)consumedGpuMemory / (float)availableTextureMemory;
}


// Create the texture and allocate storage
GLTexture::GLTexture(const std::weak_ptr<GLBackend>& backend, const Texture& texture, GLuint id, bool transferrable) :
    GLObject(backend, texture, id),
    _storageStamp(texture.getStamp()),
    _target(getGLTextureType(texture)),
    _maxMip(texture.maxMip()),
    _minMip(texture.minMip()),
    _virtualSize(texture.evalTotalSize()),
    _transferrable(transferrable)
{
    auto strongBackend = _backend.lock();
    strongBackend->recycle();
    Backend::incrementTextureGPUCount();
    Backend::updateTextureGPUVirtualMemoryUsage(0, _virtualSize);
    Backend::setGPUObject(texture, this);
}

GLTexture::~GLTexture() {
    if (_id) {
        auto backend = _backend.lock();
        if (backend) {
            backend->releaseTexture(_id, _size);
            backend->recycle();
        }
    }
    Backend::updateTextureGPUVirtualMemoryUsage(_virtualSize, 0);
}

void GLTexture::createTexture() {
    withPreservedTexture([&] {
        allocateStorage();
        (void)CHECK_GL_ERROR();
        syncSampler();
        (void)CHECK_GL_ERROR();
    });
}

void GLTexture::withPreservedTexture(std::function<void()> f) const {
    GLint boundTex = -1;
    switch (_target) {
        case GL_TEXTURE_2D:
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTex);
            break;

        case GL_TEXTURE_CUBE_MAP:
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &boundTex);
            break;

        default:
            qFatal("Unsupported texture type");
    }
    (void)CHECK_GL_ERROR();

    glBindTexture(_target, _texture);
    f();
    glBindTexture(_target, boundTex);
    (void)CHECK_GL_ERROR();
}

void GLTexture::setSize(GLuint size) const {
    Backend::updateTextureGPUMemoryUsage(_size, size);
    const_cast<GLuint&>(_size) = size;
}

bool GLTexture::isInvalid() const {
    return _storageStamp < _gpuObject.getStamp();
}

bool GLTexture::isOutdated() const {
    return GLSyncState::Idle == _syncState && _contentStamp < _gpuObject.getDataStamp();
}

bool GLTexture::isReady() const {
    // If we have an invalid texture, we're never ready
    if (isInvalid()) {
        return false;
    }

    auto syncState = _syncState.load();
    if (isOutdated() || Idle != syncState) {
        return false;
    }

    return true;
}


// Do any post-transfer operations that might be required on the main context / rendering thread
void GLTexture::postTransfer() {
    setSyncState(GLSyncState::Idle);
    ++_transferCount;

    // At this point the mip pixels have been loaded, we can notify the gpu texture to abandon it's memory
    switch (_gpuObject.getType()) {
        case Texture::TEX_2D:
            for (uint16_t i = 0; i < Sampler::MAX_MIP_LEVEL; ++i) {
                if (_gpuObject.isStoredMipFaceAvailable(i)) {
                    _gpuObject.notifyMipFaceGPULoaded(i);
                }
            }
            break;

        case Texture::TEX_CUBE:
            // transfer pixels from each faces
            for (uint8_t f = 0; f < CUBE_NUM_FACES; f++) {
                for (uint16_t i = 0; i < Sampler::MAX_MIP_LEVEL; ++i) {
                    if (_gpuObject.isStoredMipFaceAvailable(i, f)) {
                        _gpuObject.notifyMipFaceGPULoaded(i, f);
                    }
                    }
                }
            break;

        default:
            qCWarning(gpugllogging) << __FUNCTION__ << " case for Texture Type " << _gpuObject.getType() << " not supported";
            break;
    }
}

void GLTexture::initTextureTransferHelper() {
    _textureTransferHelper = std::make_shared<GLTextureTransferHelper>();
}

void GLTexture::startTransfer() {
    createTexture();
}

void GLTexture::finishTransfer() {
    if (_gpuObject.isAutogenerateMips()) {
        generateMips();
    }
}


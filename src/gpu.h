#pragma once

#include <stdint.h>
#include <SDL3/SDL.h>
#include "defs.h"
#include "image.h"

// TODO pick the texture format correctly
static SDL_GPUTexture *create_texture_for_image(SDL_GPUDevice *gpu, ImageData img) {
    SDL_assert(img.channels == 4);
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(gpu, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = img.w,
        .height = img.h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    });
    if (!tex)
        SDL_Log("ERROR: SDL_CreateGPUTexture: %s", SDL_GetError());
    return tex;
}

typedef enum { GPU_UPLOAD_ENTRY_BUFFER, GPU_UPLOAD_ENTRY_TEXTURE } GPUUploadEntryType;

typedef struct {
    GPUUploadEntryType type;
    size_t size;
    union {
        struct { SDL_GPUBuffer *buf; size_t offset; } buffer;
        struct { SDL_GPUTexture *tex; uint32_t w, h; } texture;
    };
} GPUUploadEntry;

typedef struct {
    SDL_GPUDevice *gpu;
    size_t size, cursor;
    uint8_t *ptr;
    SDL_GPUTransferBuffer *buffer;
    int entries_len;
    GPUUploadEntry entries[64];
} GPUUpload;

// On error GPUUpload.size is 0
// TODO flush on buffer overflow. Use a default buffer size?
static GPUUpload begin_gpu_upload(SDL_GPUDevice *gpu, size_t buffer_size) {
    GPUUpload result = {};
    SDL_GPUTransferBufferCreateInfo ci = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = buffer_size};
    SDL_GPUTransferBuffer *buffer = SDL_CreateGPUTransferBuffer(gpu, &ci);
    if (!buffer) {
        SDL_Log("ERROR: SDL_CreateGPUTransferBuffer: %s", SDL_GetError());
        return result;
    }

    uint8_t *ptr = SDL_MapGPUTransferBuffer(gpu, buffer, false);
    if (!ptr) {
        SDL_Log("ERROR: SDL_MapGPUTransferBuffer: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(gpu, buffer);
        return result;
    }

    result.gpu = gpu;
    result.size = buffer_size;
    result.cursor = 0;
    result.buffer = buffer;
    result.ptr = ptr;
    return result;
}

static bool end_gpu_upload(GPUUpload *up) {
    SDL_UnmapGPUTransferBuffer(up->gpu, up->buffer);

    SDL_GPUCommandBuffer *cmdbuf = SDL_AcquireGPUCommandBuffer(up->gpu);
    if (!cmdbuf) {
        SDL_Log("ERROR: SDL_AcquireGPUCommandBuffer: %s", SDL_GetError());
        return false;
    }

    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmdbuf);

    size_t pos = 0;
    for (int i = 0; i < up->entries_len; i++) {
        GPUUploadEntry *entry = &up->entries[i];
        switch (entry->type) {
        case GPU_UPLOAD_ENTRY_BUFFER:
            SDL_UploadToGPUBuffer(cp, &(SDL_GPUTransferBufferLocation){up->buffer, pos},
                &(SDL_GPUBufferRegion){entry->buffer.buf, entry->buffer.offset, entry->size},
                false);
            break;
        case GPU_UPLOAD_ENTRY_TEXTURE:
            SDL_UploadToGPUTexture(cp, &(SDL_GPUTextureTransferInfo){up->buffer, pos, entry->texture.w, 0},
                &(SDL_GPUTextureRegion){
                    .texture = entry->texture.tex,
                    .mip_level = 0,
                    .layer  = 0,
                    .x = 0, .y = 0,
                    .w = entry->texture.w,
                    .h = entry->texture.h,
                    .d = 1,
                },
                false);
            break;
        }
        pos += entry->size;
    }
    SDL_EndGPUCopyPass(cp);

    if (!SDL_SubmitGPUCommandBuffer(cmdbuf)) {
        SDL_Log("ERROR: SDL_SubmitGPUCommandBuffer: %s", SDL_GetError());
        return false;
    }
    SDL_ReleaseGPUTransferBuffer(up->gpu, up->buffer);
    return true;
}

static void gpu_upload_buffer(GPUUpload *up, const void *data, size_t size, SDL_GPUBuffer *buffer, size_t offset) {
    SDL_assert(up->cursor + size < up->size);
    SDL_memcpy(up->ptr + up->cursor, data, size);
    up->cursor += size;
    SDL_assert(up->entries_len < ARRAY_LENGTH(up->entries));
    up->entries[up->entries_len++] = (GPUUploadEntry){
        .type = GPU_UPLOAD_ENTRY_BUFFER,
        .size = size,
        .buffer = {buffer, offset},
    };
}

static void gpu_upload_texture(GPUUpload *up, ImageData img, SDL_GPUTexture *tex) {
    size_t size = image_size(img);
    SDL_assert(up->cursor + size < up->size);
    SDL_memcpy(up->ptr + up->cursor, img.data, size);
    up->cursor += size;
    SDL_assert(up->entries_len < ARRAY_LENGTH(up->entries));
    up->entries[up->entries_len++] = (GPUUploadEntry){
        .type = GPU_UPLOAD_ENTRY_TEXTURE,
        .size = size,
        .texture = {tex, img.w, img.h},
    };
}

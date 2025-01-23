#include <stdint.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include "shaders/shaders_gen.h"

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    SDL_Window *window;
    SDL_GPUDevice *gpu;
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUTexture *texture;
    SDL_GPUSampler *sampler;
    SDL_AppResult quit;
    uint64_t ticks_freq;
    uint64_t last_frame_ticks;
} AppState;

AppState g_app = { .quit = SDL_APP_CONTINUE };

typedef struct {
    float pos[2];
    float tex[2];
    float color[4];
} Vertex;

const SDL_GPUVertexAttribute vertex_attrs[] = {
    {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(Vertex, pos)},
    {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(Vertex, tex)},
    {.location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = offsetof(Vertex, color)},
};

#define V_DIM 0.5
const Vertex vertices[] = {
    {.pos = {-V_DIM, -V_DIM}, .tex = {0, 1}, .color = {1, 0, 0, 1}},
    {.pos = {+V_DIM, -V_DIM}, .tex = {1, 1}, .color = {0, 0, 1, 1}},
    {.pos = {-V_DIM, +V_DIM}, .tex = {0, 0}, .color = {0, 1, 0, 1}},
    {.pos = {+V_DIM, -V_DIM}, .tex = {1, 1}, .color = {0, 0, 1, 1}},
    {.pos = {+V_DIM, +V_DIM}, .tex = {1, 0}, .color = {1, 0, 1, 1}},
    {.pos = {-V_DIM, +V_DIM}, .tex = {0, 0}, .color = {0, 1, 0, 1}},
};

const uint8_t icon_data[] = {
    #embed "icon.png"
};

typedef struct {
    uint8_t *data;
    int w, h, channels;
} ImageData;

static size_t image_size(ImageData id) { return id.w * id.h * id.channels; }

const SDL_PixelFormat CHANNELS_TO_SDL_PIXELFORMAT[] = {
    0, 0, 0, SDL_PIXELFORMAT_RGB24, SDL_PIXELFORMAT_RGBA32,
};

const SDL_GPUTextureFormat CHANNELS_TO_SDL_GPU_TEXTURE_FORMAT[] = {
    SDL_GPU_TEXTUREFORMAT_INVALID,
    SDL_GPU_TEXTUREFORMAT_R8_UNORM,
    SDL_GPU_TEXTUREFORMAT_R8G8_UNORM,
    SDL_GPU_TEXTUREFORMAT_INVALID, // SDL_GPU_TEXTUREFORMAT_R8G8B8_UNORM,
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
};

// set desired_channels to 0 to use the channels in the file
static ImageData load_image(const uint8_t *buf, int len, int desired_channels) {
    ImageData result = {};
    int channels_in_file;
    result.data = stbi_load_from_memory(buf, len, &result.w, &result.h, &channels_in_file, desired_channels);
    if (!result.data) {
        SDL_Log("ERROR: stbi_load_from_memory");
    } else {
        result.channels = desired_channels ? desired_channels : channels_in_file;
    }
    return result;
}

static void free_image(ImageData id) {
    stbi_image_free(id.data);
}

static bool set_icon(SDL_Window *window, ImageData img) {
    SDL_Surface *sfc = nullptr;
    bool result = false;

    sfc = SDL_CreateSurfaceFrom(img.w, img.h, CHANNELS_TO_SDL_PIXELFORMAT[img.channels], img.data, img.w * img.channels);
    if (!sfc) {
        SDL_Log("ERROR: SDL_CreateSurfaceFrom: %s", SDL_GetError());
        goto exit;
    }

    if (!SDL_SetWindowIcon(window, sfc)) {
        SDL_Log("ERROR: SDL_SetWindowIcon: %s", SDL_GetError());
        goto exit;
    }

    result = true;
exit:
    if (sfc)
        SDL_DestroySurface(sfc);
    return result;
}

static SDL_AppResult sdl_fail(const char *message) {
    SDL_Log("ERROR: %s: %s", message, SDL_GetError());
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)appstate;
    (void)argc;
    (void)argv;
    if (!SDL_Init(SDL_INIT_VIDEO))
        return sdl_fail("SDL_Init");

    g_app.gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
    if (!g_app.gpu)
        return sdl_fail("SDL_CreateGPUDevice");
    SDL_Log("SDL GPU driver: %s", SDL_GetGPUDeviceDriver(g_app.gpu));

    ImageData icon_img = load_image(icon_data, sizeof(icon_data), 4);
    if (!icon_img.data)
        return SDL_APP_FAILURE;

    g_app.window = SDL_CreateWindow("App", 1024, 1024, SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!g_app.window)
        return sdl_fail("SDL_CreateWindow");
    if (!set_icon(g_app.window, icon_img))
        return SDL_APP_FAILURE;

    if (!SDL_ClaimWindowForGPUDevice(g_app.gpu, g_app.window))
        return sdl_fail("SDL_ClaimWindowForGPUDevice");

    // Create vertex buffer
    g_app.vertex_buffer = SDL_CreateGPUBuffer(g_app.gpu, &(SDL_GPUBufferCreateInfo){SDL_GPU_BUFFERUSAGE_VERTEX, sizeof(vertices), 0});
    if (!g_app.vertex_buffer)
        return sdl_fail("SDL_CreateGPUBuffer vertex_buffer");

    // Create texture
    g_app.texture = SDL_CreateGPUTexture(g_app.gpu, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D,
        // .format = CHANNELS_TO_SDL_GPU_TEXTURE_FORMAT[icon_img.channels],
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = icon_img.w,
        .height = icon_img.h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    });

    g_app.sampler = SDL_CreateGPUSampler(g_app.gpu, &(SDL_GPUSamplerCreateInfo){
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    });

    // Upload vertex data and texture
    size_t transfer_size = sizeof(vertices) + image_size(icon_img);
    size_t vertices_offset, texture_offset;
    SDL_GPUTransferBuffer *transfer_buffer = SDL_CreateGPUTransferBuffer(g_app.gpu,
        &(SDL_GPUTransferBufferCreateInfo){.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = transfer_size});
    if (!transfer_buffer)
        return sdl_fail("SDL_CreateGPUTransferBuffer");
    {
        uint8_t *map = SDL_MapGPUTransferBuffer(g_app.gpu, transfer_buffer, false);

        vertices_offset = 0;
        SDL_memcpy(map + vertices_offset, vertices, sizeof(vertices));

        texture_offset = sizeof(vertices);
        SDL_memcpy(map + texture_offset, icon_img.data, image_size(icon_img));

        SDL_UnmapGPUTransferBuffer(g_app.gpu, transfer_buffer);
    }
    {
        SDL_GPUCommandBuffer *cmdbuf = SDL_AcquireGPUCommandBuffer(g_app.gpu);
        if (!cmdbuf)
            return sdl_fail("SDL_AcquireGPUCommandBuffer");
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmdbuf);
        SDL_UploadToGPUBuffer(
            cp,
            &(SDL_GPUTransferBufferLocation){.transfer_buffer = transfer_buffer, .offset = vertices_offset},
            &(SDL_GPUBufferRegion){g_app.vertex_buffer, 0, sizeof(vertices)},
            false);
        SDL_UploadToGPUTexture(cp,
            &(SDL_GPUTextureTransferInfo){.transfer_buffer = transfer_buffer, .offset = texture_offset, .pixels_per_row = icon_img.w},
            &(SDL_GPUTextureRegion){
                .texture = g_app.texture, .mip_level = 0, .layer  = 0,
                .x = 0, .y = 0, .w = icon_img.w, .h = icon_img.h, .d = 1,
            },
            false);
        SDL_EndGPUCopyPass(cp);
        if (!SDL_SubmitGPUCommandBuffer(cmdbuf))
            return sdl_fail("SDL_SubmitGPUCommandBuffer");
    }
    SDL_ReleaseGPUTransferBuffer(g_app.gpu, transfer_buffer);

    free_image(icon_img);

    // Create pipeline
    SDL_GPUShader *vs = SDL_CreateGPUShader(g_app.gpu, &(SDL_GPUShaderCreateInfo){
        .code_size = shader_code_quad_vert_len,
        .code = shader_code_quad_vert,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
    });
    if (!vs)
        return sdl_fail("SDL_CreateGPUShader shader_code_quad_vert");

    SDL_GPUShader *fs = SDL_CreateGPUShader(g_app.gpu, &(SDL_GPUShaderCreateInfo){
        .code_size = shader_code_quad_frag_len,
        .code = shader_code_quad_frag,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,
    });
    if (!fs)
        return sdl_fail("SDL_CreateGPUShader shader_code_quad_frag");

    auto color_target_desc = (SDL_GPUColorTargetDescription){
        .format = SDL_GetGPUSwapchainTextureFormat(g_app.gpu, g_app.window),
        .blend_state = (SDL_GPUColorTargetBlendState){
            .enable_blend = true,
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
        },
    };
    auto vertex_input_state = (SDL_GPUVertexInputState){
        .vertex_buffer_descriptions = &(SDL_GPUVertexBufferDescription){.slot = 0, .pitch = sizeof(Vertex), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX},
        .num_vertex_buffers = 1,
        .vertex_attributes = vertex_attrs,
        .num_vertex_attributes = ARRAY_LENGTH(vertex_attrs),
    };

    g_app.pipeline = SDL_CreateGPUGraphicsPipeline(g_app.gpu, &(SDL_GPUGraphicsPipelineCreateInfo){
        .vertex_shader = vs,
        .fragment_shader = fs,
        .vertex_input_state = vertex_input_state,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = (SDL_GPURasterizerState){.fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = SDL_GPU_CULLMODE_BACK, .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE},
        .multisample_state = (SDL_GPUMultisampleState){.sample_count = SDL_GPU_SAMPLECOUNT_1},
        .target_info = (SDL_GPUGraphicsPipelineTargetInfo){.color_target_descriptions = &color_target_desc, .num_color_targets = 1},
    });
    if (!g_app.pipeline)
        return sdl_fail("SDL_CreateGPUGraphicsPipeline");
    // shaders are reference counted, the pipeline holds a reference
    SDL_ReleaseGPUShader(g_app.gpu, vs);
    SDL_ReleaseGPUShader(g_app.gpu, fs);

    g_app.ticks_freq = SDL_GetPerformanceFrequency();
    g_app.last_frame_ticks = SDL_GetPerformanceCounter();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    (void)appstate;

    SDL_GPUCommandBuffer *cmdbuf = SDL_AcquireGPUCommandBuffer(g_app.gpu);
    if (!cmdbuf) {
        SDL_Log("ERROR: SDL_AcquireGPUCommandBuffer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_GPUTexture *swap_tex;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdbuf, g_app.window, &swap_tex, nullptr, nullptr)) {
        SDL_Log("ERROR: SDL_AcquireGPUSwapchainTexture: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (!swap_tex) {
        SDL_CancelGPUCommandBuffer(cmdbuf);
        return g_app.quit;
    }

    uint64_t current_ticks = SDL_GetPerformanceCounter();
    uint64_t elapsed_ticks = current_ticks - g_app.last_frame_ticks;
    g_app.last_frame_ticks = current_ticks;

    SDL_GPUColorTargetInfo color_target_info = {
        .texture = swap_tex,
        .clear_color = (SDL_FColor){0x26 / 255.0f, 0xA6 / 255.0f, 0x9A / 255.0f, 1.0f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };
    {
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmdbuf, &color_target_info, 1, nullptr);

        SDL_BindGPUGraphicsPipeline(pass, g_app.pipeline);
        SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){.buffer = g_app.vertex_buffer, .offset = 0}, 1);
        SDL_BindGPUFragmentSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){g_app.texture, g_app.sampler}, 1);
        SDL_DrawGPUPrimitives(pass, ARRAY_LENGTH(vertices), 1, 0, 0);

        SDL_EndGPURenderPass(pass);
    }

    SDL_SubmitGPUCommandBuffer(cmdbuf);
    return g_app.quit;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;

    if (event->type == SDL_EVENT_QUIT) {
        g_app.quit = SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate;
    SDL_Log("SDL_AppQuit %d", result);
    if (g_app.vertex_buffer)
        SDL_ReleaseGPUBuffer(g_app.gpu, g_app.vertex_buffer);
    if (g_app.pipeline)
        SDL_ReleaseGPUGraphicsPipeline(g_app.gpu, g_app.pipeline);
    if (g_app.gpu && g_app.window)
        SDL_ReleaseWindowFromGPUDevice(g_app.gpu, g_app.window);
    if (g_app.window)
        SDL_DestroyWindow(g_app.window);
    if (g_app.gpu)
        SDL_DestroyGPUDevice(g_app.gpu);
}

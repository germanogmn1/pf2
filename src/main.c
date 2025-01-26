#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include "defs.h"
#include "image.h"
#include "gpu.h"
#include "shaders/shaders_gen.h"

typedef struct {
    SDL_Window *window;
    SDL_GPUDevice *gpu;
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUSampler *sampler;
    SDL_GPUTexture *texture, *alien_tex;
    SDL_AppResult quit;
    uint64_t ticks_freq;
    uint64_t last_frame_ticks;
    const char *base_path;
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

#define V_DIM 1
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

static bool set_icon(SDL_Window *window, ImageData img) {
    SDL_Surface *sfc = nullptr;
    bool result = false;

    sfc = SDL_CreateSurfaceFrom(img.w, img.h, SDL_PIXELFORMAT_RGBA32, img.data, img.w * img.channels);
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

static uint8_t *read_asset(const char *filename, size_t *out_size) {
    char path[512];
    SDL_snprintf(path, sizeof(path), "%s/assets/%s", g_app.base_path, filename);
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) {
        SDL_Log("ERROR: Failed to open file '%s': %s", path, SDL_GetError());
        return nullptr;
    }

    uint8_t *data = nullptr;
    int64_t ssize = SDL_GetIOSize(io);
    if (ssize < 0) {
        SDL_Log("ERROR: SDL_GetIOSize '%s': %s", path, SDL_GetError());
        goto close;
    }
    size_t size = (size_t)ssize;
    data = SDL_malloc(size);
    if (!SDL_ReadIO(io, data, size)) {
        SDL_Log("ERROR: SDL_ReadIO '%s': %s", path, SDL_GetError());
        SDL_free(data);
        data = nullptr;
        goto close;
    }
    if (out_size)
        *out_size = size;
close:
    if (!SDL_CloseIO(io))
        SDL_Log("WARNING: SDL_CloseIO '%s': %s", path, SDL_GetError());
    return data;
}

static void free_asset(uint8_t *data) {
    if (data)
        SDL_free(data);
}

static ImageData load_asset_image(const char *filename, int channels) {
    size_t size;
    uint8_t *data = read_asset(filename, &size);
    if (!data)
        return (ImageData){};
    auto result = load_image(data, size, channels);
    free_asset(data);
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

    g_app.base_path = SDL_GetBasePath();
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

    SDL_GPUSwapchainComposition composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR;
    if (!SDL_WindowSupportsGPUSwapchainComposition(g_app.gpu, g_app.window, composition)) {
        SDL_Log("ERROR: SDR_LINEAR swapchain composition is not supported");
        return SDL_APP_FAILURE;
    }

    SDL_GPUPresentMode present_mode = SDL_GPU_PRESENTMODE_VSYNC; // SDL_GPU_PRESENTMODE_MAILBOX, SDL_GPU_PRESENTMODE_IMMEDIATE
    if (!SDL_SetGPUSwapchainParameters(g_app.gpu, g_app.window, composition, present_mode))
        return sdl_fail("SDL_SetGPUSwapchainParameters");

    // Create vertex buffer
    g_app.vertex_buffer = SDL_CreateGPUBuffer(g_app.gpu, &(SDL_GPUBufferCreateInfo){SDL_GPU_BUFFERUSAGE_VERTEX, sizeof(vertices), 0});
    if (!g_app.vertex_buffer)
        return sdl_fail("SDL_CreateGPUBuffer vertex_buffer");

    // Create texture
    g_app.texture = create_texture_for_image(g_app.gpu, icon_img);
    if (!g_app.texture)
        return SDL_APP_FAILURE;

    ImageData alien_img = load_asset_image("alienGreen_jump.png", 4);
    if (!alien_img.data)
        return SDL_APP_FAILURE;
    g_app.alien_tex = create_texture_for_image(g_app.gpu, alien_img);
    if (!g_app.alien_tex)
        return SDL_APP_FAILURE;

    g_app.sampler = SDL_CreateGPUSampler(g_app.gpu, &(SDL_GPUSamplerCreateInfo){
        .min_filter = SDL_GPU_FILTER_LINEAR, .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE, .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    });
    if (!g_app.sampler)
        return sdl_fail("SDL_CreateGPUSampler");

    // Upload vertex data and texture
    GPUUpload upload = begin_gpu_upload(g_app.gpu, 16 * 1024 * 1024);
    if (!upload.size)
        return SDL_APP_FAILURE;
    gpu_upload_buffer(&upload, vertices, sizeof(vertices), g_app.vertex_buffer, 0);
    gpu_upload_texture(&upload, icon_img, g_app.texture);
    gpu_upload_texture(&upload, alien_img, g_app.alien_tex);
    if (!end_gpu_upload(&upload))
        return SDL_APP_FAILURE;

    free_image(icon_img);
    free_image(alien_img);

    // Create pipeline
    SDL_GPUShader *vs = SDL_CreateGPUShader(g_app.gpu, &(SDL_GPUShaderCreateInfo){
        .code_size = shader_code_quad_vert_len,
        .code = shader_code_quad_vert,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_uniform_buffers = 1,
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

    SDL_GPUColorTargetDescription color_target_desc = {
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
    float dt = (float)elapsed_ticks / (float)g_app.ticks_freq;

    static float t = 0;
    float s = sinf(t);
    float c = cosf(t);
    float scale = 0.5f;
    float xform1[8] = {
        c*scale, s*scale, 0.0f, 0.0f,
        -s*scale, c*scale, 0.0f, 0.0f,
    };
    s = sinf(t * -1.5f);
    c = cosf(t * -1.5f);
    scale = 0.3f;
    float xform2[8] = {
        c*scale, s*scale, 0.0f, 0.0f,
        -s*scale, c*scale, 0.0f, 0.0f,
    };
    t += dt;

    {
        SDL_GPUColorTargetInfo color_target_info = {
            .texture = swap_tex,
            .clear_color = (SDL_FColor){0x26 / 255.0f, 0xA6 / 255.0f, 0x9A / 255.0f, 1.0f},
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .store_op = SDL_GPU_STOREOP_STORE,
        };
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmdbuf, &color_target_info, 1, nullptr);

        SDL_BindGPUGraphicsPipeline(pass, g_app.pipeline);
        SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){.buffer = g_app.vertex_buffer, .offset = 0}, 1);
        SDL_BindGPUFragmentSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){g_app.texture, g_app.sampler}, 1);
        SDL_PushGPUVertexUniformData(cmdbuf, 0, xform1, sizeof(xform1));
        SDL_DrawGPUPrimitives(pass, ARRAY_LENGTH(vertices), 1, 0, 0);
        SDL_BindGPUFragmentSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){g_app.alien_tex, g_app.sampler}, 1);
        SDL_PushGPUVertexUniformData(cmdbuf, 0, xform2, sizeof(xform2));
        SDL_DrawGPUPrimitives(pass, ARRAY_LENGTH(vertices), 1, 0, 0);

        SDL_EndGPURenderPass(pass);
    }

    SDL_SubmitGPUCommandBuffer(cmdbuf);
    return g_app.quit;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;

    if (event->type == SDL_EVENT_QUIT || (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)) {
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

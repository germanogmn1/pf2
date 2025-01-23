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
    SDL_AppResult quit;
    uint64_t ticks_freq;
    uint64_t last_frame_ticks;
} AppState;

AppState g_app = { .quit = SDL_APP_CONTINUE };

typedef struct {
    float pos[2];
    float color[3];
} Vertex;

const SDL_GPUVertexBufferDescription vertex_descs[] = {
    {.slot = 0, .pitch = sizeof(Vertex), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX},
    {.slot = 1, .pitch = sizeof(Vertex), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX},
};

const SDL_GPUVertexAttribute vertex_attrs[] = {
    {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(Vertex, pos)},
    {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(Vertex, color)},
};

const SDL_GPUVertexInputState vertex_input_state = {
    .vertex_buffer_descriptions = vertex_descs, .num_vertex_buffers = ARRAY_LENGTH(vertex_descs),
    .vertex_attributes = vertex_attrs, .num_vertex_attributes = ARRAY_LENGTH(vertex_attrs),
};

#define V_DIM 0.5
const Vertex vertices[] = {
    {{-V_DIM, -V_DIM}, {1, 0, 0}},
    {{+V_DIM, -V_DIM}, {0, 0, 1}},
    {{-V_DIM, +V_DIM}, {0, 1, 0}},
    {{+V_DIM, -V_DIM}, {0, 0, 1}},
    {{+V_DIM, +V_DIM}, {1, 0, 1}},
    {{-V_DIM, +V_DIM}, {0, 1, 0}},
};

const uint8_t icon_data[] = {
    #embed "icon.png"
};

static bool set_icon(SDL_Window *window) {
    SDL_Surface *sfc = nullptr;
    uint8_t *data = nullptr;
    bool result = false;

    int w, h;
    data = stbi_load_from_memory(icon_data, sizeof(icon_data), &w, &h, nullptr, 4);
    if (!data) {
        SDL_Log("ERROR: stbi_load_from_memory");
        goto exit;
    }

    sfc = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA8888, data, w * 4);
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
    if (data)
        stbi_image_free(data);
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

    g_app.window = SDL_CreateWindow("App", 1024, 1024, SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!g_app.window)
        return sdl_fail("SDL_CreateWindow");
    if (!set_icon(g_app.window))
        return SDL_APP_FAILURE;

    if (!SDL_ClaimWindowForGPUDevice(g_app.gpu, g_app.window))
        return sdl_fail("SDL_ClaimWindowForGPUDevice");

    // Create vertex buffer
    g_app.vertex_buffer = SDL_CreateGPUBuffer(g_app.gpu, &(SDL_GPUBufferCreateInfo){SDL_GPU_BUFFERUSAGE_VERTEX, sizeof(vertices), 0});
    if (!g_app.vertex_buffer)
        return sdl_fail("SDL_CreateGPUBuffer vertex_buffer");


    // Upload vertex data
    SDL_GPUTransferBuffer *transfer_buffer = SDL_CreateGPUTransferBuffer(g_app.gpu,
        &(SDL_GPUTransferBufferCreateInfo){.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = sizeof(vertices)});
    if (!transfer_buffer)
        return sdl_fail("SDL_CreateGPUTransferBuffer");
    {
        uint8_t *map = SDL_MapGPUTransferBuffer(g_app.gpu, transfer_buffer, false);
        SDL_memcpy(map, vertices, sizeof(vertices));
        SDL_UnmapGPUTransferBuffer(g_app.gpu, transfer_buffer);
    }
    {
        SDL_GPUCommandBuffer *cmdbuf = SDL_AcquireGPUCommandBuffer(g_app.gpu);
        if (!cmdbuf)
            return sdl_fail("SDL_AcquireGPUCommandBuffer");
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmdbuf);
        SDL_UploadToGPUBuffer(
            cp,
            &(SDL_GPUTransferBufferLocation){.transfer_buffer = transfer_buffer, .offset = 0},
            &(SDL_GPUBufferRegion){g_app.vertex_buffer, 0, sizeof(vertices)},
            false);
        SDL_EndGPUCopyPass(cp);
        if (!SDL_SubmitGPUCommandBuffer(cmdbuf))
            return sdl_fail("SDL_SubmitGPUCommandBuffer");
    }
    SDL_ReleaseGPUTransferBuffer(g_app.gpu, transfer_buffer);

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
    });
    if (!fs)
        return sdl_fail("SDL_CreateGPUShader shader_code_quad_frag");

    auto color_target_desc = (SDL_GPUColorTargetDescription){.format = SDL_GetGPUSwapchainTextureFormat(g_app.gpu, g_app.window)};
    g_app.pipeline = SDL_CreateGPUGraphicsPipeline(g_app.gpu, &(SDL_GPUGraphicsPipelineCreateInfo){
        .vertex_shader = vs,
        .fragment_shader = fs,
        .vertex_input_state = vertex_input_state,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = (SDL_GPURasterizerState){
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .cull_mode = SDL_GPU_CULLMODE_BACK,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE
        },
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
    if (g_app.gpu && g_app.window)
        SDL_ReleaseWindowFromGPUDevice(g_app.gpu, g_app.window);
    if (g_app.window)
        SDL_DestroyWindow(g_app.window);
    if (g_app.gpu)
        SDL_DestroyGPUDevice(g_app.gpu);
}

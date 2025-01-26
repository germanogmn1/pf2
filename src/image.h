#pragma once

#include <stdint.h>
#include <SDL3/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

typedef struct {
    uint8_t *data;
    int w, h, channels;
} ImageData;

static size_t image_size(ImageData id) { return id.w * id.h * id.channels; }

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

#include <libretro.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_CORE_WIDTH 64U
#define TEST_CORE_HEIGHT 32U
#define TEST_CORE_AUDIO_FRAMES 735U
#define TEST_CORE_RAM_SIZE (64U * 1024U)
#define TEST_CORE_FRAME_COUNT_OFFSET 0U
/* The last RETRO_DEVICE_JOYPAD state is stored as a little-endian uint16_t in bytes 4 and 5. */
#define TEST_CORE_INPUT_MASK_OFFSET 4U
#define TEST_CORE_SERIALIZED_SIZE (sizeof(uint64_t) + TEST_CORE_RAM_SIZE)
#define TEST_CORE_MAX_PITCH (TEST_CORE_WIDTH * 4U + 16U)
#define TEST_CORE_MAX_VIDEO_SIZE (TEST_CORE_MAX_PITCH * TEST_CORE_HEIGHT)
#define TEST_CORE_OPTION_KEY "cw_test_pixel_format"
#define TEST_CORE_RESIZE_KEY "cw_test_resize"
#define TEST_CORE_AUDIO_MODE_KEY "cw_test_audio_mode"
#define TEST_CORE_SAMPLE_RATE_KEY "cw_test_sample_rate_change"
#define TEST_CORE_QUIRKS_KEY "cw_test_serialization_quirks"
/* Announced with the quirks option: two real quirks plus a bit libretro does not define, which
   the front-end must clear (as well as ENDIAN_DEPENDENT, which it cannot enforce). */
#define TEST_CORE_UNKNOWN_QUIRK ((uint64_t)1 << 40)
#define TEST_CORE_INPUT_MODE_KEY "cw_test_input_mode"
/* With cw_test_resize enabled, frames from this one on are 32x16 with a 3:2 aspect ratio,
   announced through SET_GEOMETRY. */
#define TEST_CORE_RESIZE_FRAME 3U
#define TEST_CORE_SMALL_WIDTH 32U
#define TEST_CORE_SMALL_HEIGHT 16U
#define TEST_CORE_SMALL_ASPECT 1.5f

static retro_environment_t environment_callback;
static retro_video_refresh_t video_refresh_callback;
static retro_audio_sample_t audio_sample_callback;
static retro_audio_sample_batch_t audio_sample_batch_callback;
static retro_input_poll_t input_poll_callback;
static retro_input_state_t input_state_callback;
static retro_log_printf_t log_callback;
static bool can_dupe_frames;
static bool resize_enabled;
static bool single_sample_audio;
static bool change_sample_rate;
static bool input_bitmask_mode;
static bool supports_input_bitmasks;
static bool hw_render_accepted;
static unsigned int frame_width = TEST_CORE_WIDTH;
static unsigned int frame_height = TEST_CORE_HEIGHT;

static uint64_t frame_count;
static bool game_loaded;
static enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
static uint8_t system_ram[TEST_CORE_RAM_SIZE];
static union {
    uint32_t alignment[(TEST_CORE_MAX_VIDEO_SIZE + sizeof(uint32_t) - 1U) / sizeof(uint32_t)];
    uint8_t bytes[TEST_CORE_MAX_VIDEO_SIZE];
} video_buffer;
static int16_t audio_buffer[TEST_CORE_AUDIO_FRAMES * 2U];

static struct retro_core_option_v2_definition core_option_definitions[] = {
    {
        .key = TEST_CORE_OPTION_KEY,
        .desc = "Pixel format",
        .desc_categorized = NULL,
        .info = "Select the software frame format emitted by the synthetic test core.",
        .info_categorized = NULL,
        .category_key = NULL,
        .values =
            {
                {"0RGB1555", "0RGB1555"},
                {"XRGB8888", "XRGB8888"},
                {"RGB565", "RGB565"},
                {NULL, NULL},
            },
        .default_value = "0RGB1555",
    },
    {
        .key = TEST_CORE_RESIZE_KEY,
        .desc = "Resize at frame 3",
        .desc_categorized = NULL,
        .info = "Switch to 32x16 frames (aspect 3:2) to exercise geometry changes.",
        .info_categorized = NULL,
        .category_key = NULL,
        .values =
            {
                {"disabled", "disabled"},
                {"enabled", "enabled"},
                {NULL, NULL},
            },
        .default_value = "disabled",
    },
    {
        .key = TEST_CORE_AUDIO_MODE_KEY,
        .desc = "Audio callback mode",
        .desc_categorized = NULL,
        .info = "Select whether the synthetic core emits audio in a batch or one sample at a time.",
        .info_categorized = NULL,
        .category_key = NULL,
        .values =
            {
                {"batch", "batch"},
                {"single", "single"},
                {NULL, NULL},
            },
        .default_value = "batch",
    },
    {
        .key = TEST_CORE_SAMPLE_RATE_KEY,
        .desc = "Change sample rate at frame 3",
        .desc_categorized = NULL,
        .info = "Announce a 48 kHz audio rate through SET_SYSTEM_AV_INFO at frame 3.",
        .info_categorized = NULL,
        .category_key = NULL,
        .values =
            {
                {"disabled", "disabled"},
                {"enabled", "enabled"},
                {NULL, NULL},
            },
        .default_value = "disabled",
    },
    {
        .key = TEST_CORE_INPUT_MODE_KEY,
        .desc = "Input callback mode",
        .desc_categorized = NULL,
        .info = "Select individual joypad buttons or the libretro joypad bitmask query.",
        .info_categorized = NULL,
        .category_key = NULL,
        .values =
            {
                {"individual", "individual"},
                {"bitmask", "bitmask"},
                {NULL, NULL},
            },
        .default_value = "individual",
    },
    {
        .key = TEST_CORE_QUIRKS_KEY,
        .desc = "Announce serialization quirks",
        .desc_categorized = NULL,
        .info = "Announce CORE_VARIABLE_SIZE, SINGLE_SESSION and an unknown quirk bit at load.",
        .info_categorized = NULL,
        .category_key = NULL,
        .values =
            {
                {"disabled", "disabled"},
                {"enabled", "enabled"},
                {NULL, NULL},
            },
        .default_value = "disabled",
    },
    {0},
};

static struct retro_core_options_v2 core_options_v2 = {
    .categories = NULL,
    .definitions = core_option_definitions,
};

static struct retro_variable legacy_variables[] = {
    {TEST_CORE_OPTION_KEY, "Pixel format; 0RGB1555|XRGB8888|RGB565"},
    {TEST_CORE_RESIZE_KEY, "Resize at frame 3; disabled|enabled"},
    {TEST_CORE_AUDIO_MODE_KEY, "Audio callback mode; batch|single"},
    {TEST_CORE_SAMPLE_RATE_KEY, "Change sample rate at frame 3; disabled|enabled"},
    {TEST_CORE_QUIRKS_KEY, "Announce serialization quirks; disabled|enabled"},
    {TEST_CORE_INPUT_MODE_KEY, "Input callback mode; individual|bitmask"},
    {NULL, NULL},
};

static void write_u32_le(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static void write_u64_le(uint8_t* destination, uint64_t value) {
    unsigned int index;
    for (index = 0; index < 8U; ++index)
        destination[index] = (uint8_t)(value >> (index * 8U));
}

static uint64_t read_u64_le(const uint8_t* source) {
    uint64_t value = 0;
    unsigned int index;
    for (index = 0; index < 8U; ++index)
        value |= (uint64_t)source[index] << (index * 8U);
    return value;
}

static void reset_game_state(void) {
    frame_count = 0;
    frame_width = TEST_CORE_WIDTH;
    frame_height = TEST_CORE_HEIGHT;
    memset(system_ram, 0, sizeof(system_ram));
    write_u32_le(system_ram + TEST_CORE_FRAME_COUNT_OFFSET, 0);
    write_u32_le(system_ram + TEST_CORE_INPUT_MASK_OFFSET, 0);
}

static bool parse_pixel_format(const char* value, enum retro_pixel_format* format) {
    if (value == NULL || format == NULL)
        return false;

    if (strcmp(value, "0RGB1555") == 0)
        *format = RETRO_PIXEL_FORMAT_0RGB1555;
    else if (strcmp(value, "XRGB8888") == 0)
        *format = RETRO_PIXEL_FORMAT_XRGB8888;
    else if (strcmp(value, "RGB565") == 0)
        *format = RETRO_PIXEL_FORMAT_RGB565;
    else
        return false;

    return true;
}

static size_t frame_pitch(void) {
    const unsigned int bytes_per_pixel = pixel_format == RETRO_PIXEL_FORMAT_XRGB8888 ? 4U : 2U;
    return (size_t)frame_width * bytes_per_pixel + 16U; /* 16 bytes of padding per row */
}

static void emit_video_frame(void) {
    const unsigned int bytes_per_pixel = pixel_format == RETRO_PIXEL_FORMAT_XRGB8888 ? 4U : 2U;
    const size_t pitch = frame_pitch();
    const uint8_t padding = (uint8_t)(0xA0U | (frame_count & 0x1FU));
    unsigned int y;

    for (y = 0; y < frame_height; ++y) {
        uint8_t* row = video_buffer.bytes + (size_t)y * pitch;
        unsigned int x;

        for (x = 0; x < frame_width; ++x) {
            const uint8_t red = (uint8_t)(x * 3U + frame_count * 5U);
            const uint8_t green = (uint8_t)(y * 7U + frame_count * 11U);
            const uint8_t blue = (uint8_t)((x + y) * 13U + frame_count * 17U);

            if (pixel_format == RETRO_PIXEL_FORMAT_0RGB1555) {
                const uint16_t value =
                    (uint16_t)(((uint16_t)(red >> 3U) << 10U) | ((uint16_t)(green >> 3U) << 5U) |
                               (uint16_t)(blue >> 3U));
                memcpy(row + (size_t)x * bytes_per_pixel, &value, sizeof(value));
            } else if (pixel_format == RETRO_PIXEL_FORMAT_RGB565) {
                const uint16_t value =
                    (uint16_t)(((uint16_t)(red >> 3U) << 11U) | ((uint16_t)(green >> 2U) << 5U) |
                               (uint16_t)(blue >> 3U));
                memcpy(row + (size_t)x * bytes_per_pixel, &value, sizeof(value));
            } else {
                const uint32_t value = ((uint32_t)red << 16U) | ((uint32_t)green << 8U) | blue;
                memcpy(row + (size_t)x * bytes_per_pixel, &value, sizeof(value));
            }
        }

        memset(row + (size_t)frame_width * bytes_per_pixel, padding,
               pitch - (size_t)frame_width * bytes_per_pixel);
    }

    if (video_refresh_callback != NULL)
        video_refresh_callback(video_buffer.bytes, frame_width, frame_height, pitch);
}

static void emit_audio(void) {
    unsigned int index;

    for (index = 0; index < TEST_CORE_AUDIO_FRAMES; ++index) {
        const uint32_t phase = (uint32_t)(frame_count * TEST_CORE_AUDIO_FRAMES + index);
        const int32_t centered = (int32_t)(phase % 65535U) - 32767;
        const int16_t sample = (int16_t)centered;
        audio_buffer[index * 2U] = sample;
        audio_buffer[index * 2U + 1U] = (int16_t)-sample;
    }

    if (single_sample_audio && audio_sample_callback != NULL) {
        for (index = 0; index < TEST_CORE_AUDIO_FRAMES; ++index)
            audio_sample_callback(audio_buffer[index * 2U], audio_buffer[index * 2U + 1U]);
    } else if (audio_sample_batch_callback != NULL) {
        (void)audio_sample_batch_callback(audio_buffer, TEST_CORE_AUDIO_FRAMES);
    }
}

unsigned int retro_api_version(void) {
    return RETRO_API_VERSION;
}

void retro_set_environment(retro_environment_t callback) {
    unsigned int options_version = 0;

    environment_callback = callback;
    can_dupe_frames = false;
    if (environment_callback == NULL)
        return;

    (void)environment_callback(RETRO_ENVIRONMENT_GET_CAN_DUPE, &can_dupe_frames);

    if (environment_callback(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &options_version) &&
        options_version >= 2U) {
        (void)environment_callback(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &core_options_v2);
    } else {
        (void)environment_callback(RETRO_ENVIRONMENT_SET_VARIABLES, legacy_variables);
    }
}

void retro_set_video_refresh(retro_video_refresh_t callback) {
    video_refresh_callback = callback;
}

void retro_set_audio_sample(retro_audio_sample_t callback) {
    audio_sample_callback = callback;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t callback) {
    audio_sample_batch_callback = callback;
}

void retro_set_input_poll(retro_input_poll_t callback) {
    input_poll_callback = callback;
}

void retro_set_input_state(retro_input_state_t callback) {
    input_state_callback = callback;
}

void retro_set_controller_port_device(unsigned int port, unsigned int device) {
    (void)port;
    (void)device;
}

void retro_init(void) {
    struct retro_log_callback log_interface = {0};

    if (environment_callback != NULL &&
        environment_callback(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_interface))
        log_callback = log_interface.log;

    if (log_callback != NULL)
        log_callback(RETRO_LOG_INFO, "cw-test-core initialized\n");
}

void retro_deinit(void) {
    game_loaded = false;
    log_callback = NULL;
}

void retro_get_system_info(struct retro_system_info* info) {
    if (info == NULL)
        return;

    info->library_name = "cw-test-core";
    info->library_version = "1.0";
    info->valid_extensions = "bin|dat";
    info->need_fullpath = false;
    info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info* info) {
    if (info == NULL)
        return;

    info->geometry.base_width = TEST_CORE_WIDTH;
    info->geometry.base_height = TEST_CORE_HEIGHT;
    info->geometry.max_width = TEST_CORE_WIDTH;
    info->geometry.max_height = TEST_CORE_HEIGHT;
    info->geometry.aspect_ratio = (float)TEST_CORE_WIDTH / (float)TEST_CORE_HEIGHT;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 44100.0;
}

bool retro_load_game(const struct retro_game_info* game) {
    struct retro_variable variable = {TEST_CORE_OPTION_KEY, NULL};

    if (game == NULL || game->data == NULL || game->size == 0 || environment_callback == NULL ||
        !environment_callback(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) ||
        !parse_pixel_format(variable.value, &pixel_format))
        return false;

    if (!environment_callback(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixel_format))
        return false;

    {
        struct retro_variable resize = {TEST_CORE_RESIZE_KEY, NULL};
        resize_enabled = environment_callback(RETRO_ENVIRONMENT_GET_VARIABLE, &resize) &&
                         resize.value != NULL && strcmp(resize.value, "enabled") == 0;
    }
    {
        struct retro_variable audio_mode = {TEST_CORE_AUDIO_MODE_KEY, NULL};
        single_sample_audio = environment_callback(RETRO_ENVIRONMENT_GET_VARIABLE, &audio_mode) &&
                              audio_mode.value != NULL && strcmp(audio_mode.value, "single") == 0;
    }
    {
        struct retro_variable sample_rate = {TEST_CORE_SAMPLE_RATE_KEY, NULL};
        change_sample_rate = environment_callback(RETRO_ENVIRONMENT_GET_VARIABLE, &sample_rate) &&
                             sample_rate.value != NULL && strcmp(sample_rate.value, "enabled") == 0;
    }
    {
        /* libretro.h: quirks are announced in retro_init or retro_load_game (not both). */
        struct retro_variable quirks_option = {TEST_CORE_QUIRKS_KEY, NULL};
        if (environment_callback(RETRO_ENVIRONMENT_GET_VARIABLE, &quirks_option) &&
            quirks_option.value != NULL && strcmp(quirks_option.value, "enabled") == 0) {
            uint64_t quirks = RETRO_SERIALIZATION_QUIRK_CORE_VARIABLE_SIZE |
                              RETRO_SERIALIZATION_QUIRK_SINGLE_SESSION |
                              RETRO_SERIALIZATION_QUIRK_ENDIAN_DEPENDENT | TEST_CORE_UNKNOWN_QUIRK;
            (void)environment_callback(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &quirks);
        }
    }
    {
        struct retro_variable input_mode = {TEST_CORE_INPUT_MODE_KEY, NULL};
        supports_input_bitmasks = environment_callback(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL);
        input_bitmask_mode = environment_callback(RETRO_ENVIRONMENT_GET_VARIABLE, &input_mode) &&
                             input_mode.value != NULL && strcmp(input_mode.value, "bitmask") == 0 &&
                             supports_input_bitmasks;
    }

    /* Ask for hardware rendering like a GL core would. A software-only front-end must refuse;
       if it accepted, this core would send RETRO_HW_FRAME_BUFFER_VALID instead of pixels and
       the tests would see no software frames. */
    {
        struct retro_hw_render_callback hw_render;
        memset(&hw_render, 0, sizeof(hw_render));
        hw_render.context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
        hw_render.version_major = 3;
        hw_render.version_minor = 3;
        hw_render_accepted = environment_callback(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render);
    }

    reset_game_state();
    game_loaded = true;
    if (log_callback != NULL)
        log_callback(RETRO_LOG_INFO, "cw-test-core loaded synthetic content\n");
    return true;
}

void retro_unload_game(void) {
    game_loaded = false;
}

void retro_reset(void) {
    if (game_loaded)
        reset_game_state();
}

void retro_run(void) {
    uint16_t input_mask = 0;
    size_t index;

    if (!game_loaded)
        return;

    if (input_poll_callback != NULL)
        input_poll_callback();

    if (input_state_callback != NULL && input_bitmask_mode) {
        input_mask =
            (uint16_t)input_state_callback(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    } else if (input_state_callback != NULL) {
        for (index = 0; index <= RETRO_DEVICE_ID_JOYPAD_R3; ++index) {
            if (input_state_callback(0, RETRO_DEVICE_JOYPAD, 0, (unsigned int)index) != 0)
                input_mask |= (uint16_t)(1U << index);
        }
    }

    ++frame_count;
    for (index = 0; index < TEST_CORE_RAM_SIZE; ++index)
        system_ram[index] = (uint8_t)(index * 17U + frame_count * 31U);

    write_u32_le(system_ram + TEST_CORE_FRAME_COUNT_OFFSET, (uint32_t)frame_count);
    system_ram[TEST_CORE_INPUT_MASK_OFFSET] = (uint8_t)input_mask;
    system_ram[TEST_CORE_INPUT_MASK_OFFSET + 1U] = (uint8_t)(input_mask >> 8U);

    if (change_sample_rate && frame_count == TEST_CORE_RESIZE_FRAME) {
        struct retro_system_av_info av_info;
        retro_get_system_av_info(&av_info);
        av_info.timing.sample_rate = 48000.0;
        (void)environment_callback(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info);
    }

    if (resize_enabled && frame_count == TEST_CORE_RESIZE_FRAME) {
        struct retro_game_geometry geometry;
        geometry.base_width = TEST_CORE_SMALL_WIDTH;
        geometry.base_height = TEST_CORE_SMALL_HEIGHT;
        geometry.max_width = TEST_CORE_WIDTH; /* ignored by SET_GEOMETRY */
        geometry.max_height = TEST_CORE_HEIGHT;
        geometry.aspect_ratio = TEST_CORE_SMALL_ASPECT;
        frame_width = TEST_CORE_SMALL_WIDTH;
        frame_height = TEST_CORE_SMALL_HEIGHT;
        (void)environment_callback(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
    }

    if (hw_render_accepted) {
        if (video_refresh_callback != NULL)
            video_refresh_callback(RETRO_HW_FRAME_BUFFER_VALID, frame_width, frame_height, 0);
    } else if ((frame_count % 4U) == 0U && can_dupe_frames) {
        if (video_refresh_callback != NULL)
            video_refresh_callback(NULL, frame_width, frame_height, frame_pitch());
    } else {
        emit_video_frame();
    }

    emit_audio();
}

size_t retro_serialize_size(void) {
    return TEST_CORE_SERIALIZED_SIZE;
}

bool retro_serialize(void* data, size_t size) {
    uint8_t* output = (uint8_t*)data;

    if (output == NULL || size != TEST_CORE_SERIALIZED_SIZE)
        return false;

    write_u64_le(output, frame_count);
    memcpy(output + sizeof(uint64_t), system_ram, sizeof(system_ram));
    return true;
}

bool retro_unserialize(const void* data, size_t size) {
    const uint8_t* input = (const uint8_t*)data;

    if (input == NULL || size != TEST_CORE_SERIALIZED_SIZE)
        return false;

    frame_count = read_u64_le(input);
    memcpy(system_ram, input + sizeof(uint64_t), sizeof(system_ram));
    return true;
}

void* retro_get_memory_data(unsigned int id) {
    return game_loaded && id == RETRO_MEMORY_SYSTEM_RAM ? system_ram : NULL;
}

size_t retro_get_memory_size(unsigned int id) {
    return game_loaded && id == RETRO_MEMORY_SYSTEM_RAM ? sizeof(system_ram) : 0;
}

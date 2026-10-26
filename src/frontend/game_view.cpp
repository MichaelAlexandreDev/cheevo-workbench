#include "game_view.hpp"

#include <SDL3/SDL_opengl.h>

#include <algorithm>

namespace cw {

GameView::~GameView() {
    release();
}

void GameView::release() {
    if (texture_ != 0) {
        const GLuint texture = texture_;
        glDeleteTextures(1, &texture);
        texture_ = 0;
    }
    width_ = 0;
    height_ = 0;
    has_image_ = false;
}

void GameView::update(const VideoFrame& frame) {
    if (frame.width == 0 || frame.height == 0 || frame.serial == uploaded_serial_) {
        return;
    }
    if (texture_ == 0) {
        GLuint texture = 0;
        glGenTextures(1, &texture);
        texture_ = texture;
        glBindTexture(GL_TEXTURE_2D, texture_);
        // Nearest filtering keeps the console's pixels sharp when scaled up.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, texture_);
    }

    // VideoFrame rows are tightly packed RGBA8, so the default unpack settings are right; set
    // them anyway because other code (SDL, ImGui) may leave them changed.
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (frame.width != width_ || frame.height != height_) {
        // Geometry changed (or first frame): reallocate the texture at the new size.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(frame.width),
                     static_cast<GLsizei>(frame.height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     frame.rgba.data());
        width_ = frame.width;
        height_ = frame.height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(width_),
                        static_cast<GLsizei>(height_), GL_RGBA, GL_UNSIGNED_BYTE,
                        frame.rgba.data());
    }
    uploaded_serial_ = frame.serial;
    has_image_ = true;
}

void GameView::draw(ImDrawList* draw_list, ImVec2 min, ImVec2 max, float aspect_ratio) const {
    const float area_w = max.x - min.x;
    const float area_h = max.y - min.y;
    if (!has_image_ || area_w <= 0.0f || area_h <= 0.0f) {
        return;
    }
    const float aspect = aspect_ratio > 0.0f
                             ? aspect_ratio
                             : static_cast<float>(width_) / static_cast<float>(height_);

    // Largest rectangle with that aspect ratio that fits the area, centered ("letterbox").
    const float w = std::min(area_w, area_h * aspect);
    const float h = w / aspect;
    const ImVec2 p0(min.x + (area_w - w) * 0.5f, min.y + (area_h - h) * 0.5f);
    draw_list->AddImage(static_cast<ImTextureID>(texture_), p0, ImVec2(p0.x + w, p0.y + h));
}

} // namespace cw

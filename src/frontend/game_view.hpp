#pragma once

#include "video_frame.hpp"

#include <imgui.h>

#include <cstdint>

namespace cw {

// Shows the core's image: keeps an OpenGL texture in sync with the last VideoFrame and draws
// it behind the ImGui windows, scaled to fit a rectangle while keeping the aspect ratio.
// Needs a current OpenGL context for its whole lifetime.
class GameView {
  public:
    GameView() = default;
    ~GameView();
    GameView(const GameView&) = delete;
    GameView& operator=(const GameView&) = delete;

    // Uploads the frame if it is new (its serial changed); duplicated frames cost nothing.
    void update(const VideoFrame& frame);
    // Forgets the image (e.g. after the game is unloaded); nothing is drawn until the next frame.
    void clear() { has_image_ = false; }
    // Deletes the texture. Call before the OpenGL context is destroyed; the destructor only
    // cleans up what is left, and GL calls without a context are undefined behavior.
    void release();

    // Draws the image centered in [min, max]. `aspect_ratio` is the width/height the core asks
    // for; <= 0 means "use the frame's own width/height" (libretro.h, retro_game_geometry).
    void draw(ImDrawList* draw_list, ImVec2 min, ImVec2 max, float aspect_ratio) const;

  private:
    unsigned texture_ = 0; // GLuint
    unsigned width_ = 0;
    unsigned height_ = 0;
    uint64_t uploaded_serial_ = 0;
    bool has_image_ = false;
};

} // namespace cw

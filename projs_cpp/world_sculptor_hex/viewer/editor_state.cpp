#include "editor_state.hpp"

#include <cmath>

namespace viewer {

EditorState::EditorState(int width, int height)
    : width_(width), height_(height),
      heightmap_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.5f)
{
}

void EditorState::resize(int width, int height) {
    width_  = width;
    height_ = height;
    heightmap_.assign(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
        0.5f
    );
    water_sources.clear();
}

void EditorState::reset_heights(float h) {
    std::fill(heightmap_.begin(), heightmap_.end(), h);
}

} // namespace viewer

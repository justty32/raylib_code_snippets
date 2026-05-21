#include "editor_state.hpp"

#include <algorithm>

namespace viewer {

EditorState::EditorState(int width, int height)
    : width_(width), height_(height),
      heightmap_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.5f)
{
    clear_sim_results();
}

void EditorState::resize(int width, int height) {
    width_  = width;
    height_ = height;
    heightmap_.assign(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
        0.5f
    );
    water_sources.clear();
    overlay = Overlay::Height;
    clear_sim_results();
}

void EditorState::reset_heights(float h) {
    std::fill(heightmap_.begin(), heightmap_.end(), h);
}

void EditorState::clear_sim_results() {
    const std::size_t n = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    ocean_mask .assign(n, false);
    temperature.assign(n, 0.5f);
    rainfall   .assign(n, 0.5f);
}

} // namespace viewer

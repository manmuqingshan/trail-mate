#pragma once

#include <cstdint>

namespace ui::map
{
// Rendering role, distinct from the source's geographical category.
enum class AnnotationKind : uint8_t
{
    Poi,
    Road,
    Place,
};
} // namespace ui::map

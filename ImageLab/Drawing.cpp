#include "Drawing.h"

#include <algorithm>
#include <cmath>

namespace drawing
{
namespace
{
constexpr float twoPi = 6.28318530718f;
}

Path line(Point a, Point b)
{
    return { { a, b }, false };
}

Path rectangle(float x, float y, float width, float height)
{
    return { { { x, y }, { x + width, y }, { x + width, y + height }, { x, y + height } }, true };
}

Path ellipse(Point centre, float radiusX, float radiusY, int segments)
{
    Path path;
    path.closed = true;
    segments = std::max(3, segments);
    for (int i = 0; i < segments; ++i)
    {
        const float a = twoPi * static_cast<float>(i) / static_cast<float>(segments);
        path.points.push_back({ centre.x + radiusX * std::cos(a), centre.y + radiusY * std::sin(a) });
    }
    return path;
}

Path polygon(Point centre, float radius, int sides, float rotationDegrees)
{
    Path path;
    path.closed = true;
    sides = std::max(3, sides);
    // Rotation 0 puts a corner straight up.
    const float start = rotationDegrees * twoPi / 360.0f - twoPi / 4.0f;
    for (int i = 0; i < sides; ++i)
    {
        const float a = start + twoPi * static_cast<float>(i) / static_cast<float>(sides);
        path.points.push_back({ centre.x + radius * std::cos(a), centre.y + radius * std::sin(a) });
    }
    return path;
}
} // namespace drawing

#include "Drawing.h"

#include <algorithm>
#include <cstdint>
#include <cmath>

namespace drawing
{
namespace
{
constexpr float twoPi = 6.28318530718f;

// A repeatable random number in 0..1 from three integers.
float random01(std::int64_t a, std::int64_t b, std::int64_t seed)
{
    auto h = static_cast<std::uint64_t>(a) * 0x9E3779B97F4A7C15ull ^ static_cast<std::uint64_t>(b) * 0xC2B2AE3D27D4EB4Full
           ^ static_cast<std::uint64_t>(seed) * 0x165667B19E3779F9ull;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return static_cast<float>(h >> 40) / static_cast<float>(1ull << 24);
}
}

std::vector<Stamp> placeStamps(const Path& path, const Brush& brush, int width, int height, int pathIndex)
{
    std::vector<Stamp> stamps;
    const auto n = path.points.size();
    if (n == 0 || width <= 0 || height <= 0)
        return stamps;

    const float sx = static_cast<float>(width), sy = static_cast<float>(height);
    const float diameter = brush.size * std::min(sx, sy);
    const float baseRadius = diameter * 0.5f;
    float step = std::max(0.25f, brush.spacing * diameter);
    // Bristles touch the paper all the way along: stamps no further apart than one bristle's radius (drawing.frust
    // makes a bristle 0.07 of the tip's radius), or each bristle would leave a dotted line instead of a streak.
    if (brush.tip == Brush::Tip::bristle)
        step = std::min(step, std::max(0.25f, 0.035f * diameter));

    // The path in pixels, as segments, and its length.
    struct Segment
    {
        float ax, ay, bx, by, length;
    };
    std::vector<Segment> segments;
    const size_t count = path.closed && n > 2 ? n : n - 1;
    float total = 0.0f;
    for (size_t i = 0; i < count; ++i)
    {
        const auto& a = path.points[i];
        const auto& b = path.points[(i + 1) % n];
        Segment s { a.x * sx, a.y * sy, b.x * sx, b.y * sy, 0.0f };
        s.length = std::hypot(s.bx - s.ax, s.by - s.ay);
        if (s.length > 0.0f)
        {
            segments.push_back(s);
            total += s.length;
        }
    }

    const std::int64_t stream = static_cast<std::int64_t>(pathIndex) * 1000003;
    auto add = [&](float x, float y, float along, float direction) {
        const auto k = static_cast<std::int64_t>(stamps.size());
        float f = 1.0f;
        if (brush.taperStart > 0.0f && total > 0.0f)
            f *= std::min(1.0f, along / (brush.taperStart * total));
        if (brush.taperEnd > 0.0f && total > 0.0f)
            f *= std::min(1.0f, (total - along) / (brush.taperEnd * total));
        f *= 1.0f - brush.sizeJitter * random01(k, stream, brush.seed);

        Stamp s;
        s.radius = std::max(0.0f, baseRadius * f);
        s.angle = brush.angle * twoPi / 360.0f;
        if (brush.rotation == Brush::Rotation::followStroke)
            s.angle += direction;
        else if (brush.rotation == Brush::Rotation::random)
            s.angle += random01(k, stream + 1, brush.seed) * twoPi;
        s.x = x + (random01(k, stream + 2, brush.seed) * 2.0f - 1.0f) * brush.scatter * diameter;
        s.y = y + (random01(k, stream + 3, brush.seed) * 2.0f - 1.0f) * brush.scatter * diameter;
        s.opacity = 1.0f - brush.opacityJitter * random01(k, stream + 4, brush.seed);
        s.opacity *= std::min(1.0f, 2.0f * s.radius); // a stamp smaller than a pixel fades instead of staying a dot
        stamps.push_back(s);
    };

    if (segments.empty())
    {
        add(path.points[0].x * sx, path.points[0].y * sy, 0.0f, 0.0f); // a single point, or a path of zero length
        return stamps;
    }

    // One at the start, then one every step along the path, carrying the remainder across corners.
    add(segments[0].ax, segments[0].ay, 0.0f, std::atan2(segments[0].by - segments[0].ay, segments[0].bx - segments[0].ax));
    float carry = 0.0f, done = 0.0f, lastAlong = 0.0f;
    for (const auto& seg : segments)
    {
        const float direction = std::atan2(seg.by - seg.ay, seg.bx - seg.ax);
        float along = step - carry;
        for (; along <= seg.length; along += step)
        {
            const float t = along / seg.length;
            add(seg.ax + (seg.bx - seg.ax) * t, seg.ay + (seg.by - seg.ay) * t, done + along, direction);
            lastAlong = done + along;
        }
        carry = seg.length - (along - step);
        done += seg.length;
    }
    // An open path also ends exactly on its last point.
    if (! path.closed && total - lastAlong > 1.0e-3f)
    {
        const auto& last = segments.back();
        add(last.bx, last.by, total, std::atan2(last.by - last.ay, last.bx - last.ax));
    }
    return stamps;
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

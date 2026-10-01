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

namespace drawing
{
namespace
{
constexpr float degrees = 6.28318530718f / 360.0f;

// Rotates p about c by a (radians), clockwise on screen.
Point turned(Point p, Point c, float a)
{
    const float dx = p.x - c.x, dy = p.y - c.y, ca = std::cos(a), sa = std::sin(a);
    return { c.x + dx * ca - dy * sa, c.y + dx * sa + dy * ca };
}

// Smooth 1D value noise, 0..1.
float noise1(float t, std::int64_t stream, int seed)
{
    const float f = std::floor(t);
    const auto i = static_cast<std::int64_t>(f);
    float u = t - f;
    u = u * u * (3.0f - 2.0f * u);
    const float a = random01(i, stream, seed), b = random01(i + 1, stream, seed);
    return a + (b - a) * u;
}

void append(Drawing& into, const Drawing& from)
{
    into.paths.insert(into.paths.end(), from.paths.begin(), from.paths.end());
}
}

Path star(Point centre, float outerRadius, float innerRadius, int points, float rotationDegrees)
{
    Path path;
    path.closed = true;
    points = std::max(2, points);
    const float start = rotationDegrees * degrees - twoPi / 4.0f;
    for (int i = 0; i < points * 2; ++i)
    {
        const float r = i % 2 == 0 ? outerRadius : innerRadius;
        const float a = start + twoPi * static_cast<float>(i) / static_cast<float>(points * 2);
        path.points.push_back({ centre.x + r * std::cos(a), centre.y + r * std::sin(a) });
    }
    return path;
}

Path arc(Point centre, float radius, float startDegrees, float sweepDegrees)
{
    Path path;
    const int segments = std::max(8, static_cast<int>(std::abs(sweepDegrees) / 360.0f * 128.0f));
    for (int i = 0; i <= segments; ++i)
    {
        const float a = (startDegrees + sweepDegrees * static_cast<float>(i) / static_cast<float>(segments)) * degrees;
        path.points.push_back({ centre.x + radius * std::cos(a), centre.y + radius * std::sin(a) });
    }
    return path;
}

Path bezier(Point a, Point c1, Point c2, Point b, int segments)
{
    Path path;
    segments = std::max(1, segments);
    for (int i = 0; i <= segments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(segments), u = 1.0f - t;
        const float w0 = u * u * u, w1 = 3.0f * u * u * t, w2 = 3.0f * u * t * t, w3 = t * t * t;
        path.points.push_back({ w0 * a.x + w1 * c1.x + w2 * c2.x + w3 * b.x, w0 * a.y + w1 * c1.y + w2 * c2.y + w3 * b.y });
    }
    return path;
}

Path spiral(Point centre, float innerRadius, float outerRadius, float turns)
{
    Path path;
    const int segments = std::max(16, static_cast<int>(std::abs(turns) * 128.0f));
    for (int i = 0; i <= segments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float r = innerRadius + (outerRadius - innerRadius) * t, a = t * turns * twoPi;
        path.points.push_back({ centre.x + r * std::cos(a), centre.y + r * std::sin(a) });
    }
    return path;
}

Bounds bounds(const Drawing& drawing)
{
    Bounds b;
    bool first = true;
    for (const auto& path : drawing.paths)
        for (const auto& p : path.points)
        {
            if (first)
            {
                b = { p.x, p.y, p.x, p.y };
                first = false;
            }
            b.left = std::min(b.left, p.x);
            b.top = std::min(b.top, p.y);
            b.right = std::max(b.right, p.x);
            b.bottom = std::max(b.bottom, p.y);
        }
    return b;
}

Drawing transformed(const Drawing& drawing, float moveX, float moveY, float rotationDegrees, float scaleX, float scaleY, Point pivot)
{
    Drawing result = drawing;
    const float a = rotationDegrees * degrees;
    for (auto& path : result.paths)
        for (auto& p : path.points)
        {
            const Point scaled { pivot.x + (p.x - pivot.x) * scaleX, pivot.y + (p.y - pivot.y) * scaleY };
            const Point t = turned(scaled, pivot, a);
            p = { t.x + moveX, t.y + moveY };
        }
    return result;
}

Drawing repeatLinear(const Drawing& drawing, int count, float dx, float dy, float rotateStep, float scaleStep)
{
    Drawing result;
    const auto centre = bounds(drawing).centre();
    for (int i = 0; i < std::max(0, count); ++i)
    {
        const float scale = std::pow(scaleStep, static_cast<float>(i));
        append(result, transformed(drawing, dx * static_cast<float>(i), dy * static_cast<float>(i), rotateStep * static_cast<float>(i), scale, scale, centre));
    }
    return result;
}

Drawing repeatRadial(const Drawing& drawing, int count, Point centre, float sweepDegrees)
{
    Drawing result;
    count = std::max(0, count);
    // A full turn spaces copies evenly without doubling up the first; a partial sweep reaches both ends.
    const bool fullTurn = std::abs(std::abs(sweepDegrees) - 360.0f) < 1.0e-3f;
    const float step = count <= 1 ? 0.0f : sweepDegrees / static_cast<float>(fullTurn ? count : count - 1);
    for (int i = 0; i < count; ++i)
        append(result, transformed(drawing, 0.0f, 0.0f, step * static_cast<float>(i), 1.0f, 1.0f, centre));
    return result;
}

Drawing repeatGrid(const Drawing& drawing, int columns, int rows, float dx, float dy)
{
    Drawing result;
    for (int row = 0; row < std::max(0, rows); ++row)
        for (int column = 0; column < std::max(0, columns); ++column)
            append(result, transformed(drawing, dx * static_cast<float>(column), dy * static_cast<float>(row), 0.0f, 1.0f, 1.0f, {}));
    return result;
}

Drawing scattered(const Drawing& drawing, int count, Bounds area, float maxRotationDegrees, float scaleMin, float scaleMax, int seed)
{
    Drawing result;
    const auto centre = bounds(drawing).centre();
    for (int k = 0; k < std::max(0, count); ++k)
    {
        const float x = area.left + random01(k, 11, seed) * (area.right - area.left);
        const float y = area.top + random01(k, 12, seed) * (area.bottom - area.top);
        const float rotation = (random01(k, 13, seed) * 2.0f - 1.0f) * maxRotationDegrees;
        const float scale = scaleMin + (scaleMax - scaleMin) * random01(k, 14, seed);
        append(result, transformed(drawing, x - centre.x, y - centre.y, rotation, scale, scale, centre));
    }
    return result;
}

Drawing jittered(const Drawing& drawing, float amount, int seed)
{
    Drawing result = drawing;
    std::int64_t n = 0;
    for (auto& path : result.paths)
        for (auto& p : path.points)
        {
            p.x += (random01(n, 21, seed) * 2.0f - 1.0f) * amount;
            p.y += (random01(n, 22, seed) * 2.0f - 1.0f) * amount;
            ++n;
        }
    return result;
}

Drawing wobbled(const Drawing& drawing, float amount, float wavelength, int seed)
{
    Drawing result;
    wavelength = std::max(1.0e-4f, wavelength);
    const float step = wavelength / 8.0f;
    std::int64_t stream = 31;
    for (const auto& path : drawing.paths)
    {
        Path out;
        out.closed = path.closed;
        const auto n = path.points.size();
        const size_t count = path.closed && n > 2 ? n : (n > 0 ? n - 1 : 0);
        float along = 0.0f;
        for (size_t i = 0; i < count; ++i)
        {
            const auto a = path.points[i], b = path.points[(i + 1) % n];
            const float length = std::hypot(b.x - a.x, b.y - a.y);
            if (length <= 0.0f)
                continue;
            const float nx = -(b.y - a.y) / length, ny = (b.x - a.x) / length; // sideways
            const int pieces = std::max(1, static_cast<int>(std::ceil(length / step)));
            // Each segment adds its start and inner points; an open path adds its very last point below.
            for (int k = 0; k < pieces; ++k)
            {
                const float t = static_cast<float>(k) / static_cast<float>(pieces);
                const float d = (noise1((along + length * t) / wavelength, stream, seed) * 2.0f - 1.0f) * amount;
                out.points.push_back({ a.x + (b.x - a.x) * t + nx * d, a.y + (b.y - a.y) * t + ny * d });
            }
            along += length;
        }
        if (! path.closed && n > 0)
        {
            const auto last = path.points.back();
            Point dir { 0.0f, 0.0f };
            if (n > 1)
            {
                const auto prev = path.points[n - 2];
                const float length = std::hypot(last.x - prev.x, last.y - prev.y);
                if (length > 0.0f)
                    dir = { -(last.y - prev.y) / length, (last.x - prev.x) / length };
            }
            const float d = (noise1(along / wavelength, stream, seed) * 2.0f - 1.0f) * amount;
            out.points.push_back({ last.x + dir.x * d, last.y + dir.y * d });
        }
        if (out.points.empty() && n > 0)
            out.points = path.points; // a single point stays where it is
        result.paths.push_back(std::move(out));
        ++stream;
    }
    return result;
}

Drawing mirrored(const Drawing& drawing, bool horizontal, float position, bool keepOriginal)
{
    Drawing result;
    if (keepOriginal)
        result = drawing;
    Drawing flipped = drawing;
    for (auto& path : flipped.paths)
        for (auto& p : path.points)
        {
            float& v = horizontal ? p.x : p.y;
            v = 2.0f * position - v;
        }
    append(result, flipped);
    return result;
}
} // namespace drawing

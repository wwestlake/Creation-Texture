#pragma once

#include <memory>
#include <vector>

// Drawing (docs/REQUIREMENTS.md section 5): what an Image Graph draws, kept apart from the pixels.
//
// A Drawing is shapes and paths only - no colour, no thickness. Shape nodes make one, modifier nodes turn one into
// another, and a Paint node runs a Brush along it to make an image. Making and changing a Drawing is cheap; pixels
// are made once, by Paint.
//
// Coordinates: (0, 0) is the canvas's top-left corner and (1, 1) its bottom-right, whatever its size, so a drawing
// renders at any resolution. On a canvas that is not square the drawing stretches with it, as an SVG viewBox would.
namespace drawing
{
struct Point
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Path
{
    std::vector<Point> points;
    bool closed = false; // the last point joins back to the first
};

struct Drawing
{
    std::vector<Path> paths;
};
using DrawingPtr = std::shared_ptr<const Drawing>;

// How a Drawing is painted. Sizes are fractions of the canvas's shorter side, so a brush looks the same at any
// resolution.
struct Brush
{
    float size = 0.02f;     // stamp diameter
    float hardness = 0.8f;  // 1 = a hard edge, 0 = soft all the way from the centre
    float spacing = 0.1f;   // distance between stamps, as a fraction of the diameter
    float red = 1.0f, green = 1.0f, blue = 1.0f; // linear light
    float opacity = 1.0f;
};
using BrushPtr = std::shared_ptr<const Brush>;

// Shapes. Closed shapes are polygons; curves are made of enough straight segments to look smooth at 4096 pixels.
Path line(Point a, Point b);
Path rectangle(float x, float y, float width, float height);
Path ellipse(Point centre, float radiusX, float radiusY, int segments = 128);
Path polygon(Point centre, float radius, int sides, float rotationDegrees);
} // namespace drawing

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

// An image used as a brush tip: linear RGBA. Its luminance times its alpha is how much paint each point lays down.
struct TipImage
{
    int width = 0;
    int height = 0;
    std::vector<float> rgba;
};

// How a Drawing is painted. Sizes are fractions of the canvas's shorter side, so a brush looks the same at any
// resolution.
struct Brush
{
    enum class Tip { round, square, chalk, bristle, image };          // the BrushTip enum, in this order
    enum class Rotation { fixed, followStroke, random };            // the BrushRotation enum, in this order

    Tip tip = Tip::round;
    std::shared_ptr<const TipImage> tipImage; // for Tip::image
    float size = 0.02f;     // stamp diameter
    float hardness = 0.8f;  // 1 = a hard edge, 0 = soft all the way from the centre
    float spacing = 0.1f;   // distance between stamps, as a fraction of the diameter
    float red = 1.0f, green = 1.0f, blue = 1.0f; // linear light
    float opacity = 1.0f;
    Rotation rotation = Rotation::fixed;
    float angle = 0.0f;         // degrees, added to the rotation
    float scatter = 0.0f;       // random offset of each stamp, as a fraction of the diameter
    float sizeJitter = 0.0f;    // 0..1: each stamp is up to this much smaller, at random
    float opacityJitter = 0.0f; // 0..1: each stamp is up to this much fainter, at random
    float taperStart = 0.0f;    // fraction of the path's length over which the stroke grows from nothing
    float taperEnd = 0.0f;      // fraction of the path's length over which it shrinks to nothing
    int seed = 1;               // the randomness (scatter, jitter, random rotation, chalk grain, bristles)
};
using BrushPtr = std::shared_ptr<const Brush>;

// One stamp of a brush, in pixels: centre, radius, the tip's angle (radians) and how much paint it lays down.
struct Stamp
{
    float x = 0.0f, y = 0.0f, radius = 0.0f, angle = 0.0f, opacity = 1.0f;
};

// Where a brush's stamps go along a path on a canvas of width x height pixels: one every `spacing` of the path's
// length from its start, one on the end of an open path, with taper, jitter, scatter and rotation applied.
// pathIndex keeps the randomness of different paths apart.
std::vector<Stamp> placeStamps(const Path& path, const Brush& brush, int width, int height, int pathIndex);

// Shapes. Closed shapes are polygons; curves are made of enough straight segments to look smooth at 4096 pixels.
Path line(Point a, Point b);
Path rectangle(float x, float y, float width, float height);
Path ellipse(Point centre, float radiusX, float radiusY, int segments = 128);
Path polygon(Point centre, float radius, int sides, float rotationDegrees);
} // namespace drawing

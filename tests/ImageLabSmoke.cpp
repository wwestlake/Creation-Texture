// Image Lab core smoke test. Expected values are worked out by hand from the W3C compositing formulas:
//   alpha_s = layerAlpha * opacity, alpha_o = alpha_s + alpha_b * (1 - alpha_s)
//   mixed = (1 - alpha_b) * Cs + alpha_b * B(Cb, Cs), C = (alpha_s * mixed + alpha_b * Cb * (1 - alpha_s)) / alpha_o
// Failures print and return 1 (never throw out of main - that raises a blocking abort dialog).

#include "NoCrashDialogs.h"

#include <ImageLabCore.h>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void expectPixel(const std::string& what, const float* got, float r, float g, float b, float a)
{
    const float want[] = { r, g, b, a };
    for (int i = 0; i < 4; ++i)
    {
        if (std::abs(got[i] - want[i]) > 1.0e-4f)
        {
            std::cerr << "FAIL " << what << ": got (" << got[0] << ", " << got[1] << ", " << got[2] << ", " << got[3]
                      << ") want (" << r << ", " << g << ", " << b << ", " << a << ")\n";
            ++failures;
            return;
        }
    }
    std::cout << "ok   " << what << "\n";
}

void compositeOne(image_lab::Compositor& compositor, const std::string& what, std::vector<float> canvas,
                  const std::vector<float>& layer, image_lab::BlendMode mode, float opacity,
                  float r, float g, float b, float a)
{
    juce::String error;
    if (! compositor.composite(canvas.data(), layer.data(), 1, mode, opacity, error))
    {
        std::cerr << "FAIL " << what << ": " << error << "\n";
        ++failures;
        return;
    }
    expectPixel(what, canvas.data(), r, g, b, a);
}
}

int main()
{
    disableCrashDialogs();
    image_lab::Compositor compositor;
    if (! compositor.isReady())
    {
        std::cerr << "FAIL FRust core did not load: " << compositor.getError() << "\n";
        return 1;
    }
    std::cout << "ok   FRust core compiled from embedded source\n";

    const std::vector<float> backdrop { 0.2f, 0.4f, 0.6f, 1.0f };
    const std::vector<float> halfRed { 1.0f, 0.0f, 0.0f, 0.5f };

    compositeOne(compositor, "normal, half-alpha red over opaque", backdrop, halfRed, image_lab::BlendMode::normal, 1.0f,
                 0.6f, 0.2f, 0.3f, 1.0f);
    compositeOne(compositor, "multiply, half-alpha red over opaque", backdrop, halfRed, image_lab::BlendMode::multiply, 1.0f,
                 0.2f, 0.2f, 0.3f, 1.0f);
    compositeOne(compositor, "normal at 50% opacity", backdrop, halfRed, image_lab::BlendMode::normal, 0.5f,
                 0.4f, 0.3f, 0.45f, 1.0f);
    compositeOne(compositor, "opaque grey over transparent canvas", { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f, 1.0f },
                 image_lab::BlendMode::normal, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f);

    // Two pixels in one call: the second pixel only comes out right if FRust steps f32 pointers by element.
    {
        std::vector<float> canvas { 0.2f, 0.4f, 0.6f, 1.0f, /* pixel 2 */ 0.0f, 0.0f, 1.0f, 1.0f };
        const std::vector<float> layer { 1.0f, 0.0f, 0.0f, 0.5f, /* pixel 2 */ 0.0f, 1.0f, 0.0f, 1.0f };
        juce::String error;
        if (! compositor.composite(canvas.data(), layer.data(), 2, image_lab::BlendMode::screen, 1.0f, error))
        {
            std::cerr << "FAIL two-pixel screen: " << error << "\n";
            ++failures;
        }
        else
        {
            // Pixel 1 screen: B = b + s - b*s = (1.0, 0.4, 0.6); C = 0.5*B + 0.5*Cb = (0.6, 0.4, 0.6).
            expectPixel("screen, pixel 1", canvas.data(), 0.6f, 0.4f, 0.6f, 1.0f);
            // Pixel 2 screen, opaque over opaque: C = B = (0, 1, 1).
            expectPixel("screen, pixel 2", canvas.data() + 4, 0.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    // sRGB -> linear -> sRGB round trip keeps 8-bit colours.
    {
        juce::Image image(juce::Image::ARGB, 1, 1, true);
        image.setPixelAt(0, 0, juce::Colour(128, 64, 255));
        const auto layer = image_lab::layerFromImage(image, "round trip");
        const auto back = image_lab::imageFromPixels(layer.pixels, 1, 1).getPixelAt(0, 0);
        if (std::abs(static_cast<int>(back.getRed()) - 128) > 1 || std::abs(static_cast<int>(back.getGreen()) - 64) > 1
            || back.getBlue() != 255 || back.getAlpha() != 255)
        {
            std::cerr << "FAIL sRGB round trip: got " << back.toDisplayString(true) << "\n";
            ++failures;
        }
        else
        {
            std::cout << "ok   sRGB round trip\n";
        }
        // sRGB 128 is about 0.2159 in linear light.
        if (std::abs(layer.pixels[0] - 0.2159f) > 0.001f)
        {
            std::cerr << "FAIL sRGB 128 -> linear: got " << layer.pixels[0] << "\n";
            ++failures;
        }
    }

    // Flatten skips hidden layers.
    {
        image_lab::Layer bottom;
        bottom.width = bottom.height = 1;
        bottom.pixels = { 0.2f, 0.4f, 0.6f, 1.0f };
        image_lab::Layer hidden = bottom;
        hidden.pixels = { 1.0f, 1.0f, 1.0f, 1.0f };
        hidden.visible = false;

        std::vector<float> out;
        juce::String error;
        if (! image_lab::flatten({ &bottom, &hidden }, 1, 1, compositor, out, error))
        {
            std::cerr << "FAIL flatten: " << error << "\n";
            ++failures;
        }
        else
        {
            expectPixel("flatten skips a hidden layer", out.data(), 0.2f, 0.4f, 0.6f, 1.0f);
        }
    }

    if (failures > 0)
    {
        std::cerr << failures << " Image Lab check(s) FAILED\n";
        return 1;
    }
    std::cout << "Image Lab smoke passed.\n";
    return 0;
}

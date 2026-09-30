// Surface map maths smoke test. Expected values are worked out by hand before running.
// Failures print and return 1 (crash dialogs are off - see NoCrashDialogs.h).

#include "NoCrashDialogs.h"

#include <SurfaceMaps.h>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void expect(const std::string& what, const std::vector<float>& got, const std::vector<float>& want, float tolerance = 1.0e-4f)
{
    bool ok = got.size() >= want.size();
    for (size_t i = 0; ok && i < want.size(); ++i)
        ok = std::abs(got[i] - want[i]) <= tolerance;
    if (ok)
    {
        std::cout << "ok   " << what << "\n";
        return;
    }
    std::cerr << "FAIL " << what << ": got";
    for (size_t i = 0; i < want.size() && i < got.size(); ++i)
        std::cerr << " " << got[i];
    std::cerr << "  want";
    for (float v : want)
        std::cerr << " " << v;
    std::cerr << "\n";
    ++failures;
}

void check(const std::string& what, bool ok, const juce::String& error)
{
    if (! ok)
    {
        std::cerr << "FAIL " << what << ": " << error << "\n";
        ++failures;
    }
}
}

int main()
{
    disableCrashDialogs();
    surface_maps::Engine engine;
    if (! engine.isReady())
    {
        std::cerr << "FAIL surface map FRust did not load: " << engine.getError() << "\n";
        return 1;
    }
    std::cout << "ok   surface map FRust compiled from embedded source\n";
    juce::String error;

    // Box blur radius 1 is a horizontal then a vertical box blur, so it needs an image at least 3 x 3. On a 5 x 5
    // image whose rows are all the same, the vertical pass changes nothing and each row blurs like a 1-D signal.
    {
        auto rows = [](std::vector<float> row) {
            std::vector<float> image;
            for (int y = 0; y < 5; ++y)
                image.insert(image.end(), row.begin(), row.end());
            return image;
        };
        std::vector<float> out;
        check("blur rows", engine.boxBlur(rows({ 0, 0, 1, 0, 0 }), out, 5, 5, 1, error), error);
        expect("box blur, spike in the middle", { out.begin() + 10, out.begin() + 15 }, { 0.0f, 1.0f / 3, 1.0f / 3, 1.0f / 3, 0.0f });
        check("blur wrap", engine.boxBlur(rows({ 1, 0, 0, 0, 0 }), out, 5, 5, 1, error), error);
        expect("box blur wraps around the edge", { out.begin() + 10, out.begin() + 15 }, { 1.0f / 3, 1.0f / 3, 0.0f, 0.0f, 1.0f / 3 });

        // Vertical: only row 2 is lit, so rows 1-3 become 1/3 and rows 0 and 4 stay 0 (column 0 shown).
        std::vector<float> lit(25, 0.0f);
        for (int x = 0; x < 5; ++x)
            lit[static_cast<size_t>(10 + x)] = 1.0f;
        check("blur column", engine.boxBlur(lit, out, 5, 5, 1, error), error);
        expect("box blur, vertical", { out[0], out[5], out[10], out[15], out[20] }, { 0.0f, 1.0f / 3, 1.0f / 3, 1.0f / 3, 0.0f });
    }

    // Normal from a ramp h = 0.1 x (5 x 5). At the centre the Sobel slope is 0.1 per pixel, so with strength 1:
    // n = normalize(-0.1, 0, 1) = (-0.099504, 0, 0.995037) -> encoded (0.450248, 0.5, 0.997519).
    {
        std::vector<float> heightMap(25), normal;
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x)
                heightMap[static_cast<size_t>(y * 5 + x)] = 0.1f * static_cast<float>(x);
        check("normal x", engine.normalFromHeight(heightMap, normal, 5, 5, 1.0f, false, error), error);
        expect("normal of a rise to the right faces left", { normal.begin() + 48, normal.begin() + 52 },
               { 0.450248f, 0.5f, 0.997519f, 1.0f });

        // h = 0.1 y: rising downward in the image. OpenGL green = up = -image y, so green goes above 0.5;
        // DirectX flips it.
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x)
                heightMap[static_cast<size_t>(y * 5 + x)] = 0.1f * static_cast<float>(y);
        check("normal y", engine.normalFromHeight(heightMap, normal, 5, 5, 1.0f, false, error), error);
        expect("normal, rise downward, OpenGL", { normal.begin() + 48, normal.begin() + 52 }, { 0.5f, 0.549752f, 0.997519f, 1.0f });
        check("normal y dx", engine.normalFromHeight(heightMap, normal, 5, 5, 1.0f, true, error), error);
        expect("normal, rise downward, DirectX", { normal.begin() + 48, normal.begin() + 52 }, { 0.5f, 0.450248f, 0.997519f, 1.0f });
    }

    // Occlusion: surroundings 0.2 higher, strength 2 -> 1 - 0.4 = 0.6; surroundings lower -> unchanged.
    {
        std::vector<float> ao { 1.0f, 1.0f };
        check("occlusion", engine.occlusionStep(ao, { 0.5f, 0.5f }, { 0.7f, 0.3f }, 2.0f, error), error);
        expect("occlusion darkens crevices only", ao, { 0.6f, 1.0f });
    }

    // De-light: lighting 0.5, mean 0.25, amount 1 -> gain 0.5; rgb 0.2 -> 0.1, alpha kept.
    {
        std::vector<float> out;
        check("delight", engine.delight({ 0.2f, 0.2f, 0.2f, 0.8f }, { 0.5f }, out, 0.25f, 1.0f, error), error);
        expect("de-light evens out lighting", out, { 0.1f, 0.1f, 0.1f, 0.8f });
    }

    // Specular: level 0.5 + contrast 4 * (0.6 - 0.5) = 0.9; clamped at 1.
    {
        std::vector<float> out;
        check("specular", engine.specular({ 0.6f, 0.9f }, { 0.5f, 0.5f }, out, 0.5f, 4.0f, error), error);
        expect("specular from fine detail", out, { 0.9f, 1.0f });
    }

    // Luminance of pure red; normalize [2, 4, 3] -> [0, 1, 0.5].
    {
        std::vector<float> gray;
        check("luminance", engine.luminance({ 1.0f, 0.0f, 0.0f, 1.0f }, gray, error), error);
        expect("luminance of red", gray, { 0.2126f });
        std::vector<float> values { 2.0f, 4.0f, 3.0f };
        check("normalize", engine.normalize(values, error), error);
        expect("normalize to 0..1", values, { 0.0f, 1.0f, 0.5f });
    }

    // Whole pipeline on a flat grey 16 x 16 image: flat height 0.5, straight-up normal, no occlusion, colour unchanged.
    {
        std::vector<float> source(16 * 16 * 4, 0.4f);
        for (size_t i = 3; i < source.size(); i += 4)
            source[i] = 1.0f;
        surface_maps::Maps maps;
        check("pipeline", engine.compute(source, 16, 16, {}, maps, error), error);
        if (maps.isValid())
        {
            expect("flat photo -> flat height", { maps.heightMap[0], maps.heightMap[135] }, { 0.5f, 0.5f });
            expect("flat photo -> normal straight up", { maps.normal.begin() + 400, maps.normal.begin() + 404 }, { 0.5f, 0.5f, 1.0f, 1.0f });
            expect("flat photo -> no occlusion", { maps.occlusion[77] }, { 1.0f });
            expect("flat photo -> diffuse unchanged", { maps.diffuse.begin() + 40, maps.diffuse.begin() + 44 }, { 0.4f, 0.4f, 0.4f, 1.0f });
        }
    }

    if (failures > 0)
    {
        std::cerr << failures << " surface map check(s) FAILED\n";
        return 1;
    }
    std::cout << "Surface map smoke passed.\n";
    return 0;
}

#pragma once

#include "Drawing.h"

#include <map>
#include <string>

// Draw Script (docs/REQUIREMENTS.md section 5): a Drawing written as commands instead of wired nodes. It is how the
// AI draws complex things, and anyone can write it by hand. The full language is in docs/DRAW_SCRIPT.md.
//
// A pen moves over the canvas - (0, 0) top-left, (1, 1) bottom-right - with a heading in degrees (0 = right,
// clockwise, so 90 = down). Commands draw lines from the pen, add shapes, and loop; numbers can be expressions using
// variables, the loop index and the graph's own Variables.
namespace draw_script
{
struct Result
{
    drawing::Drawing drawing;
    std::string error; // empty when the script ran; otherwise "line N: what is wrong"
};

// Runs a script. `variables` are names the script can read (the graph's Variables, by id); seed starts `random`.
Result run(const std::string& script, const std::map<std::string, double>& variables, int seed);
} // namespace draw_script

# Draw Script

Draw Script is a small command language for drawing by instruction instead of by mouse. It is how the AI draws
complex things in Djehuti Texture, and anyone can write it by hand. A **Draw Script** node in the Image Graph runs a
script and outputs a **Drawing**: shapes and paths with no paint. Wire it into **Paint** with a **Brush** to make
an image (requirements section 5).

## The canvas and the pen

- **Coordinates:** (0, 0) is the canvas's top-left corner and (1, 1) its bottom-right, at any resolution.
- **The pen** has a position (it starts at 0.5, 0.5) and a **heading** in degrees. Heading 0 points right, and
  angles turn **clockwise** because y runs down: 90 points down, 180 left, 270 up.
- **Paths:** moving the pen with `forward`, `line`, `arc` or `curve` draws a path. `move`, `jump` and `pop` start a
  new path.
- **Shapes** (`circle`, `rect`...) are separate paths. They don't move the pen.

## Commands

One command per line, or separate commands with `;`. Arguments are separated by spaces or commas. `#` starts a
comment.

| Command | Does |
|---|---|
| `move x y` | Lift the pen and put it at (x, y). |
| `line x y` | Draw a line to (x, y). |
| `forward d` / `fd d` | Draw d along the heading. |
| `back d` / `bk d` | Draw d backwards. |
| `jump d` | Move d along the heading without drawing. |
| `turn a` / `right a` / `rt a` | Turn clockwise by a degrees. |
| `left a` / `lt a` | Turn anticlockwise. |
| `heading a` | Set the heading. |
| `arc r a` | Draw an arc of radius r, turning a degrees: positive turns right, negative left. The pen ends facing along the arc. |
| `curve cx1 cy1 cx2 cy2 x y` | A smooth curve to (x, y), pulled towards the two control points. |
| `close` | Join the current path back to its start. |
| `circle x y r` | A circle. |
| `ellipse x y rx ry` | An ellipse. |
| `rect x y w h` | A rectangle from its top-left corner. |
| `polygon x y r sides [rotation]` | A regular polygon. Rotation 0 puts a corner straight up. |
| `star x y outer inner points [rotation]` | A star. Rotation 0 puts a point straight up. |
| `push` | Save the pen's position, heading and scale. |
| `pop` | Go back to the last saved pen and start a new path. |
| `scale f` | Multiply every later distance (`forward`, `back`, `jump`, `arc` radius) by f. It is saved by `push`. |
| `seed n` | Restart `random` from seed n. |

## Structure

```
let size = 0.2              # a variable here; inside a loop or procedure it is local to it
set count = count + 1       # change a variable that already exists, wherever it is

repeat 12 {                 # i counts 0, 1, 2 ... 11
  circle 0.5 + 0.3 * cos(i * 30), 0.5 + 0.3 * sin(i * 30), 0.03
}
repeat 4 as row {           # name the counter (useful for loops inside loops)
  repeat 4 as col { rect 0.1 + col * 0.2, 0.1 + row * 0.2, 0.15, 0.15 }
}

if size > 0.1 {
  circle 0.5 0.5 size
} else if size > 0 {
  circle 0.5 0.5 0.1
} else {
  rect 0.4 0.4 0.2 0.2
}

def branch len depth {      # a procedure; it may call itself
  forward len
  if depth > 0 {
    push; turn -25; branch len * 0.7, depth - 1; pop
    push; turn 25;  branch len * 0.7, depth - 1; pop
  }
}
move 0.5 0.95
heading 270
branch 0.25 8
```

## Numbers and expressions

- **Operators:** `+ - * / %`, comparisons `< > <= >= == !=` (true is 1, false 0), `and`, `or`, `not`, and parentheses.
- **Functions:** `sin(a)`, `cos(a)`, `tan(a)` (degrees), `atan2(y, x)` (degrees), `sqrt`, `abs`, `floor`, `ceil`,
  `round`, `pow(a, b)`, `min(a, b)`, `max(a, b)`, `clamp(v, lo, hi)`, `lerp(a, b, t)`, `random()` (0..1) and
  `random(lo, hi)`. The constant `pi` is also available.
- **Randomness** comes from the node's `seed` setting, or `seed n` in the script. The same seed always draws the same
  thing.
- **Minus signs:** in a command's arguments, a `-` with a space before it and none after starts a new argument.
  `move 0.5 -0.2` is two numbers; `0.5 - 0.2` and `0.5-0.2` are one.

## The graph's Variables

A script can read the graph's number, integer and toggle Variables by id (toggles are 1 or 0). Toggles come in as 0
or 1. If the graph has a param `petals`, then `repeat petals { ... }` uses it. A param's outside value (from an
Automation or the LLM) wins, so the same script draws differently when its params change.

## Limits

These stop a mistake instead of freezing the app. Each one gives an error naming the line.
- 5,000,000 steps.
- 4,000,000 points.
- 200 levels of procedures calling procedures.

Errors look like `line 3: unknown command 'forwrd'` and show on the node.

#include "DrawScript.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

namespace draw_script
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double toRadians = pi / 180.0;
constexpr long long maxSteps = 5000000;   // statements run, so a runaway loop stops instead of hanging the app
constexpr size_t maxPoints = 4000000;     // points in the drawing
constexpr int maxDepth = 200;             // procedures calling procedures

struct ScriptError
{
    int line;
    std::string message;
};

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

//==============================================================================
// Tokens
enum class Tok { number, name, op, newline, end };

struct Token
{
    Tok kind = Tok::end;
    std::string text;
    double value = 0.0;
    int line = 1;
    bool spaceBefore = false; // separates command arguments: "move 0.5 -0.2" is two numbers, "0.5 - 0.2" one
};

std::vector<Token> tokenize(const std::string& s)
{
    std::vector<Token> tokens;
    int line = 1;
    bool space = true;
    size_t i = 0;
    const size_t n = s.size();
    auto push = [&](Tok kind, std::string text, double value = 0.0) {
        tokens.push_back({ kind, std::move(text), value, line, space });
        space = false;
    };
    while (i < n)
    {
        const char c = s[i];
        if (c == '#')
        {
            while (i < n && s[i] != '\n')
                ++i;
        }
        else if (c == '\n' || c == ';')
        {
            push(Tok::newline, "\\n");
            if (c == '\n')
                ++line;
            ++i;
            space = true;
        }
        else if (std::isspace(static_cast<unsigned char>(c)))
        {
            ++i;
            space = true;
        }
        else if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(s[i + 1]))))
        {
            char* endPtr = nullptr;
            const double v = std::strtod(s.c_str() + i, &endPtr);
            const auto length = static_cast<size_t>(endPtr - (s.c_str() + i));
            push(Tok::number, s.substr(i, length), v);
            i += length;
        }
        else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
        {
            size_t j = i;
            while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_'))
                ++j;
            push(Tok::name, s.substr(i, j - i));
            i = j;
        }
        else
        {
            const std::string two = s.substr(i, 2);
            if (two == "<=" || two == ">=" || two == "==" || two == "!=")
            {
                push(Tok::op, two);
                i += 2;
            }
            else if (std::string("+-*/%(),{}<>=").find(c) != std::string::npos)
            {
                push(Tok::op, std::string(1, c));
                ++i;
            }
            else
                throw ScriptError { line, std::string("unexpected character '") + c + "'" };
        }
    }
    push(Tok::end, "end of script");
    return tokens;
}

//==============================================================================
// Syntax tree
struct Expr;
using ExprPtr = std::unique_ptr<Expr>;
struct Expr
{
    enum class Kind { number, variable, unary, binary, call } kind = Kind::number;
    double value = 0.0;
    std::string name; // variable, operator or function
    std::vector<ExprPtr> args;
    int line = 1;
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;
using Block = std::vector<StmtPtr>;
struct Stmt
{
    enum class Kind { command, let, set, repeat, ifElse, def } kind = Kind::command;
    std::string name; // command, variable, procedure, or the loop's index name
    std::vector<ExprPtr> args;
    std::vector<std::string> params; // a procedure's parameter names
    Block body, otherwise;
    int line = 1;
};

class Parser
{
public:
    explicit Parser(std::vector<Token> t) : tokens(std::move(t)) {}

    Block parseScript() { return block(false); }

private:
    const Token& peek() const { return tokens[pos]; }
    const Token& take() { return tokens[pos < tokens.size() - 1 ? pos++ : pos]; }
    bool atOp(const char* text) const { return peek().kind == Tok::op && peek().text == text; }
    bool atName(const char* text) const { return peek().kind == Tok::name && lower(peek().text) == text; }
    void expectOp(const char* text)
    {
        if (! atOp(text))
            throw ScriptError { peek().line, std::string("expected '") + text + "' but found '" + peek().text + "'" };
        take();
    }
    void skipNewlines()
    {
        while (peek().kind == Tok::newline)
            take();
    }

    Block block(bool braced)
    {
        Block statements;
        if (braced)
            expectOp("{");
        for (;;)
        {
            skipNewlines();
            if (braced && atOp("}"))
            {
                take();
                break;
            }
            if (peek().kind == Tok::end)
            {
                if (braced)
                    throw ScriptError { peek().line, "a '{' block is missing its closing '}'" };
                break;
            }
            statements.push_back(statement());
            if (! (peek().kind == Tok::newline || peek().kind == Tok::end || atOp("}")))
                throw ScriptError { peek().line, "unexpected '" + peek().text + "' - one command per line" };
        }
        return statements;
    }

    StmtPtr statement()
    {
        const Token& first = peek();
        if (first.kind != Tok::name)
            throw ScriptError { first.line, "expected a command but found '" + first.text + "'" };
        auto s = std::make_unique<Stmt>();
        s->line = first.line;
        const auto word = lower(take().text);

        if (word == "let" || word == "set")
        {
            if (peek().kind != Tok::name)
                throw ScriptError { s->line, "'" + word + "' needs a name: let size = 0.1" };
            // let makes a variable here (inside a loop or procedure it is local to it); set changes one that exists.
            s->kind = word == "let" ? Stmt::Kind::let : Stmt::Kind::set;
            s->name = take().text;
            expectOp("=");
            s->args.push_back(expression());
            return s;
        }
        if (word == "repeat")
        {
            s->kind = Stmt::Kind::repeat;
            s->args.push_back(expression());
            s->name = "i";
            if (atName("as"))
            {
                take();
                if (peek().kind != Tok::name)
                    throw ScriptError { s->line, "'repeat ... as' needs a name" };
                s->name = take().text;
            }
            s->body = block(true);
            return s;
        }
        if (word == "def")
        {
            if (peek().kind != Tok::name)
                throw ScriptError { s->line, "'def' needs a name: def branch length { ... }" };
            s->kind = Stmt::Kind::def;
            s->name = lower(take().text);
            while (peek().kind == Tok::name)
                s->params.push_back(take().text);
            s->body = block(true);
            return s;
        }
        if (word == "if")
        {
            s->kind = Stmt::Kind::ifElse;
            s->args.push_back(expression());
            s->body = block(true);
            const auto save = pos;
            skipNewlines();
            if (atName("else"))
            {
                take();
                if (atName("if"))
                    s->otherwise.push_back(statement());
                else
                    s->otherwise = block(true);
            }
            else
                pos = save;
            return s;
        }

        // A command and its arguments, separated by spaces or commas.
        s->kind = Stmt::Kind::command;
        s->name = word;
        while (! (peek().kind == Tok::newline || peek().kind == Tok::end || atOp("}")))
        {
            if (atOp(","))
            {
                take();
                continue;
            }
            argumentMode = true;
            s->args.push_back(expression());
            argumentMode = false;
        }
        return s;
    }

    // In a command's arguments, "+" or "-" with a space before it and none after starts a new argument.
    bool startsNewArgument() const
    {
        if (! argumentMode || depth > 0 || ! (atOp("+") || atOp("-")) || ! peek().spaceBefore)
            return false;
        return pos + 1 < tokens.size() && ! tokens[pos + 1].spaceBefore;
    }

    ExprPtr binary(std::string op, ExprPtr a, ExprPtr b, int line)
    {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::binary;
        e->name = std::move(op);
        e->line = line;
        e->args.push_back(std::move(a));
        e->args.push_back(std::move(b));
        return e;
    }

    ExprPtr expression() { return orExpr(); }

    ExprPtr orExpr()
    {
        auto left = andExpr();
        while (atName("or"))
        {
            const int line = take().line;
            left = binary("or", std::move(left), andExpr(), line);
        }
        return left;
    }

    ExprPtr andExpr()
    {
        auto left = comparison();
        while (atName("and"))
        {
            const int line = take().line;
            left = binary("and", std::move(left), comparison(), line);
        }
        return left;
    }

    ExprPtr comparison()
    {
        auto left = additive();
        while (atOp("<") || atOp(">") || atOp("<=") || atOp(">=") || atOp("==") || atOp("!="))
        {
            const auto& t = take();
            left = binary(t.text, std::move(left), additive(), t.line);
        }
        return left;
    }

    ExprPtr additive()
    {
        auto left = multiplicative();
        while ((atOp("+") || atOp("-")) && ! startsNewArgument())
        {
            const auto& t = take();
            left = binary(t.text, std::move(left), multiplicative(), t.line);
        }
        return left;
    }

    ExprPtr multiplicative()
    {
        auto left = unary();
        while (atOp("*") || atOp("/") || atOp("%"))
        {
            const auto& t = take();
            left = binary(t.text, std::move(left), unary(), t.line);
        }
        return left;
    }

    ExprPtr unary()
    {
        if (atOp("-") || atOp("+") || atName("not"))
        {
            const auto& t = take();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::unary;
            e->name = lower(t.text);
            e->line = t.line;
            e->args.push_back(unary());
            return e;
        }
        return primary();
    }

    ExprPtr primary()
    {
        const Token& t = peek();
        auto e = std::make_unique<Expr>();
        e->line = t.line;
        if (t.kind == Tok::number)
        {
            e->value = take().value;
            return e;
        }
        if (atOp("("))
        {
            take();
            ++depth;
            auto inner = expression();
            --depth;
            expectOp(")");
            return inner;
        }
        if (t.kind == Tok::name)
        {
            e->name = take().text;
            if (atOp("(") && ! peek().spaceBefore)
            {
                take();
                ++depth;
                e->kind = Expr::Kind::call;
                e->name = lower(e->name);
                if (! atOp(")"))
                    for (;;)
                    {
                        e->args.push_back(expression());
                        if (atOp(","))
                        {
                            take();
                            continue;
                        }
                        break;
                    }
                --depth;
                expectOp(")");
                return e;
            }
            e->kind = Expr::Kind::variable;
            return e;
        }
        throw ScriptError { t.line, "expected a number or a name but found '" + t.text + "'" };
    }

    std::vector<Token> tokens;
    size_t pos = 0;
    bool argumentMode = false;
    int depth = 0;
};

//==============================================================================
// Running a script
class Interpreter
{
public:
    Interpreter(const std::map<std::string, double>& graphVariables, int seedValue) : globals(graphVariables), seed(seedValue)
    {
        scopes.emplace_back();
    }

    drawing::Drawing run(const Block& script)
    {
        execute(script);
        endPath();
        return std::move(result);
    }

private:
    struct Pen
    {
        double x = 0.5, y = 0.5, heading = 0.0, scale = 1.0;
    };

    // ---- values
    double variable(const std::string& name, int line) const
    {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
        {
            auto found = scope->find(name);
            if (found != scope->end())
                return found->second;
        }
        auto global = globals.find(name);
        if (global != globals.end())
            return global->second;
        if (name == "pi")
            return pi;
        throw ScriptError { line, "unknown name '" + name + "'" };
    }

    void assign(const std::string& name, double value)
    {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
        {
            auto found = scope->find(name);
            if (found != scope->end())
            {
                found->second = value;
                return;
            }
        }
        scopes.back()[name] = value;
    }

    double random01()
    {
        auto h = static_cast<std::uint64_t>(randomCount++) * 0x9E3779B97F4A7C15ull ^ static_cast<std::uint64_t>(seed) * 0x165667B19E3779F9ull
               ^ 0xC2B2AE3D27D4EB4Full;
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdull;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ull;
        h ^= h >> 33;
        return static_cast<double>(h >> 11) / static_cast<double>(1ull << 53);
    }

    double eval(const Expr& e)
    {
        switch (e.kind)
        {
            case Expr::Kind::number: return e.value;
            case Expr::Kind::variable: return variable(e.name, e.line);
            case Expr::Kind::unary:
            {
                const double v = eval(*e.args[0]);
                if (e.name == "-") return -v;
                if (e.name == "not") return v == 0.0 ? 1.0 : 0.0;
                return v;
            }
            case Expr::Kind::binary:
            {
                if (e.name == "and") return eval(*e.args[0]) != 0.0 && eval(*e.args[1]) != 0.0 ? 1.0 : 0.0;
                if (e.name == "or") return eval(*e.args[0]) != 0.0 || eval(*e.args[1]) != 0.0 ? 1.0 : 0.0;
                const double a = eval(*e.args[0]), b = eval(*e.args[1]);
                if (e.name == "+") return a + b;
                if (e.name == "-") return a - b;
                if (e.name == "*") return a * b;
                if (e.name == "/" || e.name == "%")
                {
                    if (b == 0.0)
                        throw ScriptError { e.line, "division by zero" };
                    return e.name == "/" ? a / b : std::fmod(a, b);
                }
                if (e.name == "<") return a < b ? 1.0 : 0.0;
                if (e.name == ">") return a > b ? 1.0 : 0.0;
                if (e.name == "<=") return a <= b ? 1.0 : 0.0;
                if (e.name == ">=") return a >= b ? 1.0 : 0.0;
                if (e.name == "==") return a == b ? 1.0 : 0.0;
                if (e.name == "!=") return a != b ? 1.0 : 0.0;
                throw ScriptError { e.line, "unknown operator '" + e.name + "'" };
            }
            case Expr::Kind::call: return call(e);
        }
        return 0.0;
    }

    double call(const Expr& e)
    {
        std::vector<double> a;
        for (const auto& arg : e.args)
            a.push_back(eval(*arg));
        auto need = [&](size_t count) {
            if (a.size() != count)
                throw ScriptError { e.line, e.name + "() takes " + std::to_string(count) + " value(s), not " + std::to_string(a.size()) };
        };
        const auto& f = e.name;
        if (f == "sin") { need(1); return std::sin(a[0] * toRadians); }
        if (f == "cos") { need(1); return std::cos(a[0] * toRadians); }
        if (f == "tan") { need(1); return std::tan(a[0] * toRadians); }
        if (f == "atan2") { need(2); return std::atan2(a[0], a[1]) / toRadians; }
        if (f == "sqrt") { need(1); return std::sqrt(std::max(0.0, a[0])); }
        if (f == "abs") { need(1); return std::abs(a[0]); }
        if (f == "floor") { need(1); return std::floor(a[0]); }
        if (f == "ceil") { need(1); return std::ceil(a[0]); }
        if (f == "round") { need(1); return std::round(a[0]); }
        if (f == "pow") { need(2); return std::pow(a[0], a[1]); }
        if (f == "min") { need(2); return std::min(a[0], a[1]); }
        if (f == "max") { need(2); return std::max(a[0], a[1]); }
        if (f == "clamp") { need(3); return std::clamp(a[0], std::min(a[1], a[2]), std::max(a[1], a[2])); }
        if (f == "lerp") { need(3); return a[0] + (a[1] - a[0]) * a[2]; }
        if (f == "random")
        {
            if (a.empty()) return random01();
            need(2);
            return a[0] + (a[1] - a[0]) * random01();
        }
        throw ScriptError { e.line, "unknown function '" + f + "()'" };
    }

    // ---- the pen
    void addPoint(double x, double y)
    {
        if (++pointCount > maxPoints)
            throw ScriptError { currentLine, "the drawing has more than " + std::to_string(maxPoints) + " points" };
        current.points.push_back({ static_cast<float>(x), static_cast<float>(y) });
    }

    void lineTo(double x, double y)
    {
        if (current.points.empty())
            addPoint(pen.x, pen.y);
        addPoint(x, y);
        pen.x = x;
        pen.y = y;
    }

    void endPath()
    {
        if (current.points.size() >= 2)
            result.paths.push_back(std::move(current));
        current = {};
    }

    void addShape(drawing::Path path)
    {
        pointCount += path.points.size();
        if (pointCount > maxPoints)
            throw ScriptError { currentLine, "the drawing has more than " + std::to_string(maxPoints) + " points" };
        result.paths.push_back(std::move(path));
    }

    // ---- statements
    void execute(const Block& block)
    {
        for (const auto& s : block)
        {
            if (++steps > maxSteps)
                throw ScriptError { s->line, "the script runs too long (more than " + std::to_string(maxSteps) + " steps) - is a repeat too big?" };
            currentLine = s->line;
            switch (s->kind)
            {
                case Stmt::Kind::let: scopes.back()[s->name] = eval(*s->args[0]); break;
                case Stmt::Kind::set: assign(s->name, eval(*s->args[0])); break;
                case Stmt::Kind::repeat:
                {
                    const double count = std::floor(eval(*s->args[0]));
                    if (count > static_cast<double>(maxSteps))
                        throw ScriptError { s->line, "repeat count is too big" };
                    for (long long i = 0; i < static_cast<long long>(count); ++i)
                    {
                        scopes.emplace_back();
                        scopes.back()[s->name] = static_cast<double>(i);
                        execute(s->body);
                        scopes.pop_back();
                    }
                    break;
                }
                case Stmt::Kind::ifElse:
                    if (eval(*s->args[0]) != 0.0)
                        execute(s->body);
                    else
                        execute(s->otherwise);
                    break;
                case Stmt::Kind::def: procedures[s->name] = s.get(); break;
                case Stmt::Kind::command: command(*s); break;
            }
        }
    }

    void command(const Stmt& s)
    {
        std::vector<double> a;
        for (const auto& arg : s.args)
            a.push_back(eval(*arg));
        const auto& c = s.name;
        auto need = [&](size_t least, size_t most, const char* usage) {
            if (a.size() < least || a.size() > most)
                throw ScriptError { s.line, std::string("'") + c + "' takes " + usage };
        };
        auto f = [](double v) { return static_cast<float>(v); };
        const double headingRadians = pen.heading * toRadians;
        const double dx = std::cos(headingRadians), dy = std::sin(headingRadians);

        if (c == "move") { need(2, 2, "x y"); endPath(); pen.x = a[0]; pen.y = a[1]; }
        else if (c == "line") { need(2, 2, "x y"); lineTo(a[0], a[1]); }
        else if (c == "forward" || c == "fd") { need(1, 1, "a distance"); lineTo(pen.x + dx * a[0] * pen.scale, pen.y + dy * a[0] * pen.scale); }
        else if (c == "back" || c == "bk") { need(1, 1, "a distance"); lineTo(pen.x - dx * a[0] * pen.scale, pen.y - dy * a[0] * pen.scale); }
        else if (c == "jump") { need(1, 1, "a distance"); endPath(); pen.x += dx * a[0] * pen.scale; pen.y += dy * a[0] * pen.scale; }
        else if (c == "turn" || c == "right" || c == "rt") { need(1, 1, "an angle in degrees"); pen.heading += a[0]; }
        else if (c == "left" || c == "lt") { need(1, 1, "an angle in degrees"); pen.heading -= a[0]; }
        else if (c == "heading") { need(1, 1, "an angle in degrees"); pen.heading = a[0]; }
        else if (c == "arc")
        {
            need(2, 2, "a radius and a turn in degrees (positive turns right)");
            const double r = a[0] * pen.scale, sweep = a[1];
            if (sweep != 0.0 && r > 0.0)
            {
                const double side = sweep > 0.0 ? 90.0 : -90.0;
                const double cx = pen.x + std::cos((pen.heading + side) * toRadians) * r;
                const double cy = pen.y + std::sin((pen.heading + side) * toRadians) * r;
                const double start = pen.heading - side;
                const int segments = std::max(4, static_cast<int>(std::abs(sweep) / 360.0 * 128.0));
                for (int k = 1; k <= segments; ++k)
                {
                    const double angle = (start + sweep * k / segments) * toRadians;
                    lineTo(cx + std::cos(angle) * r, cy + std::sin(angle) * r);
                }
            }
            pen.heading += sweep;
        }
        else if (c == "curve")
        {
            need(6, 6, "cx1 cy1 cx2 cy2 x y");
            const double x0 = pen.x, y0 = pen.y;
            for (int k = 1; k <= 32; ++k)
            {
                const double t = k / 32.0, u = 1.0 - t;
                const double w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
                lineTo(w0 * x0 + w1 * a[0] + w2 * a[2] + w3 * a[4], w0 * y0 + w1 * a[1] + w2 * a[3] + w3 * a[5]);
            }
            if (a[4] != a[2] || a[5] != a[3])
                pen.heading = std::atan2(a[5] - a[3], a[4] - a[2]) / toRadians;
        }
        else if (c == "close")
        {
            need(0, 0, "nothing");
            if (current.points.size() >= 2)
            {
                const auto start = current.points.front();
                current.closed = true;
                endPath();
                pen.x = start.x;
                pen.y = start.y;
            }
        }
        else if (c == "circle") { need(3, 3, "x y radius"); addShape(drawing::ellipse({ f(a[0]), f(a[1]) }, f(a[2]), f(a[2]))); }
        else if (c == "ellipse") { need(4, 4, "x y radiusX radiusY"); addShape(drawing::ellipse({ f(a[0]), f(a[1]) }, f(a[2]), f(a[3]))); }
        else if (c == "rect") { need(4, 4, "x y width height"); addShape(drawing::rectangle(f(a[0]), f(a[1]), f(a[2]), f(a[3]))); }
        else if (c == "polygon")
        {
            need(4, 5, "x y radius sides [rotation]");
            addShape(drawing::polygon({ f(a[0]), f(a[1]) }, f(a[2]), static_cast<int>(a[3]), a.size() > 4 ? f(a[4]) : 0.0f));
        }
        else if (c == "star")
        {
            need(5, 6, "x y outerRadius innerRadius points [rotation]");
            addShape(drawing::star({ f(a[0]), f(a[1]) }, f(a[2]), f(a[3]), static_cast<int>(a[4]), a.size() > 5 ? f(a[5]) : 0.0f));
        }
        else if (c == "push") { need(0, 0, "nothing"); stack.push_back(pen); }
        else if (c == "pop")
        {
            need(0, 0, "nothing");
            if (stack.empty())
                throw ScriptError { s.line, "'pop' without a 'push'" };
            endPath();
            pen = stack.back();
            stack.pop_back();
        }
        else if (c == "scale") { need(1, 1, "a factor"); pen.scale *= a[0]; }
        else if (c == "seed") { need(1, 1, "a number"); seed = static_cast<int>(a[0]); randomCount = 0; }
        else
        {
            auto procedure = procedures.find(c);
            if (procedure == procedures.end())
                throw ScriptError { s.line, "unknown command '" + c + "'" };
            const auto& def = *procedure->second;
            if (a.size() != def.params.size())
                throw ScriptError { s.line, "'" + c + "' takes " + std::to_string(def.params.size()) + " value(s), not " + std::to_string(a.size()) };
            if (++depth > maxDepth)
                throw ScriptError { s.line, "'" + c + "' calls itself too deeply (more than " + std::to_string(maxDepth) + " levels)" };
            scopes.emplace_back();
            for (size_t k = 0; k < a.size(); ++k)
                scopes.back()[def.params[k]] = a[k];
            execute(def.body);
            scopes.pop_back();
            --depth;
        }
    }

    const std::map<std::string, double>& globals;
    std::vector<std::map<std::string, double>> scopes;
    std::map<std::string, const Stmt*> procedures;
    int depth = 0;
    int seed;
    std::uint64_t randomCount = 0;
    Pen pen;
    std::vector<Pen> stack;
    drawing::Path current;
    drawing::Drawing result;
    long long steps = 0;
    size_t pointCount = 0;
    int currentLine = 1;
};
} // namespace

Result run(const std::string& script, const std::map<std::string, double>& variables, int seed)
{
    Result result;
    try
    {
        Parser parser(tokenize(script));
        const auto program = parser.parseScript();
        Interpreter interpreter(variables, seed);
        result.drawing = interpreter.run(program);
    }
    catch (const ScriptError& e)
    {
        result.drawing = {};
        result.error = "line " + std::to_string(e.line) + ": " + e.message;
    }
    return result;
}
} // namespace draw_script

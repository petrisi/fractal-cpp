#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

// ── Constants ────────────────────────────────────────────────────────────────

// Escape radius squared (standard value for quadratic fractals).
constexpr double escape_radius_sq = 256.0;

// log₂(log₂(√escape_radius_sq)) — normalisation factor for smooth iteration.
// For escape_radius_sq == 256 this is log₂(log₂(16)) == log₂(4) == 2.0.
constexpr double log2_log2_escape = std::log2(std::log2(std::sqrt(escape_radius_sq)));

// Number of colour cycles across the full iteration range.
constexpr double colour_cycles = 64.0;

// Default viewport spans in the complex plane when zoom == 1.0.
constexpr double default_re_span = 2.5; // [-1.5,  1.0]
constexpr double default_im_span = 2.0; // [-1.0,  1.0]

// Sanity limits.
constexpr int max_dimension  = 16384;
constexpr int max_iterations = 65536;

// Bytes per pixel (8-bit RGB).
constexpr int bytes_per_pixel = 3;

// Progress reporting interval (rows).
constexpr int progress_interval_rows = 100;

// Phase offsets for the invertible cosine palette (R, G, B channels).
constexpr double palette_phase_r = 0.00;
constexpr double palette_phase_g = 0.33;
constexpr double palette_phase_b = 0.67;

// 2π — used in the palette cosine calculation.
constexpr double two_pi = 2.0 * M_PI;

// Newton fractal convergence tolerance.
constexpr double newton_tolerance_sq = 1.0e-10;

// Minimum squared norm of z below which we treat it as the origin (derivative singularity).
constexpr double min_square_norm = 1.0e-30;

// Threshold for transcendental function overflow protection.
// cosh/sinh overflow at |arg| ≳ 710; exp overflows at arg ≳ 709.75.
// We use 700 as a safe margin.
constexpr double transcendental_overflow_threshold = 700.0;

// Upper bound on polynomial degree (multibrot / newton).
constexpr int max_degree = 64;

// ── Fractal type ─────────────────────────────────────────────────────────────

enum class FractalType {
    Mandelbrot,
    Julia,
    Burnship,
    Tricorn,
    Multibrot,
    Newton,
    Phoenix,
    Magnet,         // Magnet 1 and Magnet 2 (rational function iteration)
    Barnsley,       // Barnsley Julia variants (z² + c·f(z))
    Nova,           // Newton's method + c perturbation
    Transcendental, // z → f(z) + c where f ∈ {sin, cos, exp, tanh}
    Rational,       // z → z² + c - 1/z² (rational function fractal)
    Clifford,       // Clifford attractor (density plot, not escape-time)
    Lyapunov,       // Lyapunov exponent fractal (alternating logistic map)
};

static constexpr const char *fractal_type_name(FractalType type)
{
    switch (type) {
        case FractalType::Mandelbrot: return "mandelbrot";
        case FractalType::Julia:      return "julia";
        case FractalType::Burnship:   return "burnship";
        case FractalType::Tricorn:    return "tricorn";
        case FractalType::Multibrot:  return "multibrot";
        case FractalType::Newton:     return "newton";
        case FractalType::Phoenix:    return "phoenix";
        case FractalType::Magnet:     return "magnet";
        case FractalType::Barnsley:  return "barnsley";
        case FractalType::Nova:      return "nova";
        case FractalType::Transcendental: return "transcendental";
        case FractalType::Rational:       return "rational";
        case FractalType::Clifford:     return "clifford";
        case FractalType::Lyapunov:     return "lyapunov";
    }
    return "unknown";
}

// ── Render parameters ───────────────────────────────────────────────────────

// Shared parameters for all fractal types. Fractal-specific extras live in
// union-like optional fields.
struct RenderParams {
    FractalType type       = FractalType::Mandelbrot;

    // Viewport
    double cx       = -0.75;   // centre (real)
    double cy       =  0.0;    // centre (imag)
    double zoom     =  1.0;

    // Iteration
    int    max_iter =  256;

    // Output
    int    width    =  1920;
    int    height   =  1080;
    std::string output = "fractal.png";

    // ── Fractal-specific ────────────────────────────────────────────────

    // Julia: the constant c used in z² + c
    double julia_cr = -0.7269; // "Dendrite" Julia set
    double julia_ci =  0.1889;

    // Multibrot: polynomial degree n (zⁿ + c)
    int    multibrot_degree = 3;

    // Newton: polynomial degree n (zⁿ - 1 = 0)
    int    newton_degree = 3;

    // Phoenix: memory coefficient p in z → z² + c + p·z[n-1]
    // p is treated as real for simplicity (p + 0i).
    double phoenix_p = -0.5;

    // Phoenix: the constant c used in the iteration (Julia-mode)
    double phoenix_cr = 0.5667;
    double phoenix_ci = 0.0;

    // Magnet: 1 = Magnet 1, 2 = Magnet 2
    int    magnet_variant = 1;

    // Barnsley: variant 1 = z²+c·Re(z), 2 = z²+c·Im(z), 3 = z²+c·Re(z)·Im(z)
    int    barnsley_variant = 1;

    // Barnsley: the constant c used in the iteration
    double barnsley_cr = 0.0;
    double barnsley_ci = 0.0;

    // Nova: polynomial degree (same as newton_degree, reused)
    // Nova: perturbation constant c
    double nova_cr = 0.3;
    double nova_ci = 0.2;

    // Transcendental: 1=sin, 2=cos, 3=exp, 4=tanh
    int    transcendental_variant = 1;

    // Clifford attractor parameters: x' = sin(a·y) + c·cos(a·x), y' = sin(b·x) + d·cos(b·y)
    double clifford_a = -1.4;
    double clifford_b =  1.6;
    double clifford_c =  1.0;
    double clifford_d =  0.7;

    // Lyapunov: pattern string for alternating logistic map
    // Stored as a simple period: lyapunov_a_period = "AB" repetition count
    // lyapunov_b_period = "BA" repetition count
    // For simplicity: use a pattern like "AABAB" encoded as lyapunov_pattern_len + array
    // We use a simple approach: repeat "AB" lyapunov_pattern times
    int    lyapunov_pattern = 2; // period of "AB" repetition (2 = "ABAB", 3 = "ABABAB")
};

// ── CLI parsing ──────────────────────────────────────────────────────────────

using ParseResult = std::variant<std::monostate, bool, RenderParams>;
//   monostate   = parse error
//   true        = --help (exit 0)
//   RenderParams = success

static void print_usage(const char *prog)
{
    std::ostringstream os;
    os << "Usage: " << prog << " [options]\n"
       << "\nFractal renderer — supports: mandelbrot, julia, burnship, tricorn, multibrot, newton, phoenix, magnet, barnsley, nova, transcendental, rational, clifford, lyapunov\n"
       << "\nOptions:\n"
       << "  -t <type>        Fractal type (default: mandelbrot)\n"
       << "  -c <re,im>       Centre of view\n"
       << "  -z <zoom>        Zoom factor (default: 1.0)\n"
       << "  -i <iterations>  Max iterations (default: 256)\n"
       << "  -w <width>       Image width  (default: 1920)\n"
       << "  -h <height>      Image height (default: 1080)\n"
       << "  -o <file>        Output PNG   (default: fractal.png)\n"
       << "\nJulia-specific:\n"
       << "  -j <re,im>       Julia constant c (default: -0.7269,0.1889 Dendrite)\n"
       << "\n  -d <degree>      Polynomial degree n (multibrot: zⁿ+c, newton: zⁿ-1=0; default: 3)\n"
       << "\nPhoenix-specific:\n"
       << "  -j <re,im>       Phoenix constant c (default: 0.5667,0 Ushiki classic)\n"
       << "  -p <value>       Memory coefficient p (default: -0.5)\n"
       << "\nMagnet-specific:\n"
       << "  -m <1|2>          Magnet variant (default: 1)\n"
       << "\nBarnsley-specific:\n"
       << "  -j <re,im>       Barnsley constant c (default: 0,0)\n"
       << "  -b <1|2|3>        Barnsley variant (default: 1)\n"
       << "\nNova-specific:\n"
       << "  -j <re,im>       Nova perturbation c (default: 0.3,0.2)\n"
       << "  -d <degree>       Polynomial degree n (default: 3)\n"
       << "\nTranscendental-specific:\n"
       << "  -f <1|2|3|4>      Function: 1=sin, 2=cos, 3=exp, 4=tanh (default: 1)\n"
       << "\nClifford-specific:\n"
       << "  -a <value>         Clifford parameter a (default: -1.4)\n"
       << "  -bb <value>        Clifford parameter b (default: 1.6)\n"
       << "  -cc <value>        Clifford parameter c (default: 1.0)\n"
       << "  -dd <value>        Clifford parameter d (default: 0.7)\n"
       << "\nLyapunov-specific:\n"
       << "  -lp <period>       AB pattern period (default: 2, i.e. ABAB...)\n"
       << "\n  --help         Show this help\n";
    std::cerr << os.str();
}

static std::optional<FractalType> parse_fractal_type(const std::string &name)
{
    if (name == "mandelbrot") return FractalType::Mandelbrot;
    if (name == "julia")      return FractalType::Julia;
    if (name == "burnship")   return FractalType::Burnship;
    if (name == "tricorn")    return FractalType::Tricorn;
    if (name == "multibrot")  return FractalType::Multibrot;
    if (name == "newton")     return FractalType::Newton;
    if (name == "phoenix")    return FractalType::Phoenix;
    if (name == "magnet")     return FractalType::Magnet;
    if (name == "barnsley")   return FractalType::Barnsley;
    if (name == "nova")       return FractalType::Nova;
    if (name == "transcendental") return FractalType::Transcendental;
    if (name == "rational")     return FractalType::Rational;
    if (name == "clifford")   return FractalType::Clifford;
    if (name == "lyapunov")   return FractalType::Lyapunov;
    return std::nullopt;
}

// Set type-specific defaults on a freshly-defaulted RenderParams.
static void set_type_defaults(RenderParams &p, FractalType type)
{
    switch (type) {
        case FractalType::Mandelbrot:
            p.cx = -0.75; p.cy =  0.0;  // Seahorse Valley
            p.max_iter = 256;
            break;
        case FractalType::Julia:
            p.cx = 0.0;  p.cy = 0.0;
            p.max_iter = 256;
            break;
        case FractalType::Burnship:
            p.cx = -0.15; p.cy = 0.65;
            p.max_iter = 256;
            break;
        case FractalType::Tricorn:
            p.cx = -0.3; p.cy = 0.0;
            p.max_iter = 256;
            break;
        case FractalType::Multibrot:
            p.cx = -0.1; p.cy = 0.7;
            p.max_iter = 256;
            break;
        case FractalType::Newton:
            p.cx = 0.0;  p.cy = 0.0;
            p.max_iter = 128;
            break;
        case FractalType::Phoenix:
            p.cx = 0.0;  p.cy = 0.0;
            p.max_iter = 256;
            p.phoenix_cr = 0.5667;
            p.phoenix_ci = 0.0;
            p.phoenix_p  = -0.5;
            break;
        case FractalType::Magnet:
            p.cx = 0.0;  p.cy = 0.0;
            p.zoom = 0.3;          // wider view to show gasket structure
            p.max_iter = 256;
            p.magnet_variant = 1;
            break;
        case FractalType::Barnsley:
            p.cx = 0.0;  p.cy = 0.0;
            p.max_iter = 256;
            p.barnsley_variant = 1;
            break;
        case FractalType::Nova:
            p.cx = 0.0;  p.cy = 0.0;
            p.max_iter = 128;
            p.newton_degree = 3;
            p.nova_cr = 0.3;
            p.nova_ci = 0.2;
            break;
        case FractalType::Transcendental:
            p.cx = 0.0;  p.cy = 0.0;
            p.zoom = 0.5;          // wider view for transcendental
            p.max_iter = 256;
            p.transcendental_variant = 1; // sin
            break;
        case FractalType::Rational:
            p.cx = 0.0;  p.cy = 0.0;
            p.zoom = 0.5;
            p.max_iter = 256;
            break;
        case FractalType::Clifford:
            p.clifford_a = -1.4;
            p.clifford_b =  1.6;
            p.clifford_c =  1.0;
            p.clifford_d =  0.7;
            break;
        case FractalType::Lyapunov:
            p.lyapunov_pattern = 2; // "AB" pattern
            break;
    }
}

static ParseResult parse_args(int argc, char **argv)
{
    // Pre-scan for -t to determine the type, so we can apply correct defaults
    // before the main parse loop (which then overrides them).
    FractalType type = FractalType::Mandelbrot;
    bool type_specified = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-t" && i + 1 < argc) {
            auto t = parse_fractal_type(argv[i + 1]);
            if (t) { type = *t; type_specified = true; break; }
            else {
                std::cerr << "error: unknown fractal type '" << argv[i + 1] << "'\n";
                return std::monostate{};
            }
        }
    }
    (void)type_specified;

    RenderParams p{};
    set_type_defaults(p, type);
    p.type = type;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            print_usage(argv[0]);
            return true;
        }

        auto next = [&]() -> std::string {
            if (i + 1 >= argc) return "";
            return argv[++i];
        };

        if (arg == "-t") {
            auto type = parse_fractal_type(next());
            if (!type) {
                std::cerr << "error: unknown fractal type\n";
                return {};
            }
            p.type = *type;
        } else if (arg == "-c") {
            std::string val = next();
            auto comma = val.find(',');
            if (comma == std::string::npos) {
                std::cerr << "error: -c expects <re,im>\n";
                return {};
            }
            try {
                p.cx = std::stod(val.substr(0, comma));
                p.cy = std::stod(val.substr(comma + 1));
            } catch (const std::exception &e) {
                std::cerr << "error: bad centre value '" << val
                          << "': " << e.what() << "\n";
                return {};
            }
        } else if (arg == "-z") {
            try { p.zoom = std::stod(next()); }
            catch (const std::exception &e) {
                std::cerr << "error: bad zoom value: " << e.what() << "\n";
                return {};
            }
        } else if (arg == "-i") {
            try { p.max_iter = std::stoi(next()); }
            catch (const std::exception &e) {
                std::cerr << "error: bad iteration count: "
                          << e.what() << "\n";
                return {};
            }
        } else if (arg == "-w") {
            try { p.width = std::stoi(next()); }
            catch (const std::exception &e) {
                std::cerr << "error: bad width: " << e.what() << "\n";
                return {};
            }
        } else if (arg == "-h") {
            try { p.height = std::stoi(next()); }
            catch (const std::exception &e) {
                std::cerr << "error: bad height: " << e.what() << "\n";
                return {};
            }
        } else if (arg == "-o") {
            p.output = next();
        } else if (arg == "-j") {
            std::string val = next();
            auto comma = val.find(',');
            if (comma == std::string::npos) {
                std::cerr << "error: -j expects <re,im>\n";
                return {};
            }
            try {
                p.julia_cr = std::stod(val.substr(0, comma));
                p.julia_ci = std::stod(val.substr(comma + 1));
                p.phoenix_cr = p.julia_cr;
                p.phoenix_ci = p.julia_ci;
                p.barnsley_cr = p.julia_cr;
                p.barnsley_ci = p.julia_ci;
                p.nova_cr = p.julia_cr;
                p.nova_ci = p.julia_ci;
            } catch (const std::exception &e) {
                std::cerr << "error: bad julia constant '" << val
                          << "': " << e.what() << "\n";
                return {};
            }
        } else if (arg == "-d") {
            try {
                int degree = std::stoi(next());
                if (degree < 2) {
                    std::cerr << "error: degree must be >= 2\n";
                    return {};
                }
                p.multibrot_degree = degree;
                p.newton_degree    = degree;
            } catch (const std::exception &e) {
                std::cerr << "error: bad degree: " << e.what() << "\n";
                return {};
            }
        } else if (arg == "-p") {
            try { p.phoenix_p = std::stod(next()); }
            catch (const std::exception &e) {
                std::cerr << "error: bad phoenix p value: " << e.what() << "\n";
                return {};
            }
        } else if (arg == "-m") {
            try {
                int variant = std::stoi(next());
                if (variant < 1 || variant > 2) {
                    std::cerr << "error: magnet variant must be 1 or 2\n";
                    return {};
                }
                p.magnet_variant = variant;
            } catch (const std::exception &e) {
                std::cerr << "error: bad magnet variant: " << e.what() << "\n";
                return {};
            }
        } else if (arg == "-b") {
            try {
                int variant = std::stoi(next());
                if (variant < 1 || variant > 3) {
                    std::cerr << "error: barnsley variant must be 1, 2, or 3\n";
                    return {};
                }
                p.barnsley_variant = variant;
            } catch (const std::exception &e) {
                std::cerr << "error: bad barnsley variant: " << e.what() << "\n";
                return {};
            }
        } else if (arg == "-f") {
            try {
                int variant = std::stoi(next());
                if (variant < 1 || variant > 4) {
                    std::cerr << "error: transcendental function must be 1-4 (sin/cos/exp/tanh)\n";
                    return {};
                }
                p.transcendental_variant = variant;
            } catch (const std::exception &e) {
                std::cerr << "error: bad transcendental function: " << e.what() << "\n";
                return {};
            }
        } else if (arg == "-a") {
            try { p.clifford_a = std::stod(next()); }
            catch (const std::exception &e) {
                std::cerr << "error: bad clifford a: " << e.what() << "\n";
                return {};
            }
        } else if (arg == "-bb") {
            try { p.clifford_b = std::stod(next()); }
            catch (const std::exception &e) {
                std::cerr << "error: bad clifford b: " << e.what() << "\n";
                return {};
            }
        } else if (arg == "-cc") {
            try { p.clifford_c = std::stod(next()); }
            catch (const std::exception &e) {
                std::cerr << "error: bad clifford c: " << e.what() << "\n";
                return {};
            }
        } else if (arg == "-dd") {
            try { p.clifford_d = std::stod(next()); }
            catch (const std::exception &e) {
                std::cerr << "error: bad clifford d: " << e.what() << "\n";
                return {};
            }
        } else if (arg == "-lp") {
            try {
                int period = std::stoi(next());
                if (period < 1 || period > 16) {
                    std::cerr << "error: lyapunov period must be 1-16\n";
                    return {};
                }
                p.lyapunov_pattern = period;
            } catch (const std::exception &e) {
                std::cerr << "error: bad lyapunov period: " << e.what() << "\n";
                return {};
            }
        } else {
            std::cerr << "error: unknown option: " << arg << "\n";
            return {};
        }
    }

    return p;
}

// ── Parameter validation ────────────────────────────────────────────────────

static bool validate_params(const RenderParams &p)
{
    auto fail = [](const char *msg) { std::cerr << "error: " << msg << "\n"; };

    if (p.width < 1 || p.width > max_dimension) {
        fail(("width must be 1.." + std::to_string(max_dimension)).c_str());
        return false;
    }
    if (p.height < 1 || p.height > max_dimension) {
        fail(("height must be 1.." + std::to_string(max_dimension)).c_str());
        return false;
    }
    if (p.max_iter < 1 || p.max_iter > max_iterations) {
        fail(("max_iter must be 1.." + std::to_string(max_iterations)).c_str());
        return false;
    }
    if (p.zoom <= 0.0) {
        fail("zoom must be > 0");
        return false;
    }
    if (p.output.empty()) {
        fail("output path must not be empty");
        return false;
    }
    if (p.type == FractalType::Multibrot && (p.multibrot_degree < 2 || p.multibrot_degree > max_degree)) {
        fail(("multibrot degree must be 2.." + std::to_string(max_degree)).c_str());
        return false;
    }
    if (p.type == FractalType::Newton && (p.newton_degree < 2 || p.newton_degree > max_degree)) {
        fail(("newton degree must be 2.." + std::to_string(max_degree)).c_str());
        return false;
    }
    if (p.type == FractalType::Phoenix) {
        if (std::isnan(p.phoenix_p) || std::isinf(p.phoenix_p)) {
            fail("phoenix p must be finite");
            return false;
        }
        if (p.phoenix_p == 0.0) {
            fail("phoenix p must not be zero (degenerates to mandelbrot)");
            return false;
        }
        if (std::isnan(p.phoenix_cr) || std::isinf(p.phoenix_cr) ||
            std::isnan(p.phoenix_ci) || std::isinf(p.phoenix_ci)) {
            fail("phoenix c must be finite");
            return false;
        }
    }
    if (p.type == FractalType::Magnet && (p.magnet_variant < 1 || p.magnet_variant > 2)) {
        fail("magnet variant must be 1 or 2");
        return false;
    }
    if (p.type == FractalType::Barnsley && (p.barnsley_variant < 1 || p.barnsley_variant > 3)) {
        fail("barnsley variant must be 1, 2, or 3");
        return false;
    }
    if (p.type == FractalType::Nova) {
        if (p.newton_degree < 2 || p.newton_degree > max_degree) {
            fail(("nova degree must be 2.." + std::to_string(max_degree)).c_str());
            return false;
        }
        if (std::isnan(p.nova_cr) || std::isinf(p.nova_cr) ||
            std::isnan(p.nova_ci) || std::isinf(p.nova_ci)) {
            fail("nova c must be finite");
            return false;
        }
    }
    if (p.type == FractalType::Transcendental &&
        (p.transcendental_variant < 1 || p.transcendental_variant > 4)) {
        fail("transcendental function must be 1-4 (sin/cos/exp/tanh)");
        return false;
    }
    if (p.type == FractalType::Clifford) {
        if (std::isnan(p.clifford_a) || std::isinf(p.clifford_a) ||
            std::isnan(p.clifford_b) || std::isinf(p.clifford_b) ||
            std::isnan(p.clifford_c) || std::isinf(p.clifford_c) ||
            std::isnan(p.clifford_d) || std::isinf(p.clifford_d)) {
            fail("clifford parameters must be finite");
            return false;
        }
        if (p.clifford_a == 0.0 || p.clifford_b == 0.0) {
            fail("clifford a and b must not be zero");
            return false;
        }
    }
    if (p.type == FractalType::Lyapunov &&
        (p.lyapunov_pattern < 1 || p.lyapunov_pattern > 16)) {
        fail("lyapunov pattern period must be 1-16");
        return false;
    }

    // Guard against overflow in w * h * 3.
    size_t pixel_count = static_cast<size_t>(p.width) * static_cast<size_t>(p.height);
    if (pixel_count > std::numeric_limits<size_t>::max() / bytes_per_pixel) {
        fail("image too large (width * height * bytes_per_pixel would overflow)");
        return false;
    }

    return true;
}

// ── Colour palette (smooth iteration → RGB) ─────────────────────────────────

// Invertible cosine palette (Akenine-Möller / Shirley trick).
// t in [0,1) → RGB in [0,255].
static inline std::array<uint8_t, 3> palette(double colour_t)
{
    auto channel = [colour_t](double freq, double phase) {
        return 0.5 * (1.0 + std::cos(two_pi * (freq * colour_t + phase)));
    };
    std::array<uint8_t, 3> rgb;
    rgb[0] = static_cast<uint8_t>(channel(1.0, palette_phase_r) * 255.0);
    rgb[1] = static_cast<uint8_t>(channel(1.0, palette_phase_g) * 255.0);
    rgb[2] = static_cast<uint8_t>(channel(1.0, palette_phase_b) * 255.0);
    return rgb;
}

// ── Fractal iteration functions ──────────────────────────────────────────────
//
// All return a smooth iteration value in (0, max_iter] for points that
// escape, or 0.0 for points that appear to be inside (or converged).

// ── Mandelbrot: z → z² + c, z₀ = 0, c = (cr, ci) ───────────────────────────

static double mandelbrot_smooth(double cr, double ci, int max_iter)
{
    double zr = 0.0, zi = 0.0;
    double zr_sq = 0.0, zi_sq = 0.0;
    int iter = 0;

    while (zr_sq + zi_sq <= escape_radius_sq && iter < max_iter) {
        zi    = 2.0 * zr * zi + ci;
        zr    = zr_sq - zi_sq + cr;
        zr_sq = zr * zr;
        zi_sq = zi * zi;
        ++iter;
    }

    if (iter == max_iter)
        return 0.0;

    double log_zn = std::log2(std::log2(std::sqrt(zr_sq + zi_sq)));
    return static_cast<double>(iter) - 1.0 + log_zn / log2_log2_escape;
}

// ── Julia: z → z² + c, z₀ = (zr, zi), c = fixed ────────────────────────────

static double julia_smooth(double zr, double zi, double cr, double ci, int max_iter)
{
    double zr_sq = zr * zr;
    double zi_sq = zi * zi;
    int iter = 0;

    while (zr_sq + zi_sq <= escape_radius_sq && iter < max_iter) {
        zi    = 2.0 * zr * zi + ci;
        zr    = zr_sq - zi_sq + cr;
        zr_sq = zr * zr;
        zi_sq = zi * zi;
        ++iter;
    }

    if (iter == max_iter)
        return 0.0;

    double log_zn = std::log2(std::log2(std::sqrt(zr_sq + zi_sq)));
    return static_cast<double>(iter) - 1.0 + log_zn / log2_log2_escape;
}

// ── Burnship: z → swap(z²) + c ──────────────────────────────────────────────
//
// Standard iteration: z ← z² + c
// Burnship variant: after computing z² = (a + bi), swap real and imaginary
// to get (b + ai), then add c.  This is NOT the same as conj(z²) — it
// produces a different fractal with organic, almost "burnt" shapes.

static double burnship_smooth(double cr, double ci, int max_iter)
{
    double zr = 0.0, zi = 0.0;
    double zr_sq = 0.0, zi_sq = 0.0;
    int iter = 0;

    while (zr_sq + zi_sq <= escape_radius_sq && iter < max_iter) {
        // z² = (zr² - zi²) + i(2·zr·zi)
        double new_zr = zr_sq - zi_sq;
        double new_zi = 2.0 * zr * zi;

        // Burnship swap: swap real and imaginary parts after squaring
        zr = new_zi + cr;  // real part gets the imaginary part of z²
        zi = new_zr + ci;  // imag part gets the real part of z²

        zr_sq = zr * zr;
        zi_sq = zi * zi;
        ++iter;
    }

    if (iter == max_iter)
        return 0.0;

    double log_zn = std::log2(std::log2(std::sqrt(zr_sq + zi_sq)));
    return static_cast<double>(iter) - 1.0 + log_zn / log2_log2_escape;
}

// ── Tricorn (Mandelbar): z → conj(z)² + c ───────────────────────────────────
//
// conj(z)² = conj(z²), so this is equivalent to z ← conj(z²) + c.
// Produces a mirror-image of the Mandelbrot set with different topology.

static double tricorn_smooth(double cr, double ci, int max_iter)
{
    double zr = 0.0, zi = 0.0;
    double zr_sq = 0.0, zi_sq = 0.0;
    int iter = 0;

    while (zr_sq + zi_sq <= escape_radius_sq && iter < max_iter) {
        // conj(z)² = (zr - i·zi)² = (zr² - zi²) - i(2·zr·zi)
        zi    = -2.0 * zr * zi + ci;  // note the sign flip
        zr    = zr_sq - zi_sq + cr;
        zr_sq = zr * zr;
        zi_sq = zi * zi;
        ++iter;
    }

    if (iter == max_iter)
        return 0.0;

    double log_zn = std::log2(std::log2(std::sqrt(zr_sq + zi_sq)));
    return static_cast<double>(iter) - 1.0 + log_zn / log2_log2_escape;
}

// ── Multibrot: z → zⁿ + c for degree n ≥ 2 ──────────────────────────────────
//
// n = 2 is the standard Mandelbrot. Higher n produces n-fold symmetric sets.
// Uses polar form: z = r·e^(iθ), zⁿ = rⁿ·e^(inθ). For small degrees a repeated
// cartesian multiplication could be faster, but polar generalises cleanly to
// arbitrary n at the cost of four transcendental calls per iteration.

static double multibrot_smooth(double cr, double ci, int max_iter, int degree)
{
    double zr = 0.0, zi = 0.0;
    double r_sq = 0.0;
    int iter = 0;

    while (r_sq <= escape_radius_sq && iter < max_iter) {
        // Convert to polar: r = √(zr² + zi²), θ = atan2(zi, zr)
        double r = std::sqrt(r_sq);
        double theta = std::atan2(zi, zr);

        // zⁿ: rⁿ, n·θ
        double r_n = std::pow(r, degree);
        double theta_n = theta * degree;

        // Convert back to cartesian and add c
        zr = r_n * std::cos(theta_n) + cr;
        zi = r_n * std::sin(theta_n) + ci;
        r_sq = zr * zr + zi * zi;
        ++iter;
    }

    if (iter == max_iter)
        return 0.0;

    double log_zn = std::log2(std::log2(std::sqrt(r_sq)));
    return static_cast<double>(iter) - 1.0 + log_zn / log2_log2_escape;
}

// ── Newton: Newton's method on zⁿ - 1 = 0 ───────────────────────────────────
//
// Newton iteration: z ← z - f(z)/f'(z) = z - (zⁿ - 1)/(n·zⁿ⁻¹)
//                   = z - (1/n)(z - 1/zⁿ⁻¹)
//                   = ((n-1)·z + 1/zⁿ⁻¹) / n
//
// Roots are the n-th roots of unity. Colour by which root it converges to.
// Returns 0.0 if no convergence within max_iter.

// Precomputed n-th roots of unity — shared across all pixels to avoid
// redundant trigonometry.
struct newton_roots {
    std::vector<double> re;
    std::vector<double> im;

    static newton_roots compute(int degree)
    {
        newton_roots result;
        result.re.resize(degree);
        result.im.resize(degree);
        for (int k = 0; k < degree; ++k) {
            double angle = two_pi * k / degree;
            result.re[k] = std::cos(angle);
            result.im[k] = std::sin(angle);
        }
        return result;
    }
};

static double newton_smooth(double zr, double zi, int max_iter,
                            int degree, const newton_roots &roots)
{
    double cur_r = zr;
    double cur_i = zi;
    int iter = 0;

    while (iter < max_iter) {
        double r_sq = cur_r * cur_r + cur_i * cur_i;
        if (r_sq <= min_square_norm) {
            // At the origin — derivative is zero, escape.
            return 0.0;
        }

        double r = std::sqrt(r_sq);
        double theta = std::atan2(cur_i, cur_r);

        // Simplified Newton formula: z ← ((n-1)·z + 1/zⁿ⁻¹) / n
        // In polar: 1/zⁿ⁻¹ = r^(1-n)·e^(i(1-n)θ)
        double inv_znm1_r = std::pow(r, 1 - degree) * std::cos((1 - degree) * theta);
        double inv_znm1_i = std::pow(r, 1 - degree) * std::sin((1 - degree) * theta);

        double new_r = ((degree - 1) * cur_r + inv_znm1_r) / degree;
        double new_i = ((degree - 1) * cur_i + inv_znm1_i) / degree;
        cur_r = new_r;
        cur_i = new_i;
        ++iter;

        // Check convergence to any root.
        for (int k = 0; k < degree; ++k) {
            double dr = cur_r - roots.re[k];
            double di = cur_i - roots.im[k];
            if (dr * dr + di * di <= newton_tolerance_sq) {
                // Converged to root k. Return smooth value encoding the root index.
                return static_cast<double>(iter) + 1.0 + k;
            }
        }
    }

    return 0.0; // no convergence
}

// ── Phoenix: z → z² + c + p·z[n-1] ──────────────────────────────────────────
//
// Discovered by Shigehiro Ushiki (1988). The memory term p·z[n-1] creates
// flowing flame-like spirals. This is a second-order recurrence requiring
// two previous values. Implemented in Julia mode (c is fixed, pixel position
// determines the initial orbit).
//
// Standard initial conditions (canonical Phoenix swap, as used by XaoS,
// Ultra Fractal, and Flux):
//   z₋₁ = conj(pixel) = (zr, -zi)  — conjugate of the pixel position
//   z₋₂ = 0
// The conjugate swap rotates the output so the Phoenix appears upright.
//
// Julia mode: c = (cr, ci), p = phoenix_p
//
// Returns smooth iteration value for escaping points, 0.0 for bounded orbits.

static double phoenix_smooth(double zr, double zi, double cr, double ci,
                             double p, int max_iter)
{
    // z starts at z₋₁ = conj(pixel), z_prev is z₋₂ = 0
    double zr_cur = zr;       // real part of pixel
    double zi_cur = -zi;      // conjugate (swap convention)
    double zr_prev = 0.0;     // z₋₂ real
    double zi_prev = 0.0;     // z₋₂ imag
    double zr_sq = zr_cur * zr_cur + zi_cur * zi_cur;
    int iter = 0;

    while (zr_sq <= escape_radius_sq && iter < max_iter) {
        // z² = (zr² - zi²) + i(2·zr·zi)
        double new_zr = zr_cur * zr_cur - zi_cur * zi_cur + cr + p * zr_prev;
        double new_zi = 2.0 * zr_cur * zi_cur + ci + p * zi_prev;

        // Shift history
        zr_prev = zr_cur;
        zi_prev = zi_cur;
        zr_cur = new_zr;
        zi_cur = new_zi;
        zr_sq = zr_cur * zr_cur + zi_cur * zi_cur;
        ++iter;
    }

    if (iter == max_iter)
        return 0.0;

    double log_zn = std::log2(std::log2(std::sqrt(zr_sq)));
    return static_cast<double>(iter) - 1.0 + log_zn / log2_log2_escape;
}

// ── Magnet 1 & Magnet 2: rational function iteration ─────────────────────────
//
// Derived from magnetic phase renormalization (Yang-Lee edge singularities).
// Produces stunning Sierpinski-gasket-like structures.
//
// Magnet 1: z → ((z² + (c-1)) / (2z + (c-2)))²
// Magnet 2: z → ((z³ + 3(c-1)z + (c-1)(c-2)) / (3z² + 3(c-2)z + (c-1)(c-2) + 1))²
//
// These are rational functions with poles (denominator = 0). When the
// denominator approaches zero, |z| → ∞, which is treated as escape.
// Uses a larger escape radius (10000) due to the rational function dynamics.
//
// Mandelbrot mode: z₀ = 0, c = (cr, ci) from pixel position.

constexpr double magnet_escape_radius_sq = 10000.0;
constexpr double magnet_log2_log2_escape = std::log2(std::log2(std::sqrt(magnet_escape_radius_sq)));
// |denominator|² below this → treat as pole escape. 1e-10 is safe for double
// precision: it corresponds to |denominator| < 1e-5, far below any meaningful
// orbit value while still catching the singularity before division blow-up.
constexpr double magnet_pole_threshold   = 1.0e-10;

static double magnet_smooth(double cr, double ci, int max_iter, int variant)
{
    double zr = 0.0, zi = 0.0;
    double zr_sq = 0.0, zi_sq = 0.0;
    int iter = 0;

    // Precompute c-1 and c-2 (used in both variants)
    double cm1r = cr - 1.0, cm1i = ci;
    double cm2r = cr - 2.0, cm2i = ci;

    while (zr_sq + zi_sq <= magnet_escape_radius_sq && iter < max_iter) {
        double num_r, num_i, den_r, den_i, den_sq;

        if (variant == 1) {
            // Magnet 1: ((z² + (c-1)) / (2z + (c-2)))²
            // Numerator: z² + (c-1)
            num_r = zr_sq - zi_sq + cm1r;
            num_i = 2.0 * zr * zi + cm1i;

            // Denominator: 2z + (c-2)
            den_r = 2.0 * zr + cm2r;
            den_i = 2.0 * zi + cm2i;
        } else {
            // Magnet 2: ((z³ + 3(c-1)z + (c-1)(c-2)) / (3z² + 3(c-2)z + (c-1)(c-2) + 1))²
            // z² = (zr² - zi²) + i(2·zr·zi)
            double z2_r = zr_sq - zi_sq;
            double z2_i = 2.0 * zr * zi;

            // z³ = z · z²
            double z3_r = zr * z2_r - zi * z2_i;
            double z3_i = zr * z2_i + zi * z2_r;

            // (c-1)(c-2)
            double c1c2_r = cm1r * cm2r - cm1i * cm2i;
            double c1c2_i = cm1r * cm2i + cm1i * cm2r;

            // Numerator: z³ + 3(c-1)z + (c-1)(c-2)
            num_r = z3_r + 3.0 * (cm1r * zr - cm1i * zi) + c1c2_r;
            num_i = z3_i + 3.0 * (cm1r * zi + cm1i * zr) + c1c2_i;

            // Denominator: 3z² + 3(c-2)z + (c-1)(c-2) + 1
            den_r = 3.0 * z2_r + 3.0 * (cm2r * zr - cm2i * zi) + c1c2_r + 1.0;
            den_i = 3.0 * z2_i + 3.0 * (cm2r * zi + cm2i * zr) + c1c2_i;
        }

        den_sq = den_r * den_r + den_i * den_i;
        if (den_sq <= magnet_pole_threshold) {
            // Near a pole — denominator too small, treat as escape
            break;
        }

        // Complex division: (num_r + i·num_i) / (den_r + i·den_i)
        zr = (num_r * den_r + num_i * den_i) / den_sq;
        zi = (num_i * den_r - num_r * den_i) / den_sq;
        zr_sq = zr * zr;
        zi_sq = zi * zi;
        ++iter;
    }

    if (iter == max_iter)
        return 0.0;

    double total_sq = zr_sq + zi_sq;
    double log_zn = std::log2(std::log2(std::sqrt(total_sq)));
    return static_cast<double>(iter) - 1.0 + log_zn / magnet_log2_log2_escape;
}

// ── Barnsley: z → z² + c·f(z) ────────────────────────────────────────────────
//
// Three variants described by Michael Barnsley in "Fractals Everywhere".
// Julia-mode: z₀ = pixel position, c = (cr, ci) is the fixed constant.
//
// Variant 1: z → z² + c·Re(z)
// Variant 2: z → z² + c·Im(z)
// Variant 3: z → z² + c·Re(z)·Im(z)
//
// Produces crystalline, geometric Julia sets with sharp edges.
// Returns smooth iteration value for escaping points, 0.0 for bounded orbits.

static double barnsley_smooth(double zr, double zi, double cr, double ci,
                              int variant, int max_iter)
{
    double zr_sq = zr * zr;
    double zi_sq = zi * zi;
    int iter = 0;

    while (zr_sq + zi_sq <= escape_radius_sq && iter < max_iter) {
        // z² = (zr² - zi²) + i(2·zr·zi)
        double z2_r = zr_sq - zi_sq;
        double z2_i = 2.0 * zr * zi;

        double add_r = 0.0, add_i = 0.0;

        if (variant == 1) {
            // c · Re(z) = c · zr
            add_r = cr * zr;
            add_i = ci * zr;
        } else if (variant == 2) {
            // c · Im(z) = c · zi
            add_r = cr * zi;
            add_i = ci * zi;
        } else {
            // c · Re(z) · Im(z) = c · zr · zi
            double factor = zr * zi;
            add_r = cr * factor;
            add_i = ci * factor;
        }

        zr = z2_r + add_r;
        zi = z2_i + add_i;
        zr_sq = zr * zr;
        zi_sq = zi * zi;
        ++iter;
    }

    if (iter == max_iter)
        return 0.0;

    double log_zn = std::log2(std::log2(std::sqrt(zr_sq + zi_sq)));
    return static_cast<double>(iter) - 1.0 + log_zn / log2_log2_escape;
}

// ── Nova: Newton's method + c perturbation ──────────────────────────────────
//
// Nova (Newton Julia): z → z - (zⁿ - 1)/(n·zⁿ⁻¹) + c
//                     = ((n-1)·z + 1/zⁿ⁻¹) / n + c
//
// This is Newton's method on zⁿ - 1 = 0 with a constant perturbation +c.
// When c = 0, it reduces to the standard Newton fractal. Small c values
// create Julia-like basin-of-attraction structures.
//
// Julia mode: z₀ = pixel position, c = (nova_cr, nova_ci) is fixed.
// Colour by which root of unity the orbit converges to.
// Returns 0.0 if no convergence within max_iter.

static double nova_smooth(double zr, double zi, int max_iter,
                          int degree, const newton_roots &roots,
                          double c_r, double c_i)
{
    double cur_r = zr;
    double cur_i = zi;
    int iter = 0;

    while (iter < max_iter) {
        double r_sq = cur_r * cur_r + cur_i * cur_i;
        if (r_sq <= min_square_norm) {
            // At the origin — derivative is zero, escape.
            return 0.0;
        }

        double r = std::sqrt(r_sq);
        double theta = std::atan2(cur_i, cur_r);

        // 1/zⁿ⁻¹ in polar: r^(1-n)·e^(i(1-n)θ)
        double inv_r = std::pow(r, 1 - degree);
        double inv_angle = (1 - degree) * theta;
        double inv_znm1_r = inv_r * std::cos(inv_angle);
        double inv_znm1_i = inv_r * std::sin(inv_angle);

        // z ← ((n-1)·z + 1/zⁿ⁻¹) / n + c
        double new_r = ((degree - 1) * cur_r + inv_znm1_r) / degree + c_r;
        double new_i = ((degree - 1) * cur_i + inv_znm1_i) / degree + c_i;
        cur_r = new_r;
        cur_i = new_i;
        ++iter;

        // Check convergence to any root.
        for (int k = 0; k < degree; ++k) {
            double dr = cur_r - roots.re[k];
            double di = cur_i - roots.im[k];
            if (dr * dr + di * di <= newton_tolerance_sq) {
                return static_cast<double>(iter) + 1.0 + k;
            }
        }
    }

   return 0.0; // no convergence
}

// ── Transcendental: z → f(z) + c ─────────────────────────────────────────────
//
// Replace the polynomial z² with transcendental functions. Produces wild,
// organic structures very different from polynomial fractals.
//
// Variants: 1=sin, 2=cos, 3=exp, 4=tanh
// Mandelbrot mode: z₀ = 0, c = pixel position.
//
// Uses a larger escape radius (50² = 2500) since transcendental functions
// grow much faster than polynomials.

constexpr double transcendental_escape_radius_sq = 2500.0;
constexpr double transcendental_log2_log2_escape = std::log2(std::log2(std::sqrt(transcendental_escape_radius_sq)));

static double transcendental_smooth(double cr, double ci, int max_iter, int variant)
{
    double zr = 0.0, zi = 0.0;
    double r_sq = 0.0;
    int iter = 0;

    while (r_sq <= transcendental_escape_radius_sq && iter < max_iter) {
        double new_zr, new_zi;

        if (variant == 1) {
            // sin(z) = sin(zr)·cosh(zi) + i·cos(zr)·sinh(zi)
            // cosh/sinh overflow when |zi| ≳ 710
            if (std::abs(zi) > transcendental_overflow_threshold) break;
            new_zr = std::sin(zr) * std::cosh(zi) + cr;
            new_zi = std::cos(zr) * std::sinh(zi) + ci;
        } else if (variant == 2) {
            // cos(z) = cos(zr)·cosh(zi) - i·sin(zr)·sinh(zi)
            if (std::abs(zi) > transcendental_overflow_threshold) break;
            new_zr = std::cos(zr) * std::cosh(zi) + cr;
            new_zi = -std::sin(zr) * std::sinh(zi) + ci;
        } else if (variant == 3) {
            // exp(z) = e^zr · (cos(zi) + i·sin(zi))
            // Guard against overflow: e^zr → inf when zr ≳ 709
            if (zr > transcendental_overflow_threshold) {
                // Will definitely escape
                break;
            }
            double er = std::exp(zr);
            new_zr = er * std::cos(zi) + cr;
            new_zi = er * std::sin(zi) + ci;
        } else {
            // tanh(z) = (sinh(2·zr) + i·sin(2·zi)) / (cosh(2·zr) + cos(2·zi))
            double s2r = std::sinh(2.0 * zr);
            double c2r = std::cosh(2.0 * zr);
            double s2i = std::sin(2.0 * zi);
            double c2i = std::cos(2.0 * zi);
            double denom = c2r + c2i;
            new_zr = (s2r / denom) + cr;
            new_zi = (s2i / denom) + ci;
        }

        zr = new_zr;
        zi = new_zi;
        r_sq = zr * zr + zi * zi;
        ++iter;
    }

    if (iter == max_iter)
        return 0.0;

    double log_zn = std::log2(std::log2(std::sqrt(r_sq)));
    return static_cast<double>(iter) - 1.0 + log_zn / transcendental_log2_log2_escape;
}

// ── Rational: z → z² + c - 1/z² ──────────────────────────────────────────────
//
// Rational function fractal. Produces Julia sets with characteristic
// gasket-like structures and holes. The -1/z² term introduces a pole at
// z = 0, creating regions where orbits are repelled from the origin.
//
// Mandelbrot mode: z₀ = 0, c = pixel position.
// Uses a larger escape radius due to the rational dynamics.
// Returns 0.0 for points that remain bounded, smooth value for escaping.

constexpr double rational_escape_radius_sq = 10000.0;
constexpr double rational_log2_log2_escape = std::log2(std::log2(std::sqrt(rational_escape_radius_sq)));
constexpr double rational_pole_threshold   = 1.0e-10; // |z|² below this → pole escape

static double rational_smooth(double cr, double ci, int max_iter)
{
    double zr = 0.0, zi = 0.0;
    double zr_sq = 0.0, zi_sq = 0.0;
    int iter = 0;

    while (zr_sq + zi_sq <= rational_escape_radius_sq && iter < max_iter) {
        double r_sq = zr_sq + zi_sq;
        if (r_sq <= rational_pole_threshold) {
            // Near the pole at z = 0 — treat as escape
            break;
        }

        // z² = (zr² - zi²) + i(2·zr·zi)
        double z2_r = zr_sq - zi_sq;
        double z2_i = 2.0 * zr * zi;

        // 1/z² = conj(z²) / |z²|² = (z2_r - i·z2_i) / (z2_r² + z2_i²)
        // Note: |z²|² = |z|⁴ = r_sq²
        double r4 = r_sq * r_sq;
        double inv_z2_r = z2_r / r4;
        double inv_z2_i = -z2_i / r4;

        // z ← z² + c - 1/z²
        zr = z2_r + cr - inv_z2_r;
        zi = z2_i + ci - inv_z2_i;
        zr_sq = zr * zr;
        zi_sq = zi * zi;
        ++iter;
    }

    if (iter == max_iter)
        return 0.0;

    double total_sq = zr_sq + zi_sq;
    double log_zn = std::log2(std::log2(std::sqrt(total_sq)));
    return static_cast<double>(iter) - 1.0 + log_zn / rational_log2_log2_escape;
}

// ── Rendering ────────────────────────────────────────────────────────────────

// ── Clifford Attractor: density plot rendering ──────────────────────────────
//
// Clifford attractor: x' = sin(a·y) + c·cos(a·x), y' = sin(b·x) + d·cos(b·y)
// Rendered as a density plot (2D histogram). Iterate the map for many points,
// bin into a grid, then apply a colormap to the density values.
//
// Unlike escape-time fractals, there is no "inside/outside" — the attractor
// is a set of points that the orbit visits frequently.

static std::vector<uint8_t> render_clifford(const RenderParams &p)
{
    const size_t w = static_cast<size_t>(p.width);
    const size_t h = static_cast<size_t>(p.height);

    std::cout << "Rendering Clifford attractor " << w << "x" << h
              << "  a=" << p.clifford_a
              << "  b=" << p.clifford_b
              << "  c=" << p.clifford_c
              << "  d=" << p.clifford_d << "\n";

    // Density grid (uint32 for large counts)
    std::vector<uint32_t> density(w * h, 0);

    // The attractor typically lives in [-10, 10] × [-10, 10].
    // Map pixel coordinates to this range.
    constexpr double view_min = -10.0;
    constexpr double view_max =  10.0;
    double dx = (view_max - view_min) / static_cast<double>(w);
    double dy = (view_max - view_min) / static_cast<double>(h);

    const double a = p.clifford_a;
    const double b = p.clifford_b;
    const double cc = p.clifford_c;
    const double dd = p.clifford_d;

    // Number of iterations per seed point.
    constexpr int burn_in  = 100;  // discard initial iterations
    constexpr int plot_per = 500;  // plot iterations per seed
    constexpr int num_seeds = 2500000; // total seed points

    // Use a simple LCG for reproducible pseudo-random starting positions.
    // This gives better coverage than a grid and avoids std::rand().
    uint64_t lcg_state = 12345;
    auto lcg_next = [&lcg_state]() -> double {
        lcg_state = lcg_state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(lcg_state >> 33) / static_cast<double>(1ULL << 31);
    };

    std::cout << "  seeds=" << num_seeds << "  burn_in=" << burn_in
              << "  plot_per=" << plot_per << "\n";

    for (int seed = 0; seed < num_seeds; ++seed) {
        // Random starting position in [-10, 10] × [-10, 10]
        double x = view_min + lcg_next() * (view_max - view_min);
        double y = view_min + lcg_next() * (view_max - view_min);

        // Burn-in iterations
        for (int i = 0; i < burn_in; ++i) {
            double nx = std::sin(a * y) + cc * std::cos(a * x);
            double ny = std::sin(b * x) + dd * std::cos(b * y);
            x = nx;
            y = ny;
        }

        // Plot iterations
        for (int i = 0; i < plot_per; ++i) {
            double nx = std::sin(a * y) + cc * std::cos(a * x);
            double ny = std::sin(b * x) + dd * std::cos(b * y);
            x = nx;
            y = ny;

            // Map to pixel coordinates
            int px = static_cast<int>((x - view_min) / dx);
            int py = static_cast<int>((y - view_min) / dy);

            if (px >= 0 && px < static_cast<int>(w) && py >= 0 && py < static_cast<int>(h)) {
                density[py * w + px]++;
            }
        }

        if (seed % 500000 == 0)
            std::cout << "  seed " << seed << "/" << num_seeds << "\n";
    }

    // Find max density for normalisation
    uint32_t max_density = 0;
    for (size_t i = 0; i < w * h; ++i) {
        if (density[i] > max_density)
            max_density = density[i];
    }

    std::cout << "  max_density=" << max_density << "\n";

    // Convert density to RGB using the palette
    std::vector<uint8_t> pixels(w * h * bytes_per_pixel, 0);
    if (max_density > 0) {
        for (size_t y = 0; y < h; ++y) {
            for (size_t x = 0; x < w; ++x) {
                uint32_t d = density[y * w + x];
                if (d > 0) {
                    // Log-scale for better visual range
                    double t = std::log1p(static_cast<double>(d)) / std::log1p(static_cast<double>(max_density));
                    auto rgb = palette(t);
                    size_t idx = (y * w + x) * bytes_per_pixel;
                    pixels[idx + 0] = rgb[0];
                    pixels[idx + 1] = rgb[1];
                    pixels[idx + 2] = rgb[2];
                }
            }
        }
    }

    return pixels;
}

// ── Lyapunov Fractal: alternating logistic map ──────────────────────────────
//
// For each pixel (A, B), iterate an alternating logistic map:
//   x[n+1] = r[n] · x[n] · (1 - x[n])
// where r[n] alternates between A and B according to a pattern.
// Compute the Lyapunov exponent λ = (1/N) · Σ log₂(|r[n] · (1 - 2·x[n])|)
//
// λ < 0 → stable (attractor), λ > 0 → chaotic
// Color by the value of λ.
//
// A is mapped to pixel x (real axis), B to pixel y (imag axis).
// Both A and B range from 0 to 4 (the standard logistic map range).

static std::vector<uint8_t> render_lyapunov(const RenderParams &p)
{
    const size_t w = static_cast<size_t>(p.width);
    const size_t h = static_cast<size_t>(p.height);

    std::cout << "Rendering Lyapunov fractal " << w << "x" << h
              << "  pattern_period=" << p.lyapunov_pattern << "\n";

    // Build the pattern: "ABAB..." repeated lyapunov_pattern times
    std::vector<bool> pattern; // true = A, false = B
    for (int i = 0; i < p.lyapunov_pattern; ++i) {
        pattern.push_back(true);  // A
        pattern.push_back(false); // B
    }
    int plen = static_cast<int>(pattern.size());

    constexpr int burn_in = 200;
    constexpr int compute_iters = 1000;

    std::vector<uint8_t> pixels(w * h * bytes_per_pixel, 0);

    // A and B both range from 0 to 4
    constexpr double r_min = 0.0;
    constexpr double r_max = 4.0;

    for (size_t y = 0; y < h; ++y) {
        double B = r_min + (static_cast<double>(y) + 0.5) / static_cast<double>(h) * (r_max - r_min);
        uint8_t *row = pixels.data() + y * w * bytes_per_pixel;

        for (size_t x = 0; x < w; ++x) {
            double A = r_min + (static_cast<double>(x) + 0.5) / static_cast<double>(w) * (r_max - r_min);

            double cur_x = 0.5; // initial condition
            double lambda = 0.0;

            // Burn-in
            for (int i = 0; i < burn_in; ++i) {
                double r = pattern[i % plen] ? A : B;
                cur_x = r * cur_x * (1.0 - cur_x);
                if (cur_x <= 0.0 || cur_x >= 1.0) {
                    cur_x = 0.5; // reset if diverged
                }
            }

            // Compute Lyapunov exponent
            for (int i = 0; i < compute_iters; ++i) {
                double r = pattern[i % plen] ? A : B;
                double deriv = std::abs(r * (1.0 - 2.0 * cur_x));
                if (deriv > 1.0e-15)
                    lambda += std::log2(deriv);
                cur_x = r * cur_x * (1.0 - cur_x);
                if (cur_x <= 0.0 || cur_x >= 1.0) {
                    cur_x = 0.5;
                }
            }

            lambda /= compute_iters;

            // Map lambda to color. λ ranges roughly from -2 to +2.
            // Normalize to [0, 1]: t = (lambda + 2) / 4
            double t = (lambda + 2.0) / 4.0;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;

            auto rgb = palette(t);
            size_t idx = x * bytes_per_pixel;
            row[idx + 0] = rgb[0];
            row[idx + 1] = rgb[1];
            row[idx + 2] = rgb[2];
        }

        if (y % progress_interval_rows == 0)
            std::cout << "  row " << y << "/" << h << "\n";
    }

    return pixels;
}

// Returns an owned flat RGB buffer of size width × height × 3 bytes.
static std::vector<uint8_t> render(const RenderParams &p)
{
    const size_t w = static_cast<size_t>(p.width);
    const size_t h = static_cast<size_t>(p.height);
    const size_t pixel_count = w * h * bytes_per_pixel;
    std::vector<uint8_t> pixels(pixel_count);

    // Map pixel coordinates to the complex plane.
    double re_span = default_re_span / p.zoom;
    double im_span = default_im_span / p.zoom;

    double re_min = p.cx - re_span * 0.5;
    double im_min = p.cy - im_span * 0.5;

    double dr = re_span / static_cast<double>(w);
    double di = im_span / static_cast<double>(h);

    std::cout << "Rendering " << w << "x" << h
              << "  type=" << fractal_type_name(p.type)
              << "  centre=(" << std::setprecision(10) << p.cx << "," << p.cy << ")"
              << "  zoom=" << p.zoom
              << "  max_iter=" << p.max_iter << "\n";

    // Pre-compute normalised iteration scale.
    assert(p.max_iter >= 1);
    double iter_scale = colour_cycles / static_cast<double>(p.max_iter);

    // Pre-compute roots of unity (shared across all pixels).
    // Needed for Newton and Nova fractals.
    newton_roots nroots = (p.type == FractalType::Newton || p.type == FractalType::Nova)
        ? newton_roots::compute(p.newton_degree)
        : newton_roots{};

    // Render row by row (cache-friendly).
    // Sample at pixel centres (offset by 0.5) for anti-aliasing.
    for (size_t y = 0; y < h; ++y) {
        double ci = im_min + (static_cast<double>(y) + 0.5) * di;
        uint8_t *row = pixels.data() + y * w * bytes_per_pixel;

        double cr = re_min + 0.5 * dr;
        for (size_t x = 0; x < w; ++x) {
            double smooth = 0.0;

            switch (p.type) {
                case FractalType::Mandelbrot:
                    smooth = mandelbrot_smooth(cr, ci, p.max_iter);
                    break;
                case FractalType::Julia:
                    smooth = julia_smooth(cr, ci, p.julia_cr, p.julia_ci, p.max_iter);
                    break;
                case FractalType::Burnship:
                    smooth = burnship_smooth(cr, ci, p.max_iter);
                    break;
                case FractalType::Tricorn:
                    smooth = tricorn_smooth(cr, ci, p.max_iter);
                    break;
                case FractalType::Multibrot:
                    smooth = multibrot_smooth(cr, ci, p.max_iter, p.multibrot_degree);
                    break;
                case FractalType::Newton:
                    smooth = newton_smooth(cr, ci, p.max_iter, p.newton_degree, nroots);
                    break;
                case FractalType::Phoenix:
                    smooth = phoenix_smooth(cr, ci, p.phoenix_cr, p.phoenix_ci, p.phoenix_p, p.max_iter);
                    break;
                case FractalType::Magnet:
                    smooth = magnet_smooth(cr, ci, p.max_iter, p.magnet_variant);
                    break;
                case FractalType::Barnsley:
                    smooth = barnsley_smooth(cr, ci, p.barnsley_cr, p.barnsley_ci, p.barnsley_variant, p.max_iter);
                    break;
                case FractalType::Nova:
                    smooth = nova_smooth(cr, ci, p.max_iter, p.newton_degree, nroots, p.nova_cr, p.nova_ci);
                    break;
                case FractalType::Transcendental:
                    smooth = transcendental_smooth(cr, ci, p.max_iter, p.transcendental_variant);
                    break;
                case FractalType::Rational:
                    smooth = rational_smooth(cr, ci, p.max_iter);
                    break;
                case FractalType::Clifford:
                    // Should never reach here — Clifford uses render_clifford()
                    assert(false);
                    break;
                case FractalType::Lyapunov:
                    // Should never reach here — Lyapunov uses render_lyapunov()
                    assert(false);
                    break;
            }

            if (smooth == 0.0) {
                row[x * bytes_per_pixel + 0] = 0;
                row[x * bytes_per_pixel + 1] = 0;
                row[x * bytes_per_pixel + 2] = 0;
            } else {
                // Map smooth iteration count to [0,1) for palette lookup.
                // The guard for negative t handles cases where smooth < 1.0
                // (points that escaped on the first iteration).
                double t = std::fmod(smooth * iter_scale, 1.0);
                if (t < 0.0) t += 1.0;
                auto rgb = palette(t);
                row[x * bytes_per_pixel + 0] = rgb[0];
                row[x * bytes_per_pixel + 1] = rgb[1];
                row[x * bytes_per_pixel + 2] = rgb[2];
            }
            cr += dr;
        }

        if (y % progress_interval_rows == 0)
            std::cout << "  row " << y << "/" << h << "\n";
    }

    return pixels;
}

// ── PNG writing via stb_image_write ─────────────────────────────────────────

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static bool write_png(const std::string &path,
                      const std::vector<uint8_t> &pixels, int w, int h)
{
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cerr << "error: cannot open '" << path << "' for writing: "
                  << std::strerror(errno) << "\n";
        return false;
    }

    int ret = stbi_write_png_to_func(
        [](void *ctx, void *data, int size) {
            std::ofstream *f = static_cast<std::ofstream *>(ctx);
            f->write(static_cast<const char *>(data), size);
        },
        &ofs, w, h, bytes_per_pixel, pixels.data(), w * bytes_per_pixel);

    ofs.close();
    if (ret == 0) {
        std::cerr << "error: failed to write PNG data to '" << path << "'\n";
        std::remove(path.c_str()); // remove corrupt partial file
        return false;
    }

    return true;
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char **argv)
{
    auto result = parse_args(argc, argv);

    if (std::holds_alternative<bool>(result))
        return 0;

    if (std::holds_alternative<std::monostate>(result))
        return 1;

    const auto &p = std::get<RenderParams>(result);

    if (!validate_params(p))
        return 1;

    auto pixels = (p.type == FractalType::Clifford)
        ? render_clifford(p)
        : (p.type == FractalType::Lyapunov)
          ? render_lyapunov(p)
          : render(p);

    if (!write_png(p.output, pixels, p.width, p.height))
        return 1;

    std::cout << "Wrote " << p.output << " (" << p.width << "x" << p.height << ")\n";
    return 0;
}

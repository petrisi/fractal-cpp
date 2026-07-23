# Mandelbrot Fractal Renderer

A high-performance C++20 fractal renderer that produces PNG images from the command line. Supports **14 fractal types** ranging from classic escape-time sets to density-plot attractors and Lyapunov exponent fractals. Single source file, no external dependencies beyond the C++ standard library and [stb_image_write](https://github.com/nothings/stb) (public domain).

## Features

- **14 fractal types** with type-specific default viewports and parameters
- **Invertible cosine palette** (Akenine-Möller / Shirley trick) for smooth, continuous colouring
- **Smooth iteration** via `log₂(log₂(|z|))` normalisation — no banding
- **Overflow and pole protection** for transcendental and rational functions
- **Density-plot rendering** for Clifford attractors (2.5 M seeds, LCG random)
- **Lyapunov exponent** computation for alternating logistic map fractals
- **Row-by-row rendering** with progress reporting for large images
- **Sanity limits**: max 16384×16384, max 65536 iterations, max degree 64

## Fractal Types

### Escape-time fractals

| # | Type | Formula | Mode | Default View | Extra Flags |
|---|------|---------|------|-------------|-------------|
| 1 | `mandelbrot` | z → z² + c | Mandelbrot | Seahorse Valley (-0.75, 0) | — |
| 2 | `julia` | z → z² + c | Julia | (0, 0) | `-j <re,im>` (default: -0.7269, 0.1889 "Dendrite") |
| 3 | `burnship` | z → swap(z²) + c | Mandelbrot | (-0.15, 0.65) | — |
| 4 | `tricorn` | z → conj(z)² + c | Mandelbrot | (-0.3, 0) | — |
| 5 | `multibrot` | z → zⁿ + c | Mandelbrot | (-0.1, 0.7) | `-d <degree>` (default: 3; n=2 is standard Mandelbrot) |
| 6 | `newton` | z → ((n-1)z + 1/zⁿ⁻¹) / n | Julia | (0, 0) | `-d <degree>` (default: 3; colours by root index) |
| 7 | `phoenix` | z → z² + c + p·z[n-1] | Julia | (0, 0) | `-j <re,im>` (default: 0.5667, 0), `-p <val>` (default: -0.5) |
| 8 | `magnet` | M1: \|(z²+(c-1))/(2z+(c-2))\|²; M2: cubic variant | Mandelbrot | (0, 0), zoom=0.3 | `-m <1\|2>` (default: 1) |
| 9 | `barnsley` | V1: z²+c·Re(z); V2: z²+c·Im(z); V3: z²+c·Re(z)·Im(z) | Julia | (0, 0) | `-j <re,im>` (default: 0, 0), `-b <1\|2\|3>` (default: 1) |
| 10 | `nova` | ((n-1)z + 1/zⁿ⁻¹) / n + c | Julia | (0, 0) | `-j <re,im>` (default: 0.3, 0.2), `-d <degree>` (default: 3) |
| 11 | `transcendental` | z → f(z) + c; f ∈ {sin, cos, exp, tanh} | Mandelbrot | (0, 0), zoom=0.5 | `-f <1\|2\|3\|4>` (default: 1 = sin) |
| 12 | `rational` | z → z² + c − 1/z² | Julia | (0, 0) | `-j <re,im>` (default: 0.5, 0.3) |

<img width="3220" height="1964" alt="image" src="https://github.com/user-attachments/assets/23bc9685-55dd-42b4-8e44-b8a12e596041" />

### Alternative rendering paradigms

| # | Type | Approach | Default Parameters | Extra Flags |
|---|------|----------|-------------------|-------------|
| 13 | `clifford` | Density plot (2D histogram, 2.5 M seeds, LCG random seeding, log-scale colouring) | a=-1.4, b=1.6, c=1.0, d=0.7 | `-a`, `-bb`, `-cc`, `-dd` |
| 14 | `lyapunov` | Lyapunov exponent per pixel (alternating logistic map, colours by λ) | period=2 ("ABAB") | `-lp <period>` (1–16) |

## Build

Requires **CMake ≥ 3.16** and a **C++20**-capable compiler (GCC 11+, Clang 14+).

```bash
mkdir build && cd build
cmake ..
make
```

The binary `mandelbrot` is produced in the `build/` directory.

## Usage

```bash
./build/mandelbrot [options]
```

### Global options

| Flag | Description | Default |
|------|-------------|---------|
| `-t <type>` | Fractal type (see table above) | `mandelbrot` |
| `-c <re,im>` | Viewport centre in the complex plane | type-dependent |
| `-z <zoom>` | Zoom factor (higher = closer) | type-dependent |
| `-i <iterations>` | Maximum iteration count | type-dependent (128–256) |
| `-w <width>` | Image width in pixels | 1920 |
| `-h <height>` | Image height in pixels | 1080 |
| `-o <file>` | Output PNG path | `fractal.png` |
| `--help` | Show usage and exit | — |

### Examples

```bash
# Default Mandelbrot (Seahorse Valley, 1920×1080)
./build/mandelbrot -o mandelbrot.png

# Julia set — "Dendrite" constant
./build/mandelbrot -t julia -o julia.png

# Julia set with custom constant
./build/mandelbrot -t julia -j -0.4,0.6 -o julia_custom.png

# Phoenix fractal with Ushiki's classic parameters
./build/mandelbrot -t phoenix -j 0.5667,0 -p -0.5 -o phoenix.png

# Magnet 1 (Sierpinski gasket structure)
./build/mandelbrot -t magnet -m 1 -o magnet1.png

# Multibrot degree 5
./build/mandelbrot -t multibrot -d 5 -o multibrot5.png

# Newton fractal (roots of z⁷ - 1 = 0)
./build/mandelbrot -t newton -d 7 -o newton7.png

# Nova (perturbed Newton, degree 4)
./build/mandelbrot -t nova -j 0.3,0.2 -d 4 -o nova.png

# Transcendental (cos variant)
./build/mandelbrot -t transcendental -f 2 -o transcendental_cos.png

# Rational Julia set (pole at origin)
./build/mandelbrot -t rational -j 0.5,0.3 -o rational.png

# Clifford attractor with custom parameters
./build/mandelbrot -t clifford -a -1.8 -bb 1.6 -cc 1.0 -dd 0.7 -o clifford.png

# Lyapunov fractal (period 4 = ABABABAB)
./build/mandelbrot -t lyapunov -lp 4 -o lyapunov.png

# Deep zoom into Seahorse Valley
./build/mandelbrot -c -0.74875,0.1035 -z 2000 -i 1000 -o seahorse_zoom.png

# High-res output (4K)
./build/mandelbrot -w 3840 -h 2160 -i 512 -o mandelbrot_4k.png
```

## Architecture

```
mandelbrot
├── Constants & limits
├── FractalType enum (14 types)
├── RenderParams struct (all parameters)
├── CLI parser (two-pass: pre-scan -t, then parse args)
├── Parameter validation
├── Colour palette (invertible cosine, 64 cycles)
├── Iteration functions (one per escape-time type)
├── Special renderers (Clifford density, Lyapunov exponent)
├── Row-by-row dispatcher (render → colour → buffer)
├── PNG writer (stb_image_write via callback)
└── main() (parse → validate → render → write)
```

### Key design decisions

- **Two-pass CLI parsing**: `-t` is pre-scanned to apply type-specific defaults *before* the main parse loop, so user-specified flags correctly override defaults (not the other way around).
- **Single source file**: everything lives in `src/main.cpp` for simplicity and portability.
- **Row-by-row rendering**: processes one scanline at a time for cache-friendly memory access.
- **Overflow guards**: transcendental functions (`sin`, `cos`, `exp`) check argument bounds before calling libc to avoid silent `inf`/`NaN` propagation.
- **Pole detection**: rational functions check `|denominator|² ≤ 1e-10` before division to avoid blow-up at singularities.
- **Nova escape radius**: perturbed Newton orbits add an escape check (`|z| > 50`) for divergent orbits that never converge to a root.

## Notable fractals

- **Phoenix** — Discovered by Shigehiro Ushiki (1988). A second-order recurrence (`z[n]` depends on `z[n-1]`) producing flame-like spirals. Uses conjugate-swap initialisation (`z₀ = conj(pixel)`, `z₋₁ = 0`) for correct orientation.
- **Magnet** — Derived from magnetic phase renormalisation (Yang-Lee edge singularities). Iterates the squared modulus of a rational function, producing Sierpinski-gasket-like structures. Mandelbrot mode (`z₀ = 0`).
- **Barnsley** — From Michael Barnsley's *Fractals Everywhere*. Three Julia-set variants that multiply the constant by `Re(z)`, `Im(z)`, or `Re(z)·Im(z)` to create crystalline structures.
- **Nova** — Newton's method on `zⁿ − 1 = 0` with a constant perturbation `+c`. Small `c` values create Julia-like basin boundaries.
- **Transcendental** — Replaces the polynomial `z²` with `sin`, `cos`, `exp`, or `tanh`. Produces wild, organic structures very different from polynomial fractals.
- **Rational** — The `−1/z²` term introduces a pole at the origin. Rendered in Julia mode (`z₀ = pixel`, c fixed), creating structures with characteristic holes around the pole.
- **Clifford** — A 2D discrete dynamical system (`x' = sin(ay) + c·cos(ax)`, `y' = sin(bx) + d·cos(by)`). Rendered as a density plot rather than escape-time.
- **Lyapunov** — Computes the Lyapunov exponent `λ` for alternating logistic maps. `λ < 0` indicates stable orbits (attractors), `λ > 0` indicates chaos.

## Dependencies

| Dependency | License | Purpose |
|------------|---------|---------|
| `stb_image_write.h` | Public domain (Sean Barrett) | PNG writer (single-header, v1.16) |
| C++20 STL | — | Standard library |
| libm | — | Math functions (`sin`, `cos`, `exp`, `log`, etc.) |

No package managers, no build-time dependencies beyond CMake and a C++20 compiler.

## License

The project code is released under the MIT License. `stb_image_write.h` is public domain.

# SketchCraft

A desktop 3D modeller written in C++20 with Qt 6 and OpenGL. SketchCraft opens,
edits, and saves models in the SketchUp 2017 `.skp` format.

The app includes drawing tools (line, rectangle, circle, polygon, and arc),
push/pull, move, rotate, scale, materials, groups and components, layers, scenes,
camera navigation, undo/redo, and image export. Some menu commands are placeholders;
this is an experimental application, and full SketchUp compatibility is not guaranteed.

## Requirements

- CMake 3.25 or newer
- A C++20 compiler
- Qt 6 with Core, Gui, Widgets, OpenGL, OpenGLWidgets, and Test modules
- A desktop graphics environment with OpenGL 2.1 support

SketchUp and its SDK are not required to build or run the app. On macOS, the app
can optionally discover templates from an existing SketchUp installation. No
SketchUp binaries, templates, or sample models are included in this repository.

## Build

From the repository root, with Qt available to CMake:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

If CMake cannot find Qt, add `-DCMAKE_PREFIX_PATH=/path/to/qt` to the configure
command, pointing to your Qt installation prefix.

For example, with Homebrew's `qtbase` package on macOS:

```sh
brew install cmake qtbase
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase)"
cmake --build build --parallel
```

For a multi-configuration generator, add `--config Release` to the build command.
Executables are written to `build/bin/` (with `.exe` on Windows).

## Run

```sh
./build/bin/sketchcraft
./build/bin/sketchcraft /path/to/model.skp
```

Use the drawing and editing tools from the menus or toolbars. The measurement
field accepts typed dimensions. Use **File → New from Template → Empty (built-in)**
to start without an external template.

To capture the app window and exit after about 120 frames:

```sh
./build/bin/sketchcraft /path/to/model.skp --screenshot /tmp/sketchcraft.png
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

Tests cover model reading/writing, geometry, editing, and interaction with the
real application window. The UI tests require a working display and OpenGL
context. For just the core suite in a headless environment:

```sh
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^sketchcraft_tests$' --output-on-failure
```

Optional tests against your own `.skp` files are enabled by setting
`SKETCHCRAFT_TEMPLATES` to a directory containing those files. Tests using those
files are skipped when the variable is unset.

Address and undefined-behaviour sanitizers can be enabled with
`-DCRAFT_SANITIZE=ON` when configuring with a supported non-MSVC compiler.

## Command-line model tool

The build also produces `skptool` for inspecting and checking models:

```sh
./build/bin/skptool info /path/to/model.skp
./build/bin/skptool roundtrip /path/to/model.skp
./build/bin/skptool resave /path/to/input.skp /path/to/output.skp
```

## Source layout

- `src/skp/`: model representation and `.skp` reader/writer
- `src/geom/`: geometry and triangulation
- `src/render/`: scene building and OpenGL viewport
- `src/edit/`: interactive modelling tools
- `src/ui/`: application window, document, and panels
- `src/tools/`: command-line model tool
- `tests/`: core and UI test suites

## License

No project license has been selected yet. Qt and other external dependencies
retain their own licenses. SketchCraft is an independent project and is not
affiliated with or endorsed by SketchUp or Trimble.

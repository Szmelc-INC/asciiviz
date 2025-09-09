<div align="center">

<h1><samp><b>asciiviz</b></samp></h1>

<table><tr><td>ASCII/ANSI function visualizer for your terminal</td></tr></table>
<i><samp><h4>Render math expressions, Mandelbrot and Julia fractals with colorful palettes</h4></samp></i>

</div>

---

> [!TIP]
> ## Quick Install
> Clone
> ```sh
> git clone https://github.com/Szmelc-INC/asciiviz.git && cd asciiviz
> ```
> Build, install and clean
> ```sh
> make && sudo make install && make clean
> ```

---

> [!NOTE]
> ## Features
> - Expression based renderer (`expr` mode)
> - Mandelbrot and Julia fractals
> - UTF‑8 character and 256‑color palette system
> - Baked presets, external `.cfg` files and hot reload (`r` key)
> - Background fill characters and whitespace transparency
> - Interactive control: pan, zoom, change palettes and more

---

> [!TIP]
> ## Usage
> ```bash
> asciiviz [--config file] [--preset NAME] [--char NAME] [--color NAME] [--background UTF8] [--color-func]
> ```
> ### Flags
> | Flag | Description |
> |------|-------------|
> | `--config <file>` | Load external configuration (`functions/*.cfg`) |
> | `--preset <name>` | Use baked function preset |
> | `--char <name>` | Select character palette |
> | `--color <name>` | Select color palette |
> | `--background <utf8>` | Override background fill glyph |
> | `--color-func` | Derive color index from function value |
>
> ### Hotkeys
> | Action | Key(s) |
> |---|---|
> | Quit | `q` |
> | Pause | `p` |
> | Toggle info HUD | `i` |
> | Toggle whitespace transparency | `W` |
> | Cycle background | `w` |
> | FPS ± | `+` / `-` |
> | Toggle color | `C` |
> | Next color palette | `c` |
> | Toggle color‑math | `f` |
> | Next char palette | `n` |
> | Next function preset | `m` |
> | Reload config | `r` |
> | Pan | arrow keys |
> | Zoom | `[` / `]` |

---

> [!IMPORTANT]
> ## Compiling
> ```makefile
> # make                 # build with baked presets & palettes
> # make run             # run with defaults
> # sudo make install    # install to /usr/local/bin (override PREFIX/BINDIR)
> # sudo make uninstall  # remove installed binary
> # make clean           # clean artifacts
> ```

---

> [!NOTE]
> ## Project Structure
> ```text
> asciiviz/
> ├── functions/        # function presets (*.cfg)
> ├── palettes/         # character and color palettes
> ├── main.c            # application entry
> ├── terminal.c/.h     # terminal helpers
> ├── util.c/.h         # utility functions
> └── Makefile          # build script
> ```

> ### Config Example
> ```ini
[render]
fps=30
duration=-1          ; -1 = infinite
use_color=1          ; 1=256-color, 0=mono
width=0              ; 0 = use terminal width
height=0             ; 0 = use terminal height
charset=" .:-=+*#%@" ; ramp for mono

[mode]
type=expr            ; expr | mandelbrot | julia

[expr]
value="sin(6.0*(x+0.2*sin(t*0.7))+t)*cos(6.0*(y+0.2*cos(t*0.5))-t)"
color="128+127*sin(t+3.0*r)" ; 0..255 (only used if use_color=1)

[fractal]
max_iter=200
center_x=-0.5
center_y=0.0
scale=2.8
c_re=-0.8             ; julia only
c_im=0.156            ; julia only
```
Character palette template:
```ini
[char]
name="thin"
charset=" .:-=+*#%@"
```
Color palette template:
```ini
[color]
name="rainbow_bands"
codes="21,27,33,39,45,51,50,49,48,47"

[effect]
# diagonal bands; wrap by palette length (n)
index="floor(i*0.07 + j*0.07 + t*3.0) mod n"
```

---

> [!CAUTION]
> ## Requirements
> - POSIX like environment (Linux, BSD, macOS)
> - `gcc`/`clang` and `make`
> - UTF‑8 capable terminal

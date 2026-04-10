# Bottom Terminal

A multi-tabbed VTE terminal plugin for [Geany](https://www.geany.org/) that replaces the built-in terminal with a more capable one, including tmux integration.

## Features

### Terminal
- Multiple terminal tabs with scrollable tab bar
- Tab reordering via drag-and-drop
- Right-click context menu (copy, paste, select all, clear scrollback, tab management)
- Configurable font and 14 built-in color schemes (Tango, Solarized, Monokai, Gruvbox, Dracula, Nord, Tokyo Night, One Dark, Catppuccin Mocha, Seoul256, Turbo Pascal)
- Import colors from the active Geany theme
- Individual ANSI palette color pickers
- 10,000-line scrollback
- Persistent paned split position across sessions

### Run in Terminal
- Execute Geany's configured build commands in a dedicated tab
- Placeholder expansion (`%f`, `%d`, `%e`, `%p`)
- Auto-saves before running
- Shows exit code and waits for dismissal

### tmux Integration
- Attach to existing tmux sessions or create new ones via control mode (`tmux -C`)
- Each tmux window appears as a native Geany tab (with grid icon)
- Live tab titles from the running command (e.g. `0,@0: vim`)
- Pane content captured on attach so tabs aren't blank
- Input forwarded via `send-keys`; output rendered via `vte_terminal_feed()`
- Create/close tmux windows from Geany
- Detach cleanly — tmux session survives plugin unload or Geany exit
- Only exposed when `tmux` is found in `PATH`

### Keybindings

All customizable through Geany's preferences:

| Action | Default |
|---|---|
| New terminal tab | *(unset)* |
| Close terminal tab | *(unset)* |
| Focus terminal | *(unset)* |
| Next tab | *(unset)* |
| Previous tab | *(unset)* |
| Toggle terminal | *(unset)* |
| Run in terminal | *(unset)* |
| Attach tmux session | *(unset)* |
| Detach tmux session | *(unset)* |
| New tmux window | *(unset)* |

Terminal-local shortcuts: `Alt+C` / `Ctrl+Shift+C` (copy), `Alt+V` / `Ctrl+Shift+V` (paste).

## Building

### Dependencies

- [Geany](https://www.geany.org/) >= 2.0 (with development headers)
- GTK+ 3.0
- VTE 2.91
- [Meson](https://mesonbuild.com/) >= 0.60
- A C11 compiler

On Arch Linux:
```sh
sudo pacman -S geany vte3 meson
```

On Debian/Ubuntu:
```sh
sudo apt install geany libgeany-dev libvte-2.91-dev meson
```

### Build & Install

```sh
meson setup build
ninja -C build
ninja -C build install
```

The plugin installs to Geany's plugin directory automatically (`lib/geany/geany-bottom-term.so`).

### Development

For local development without system-wide install, symlink into your user plugin directory:

```sh
meson setup build
ninja -C build
ln -sf "$(pwd)/build/geany-bottom-term.so" ~/.config/geany/plugins/
```

Reload the plugin from Geany's Plugin Manager after rebuilding.

## Configuration

All settings are stored in `~/.config/geany/plugins/bottom-terminal.conf` and can be changed through the plugin's preferences dialog (Edit > Plugin Preferences > Bottom Terminal):

- Font selection
- Color scheme (preset or custom)
- Foreground, background, and 16 ANSI palette colors

## Project Structure

```
src/
  plugin.c      Main lifecycle, settings, keybindings, menus
  terminal.c    VTE terminal creation and management
  tab_manager.c Tab notebook UI and lifecycle
  colors.c      Color scheme definitions and Geany theme import
  reparent.c    Widget hierarchy injection for the paned layout
  tmux.c        tmux control mode protocol and bridge
```

## License

MIT

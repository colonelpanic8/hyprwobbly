# hyprwobbly

Compiz-style wobbly windows for Hyprland.

This is a standalone Hyprland plugin. It attaches an `IWindowTransformer` to
each mapped window, simulates a small spring mesh over the window bounds, and
renders Hyprland's offscreen window framebuffer through a deformed OpenGL grid.

## Build

```sh
nix build
```

For `hyprpm`:

```sh
hyprpm add .
hyprpm enable hyprwobbly
```

## Configuration

By default, the plugin applies wobble to all window movement and resize geometry
changes, including keyboard dispatchers such as `moveactive`,
`movewindowpixel`, and layout-driven movements.

```ini
plugin {
  hyprwobbly {
    enabled = true
    mode = always
    grid_width = 4
    grid_height = 4
    tiles_x = 12
    tiles_y = 12
    spring_k = 18.0
    friction = 8.0
    mass = 12.0
    move_factor = 0.65
    resize_factor = 0.45
    max_warp = 140.0
  }
}
```

You can also make wobble opt in through Hyprland's `windowsMove` animation
style:

```ini
animation = windowsMove, 1, 4, default, wobbly

plugin {
  hyprwobbly {
    mode = style
  }
}
```

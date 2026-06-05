# wlclip-watch

I built this small daemon to watch the Wayland clipboard contents and print it to stdout every time the clipboard changes as an excuse to learn the [Wayland protocol,](https://wayland-book.com/), libwayland, and write some modern C++ on top of it. 

I wanted to create a replacement for my existing workflow for clipboard-watching that would spawn a `wl-paste` subprocess every 200ms. So instead of paying the cost for spamming forking of subprocesses, we can just use a small memory-footprint event-driven daemon that talks to the Wayland clipboard.

This project uses the [ext-data-control-v1 protocol](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/releases).

This *should* work on Niri, Hyprland, sway, KDE Plasma, and any other compositors that support ext-data-control-v1. I've only tested it on Niri.

## Build instructions

Install (places at ~/.local/bin/wlclip-watch):
```
cmake --install build --prefix ~/.local
```

```bash
cmake -B build
cmake --build build
./build/wlclip-watch
```

## TODO

- [ ] Zero-copy I/O: Replace the read/write pipe loop with `splice(2)` to move data between pipes in kernel-space without context-switch overhead
- [ ] Integrate this into my custom-made `texthook-rs` in Rust with axum+tokio that will consume this binary's stdout and serve it as HTTP/SSE to stream clipboard contents to a [web page](https://texthooker.com/) for [language mining](https://youtu.be/bbg6ztWecbU?si=anPlQi2eac5O9x2U)


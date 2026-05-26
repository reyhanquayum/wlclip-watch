// wlclip-watch — print clipboard contents to stdout each time the Wayland
// selection changes. Uses the ext-data-control-v1 protocol so the program
// does not need to own a window/surface (most Wayland clipboard APIs require
// focus).
//
// ext-data-control-v1 is the official, cross-compositor successor to the
// older (now deprecated) wlr-data-control-unstable-v1. It lives in the
// `staging/` set of wayland-protocols. Supported by niri, sway, hyprland,
// wayfire, river, and KDE Plasma. See README for the up-to-date support
// matrix and notes on GNOME.
//
// Reference implementation in C: bugaevc/wl-clipboard (see README).
//
// Build:
//     cmake -B build && cmake --build build
//     ./build/wlclip-watch

#include <memory>
#include <print>
#include <sys/types.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include "ext-data-control-v1-client-protocol.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {

// Add more mimetypes if want HTML / RTF / etc.
constexpr const char* kPreferredMimes[] = {
    "text/plain;charset=utf-8",
    "text/plain",
    "UTF8_STRING",
    "STRING",
    "TEXT",
};

// RAII wrappers for wayland state handlers so no need to manually clean up

struct WlDisplayDeleter{
  void operator()(wl_display* d) const {wl_display_disconnect(d);}
};

using WlDisplayPtr = std::unique_ptr<wl_display, WlDisplayDeleter>;

struct WlRegistryDeleter{
  void operator()(wl_registry* r) const {wl_registry_destroy(r);}
};

using WlRegistryPtr = std::unique_ptr<wl_registry, WlRegistryDeleter>;

struct WlSeatDeleter{
  void operator()(wl_seat* s) const {wl_seat_destroy(s);}
};

using WlSeatPtr = std::unique_ptr<wl_seat, WlSeatDeleter>;

struct ControlManagerDeleter{
  void operator()(ext_data_control_manager_v1* m) const {ext_data_control_manager_v1_destroy(m);}
};

using ControlManagerPtr = std::unique_ptr<ext_data_control_manager_v1, ControlManagerDeleter>;

struct ControlDeviceDeleter{
  void operator()(ext_data_control_device_v1* d) const{ext_data_control_device_v1_destroy(d);}
};

using ControlDevicePtr = std::unique_ptr<ext_data_control_device_v1, ControlDeviceDeleter>;

// wayland handlers filled in during registry binding
struct State {
    WlDisplayPtr display;
    WlRegistryPtr registry;
    WlSeatPtr seat;
    ControlManagerPtr manager;
    ControlDevicePtr device;

    // Per-offer scratch state. The compositor will call our offer() callback
    // once per supported mime type, so we accumulate the best match here.
    std::string chosen_mime;
    int chosen_priority = -1;  // lower = better (index into kPreferredMimes)
};

// ────────────────────────────────────────────────────────────────────────────
// wl_registry listener: fires once per global the compositor exposes.
// ────────────────────────────────────────────────────────────────────────────

void on_registry_global(void* data, wl_registry* registry, uint32_t name,
                        const char* interface, uint32_t /*version*/) {
    auto* s = static_cast<State*>(data);

    if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        s->seat.reset(static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, 1)));
    } else if (std::strcmp(interface, ext_data_control_manager_v1_interface.name) == 0) {
        s->manager.reset(static_cast<ext_data_control_manager_v1*>(
            wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1)));
    }
}

void on_registry_global_remove(void*, wl_registry*, uint32_t) {
}

const wl_registry_listener kRegistryListener = {
    .global = on_registry_global,
    .global_remove = on_registry_global_remove,
};

// ────────────────────────────────────────────────────────────────────────────
// ext_data_control_offer_v1 listener: tells us which mime types this
// particular offer supports. Called multiple times — once per mime type.
// ────────────────────────────────────────────────────────────────────────────

void on_offer_mime(void* data, ext_data_control_offer_v1* /*offer*/,
                   const char* mime_type) {
    auto* s = static_cast<State*>(data);
    for (int i = 0; i < (int)(sizeof(kPreferredMimes) / sizeof(*kPreferredMimes)); ++i) {
        if (std::strcmp(mime_type, kPreferredMimes[i]) == 0) {
            if (s->chosen_priority < 0 || i < s->chosen_priority) {
                s->chosen_priority = i;
                s->chosen_mime = mime_type;
            }
            return;
        }
    }
}

const ext_data_control_offer_v1_listener kOfferListener = {
    .offer = on_offer_mime,
};

// ────────────────────────────────────────────────────────────────────────────
// ext_data_control_device_v1 listener: this is where the action happens.
//
// Lifecycle of a clipboard change:
//   1. data_offer    — compositor announces a new offer object
//   2. (offer.offer) — that offer announces its mime types (kOfferListener)
//   3. selection     — the offer becomes the current clipboard
//   4. (we read it)  — we pipe(), receive(), then read the fd
// ────────────────────────────────────────────────────────────────────────────

void on_device_data_offer(void* data, ext_data_control_device_v1* /*device*/,
                          ext_data_control_offer_v1* offer) {
    auto* s = static_cast<State*>(data);
    s->chosen_mime.clear();
    s->chosen_priority = -1;
    ext_data_control_offer_v1_add_listener(offer, &kOfferListener, s);
}

void on_device_selection(void* data, ext_data_control_device_v1* /*device*/,
    ext_data_control_offer_v1* offer) {

    auto* s = static_cast<State*>(data);

    // offer == nullptr means "selection cleared" (no clipboard contents).
    if (!offer) return;

    if (s->chosen_mime.empty()) {
        // No text-ish mime types; this is probably an image or something.
        ext_data_control_offer_v1_destroy(offer);
        return;
    }

    int fds[2];
    if(pipe(fds) == -1){
      std::println(stderr, "pipe failed: {}", std::strerror(errno)); 
      return;
    }
    // reminder: fds[0] = read end, fds[1] = write end

    ext_data_control_offer_v1_receive(offer, s->chosen_mime.c_str(), fds[1]);
    close(fds[1]);
    wl_display_flush(s->display.get());
    char buf[4096];
    ssize_t got = 0;
    while((got = read(fds[0], buf, sizeof(buf))) > 0){
      fwrite(buf,  1, got, stdout);
    }
    fputc('\n', stdout);
    fflush(stdout);
    close(fds[0]);

    ext_data_control_offer_v1_destroy(offer);
}

void on_device_finished(void* data, ext_data_control_device_v1* /*device*/) {
  auto *s = static_cast<State*>(data);
  s->device.reset();
  std::exit(0);
}

void on_device_primary_selection(void*, ext_data_control_device_v1*,
                                 ext_data_control_offer_v1* offer) {
    // Middle-click (primary) selection. We don't care about this for mining;
    // just destroy the offer so the compositor doesn't think we want it.
    if (offer) ext_data_control_offer_v1_destroy(offer);
}

const ext_data_control_device_v1_listener kDeviceListener = {
    .data_offer = on_device_data_offer,
    .selection = on_device_selection,
    .finished = on_device_finished,
    .primary_selection = on_device_primary_selection,
};

}  // namespace

int main() {
    State state;

    state.display = WlDisplayPtr{wl_display_connect(nullptr)};
    if (!state.display) {
        std::println(stderr, "wlclip-watch: failed to connect to Wayland display");
        std::println(stderr, "  (is WAYLAND_DISPLAY set? are you running on Wayland?)");
        return 1;
    }

    state.registry = WlRegistryPtr{wl_display_get_registry(state.display.get())};
    wl_registry_add_listener(state.registry.get(), &kRegistryListener, &state);

    // Roundtrip: send our pending requests and wait for the server's reply.
    // After this, on_registry_global has fired once per global.
    wl_display_roundtrip(state.display.get());

    if (!state.seat) {
        std::println(stderr, "wlclip-watch: no wl_seat advertised by compositor");
        return 1;
    }
    if (!state.manager) {
        std::print(stderr,
            "wlclip-watch: compositor doesn't expose ext_data_control_manager_v1.\n"
            "  This protocol is required. Supported on niri, sway, hyprland,\n"
            "  wayfire, river, and KDE Plasma. See README for the support matrix.\n");
        return 1;
    }

    state.device.reset(ext_data_control_manager_v1_get_data_device(
        state.manager.get(), state.seat.get()));
    ext_data_control_device_v1_add_listener(state.device.get(), &kDeviceListener, &state);

    // Main event loop. Each iteration dispatches whatever events arrived and
    // calls our listener callbacks. wl_display_dispatch blocks until events
    // are available, so this loop is cheap when idle.
    while (wl_display_dispatch(state.display.get()) != -1) {
        // (callbacks did the work)
    }
    return 0;
}

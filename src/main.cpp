// wlclip-watch — print clipboard contents to stdout each time the Wayland
// selection changes. Uses the wlr-data-control-unstable-v1 protocol so the
// program does not need to own a window/surface (most Wayland clipboard APIs
// require focus). Compositors that implement this protocol: niri, sway,
// hyprland, wayfire, river, KDE Plasma 5.27+. GNOME does NOT.
//
// Reference implementation in C: bugaevc/wl-clipboard (see README).
//
// Build:
//     cmake -B build && cmake --build build
//     ./build/wlclip-watch

#include <wayland-client.h>
#include "wlr-data-control-unstable-v1-client-protocol.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {

// Mime types we care about, in priority order. Most clipboards offer all of
// these; we pick the first match. Add more if you want HTML / RTF / etc.
constexpr const char* kPreferredMimes[] = {
    "text/plain;charset=utf-8",
    "text/plain",
    "UTF8_STRING",
    "STRING",
    "TEXT",
};

// Holds every Wayland handle we care about. Filled in during registry binding.
struct State {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_seat* seat = nullptr;
    zwlr_data_control_manager_v1* manager = nullptr;
    zwlr_data_control_device_v1* device = nullptr;

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
        // Bind v1 of wl_seat (we don't need keyboard/pointer events, just the
        // seat itself as a "where does the clipboard belong" handle).
        s->seat = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, 1));
    } else if (std::strcmp(interface, zwlr_data_control_manager_v1_interface.name) == 0) {
        // v2 added primary_selection support; we want v2 even though we only
        // care about regular selection (so we can ignore primary cleanly).
        s->manager = static_cast<zwlr_data_control_manager_v1*>(
            wl_registry_bind(registry, name, &zwlr_data_control_manager_v1_interface, 2));
    }
}

void on_registry_global_remove(void*, wl_registry*, uint32_t) {
    // Globals can disappear (seat disconnect, etc.). For a daemon you'd
    // handle this; for our tool, just exit on event-loop error if it happens.
}

const wl_registry_listener kRegistryListener = {
    .global = on_registry_global,
    .global_remove = on_registry_global_remove,
};

// ────────────────────────────────────────────────────────────────────────────
// zwlr_data_control_offer_v1 listener: tells us which mime types this
// particular offer supports. Called multiple times — once per mime type.
// ────────────────────────────────────────────────────────────────────────────

void on_offer_mime(void* data, zwlr_data_control_offer_v1* /*offer*/,
                   const char* mime_type) {
    auto* s = static_cast<State*>(data);
    for (int i = 0; i < (int)(sizeof(kPreferredMimes) / sizeof(*kPreferredMimes)); ++i) {
        if (std::strcmp(mime_type, kPreferredMimes[i]) == 0) {
            // Better match (lower index) wins.
            if (s->chosen_priority < 0 || i < s->chosen_priority) {
                s->chosen_priority = i;
                s->chosen_mime = mime_type;
            }
            return;
        }
    }
}

const zwlr_data_control_offer_v1_listener kOfferListener = {
    .offer = on_offer_mime,
};

// ────────────────────────────────────────────────────────────────────────────
// zwlr_data_control_device_v1 listener: this is where the action happens.
//
// Lifecycle of a clipboard change:
//   1. data_offer    — compositor announces a new offer object
//   2. (offer.offer) — that offer announces its mime types (kOfferListener)
//   3. selection     — the offer becomes the current clipboard
//   4. (we read it)  — we pipe(), receive(), then read the fd
// ────────────────────────────────────────────────────────────────────────────

void on_device_data_offer(void* data, zwlr_data_control_device_v1* /*device*/,
                          zwlr_data_control_offer_v1* offer) {
    auto* s = static_cast<State*>(data);
    s->chosen_mime.clear();
    s->chosen_priority = -1;
    zwlr_data_control_offer_v1_add_listener(offer, &kOfferListener, s);
}

void on_device_selection(void* data, zwlr_data_control_device_v1* /*device*/,
                         zwlr_data_control_offer_v1* offer) {
    auto* s = static_cast<State*>(data);

    // offer == nullptr means "selection cleared" (no clipboard contents).
    if (!offer) return;

    if (s->chosen_mime.empty()) {
        // No text-ish mime types; this is probably an image or something.
        zwlr_data_control_offer_v1_destroy(offer);
        return;
    }

    // TODO #1 — read the clipboard content and write to stdout.
    //
    // The pattern (see wl-clipboard's paste.c for reference):
    //
    //   1. int fds[2]; pipe(fds);
    //   2. zwlr_data_control_offer_v1_receive(offer, mime, fds[1]);
    //      The compositor will write the selection contents to fds[1].
    //   3. close(fds[1]);
    //   4. wl_display_flush(state.display);  // make sure receive() is sent
    //   5. read fds[0] in a loop until EOF, accumulating bytes.
    //   6. fwrite(...) to stdout, fputc('\n'), fflush(stdout).
    //   7. close(fds[0]);
    //   8. zwlr_data_control_offer_v1_destroy(offer);
    //
    // Subtlety: receive() is async. The compositor won't write to fds[1]
    // until you flush the display AND let it process. Easiest is to just
    // read(fds[0]) which blocks until the compositor closes the write end.

    std::fprintf(stderr, "TODO: read offer for mime '%s' and print to stdout\n",
                 s->chosen_mime.c_str());

    zwlr_data_control_offer_v1_destroy(offer);
}

void on_device_finished(void*, zwlr_data_control_device_v1* device) {
    // Compositor told us we're done (e.g. seat disappeared). Clean exit.
    zwlr_data_control_device_v1_destroy(device);
    std::exit(0);
}

void on_device_primary_selection(void*, zwlr_data_control_device_v1*,
                                 zwlr_data_control_offer_v1* offer) {
    // Middle-click (primary) selection. We don't care about this for mining;
    // just destroy the offer so the compositor doesn't think we want it.
    if (offer) zwlr_data_control_offer_v1_destroy(offer);
}

const zwlr_data_control_device_v1_listener kDeviceListener = {
    .data_offer = on_device_data_offer,
    .selection = on_device_selection,
    .finished = on_device_finished,
    .primary_selection = on_device_primary_selection,
};

}  // namespace

int main() {
    State state;

    state.display = wl_display_connect(nullptr);
    if (!state.display) {
        std::fprintf(stderr, "wlclip-watch: failed to connect to Wayland display\n");
        std::fprintf(stderr, "  (is WAYLAND_DISPLAY set? are you running on Wayland?)\n");
        return 1;
    }

    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &kRegistryListener, &state);

    // Roundtrip: send our pending requests and wait for the server's reply.
    // After this, on_registry_global has fired once per global.
    wl_display_roundtrip(state.display);

    if (!state.seat) {
        std::fprintf(stderr, "wlclip-watch: no wl_seat advertised by compositor\n");
        return 1;
    }
    if (!state.manager) {
        std::fprintf(stderr,
            "wlclip-watch: compositor doesn't expose zwlr_data_control_manager_v1.\n"
            "  This protocol is required. Supported on niri, sway, hyprland,\n"
            "  wayfire, river, KDE Plasma 5.27+. NOT on GNOME.\n");
        return 1;
    }

    state.device = zwlr_data_control_manager_v1_get_data_device(
        state.manager, state.seat);
    zwlr_data_control_device_v1_add_listener(state.device, &kDeviceListener, &state);

    // Main event loop. Each iteration dispatches whatever events arrived and
    // calls our listener callbacks. wl_display_dispatch blocks until events
    // are available, so this loop is cheap when idle.
    while (wl_display_dispatch(state.display) != -1) {
        // (callbacks did the work)
    }

    // Unreachable in normal operation — only here if dispatch errored.
    zwlr_data_control_device_v1_destroy(state.device);
    zwlr_data_control_manager_v1_destroy(state.manager);
    wl_seat_destroy(state.seat);
    wl_registry_destroy(state.registry);
    wl_display_disconnect(state.display);
    return 0;
}

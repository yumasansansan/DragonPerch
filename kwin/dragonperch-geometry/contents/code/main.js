// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tells DragonPerch where the windows are.
//
// A Wayland client cannot see another client's windows -- that is the protocol working as
// designed, not a gap to route around. The compositor is the only thing that knows, so on
// Plasma the answer is to run a few lines of JavaScript inside it. This also works under
// KWin on X11, so one implementation covers both session types.
//
// This script runs on KWin's main thread. Anything slow here is session-wide jank, so it
// does the least it possibly can: format a line per window and hand the string to D-Bus.
// Every judgement about what is walkable -- occlusion, minimum widths, coalescing -- is
// made on the other side of that call.

const SERVICE = "org.dragonperch.Geometry";
const PATH = "/org/dragonperch/Geometry";
const INTERFACE = "org.dragonperch.Geometry1";

// A line-oriented text format rather than JSON, so that neither side needs a parser:
//
//   v 1
//   s <output> <x> <y> <width> <height>     the strut-adjusted usable area
//   w <id> <x> <y> <width> <height> <z> <kind>
//
// kind is 0 for a normal window and 1 for a panel or dock. Coordinates are logical, which
// is what KWin reports and what a Wayland client works in.
const FORMAT_VERSION = 1;

function isInteresting(window) {
    if (window === null || window === undefined) {
        return false;
    }
    if (window.minimized || window.hidden || window.deleted) {
        return false;
    }

    // On another virtual desktop or another activity. Its geometry is perfectly plausible
    // and completely wrong to stand on.
    if (!window.onAllDesktops && window.desktops.length > 0
        && window.desktops.indexOf(workspace.currentDesktop) === -1) {
        return false;
    }

    // DragonPerch's own overlay, which covers the whole screen and would give every pet a
    // ledge across the top of the world.
    if (window.resourceClass === "dragonperch" || window.resourceName === "dragonperch") {
        return false;
    }

    return window.normalWindow || window.dialog || window.dock;
}

function report() {
    const lines = ["v " + FORMAT_VERSION];

    const screens = workspace.screens;
    for (let i = 0; i < screens.length; ++i) {
        // clientArea with MaximizeArea is the usable area: what is left after the panels
        // have taken their struts. That is where the floor and the ceiling go.
        const area = workspace.clientArea(KWin.MaximizeArea, screens[i],
                                          workspace.currentDesktop);
        lines.push("s " + screens[i].name
                   + " " + Math.round(area.x) + " " + Math.round(area.y)
                   + " " + Math.round(area.width) + " " + Math.round(area.height));
    }

    // Bottom to top, so the index is the stacking rank: higher is nearer the front, which
    // is what the occlusion pass on the other side expects.
    const stack = workspace.stackingOrder;
    for (let z = 0; z < stack.length; ++z) {
        const window = stack[z];
        if (!isInteresting(window)) {
            continue;
        }

        const frame = window.frameGeometry;
        lines.push("w " + window.internalId
                   + " " + Math.round(frame.x) + " " + Math.round(frame.y)
                   + " " + Math.round(frame.width) + " " + Math.round(frame.height)
                   + " " + z
                   + " " + (window.dock ? 1 : 0));
    }

    // Fire and forget. If DragonPerch is not running nobody owns the name, the call fails,
    // and there is nothing to do about that: the next window movement will try again.
    callDBus(SERVICE, PATH, INTERFACE, "Update", lines.join("\n"));
}

function watch(window) {
    if (window === null || window === undefined) {
        return;
    }

    // frameGeometryChanged fires once per compositor frame while a window is dragged, which
    // is exactly the rate the pets are drawn at -- a pet riding a title bar has to move
    // with it, so reporting only when the drag ends would leave it behind.
    window.frameGeometryChanged.connect(report);
    window.minimizedChanged.connect(report);
    window.outputChanged.connect(report);
    window.desktopsChanged.connect(report);
}

const initial = workspace.stackingOrder;
for (let i = 0; i < initial.length; ++i) {
    watch(initial[i]);
}

workspace.windowAdded.connect(function (window) {
    watch(window);
    report();
});
workspace.windowRemoved.connect(report);
workspace.currentDesktopChanged.connect(report);
workspace.screensChanged.connect(report);
workspace.virtualScreenGeometryChanged.connect(report);

// A heartbeat, so that DragonPerch started *after* this script -- the usual order, since
// the script is loaded with the session -- gets a picture of the desktop without waiting
// for somebody to move a window. Two seconds is far too slow to animate anything and far
// too cheap to measure.
//
// Guarded, because whether QTimer is constructible depends on the KWin version. Without it
// the script still works; it just cannot introduce itself.
try {
    const heartbeat = new QTimer();
    heartbeat.interval = 2000;
    heartbeat.repeat = true;
    heartbeat.timeout.connect(report);
    heartbeat.start();
} catch (error) {
    print("dragonperch: no QTimer, running without the heartbeat: " + error);
}

report();

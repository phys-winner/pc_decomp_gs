local root = getroottable();
root.PortCameraArenaCenter <- function(left, right, width, cx) {
    local span = right - left;
    if (span > 0 && span < width) return left + span * 0.5 + cx - width * 0.5;
    return null;
};
root.PortWrapCameraUpdate <- function(original) {
    return function() : (original) {
        local offline = !::PortMenu(6, 0);
        if (offline && ::PortMenu(8, 0) >= 2) {
            // Victory art is anchored to this view; keep it fixed through the result overlay.
            // The clear-map switch resets cx to 400 even after the world's usual resync.
            cx = width * 0.5;
            return true;
        }
        // Capture forced targeting before the original callback can consume its flag. Boss
        // introductions leave enableLock set for good; it only qualifies a forced target.
        local centerArena = width > 800 && !enableForceTarget && offline;
        local previous = x;
        local result = original.call(this);
        if (centerArena && "stage" in getroottable() &&
            "scrollLeft" in ::stage && "scrollRight" in ::stage) {
            local center = ::PortCameraArenaCenter(::stage.scrollLeft, ::stage.scrollRight, width, cx);
            if (center != null) {
                // Sequential left/right clamps conflict when the whole arena fits in the view.
                // Approach the center with the original easing, e.g. after an introduction.
                local distance = center > previous ? center - previous : previous - center;
                target_x = center;
                x = distance < 1 ? center : distance < 10 ? previous + (center - previous) / distance :
                    previous + (center - previous) * 0.1;
            }
        }
        return result;
    };
};
// Camera2D.Init caches this callback with SetUpdateFunction after this closure is loaded.
root.UpdateCamera2d = root.PortWrapCameraUpdate(root.UpdateCamera2d);

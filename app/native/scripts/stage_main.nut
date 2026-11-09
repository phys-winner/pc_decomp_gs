// Gameplay HUD anchors on the widened canvas. The original StageMain draws an 800x600 layout:
// boss gauge and combo counter at its right edge, character gauges centred along its bottom
// for one to three players, score rows at the top left.
this.Port_hudShift <- function(x, y) {
    if (y >= 440) return extra * 0.5;  // character gauges keep their 1/2/3-player spacing
    if (x >= 262) return extra;        // boss name, level and life bar; combo digits and label
    return 0;                          // score rows and player faces stay at the left
};
this.Port_hudBitBlt <- function(x, y, w, h, image, sx, sy, blend, alpha) {
    return real.BitBlt(x + shift(x, y), y, w, h, image, sx, sy, blend, alpha);
};
this.Port_hudStretchBlt <- function(x, y, w, h, image, sx, sy, sw, sh) {
    return real.StretchBlt(x + shift(x, y), y, w, h, image, sx, sy, sw, sh);
};
this.Port_hudSetBlend <- function(blend, alpha) {
    return real.SetBlend(blend, alpha);
};
this.Port_hudFailed <- false;
this.Port_hudUpdate <- this.Update;
this.Update = function() {
    local extra = ::PortMenu(5, 0) - 800;
    if (extra <= 0 || Port_hudFailed) return Port_hudUpdate();
    // The original callback and DrawNumber/DrawGem read this.pl; a per-frame view supplies
    // the shifted player and delegates every other read and write to this environment.
    local player = { real = pl, extra = extra, shift = Port_hudShift,
        BitBlt = Port_hudBitBlt, StretchBlt = Port_hudStretchBlt, SetBlend = Port_hudSetBlend };
    local view = delegate this : { pl = player };
    try {
        return Port_hudUpdate.call(view);
    } catch (error) {
        Port_hudFailed = true;
        ::PortMenu(3, "StageMain HUD: " + error);
        throw error;
    }
};

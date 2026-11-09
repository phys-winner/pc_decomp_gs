local centerClear = function(original, picture) {
    return function(t) : (original, picture) {
        original.call(this, t);
        // The stage places the picture at the camera's left edge; center it in the widened view,
        // where the host continues it to both edges. LAN keeps the original 800-wide camera.
        x += (camera2d.width - 800) * 0.5;
        if (::PortMenu(9, 0)) {
            // Take 0 bakes the old background into ten tiny distorted frames.
            // Take 1 uses the full-resolution background supplied by the bank.
            sx = sy = 1.0;
            SetTake(1);
        }
        // The id selects the art that continues the picture: inside the widened scene for single
        // player, beside the original 800-pixel presentation for LAN.
        ::PortMenu(7, (picture << 10));
        SetUpdateFunction(function() : (picture) {
            // These five original callbacks only fade alpha by .02 per frame.
            if (alpha < 0.98) alpha += 0.02;
            else alpha = 1.0;
            local curtain = ::FaderAct.global.curtain;
            local shade = curtain.visible ? 1.0 - curtain.alpha : 1.0;
            if (::StageResultAct.pl.IsActive()) {
                local dimmer = ::StageResultAct.global.black_dot;
                if (dimmer.visible) shade *= 1.0 - dimmer.alpha;
            }
            ::PortMenu(7, (picture << 10) | (alpha * shade * 1000).tointeger());
            return true;
        });
    };
};
for (local boss = 1; boss <= 5; ++boss) {
    local name = "Boss" + boss + "_ClearBack";
    this[name] <- centerClear(this[name], boss + 1);
}

// Boss introductions are screen actors created at (0,0) over the original 800-pixel canvas.
local centerName = function(original) {
    return function(t) : (original) {
        original.call(this, t);
        x += (::PortMenu(5, 0) - 800) * 0.5;
    };
};
for (local boss = 1; boss <= 5; ++boss) {
    local name = "Boss" + boss + "_Name";
    this[name] <- centerName(this[name]);
}

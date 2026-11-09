this.Port_backgroundDraw <- this.PreUpdate;
this.PreUpdate = function() {
    Port_backgroundDraw();
    if (::PortMenu(8, 0) == 1) {
        local alpha = 1.0 - stageFaderAlpha, curtain = FaderAct.global.curtain;
        if (curtain.visible) alpha *= 1.0 - curtain.alpha;
        ::PortMenu(7, (1 << 10) | (alpha * 1000.0).tointeger());
    }
};

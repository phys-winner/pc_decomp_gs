// Temporarily append after either StageSelect adapter in the real Squirrel VM.
local fadeRoot = getroottable(), savedFadeMenu = fadeRoot.PortMenu;
local fadeState = { scene = 1, value = -1, calls = 0 };
local fadeProbe = {
    Port_stageDraw = function() {}, Port_backgroundDraw = function() {},
    Port_stageUpdate = function() {}, Update_StageSelect = function() {}, funcUpdate = null,
    stageFaderAlpha = 0.0, FaderAct = { global = { curtain = { visible = false, alpha = 1.0 } } },
    stagePanel = [], stageImage = [], stageIndexMap = [], selector = { cursor = 0 }, focusExitItem = false,
    exitB = { dst_x = 0, dst_y = 0 }, resource = { exit = { image_width = 1, image_height = 1 } }
};
fadeProbe.PreUpdate <- this.PreUpdate;
try {
    fadeRoot.PortMenu = function(command, value, a = null, b = null, c = null, d = null) : (fadeState) {
        if (command == 8) return fadeState.scene;
        if (command == 7) { fadeState.value = value; fadeState.calls++; }
        return 0;
    };
    fadeProbe.PreUpdate();
    assert(fadeState.value == ((1 << 10) | 1000));
    fadeProbe.stageFaderAlpha = 0.25;
    fadeProbe.FaderAct.global.curtain.visible = true;
    fadeProbe.FaderAct.global.curtain.alpha = 0.5;
    fadeProbe.PreUpdate();
    assert(fadeState.value == ((1 << 10) | 375));
    fadeProbe.FaderAct.global.curtain.alpha = 1.0;
    fadeProbe.PreUpdate();
    assert(fadeState.value == (1 << 10));
    fadeProbe.FaderAct.global.curtain.visible = false;
    fadeProbe.PreUpdate();
    assert(fadeState.value == ((1 << 10) | 750));
    fadeState.scene = 6;
    fadeProbe.PreUpdate();
    assert(fadeState.calls == 4);
} catch(error) {
    fadeRoot.PortMenu = savedFadeMenu;
    throw error;
}
fadeRoot.PortMenu = savedFadeMenu;
::PortMenu(3, "PASS: production StageSelect panorama combines local and global fades, ignores hidden curtain and preserves other scenes");

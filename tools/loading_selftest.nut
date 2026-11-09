// Run temporarily after now_loading.nut in the real Squirrel VM.
local loadingRoot = getroottable(), savedLoadingPort = loadingRoot.PortMenu;
local loadingState = { events = [] };
local loadingProbe = {
    nowloading = { dst_x = 670, visible = false },
    QB0000 = { dst_x = 648, visible = true },
    originalModes = [],
    leftQB = 800,
    Port_loadingInit = function() { assert(!QB0000.visible); Show(false); },
    Port_loadingShow = function(show) { originalModes.append(show); }
};
loadingProbe.Init <- this.Init;
loadingProbe.Show <- this.Show;
try {
    loadingRoot.PortMenu = function(command, value) : (loadingState) {
        if (command == 5) return 2134;
        assert(command == 10);
        loadingState.events.append(value);
        return 0;
    };
    loadingProbe.Init();
    assert(loadingProbe.originalModes.len() == 1 && !loadingProbe.originalModes[0]);
    assert(!loadingProbe.Port_loadingWide && loadingProbe.Port_loadingLabelX == 670);
    assert(loadingState.events.len() == 0);
    loadingProbe.Show(true);
    assert(loadingProbe.Port_loadingWide && loadingProbe.QB0000.dst_x == 1982 && loadingProbe.QB0000.visible);
    assert(loadingProbe.nowloading.visible);
    assert(loadingProbe.nowloading.dst_x == 2004);
    assert(loadingState.events.len() == 1 && loadingState.events[0] == 1);
    loadingProbe.Show(true);
    assert(loadingState.events.len() == 1);
    assert(loadingProbe.QB0000.dst_x == 1982 && loadingProbe.nowloading.dst_x == 2004);
    loadingProbe.Init();
    assert(!loadingProbe.Port_loadingWide && !loadingProbe.QB0000.visible && !loadingProbe.nowloading.visible);
    assert(loadingProbe.Port_loadingLabelX == 670 && loadingState.events.len() == 2 && loadingState.events[1] == 0);
    loadingProbe.Show(true);
    assert(loadingProbe.QB0000.dst_x == 1982 && loadingProbe.nowloading.dst_x == 2004);
    assert(loadingState.events.len() == 3 && loadingState.events[2] == 1);
    loadingProbe.Show(false);
    assert(!loadingProbe.Port_loadingWide && loadingState.events.len() == 4 && loadingState.events[3] == 0);
    assert(!loadingProbe.QB0000.visible && !loadingProbe.nowloading.visible);
    assert(loadingProbe.originalModes.len() == 6 && !loadingProbe.originalModes[5]);
} catch(error) {
    loadingRoot.PortMenu = savedLoadingPort;
    throw error;
}
loadingRoot.PortMenu = savedLoadingPort;
::PortMenu(3, "PASS: loading Init reentry and reinit preserve base, hide active state, anchor native cat and label at width2134, deduplicate visibility and forward calls");

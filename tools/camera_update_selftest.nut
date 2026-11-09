// Temporarily append to camera.nut to execute the production wrapper in the game VM.
local testRoot = getroottable();
local savedMenu = testRoot.PortMenu, savedStage = "stage" in testRoot ? testRoot.stage : null;
local hadStage = "stage" in testRoot;
local state = { lan = 0, picture = -1, calls = 0, forceConsumed = false };
try {
    testRoot.PortMenu = function(command, value) : (state) {
        return command == 6 ? state.lan : state.picture;
    };
    testRoot.stage <- { scrollLeft = 0, scrollRight = 1024, scrollLeftLock = true, scrollRightLock = true };
    local original = function() : (state) {
        state.calls++;
        target_x = -43;
        x = 100;
        if (state.forceConsumed) enableForceTarget = false;
        return 37;
    };
    local update = testRoot.PortWrapCameraUpdate(original);
    local settle = function(camera) : (update) {
        for (local i = 0; i < 100; ++i) update.call(camera);
    };
    local camera = { width = 2134, cx = 1067, enableForceTarget = false, enableLock = false,
        x = 0, target_x = 0, y = 600, offset_x = 12, offset_y = -5 };
    assert(update.call(camera) == 37 && state.calls == 1);
    // The original easing covers a tenth of the remaining distance per update.
    assert(camera.target_x == 512 && camera.x > 51 && camera.x < 52);
    settle(camera);
    assert(camera.x == 512 && camera.target_x == 512);
    assert(camera.y == 600 && camera.offset_x == 12 && camera.offset_y == -5);
    testRoot.stage.scrollLeft = 2000; testRoot.stage.scrollRight = 3024;
    settle(camera);
    assert(camera.x == 2512);
    camera.cx = 400;
    settle(camera);
    assert(camera.x == 1845 && camera.x - camera.cx == 1445);
    camera.width = 800;
    update.call(camera);
    assert(camera.x == 100 && camera.target_x == -43);
    camera.width = 2134; camera.cx = 1067;
    testRoot.stage.scrollRight = testRoot.stage.scrollLeft + 2134;
    update.call(camera);
    assert(camera.x == 100 && camera.target_x == -43);
    testRoot.stage.scrollRight = testRoot.stage.scrollLeft - 1;
    update.call(camera);
    assert(camera.x == 100);
    testRoot.stage.scrollRight = testRoot.stage.scrollLeft + 1024;
    camera.enableForceTarget = true; state.forceConsumed = true;
    update.call(camera);
    assert(camera.x == 100 && !camera.enableForceTarget);
    // Boss introductions leave enableLock set; once their forced target ends the arena recenters.
    state.forceConsumed = false; camera.enableLock = true;
    update.call(camera);
    assert(camera.target_x == 2512 && camera.x > 100 && camera.x < 2512);
    settle(camera);
    assert(camera.x == 2512);
    camera.enableLock = false; state.lan = 1;
    update.call(camera);
    assert(camera.x == 100);
    state.lan = 0; state.picture = 2;
    camera.cx = 400; // The original clear-map switch repeats this reset after the usual resync.
    local calls = state.calls;
    assert(update.call(camera) == true);
    assert(camera.x == 100 && camera.target_x == -43 && camera.cx == 1067 && state.calls == calls);
    assert(camera.y == 600 && camera.offset_x == 12 && camera.offset_y == -5);
    state.picture = -1;
    settle(camera);
    assert(camera.x == 2512 && state.calls == calls + 100);
} catch (error) {
    testRoot.PortMenu = savedMenu;
    if (hadStage) testRoot.stage = savedStage;
    else delete testRoot.stage;
    throw error;
}
testRoot.PortMenu = savedMenu;
if (hadStage) testRoot.stage = savedStage;
else delete testRoot.stage;
::PortMenu(3, "PASS: production camera eases narrow arenas to center, ignores the sticky introduction lock, preserves normal bounds/forced targets/LAN, freezes CG through cx reset and keeps vertical shake");

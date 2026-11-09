// Append temporarily after common.nut in a diagnostic build; exercise the production wrapper in the game VM.
local createRoot = getroottable();
local savedCreateAct = createRoot.PortCreateAct;
local titlePaths = ["data/system/Title/TitleBG.act", "data/system/Title/TitleCloud.act", "data/system/Title/TitleShadow.act"];
local titleNames = ["TitleBGAct", "TitleCloudAct", "TitleShadowAct"];
local savedTitleActs = {};
local createCalls = [];
foreach (name in titleNames) if (name in createRoot) {
    savedTitleActs[name] <- createRoot[name];
    delete createRoot[name];
}
try {
    createRoot.PortCreateAct = function(path) : (createRoot, createCalls, titlePaths, titleNames) {
        createCalls.append([path, this]);
        for (local i = 0; i < titlePaths.len(); ++i) if (path == titlePaths[i]) {
            createRoot[titleNames[i]] <- {};
            return 1;
        }
        return 73;
    };
    for (local i = 0; i < titlePaths.len(); ++i) {
        assert(createRoot.CreateAct(titlePaths[i]) == 1);
        assert(createCalls.len() == i + 1 && createCalls[i][0] == titlePaths[i]);
        for (local repeat = 0; repeat < 120; ++repeat) assert(createRoot.CreateAct(titlePaths[i]) == 1);
        assert(createCalls.len() == i + 1);
    }
    local caller = { marker = 19 };
    assert(createRoot.CreateAct.call(caller, "data/system/Title/TitleBGExtra.act") == 73);
    assert(createCalls[3][0] == "data/system/Title/TitleBGExtra.act" && createCalls[3][1] == caller);
    assert(createRoot.CreateAct("data/stage/stage3/stage3A.act") == 73);
    assert(createRoot.CreateAct("data/stage/stage3/stage3A.act") == 73 && createCalls.len() == 6);
    delete createRoot.TitleBGAct;
    assert(createRoot.CreateAct(titlePaths[0]) == 1 && createCalls.len() == 7);
} catch (error) {
    createRoot.PortCreateAct = savedCreateAct;
    foreach (name in titleNames) {
        if (name in createRoot) delete createRoot[name];
        if (name in savedTitleActs) createRoot[name] <- savedTitleActs[name];
    }
    throw error;
}
createRoot.PortCreateAct = savedCreateAct;
foreach (name in titleNames) {
    if (name in createRoot) delete createRoot[name];
    if (name in savedTitleActs) createRoot[name] <- savedTitleActs[name];
}
::PortMenu(3, "PASS: CreateAct creates each title dependency once, reuses existing root actors, restores missing actors and forwards unrelated paths/receiver/return values");

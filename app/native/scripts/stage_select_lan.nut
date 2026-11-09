// Local metadata drives hitboxes; every selection still travels through GSOnline's input stream.
this.Port_lanPublish <- function(context) {
    if (context == 2) {
        local mask = 0;
        foreach (index in stageIndexMap) mask = mask | (1 << index);
        ::PortMenu(0, 2 | (stageIndexMap[selector.cursor] << 4) | (mask << 8) | (focusExitItem ? 16384 : 0));
    } else {
        local player = ::PortMenu(11, 0);
        if (player >= 0 && player < sel_charactor.len()) {
            local glasses = "useMeganeHomura" in thisAct && thisAct.useMeganeHomura;
            ::PortMenu(0, context | (sel_charactor[player].cursor << 4) | (player_state[player] << 8) | (glasses ? 1024 : 0));
        }
    }
};
this.Port_lanAssign <- function() {
    local used = 0;
    foreach (i, selection in sel_charactor) {
        use_charactor[i] = null;
        player_state[i] = (Port_lanGroup & (1 << i)) ? 1 : 0;
        if (!player_state[i]) continue;
        local wanted = thisAct.SelectCharactor[i];
        if (wanted == 5 && "useMeganeHomura" in thisAct) wanted = 0;
        if (wanted < 0 || wanted >= 5) wanted = thisAct.DefaultCharactor[i];
        for (local n = 0; n < 5; n++) {
            local candidate = (wanted + n) % 5;
            if (!(used & (1 << candidate)) && condition[candidate] == 0) { wanted = candidate; break; }
        }
        selection.cursor = wanted;
        selection.cursorPrev = wanted;
        selection.intervalCount = 0;
        used = used | (1 << wanted);
    }
};
this.Port_lanReady <- function() {
    if (!Port_lanGroup) return false;
    foreach (i, state in player_state) {
        if (!(Port_lanGroup & (1 << i))) continue;
        local cursor = sel_charactor[i].cursor;
        if (state != 2 || use_charactor[i] == null || cursor < 0 || cursor >= condition.len() || condition[cursor] != 0) return false;
    }
    return true;
};
this.Port_lanStageUpdate <- this.Update_StageSelect;
this.Update_StageSelect = function() {
    Port_lanChoosing = false;
    ::PortHostMenuInput.call(this, Port_lanStageUpdate);
    if (funcUpdate == Update_CharactorSelectEnter || funcUpdate == Port_lanCharacterEnter) {
        Port_lanGroup = ::PortMenu(12, 0);
        Port_lanAssign();
        Port_lanChoosing = true;
        Port_lanPublish(9);
    } else Port_lanPublish(2);
};
this.Port_lanCharacterEnter <- this.Update_CharactorSelectEnter;
this.Update_CharactorSelectEnter = function() {
    if (!Port_lanChoosing) { Port_lanGroup = ::PortMenu(12, 0); Port_lanAssign(); Port_lanChoosing = true; }
    Port_lanPublish(9);
    Port_lanCharacterEnter();
};
this.Port_lanCharacterUpdate <- this.Update_CharactorSelect;
this.Update_CharactorSelect = function() {
    // Leaving selection never removes a connected participant from the party.
    if (player_state[0] == 1 && inputArray[0].b1 == 1) {
        foreach (i, state in player_state) { player_state[i] = 0; use_charactor[i] = null; }
        funcUpdate = Update_CharactorSelectLeave;
        Port_lanChoosing = false;
        return;
    }
    local cancels = [];
    foreach (i, selection in sel_charactor) {
        cancels.append([selection.input.b0, selection.input.b1]);
        if (!(Port_lanGroup & (1 << i))) {
            player_state[i] = 0; use_charactor[i] = null;
            selection.input.b0 = selection.input.b1 = 0;
        }
        else {
            if (!player_state[i]) player_state[i] = 1;
            if (i > 0 && player_state[i] == 1) selection.input.b1 = 0;
        }
    }
    try { Port_lanCharacterUpdate(); }
    catch (error) {
        foreach (i, value in cancels) { sel_charactor[i].input.b0 = value[0]; sel_charactor[i].input.b1 = value[1]; }
        throw error;
    }
    foreach (i, value in cancels) { sel_charactor[i].input.b0 = value[0]; sel_charactor[i].input.b1 = value[1]; }
    Port_lanPublish(3);
};
this.Port_lanStartGame <- this.Update_StartGame;
this.Update_StartGame = function() {
    if (!Port_lanReady()) { funcUpdate = Update_CharactorSelect; return; }
    ::PortMenu(0, 7);
    Port_lanStartGame();
};
this.Port_lanInit <- this.Init;
this.Init = function() {
    Port_lanInit();
    this.Port_lanGroup <- ::PortMenu(12, 0);
    this.Port_lanChoosing <- false;
    funcUpdate = Update_StageSelect;
};
this.Port_lanDraw <- this.PreUpdate;
this.PreUpdate = function() {
    if (funcUpdate == Port_lanStageUpdate) funcUpdate = Update_StageSelect;
    if (funcUpdate == Port_lanCharacterEnter) funcUpdate = Update_CharactorSelectEnter;
    if (funcUpdate == Port_lanCharacterUpdate) funcUpdate = Update_CharactorSelect;
    if (funcUpdate == Port_lanStartGame) funcUpdate = Update_StartGame;
    Port_lanDraw();
    if (::PortMenu(8, 0) == 1) {
        local alpha = 1.0 - stageFaderAlpha, curtain = FaderAct.global.curtain;
        if (curtain.visible) alpha *= 1.0 - curtain.alpha;
        ::PortMenu(7, (1 << 10) | (alpha * 1000.0).tointeger());
    }
    foreach (i, panel in stagePanel) {
        local selected = i == selector.cursor && !focusExitItem;
        local image = stageImage[i][selected ? 0 : 1];
        local x = selected ? panel.left + panel.offsetX / 3.0 : stageImage[i][2].dst_x;
        local y = selected ? panel.top + panel.offsetY / 3.0 : stageImage[i][2].dst_y;
        ::PortMenu(4, stageIndexMap[i], x.tointeger(), y.tointeger(),
            (x + image.image_width).tointeger(), (y + image.image_height).tointeger());
    }
    ::PortMenu(4, 6, exitB.dst_x.tointeger(), exitB.dst_y.tointeger(),
        (exitB.dst_x + resource.exit.image_width).tointeger(), (exitB.dst_y + resource.exit.image_height).tointeger());
};

// Append temporarily after stage_select_lan.nut in the real Squirrel VM.
local lanRoot = getroottable(), savedLanMenu = lanRoot.PortMenu;
local lanEvents = [];
local lanInputs = [{ b0 = 0, b1 = 0 }, { b0 = 0, b1 = 0 }, { b0 = 0, b1 = 0 }];
local lanProbe = {
    thisAct = { DefaultCharactor = [2, 1, 3], SelectCharactor = [-1, -1, -1] },
    condition = [0, 0, 0, 0, 0], player_state = [0, 0, 0], use_charactor = [null, null, null],
    inputArray = lanInputs, sel_charactor = [], Port_lanGroup = 3, Port_lanChoosing = true,
    funcUpdate = null, originalStarts = 0, originalUpdates = 0,
    Port_lanStartGame = function() { originalStarts++; },
    Port_lanCharacterUpdate = function() {},
    Port_lanPublish = function(context) { assert(context == 3); },
    Update_CharactorSelectLeave = function() {}
};
foreach (input in lanInputs)
    lanProbe.sel_charactor.append({ input = input, cursor = -1, cursorPrev = -1, intervalCount = 99 });
lanProbe.Port_lanAssign <- this.Port_lanAssign;
lanProbe.Port_lanReady <- this.Port_lanReady;
lanProbe.Update_StartGame <- this.Update_StartGame;
lanProbe.Update_CharactorSelect <- this.Update_CharactorSelect;
try {
    lanRoot.PortMenu = function(command, value) : (lanEvents) {
        assert(command == 0 && value == 7);
        lanEvents.append(value);
        return 0;
    };
    foreach (mask in [3, 7]) {
        lanProbe.Port_lanGroup = mask;
        lanProbe.Port_lanAssign();
        assert(!lanProbe.Port_lanReady());
        foreach (i, selection in lanProbe.sel_charactor) {
            assert(lanProbe.player_state[i] == ((mask & (1 << i)) ? 1 : 0));
            assert(lanProbe.use_charactor[i] == null);
            if (mask & (1 << i)) {
                assert(selection.cursor == lanProbe.thisAct.DefaultCharactor[i]);
                assert(selection.cursorPrev == selection.cursor && selection.intervalCount == 0);
                lanProbe.player_state[i] = 2;
                lanProbe.use_charactor[i] = selection.cursor;
            }
        }
        assert(lanProbe.Port_lanReady());
        foreach (i, selection in lanProbe.sel_charactor) if (mask & (1 << i)) {
            lanProbe.player_state[i] = 1;
            assert(!lanProbe.Port_lanReady());
            lanProbe.player_state[i] = 2;
            lanProbe.use_charactor[i] = null;
            assert(!lanProbe.Port_lanReady());
            lanProbe.use_charactor[i] = selection.cursor;
        }
    }
    lanProbe.Update_StartGame();
    assert(lanProbe.originalStarts == 1 && lanEvents.len() == 1);
    lanProbe.player_state[2] = 1;
    lanProbe.Update_StartGame();
    assert(lanProbe.originalStarts == 1 && lanEvents.len() == 1 &&
        lanProbe.funcUpdate == lanProbe.Update_CharactorSelect);
    lanProbe.player_state[2] = 2;
    lanProbe.use_charactor[2] = null;
    lanProbe.Update_StartGame();
    assert(lanProbe.originalStarts == 1 && lanEvents.len() == 1);
    lanProbe.Port_lanGroup = 0;
    assert(!lanProbe.Port_lanReady());

    lanProbe.Port_lanGroup = 7;
    lanProbe.thisAct.SelectCharactor = [2, 2, 2];
    lanProbe.condition[2] = 1;
    lanProbe.Port_lanAssign();
    assert(lanProbe.sel_charactor[0].cursor == 3 && lanProbe.sel_charactor[1].cursor == 4 &&
        lanProbe.sel_charactor[2].cursor == 0);
    lanProbe.thisAct.SelectCharactor = [-1, -1, -1];
    lanProbe.condition[2] = 0;
    lanProbe.Port_lanAssign();
    lanInputs[1].b1 = 1;
    lanProbe.Port_lanCharacterUpdate = function() {
        originalUpdates++;
        assert(player_state[1] == 1 && sel_charactor[1].input.b1 == 0);
    };
    lanProbe.Update_CharactorSelect();
    assert(lanProbe.originalUpdates == 1 && lanProbe.player_state[1] == 1 && lanInputs[1].b1 == 1);
    lanInputs[0].b1 = 1;
    lanProbe.Update_CharactorSelect();
    assert(lanProbe.originalUpdates == 1 && !lanProbe.Port_lanChoosing &&
        lanProbe.funcUpdate == lanProbe.Update_CharactorSelectLeave);
    foreach (i, state in lanProbe.player_state) assert(state == 0 && lanProbe.use_charactor[i] == null);

    lanInputs[0].b1 = 0;
    lanProbe.Port_lanAssign();
    lanProbe.player_state[1] = 2;
    lanProbe.use_charactor[1] = 1;
    lanProbe.Port_lanCharacterUpdate = function() {
        originalUpdates++;
        assert(player_state[1] == 2 && sel_charactor[1].input.b1 == 1);
        player_state[1] = 1;
        use_charactor[1] = null;
    };
    lanProbe.Update_CharactorSelect();
    assert(lanProbe.originalUpdates == 2 && lanProbe.player_state[1] == 1 &&
        lanProbe.use_charactor[1] == null && lanInputs[1].b1 == 1);
    lanProbe.Port_lanGroup = 3;
    lanProbe.Port_lanAssign();
    lanInputs[1].b1 = 0;
    lanInputs[2].b0 = 1;
    lanProbe.player_state[2] = 1;
    lanProbe.use_charactor[2] = 3;
    lanProbe.Port_lanCharacterUpdate = function() {
        originalUpdates++;
        assert(player_state[2] == 0 && use_charactor[2] == null &&
            sel_charactor[2].input.b0 == 0 && sel_charactor[2].input.b1 == 0);
    };
    lanProbe.Update_CharactorSelect();
    assert(lanProbe.originalUpdates == 3 && lanProbe.player_state[2] == 0 &&
        lanProbe.use_charactor[2] == null && lanInputs[2].b0 == 1);
} catch (error) {
    lanRoot.PortMenu = savedLanMenu;
    throw error;
}
lanRoot.PortMenu = savedLanMenu;
::PortMenu(3, "PASS: LAN masks3/7 assign all peers distinct available defaults, readiness guards start, client cancel retains membership, host cancel leaves and empty input cannot join");

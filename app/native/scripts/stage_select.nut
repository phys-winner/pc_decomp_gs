this.Port_stageUpdate <- this.Update_StageSelect;
this.Update_StageSelect = function() {
    local mask = 0;
    foreach (index in stageIndexMap) mask = mask | (1 << index);
    local command = ::PortMenu(0, 2 | (stageIndexMap[selector.cursor] << 4) | (mask << 8));
    local input = inputArray[0], confirm = input.b0, allConfirm = inputAll.b0, vertical = inputAll.y;
    if (command & 1024) {
        focusExitItem = true;
        inputAll.b0 = 1;
        inputAll.y = 0;
    } else if (command) {
        local wanted = (command & 255) - 1;
        foreach (i, index in stageIndexMap) if (index == wanted) {
            if (selector.cursor != i) PlaySE(0);
            selector.cursor = i;
            selector.intervalCount = 0;
            focusExitItem = false;
            inputAll.y = 0;
            if (command & 256) input.b0 = 1;
            break;
        }
    }
    Port_stageUpdate();
    if (command) { input.b0 = confirm; inputAll.b0 = allConfirm; inputAll.y = vertical; }
};

this.Port_characterUpdate <- this.Update_CharactorSelect;
this.Port_characterEnter <- this.Update_CharactorSelectEnter;
this.Update_CharactorSelectEnter = function() {
    ::PortMenu(0, 9 | (sel_charactor[0].cursor << 4));
    Port_characterEnter();
};
this.Update_CharactorSelect = function() {
    local selection = sel_charactor[0];
    local command = ::PortMenu(0, 3 | (selection.cursor << 4));
    local input = selection.input, confirm = input.b0, cancel = input.b1, vertical = input.y;
    if (command & 1024) input.b1 = 1;
    else if (command) {
        local wanted = (command & 255) - 1;
        if (wanted >= 0 && wanted < 5) {
            if (selection.cursor != wanted) PlaySE(0);
            selection.cursor = wanted;
            selection.intervalCount = 0;
            player_state[0] = 1;
            // The updated selector enables glasses Homura on Up and restores her on Down.
            local glasses = "useMeganeHomura" in thisAct && thisAct.useMeganeHomura;
            input.y = (command & 512) ? (glasses ? 1 : -1) : 0;
            if (command & 256) input.b0 = 1;
        }
    }
    Port_characterUpdate();
    if (command) { input.b0 = confirm; input.b1 = cancel; input.y = vertical; }
};

this.Port_startGame <- this.Update_StartGame;
this.Update_StartGame = function() { ::PortMenu(0, 7); Port_startGame(); };
this.Port_stageInit <- this.Init;
this.Init = function() {
    local states = [];
    try {
        if (::PortMenu(1, 0) & 1) foreach (result in ::gameData.stageResult) {
            states.append(result.state);
            result.state = 1;
        }
        Port_stageInit();
        funcUpdate = Update_StageSelect;
    }
    catch (error) {
        foreach (i, state in states) ::gameData.stageResult[i].state = state;
        ::PortMenu(3, "StageSelect Init: " + error.tostring());
        throw error;
    }
    foreach (i, state in states) ::gameData.stageResult[i].state = state;
};

this.Port_stageDraw <- this.PreUpdate;
this.PreUpdate = function() {
    // Init and the character-leave animation can retain the original closure.
    if (funcUpdate == Port_stageUpdate) funcUpdate = Update_StageSelect;
    Port_stageDraw();
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
        (exitB.dst_x + resource.exit.image_width).tointeger(),
        (exitB.dst_y + resource.exit.image_height).tointeger());
};

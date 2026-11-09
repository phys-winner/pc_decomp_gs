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

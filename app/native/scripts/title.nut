// Reclaim unreachable scene objects when returning to the title.
if (::PortMenu(6, 0)) {
    // Both peers read the host's synchronized input; local touch never changes shared menu state.
    ::PortHostMenuInput <- function(callback) {
        local fields = ["x", "y", "b0", "b1", "b2", "b3", "b4", "b5"], saved = [];
        foreach (name in fields) { saved.append(::inputAll[name]); ::inputAll[name] = ::inputArray[0][name]; }
        try { callback.call(this); }
        catch (error) {
            foreach (i, name in fields) ::inputAll[name] = saved[i];
            throw error;
        }
        foreach (i, name in fields) ::inputAll[name] = saved[i];
    };
}
this.Port_titleInit <- this.Init;
this.Port_titleLayout <- function() {
    local shift = ::PortMenu(9, 0) ? (::PortMenu(2, 0) - 800) * 0.5 : 0.0;
    title.dst_x = Port_titleOrigins[0] + shift;
    title_back.dst_x = Port_titleOrigins[1] + shift;
};
this.Init = function() {
    if (!("Port_titleOrigins" in this)) this.Port_titleOrigins <- [title.dst_x, title_back.dst_x];
    Port_titleInit();
    Port_titleLayout();
    if ("collectgarbage" in getroottable()) ::collectgarbage();
};

this.Port_titleUpdate <- this.Update;
this.Update = function() {
    Port_titleLayout();
    if (::PortMenu(9, 0)) title_cloud.visible = false;
    if (::PortMenu(6, 0)) ::PortHostMenuInput.call(this, Port_titleUpdate);
    else Port_titleUpdate();
    local bg = TitleBGAct;
    if (pl.IsActive()) {
        if (::PortMenu(9, 0)) bg.global.Port_titlePaint();
    } else {
        // The original title exit leaves its background shadow stage active.
        local cloud = bg.global.TitleCloudAct.pl;
        local shadow = bg.global.TitleShadowAct.pl;
        if (bg.pl.IsActive()) bg.pl.EndStage();
        if (cloud.IsActive()) cloud.EndStage();
        if (shadow.IsActive()) shadow.EndStage();
    }
    if (::PortMenu(8, 0) == 0) {
        local alpha = title_back.alpha * (1.0 - frontFaderAlpha), curtain = FaderAct.global.curtain;
        if (curtain.visible) alpha *= 1.0 - curtain.alpha;
        ::PortMenu(7, (alpha * 1000.0).tointeger());
    }
};

// Append temporarily after title.nut; only the LAN path installs this production helper.
if (::PortMenu(6, 0)) {
    local hostRoot = getroottable(), savedHostGlobals = {};
    local hostNames = ["inputAll", "inputArray"], hostFields = ["x", "y", "b0", "b1", "b2", "b3", "b4", "b5"];
    foreach (name in hostNames) if (name in hostRoot) savedHostGlobals[name] <- hostRoot[name];
    local hostAll = {}, hostInput = {}, clientInput = {};
    foreach (i, name in hostFields) {
        hostAll[name] <- 100 + i;
        hostInput[name] <- 10 + i;
        clientInput[name] <- 20 + i;
    }
    local hostReceiver = { marker = 73, calls = 0 };
    try {
        hostRoot.inputAll <- hostAll;
        hostRoot.inputArray <- [hostInput, clientInput];
        hostRoot.PortHostMenuInput.call(hostReceiver, function() : (hostFields, hostInput) {
            assert(marker == 73);
            calls++;
            foreach (name in hostFields) assert(::inputAll[name] == hostInput[name]);
            ::inputAll.b0 = 999;
        });
        assert(hostReceiver.calls == 1);
        foreach (i, name in hostFields) assert(hostAll[name] == 100 + i &&
            hostInput[name] == 10 + i && clientInput[name] == 20 + i);
        local caught = false;
        try {
            hostRoot.PortHostMenuInput.call(hostReceiver, function() : (hostFields, hostInput) {
                assert(marker == 73);
                calls++;
                foreach (name in hostFields) {
                    assert(::inputAll[name] == hostInput[name]);
                    ::inputAll[name] = -123;
                }
                throw "host input fixture";
            });
        } catch (error) {
            assert(error == "host input fixture");
            caught = true;
        }
        assert(caught && hostReceiver.calls == 2);
        foreach (i, name in hostFields) assert(hostAll[name] == 100 + i &&
            hostInput[name] == 10 + i && clientInput[name] == 20 + i);
    } catch (error) {
        foreach (name in hostNames) {
            if (name in savedHostGlobals) hostRoot[name] <- savedHostGlobals[name];
            else delete hostRoot[name];
        }
        throw error;
    }
    foreach (name in hostNames) {
        if (name in savedHostGlobals) hostRoot[name] <- savedHostGlobals[name];
        else delete hostRoot[name];
    }
    ::PortMenu(3, "PASS: production host menu input uses all8 host fields and receiver, restores merged fields after success/exception and preserves client input");
}

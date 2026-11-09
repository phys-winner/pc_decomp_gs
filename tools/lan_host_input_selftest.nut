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

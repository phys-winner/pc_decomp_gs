this.Port_menuUpdate <- this.Update1;
this.Update1 = function() {
    local command = ::PortMenu(0, 4 | (selector.cursor << 4));
    local confirm = inputAll.b0, cancel = inputAll.b4;
    if (command & 1024) inputAll.b4 = 1;
    else if (command) {
        local wanted = (command & 255) - 1;
        if (wanted >= 0 && wanted < 3) {
            if (selector.cursor != wanted) PlaySE(0);
            selector.cursor = wanted;
            selector.intervalCount = 0;
            if (command & 256) inputAll.b0 = 1;
        }
    }
    Port_menuUpdate();
    if (command) { inputAll.b0 = confirm; inputAll.b4 = cancel; }
};

this.Port_hiddenUpdate <- this.Update2;
this.Update2 = function() {
    local command = ::PortMenu(0, 8);
    local cancel = inputAll.b1, pause = inputAll.b4;
    if (command) inputAll.b1 = 1;
    Port_hiddenUpdate();
    if (command) { inputAll.b1 = cancel; inputAll.b4 = pause; }
};

this.Port_confirmUpdate <- this.Update3;
this.Update3 = function() {
    local command = ::PortMenu(0, 5 | ((exitAcceptYes ? 0 : 1) << 4));
    local confirm = inputAll.b0, cancel = inputAll.b1, horizontal = inputAll.x;
    if (command & 1024) inputAll.b1 = 1;
    else if (command) {
        exitAcceptYes = (command & 255) == 1;
        inputAll.x = 0;
        if (command & 256) inputAll.b0 = 1;
    }
    Port_confirmUpdate();
    if (command) { inputAll.b0 = confirm; inputAll.b1 = cancel; inputAll.x = horizontal; }
};

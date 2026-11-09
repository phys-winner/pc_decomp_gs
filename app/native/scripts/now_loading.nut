this.Port_loadingInit <- this.Init;
this.Init = function() {
    // Original Init calls Show(false), which enters the wrapper below.
    if (!("Port_loadingWide" in this)) this.Port_loadingWide <- false;
    if (!("Port_loadingLabelX" in this)) this.Port_loadingLabelX <- nowloading.dst_x;
    QB0000.visible = false;
    Port_loadingInit();
};

this.Port_loadingShow <- this.Show;
this.Show = function(show = true) {
    Port_loadingShow(show);
    if (show != Port_loadingWide) {
        ::PortMenu(10, show ? 1 : 0);
        Port_loadingWide = show;
    }
    // Native layers remain live while loading pauses the immediate sprite updates.
    QB0000.visible = show;
    nowloading.visible = show;
    if (show) {
        local extra = ::PortMenu(5, 0) - 800;
        QB0000.dst_x = 648 + extra;
        nowloading.dst_x = Port_loadingLabelX + extra;
    }
};

local originalInit = this.Init, originalUpdate = this.Update;
this.Init = function() : (originalInit) {
    originalInit.call(this);
    // shortcut: stage3C chip positions are static; rebuild these arrays if runtime layout edits are added.
    local coordinates = function(chips) {
        local result = [];
        foreach (chip in chips) { result.append(chip.left); result.append(chip.top); }
        return result;
    };
    this.PortTVData <- [coordinates(animChipListA), coordinates(animChipListB),
        coordinates(animChipListC), coordinates(animChipListD), tvSrcLeft, tvSrcTop,
        tvSrcWidth, tvSrcHeight, coordinates(colorBarTVchip), colorBarTVFrame];
};
this.Update = function() : (originalUpdate) {
    if (!::PortTVFrame(layermain, layer_1, ::CaptureAct.bbSnap,
        ::UtilityAct.resource.TvLightMap, ::UtilityAct.resource.tv3, ::UtilityAct.resource.TvLightMap2,
        PortTVData, camera2d.x, camera2d.y, ::stage.renderState.pixelShader,
        GameOverAct.pl, rand, BLEND_ALPHA, BLEND_SUB)) originalUpdate.call(this);
};

local root = getroottable();
root.PortCreateAct <- root.CreateAct;
root.CreateAct = function(path) {
    // The title reloads these declarations; native CreateAct allocates even when their root name exists.
    local name = path == "data/system/Title/TitleBG.act" ? "TitleBGAct" :
        path == "data/system/Title/TitleCloud.act" ? "TitleCloudAct" :
        path == "data/system/Title/TitleShadow.act" ? "TitleShadowAct" : null;
    if (name != null && name in getroottable()) return 1;
    return ::PortCreateAct.call(this, path);
};
root.PortPlayBgmSync <- root.PlayBgmSync;
root.PlayBgmSync = function(name, fade, volume) {
    // Stage 5 requests the same track every update; reopening it repeats the Vorbis seek.
    if (GetBgmName() != name) PortPlayBgmSync(name, fade, volume);
};
root.PortWorldUpdate <- root.UpdateWorld;
root.UpdateWorld = function() {
    ::PortMenu(0, 6);
    if (!::PortMenu(6, 0)) {
        // Each map resets cx to 400; match the expanded camera and avoid the 150-pixel lookahead.
        camera2d.cx = camera2d.width * 0.5;
        camera2d.offset_cx = 0;
    }
    if (::PortMenu(1, 0) & 2) for (local i = 1; i <= 3; ++i) if (("player" + i) in actor) {
        local p = actor["player" + i];
        p.life = p.lifeMax;
        p.regainLife = p.lifeMax;
        p.soul = p.soulMax;
    }
    PortWorldUpdate();
};
root.PortEndPause <- root.EndPause;
root.EndPause = function() { ::PortMenu(0, 6); PortEndPause(); };

root.PortActorHit <- root.OnHitActor;
root.OnHitActor = function(atk, def, info) {
    local flags = ::PortMenu(1, 0);
    if ((flags & 2) && def.updateGroup == 1) return;
    if ((flags & 4) && game.enableHit && def.invin <= 0 && def.updateGroup != 1 &&
        atk.callbackGroup == 1 && atk.hitTarget != null && def.baseDef != 0 &&
        atk.baseAtk > 0 && atk.atkRate > 0 && def.life > 0) {
        local alreadyHit = false;
        foreach (name in atk.hitTarget) if (name == def.name) { alreadyHit = true; break; }
        if (!alreadyHit) def.life = 1;
    }
    return PortActorHit(atk, def, info);
};

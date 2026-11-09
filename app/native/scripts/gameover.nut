// Stage-failed and game-over captions are original ACT layers laid out on the 800-pixel canvas.
this.Port_gameOverUpdate <- this.Update;
this.Update = function() {
    if (!("Port_gameOverOrigins" in this)) this.Port_gameOverOrigins <- [gameover.dst_x, gameover2.dst_x];
    local shift = (::PortMenu(5, 0) - 800) * 0.5;
    gameover.dst_x = Port_gameOverOrigins[0] + shift;
    gameover2.dst_x = Port_gameOverOrigins[1] + shift;
    Port_gameOverUpdate();
};

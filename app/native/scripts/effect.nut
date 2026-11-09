// The level direction marker is a screen actor: ahead at x=700 near the original canvas'
// right edge, back at x=100 near its left edge. Keep both at the same edge of the wide screen.
local goEffect = this.GoEffect;
this.GoEffect <- function(t) : (goEffect) {
    goEffect.call(this, t);
    if (t.flag1 == 0) x += ::PortMenu(5, 0) - 800;
};

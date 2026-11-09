// Append temporarily after title_background.nut in the real Squirrel VM.
local titleRoot = getroottable(), savedTitlePort = titleRoot.PortMenu;
local titleState = { width = 2134, mounted = true, creates = 0, draws = [] };
local titleImage = { image_width = 1876, image_height = 600 };
local titleProbe = {
    title_back = { visible = true }, title_back_B = { visible = true }, white_dot = { visible = true },
    white_alpha = 1.0, BLEND_ALPHA = 1, funcUpdate = null,
    Port_titleBGInit = function() {},
    pl = {
        CreateResource2D = function(path) : (titleState, titleImage) {
            assert(path == "data/widescreen/title");
            titleState.creates++;
            return titleImage;
        },
        SetBltOrder = function(order) { assert(order == -1); },
        SetBlend = function(blend, alpha) { assert(blend == 1 && alpha == 1.0); },
        StretchBlt = function(x, y, width, height, image, sx, sy, sw, sh) : (titleState, titleImage) {
            assert(image == titleImage && y == 0 && height == 600 && sy == 0 && sh == 600);
            assert(sw > 0 && sx >= 0 && sx + sw <= image.image_width);
            titleState.draws.append({ x = x, width = width, sx = sx, sw = sw });
        }
    }
};
titleProbe.Init <- this.Init;
titleProbe.Port_titlePaint <- this.Port_titlePaint;
try {
    titleRoot.PortMenu = function(command, value) : (titleState) {
        if (command == 9) return titleState.mounted ? 1 : 0;
        assert(command == 5);
        return titleState.width;
    };
    titleProbe.Init();
    assert(titleState.creates == 1 && titleState.draws.len() == 3);
    assert(!titleProbe.title_back.visible && !titleProbe.title_back_B.visible &&
        !titleProbe.white_dot.visible && titleProbe.white_alpha == 0.0);
    foreach (width in [800, 1050, 1067, 1876, 2134, 6000, 8192]) {
        titleState.width = width;
        titleState.draws = [];
        titleProbe.Port_titlePaint();
        assert(titleState.creates == 1);
        // Compare every destination pixel with the existing presenter's edge reflection.
        for (local column = 0; column < width; column++) {
            local x = column + 0.5, hits = 0, source = 0.0;
            foreach (draw in titleState.draws) {
                local t = (x - draw.x) / draw.width;
                if (t >= 0.0 && t < 1.0) { hits++; source = draw.sx + t * draw.sw; }
            }
            assert(hits == 1);
            local expected = x + (1876 - width) / 2;
            if (expected < 0.0 || expected > 1876.0) {
                local distance = expected < 0.0 ? -expected : expected - 1876.0;
                local phase = distance - 512.0 * (distance / 512.0).tointeger();
                local reflected = phase <= 256.0 ? phase : 512.0 - phase;
                expected = expected < 0.0 ? reflected : 1876.0 - reflected;
            }
            assert(fabs(source - expected) < 0.001);
        }
    }
    titleProbe.Init();
    assert(titleState.creates == 2); // A new native stage gets one fresh owned resource.
    titleState.mounted = false;
    titleProbe.Init();
    assert(titleState.creates == 2);
} catch(error) {
    titleRoot.PortMenu = savedTitlePort;
    throw error;
}
titleRoot.PortMenu = savedTitlePort;
::PortMenu(3, "PASS: production Title panorama covers widths800..8192 exactly, shares one stage image, preserves center and mirrors outer256 edges before UI");

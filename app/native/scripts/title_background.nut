// Draw the panorama in its native player, before the title's foreground layers.
this.Port_titlePaint <- function() {
    title_back.visible = false;
    title_back_B.visible = false;
    white_dot.visible = false;
    white_alpha = 0.0;
    local image = Port_titleImage;
    local width = ::PortMenu(5, 0), height = image.image_height;
    local size = image.image_width, left = (width - size) / 2;
    local edge = 256;
    pl.SetBltOrder(-1);
    pl.SetBlend(BLEND_ALPHA, 1.0);
    pl.StretchBlt(left, 0, size, 600, image, 0, 0, size, height);
    // Reflect only the outer edge strips; the central illustration never repeats.
    local flip = true;
    for (local x = left; x > 0; x -= edge) {
        pl.StretchBlt(flip ? x : x - edge, 0, flip ? -edge : edge, 600,
            image, 0, 0, edge, height);
        flip = !flip;
    }
    flip = true;
    for (local x = left + size; x < width; x += edge) {
        pl.StretchBlt(flip ? x + edge : x, 0, flip ? -edge : edge, 600,
            image, size - edge, 0, edge, height);
        flip = !flip;
    }
};
this.Port_titleBGInit <- this.Init;
this.Init <- function() {
    Port_titleBGInit();
    if (::PortMenu(9, 0)) {
        // BeginStage replaces the resource container; keep one image per Init.
        this.Port_titleImage <- pl.CreateResource2D("data/widescreen/title");
        Port_titlePaint();
        funcUpdate = Port_titlePaint.bindenv(this);
    }
};
this.Port_titleBGSetMode <- this.SetMode;
this.SetMode <- function(value) {
    Port_titleBGSetMode(value);
    if (::PortMenu(9, 0)) funcUpdate = Port_titlePaint.bindenv(this);
};

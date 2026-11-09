#include "host.h"

WINAPI_EXPORT("user32.dll", CharLowerBuffA, 2)
{
    if (!ARG(0)) return 0;
    for (uint32_t i = 0; i < ARG(1); ++i) {
        uint8_t *text = (uint8_t *)GPTR(ARG(0));
        if (text[i] >= 'A' && text[i] <= 'Z') text[i] = (uint8_t)(text[i] + ('a' - 'A'));
    }
    return ARG(1);
}

static int char_lead(uint8_t value)
{
    return void_ansi_codepage() == 932 && ((value >= 0x81 && value <= 0x9F) || (value >= 0xE0 && value <= 0xFC));
}

WINAPI_EXPORT("user32.dll", CharNextA, 1)
{
    if (!ARG(0)) return 0;
    const uint8_t *text = (const uint8_t *)GPTR(ARG(0));
    return ARG(0) + (char_lead(text[0]) && text[1] ? 2u : 1u);
}

WINAPI_EXPORT("user32.dll", CharPrevA, 2)
{
    if (!ARG(0) || ARG(0) >= ARG(1)) return ARG(1);
    const uint8_t *start = (const uint8_t *)GPTR(ARG(0));
    const uint8_t *current = (const uint8_t *)GPTR(ARG(1));
    if (current - start > 1 && char_lead(current[-2])) return ARG(1) - 2;
    return ARG(1) - 1;
}

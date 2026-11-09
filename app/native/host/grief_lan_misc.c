#include "host.h"
#include "com.h"

WINAPI_EXPORT("kernel32.dll", FlushInstructionCache, 3) { return 1; }
WINAPI_EXPORT("shell32.dll", DragAcceptFiles, 2) { return 0; }
WINAPI_EXPORT("shell32.dll", DragFinish, 1) { return 0; }
WINAPI_EXPORT("shell32.dll", DragQueryFileW, 4) { return 0; }
WINAPI_EXPORT("winmm.dll", PlaySoundW, 3) { return 1; }
WINAPI_EXPORT("winmm.dll", timeGetDevCaps, 2)
{
    if (ARG(0) && void_vm_writable(ARG(0), 12)) memset(GPTR(ARG(0)), 0, 12);
    return 0;
}
WINAPI_EXPORT("imm32.dll", ImmAssociateContext, 2) { return 0; }
WINAPI_EXPORT("gdi32.dll", CreateFontW, 14) { return 0; }
uint32_t d3dx_font_create(uint32_t device, uint32_t height, uint32_t out);
WINAPI_EXPORT("d3dx9_43.dll", D3DXCreateFontW, 11)
{
    return d3dx_font_create(ARG(0), ARG(1), ARG(10));
}

typedef struct { void_com_object com; uint32_t device; } lan_sprite_t;
#define LAN_SPRITE ((lan_sprite_t *)void_com_host(THIS_))
COM_METHOD(lan_sprite, QueryInterface, 3) { void_com_addref(&LAN_SPRITE->com); MEM32(ARG(2)) = THIS_; return 0; }
COM_METHOD(lan_sprite, AddRef, 1) { return void_com_addref(&LAN_SPRITE->com); }
COM_METHOD(lan_sprite, Release, 1) { return void_com_release(&LAN_SPRITE->com); }
COM_METHOD(lan_sprite, GetDevice, 2)
{
    void_com_addref(void_com_host(LAN_SPRITE->device));
    MEM32(ARG(1)) = LAN_SPRITE->device;
    return 0;
}
COM_METHOD(lan_sprite, GetTransform, 2)
{
    memset(GPTR(ARG(1)), 0, 64);
    MEM32(ARG(1)) = MEM32(ARG(1) + 20) = MEM32(ARG(1) + 40) = MEM32(ARG(1) + 60) = 0x3F800000u;
    return 0;
}
COM_METHOD(lan_sprite, SetTransform, 2) { return 0; }
COM_METHOD(lan_sprite, SetWorldViewLH, 2) { return 0; }
COM_METHOD(lan_sprite, SetWorldViewRH, 2) { return 0; }
COM_METHOD(lan_sprite, Begin, 2) { return 0; }
COM_METHOD(lan_sprite, Draw, 6) { return 0; }
COM_METHOD(lan_sprite, DrawTransform, 6) { return 0; }
COM_METHOD(lan_sprite, End, 1) { return 0; }
COM_METHOD(lan_sprite, OnLostDevice, 1) { return 0; }
COM_METHOD(lan_sprite, OnResetDevice, 1) { return 0; }
static const void_fn lan_sprite_methods[] = {
    lan_sprite_QueryInterface, lan_sprite_AddRef, lan_sprite_Release, lan_sprite_GetDevice,
    lan_sprite_GetTransform, lan_sprite_SetTransform, lan_sprite_SetWorldViewLH, lan_sprite_SetWorldViewRH,
    lan_sprite_Begin, lan_sprite_Draw, lan_sprite_DrawTransform, lan_sprite_End,
    lan_sprite_OnLostDevice, lan_sprite_OnResetDevice,
};
static void_com_class lan_sprite_class = { "ID3DXSprite", 14, lan_sprite_methods, 0 };

WINAPI_EXPORT("d3dx9_43.dll", D3DXCreateSprite, 2)
{
    if (!ARG(1)) return 0x8876086Cu;
    lan_sprite_t *sprite = (lan_sprite_t *)calloc(1, sizeof *sprite);
    if (!sprite) return 0x8876017Cu;
    sprite->device = ARG(0);
    MEM32(ARG(1)) = void_com_wrap(&sprite->com, &lan_sprite_class, 0);
    return 0;
}

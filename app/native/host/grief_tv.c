/* stage3C's complete per-frame TV algorithm, with the original VM/renderer at its boundaries. */
#include "host.h"
#include "grief_tv_kernel.inc"

static tv_value tv_at(uint32_t vm, unsigned index)
{
    tv_value value;
    uint32_t address=MEM32(vm+24)+8u*(MEM32(vm+52)+index-1);
    memcpy(&value,GPTR(address),sizeof value);
    return value;
}

static int tv_array_view(tv_value value, tv_array *out)
{
    if (value.type != 0x08000040u || !value.bits || !void_vm_readable(value.bits,36)) return 0;
    uint32_t data=MEM32(value.bits+24), count=MEM32(value.bits+28);
    if (count > 16384 || (count && (!data || !void_vm_writable(data,count*8)))) return 0;
    out->count=count; out->values=GPTR(data);
    return 1;
}

static uint32_t tv_instance(tv_value value, uint32_t size)
{
    if (value.type != 0x0a008000u || !value.bits || !void_vm_readable(value.bits,36)) return 0;
    uint32_t userdata=MEM32(value.bits+32);
    if (!userdata || !void_vm_readable(userdata,4)) return 0;
    return void_vm_writable(userdata,size) ? userdata : 0;
}

typedef struct { uint32_t vm, random, player; } tv_bridge;

/* Same 72-byte CActLayer command and ownership callbacks as BitBlt/StretchBlt; no Sqrat or RTTI round trip. */
static void tv_queue(const tv_command *c)
{
    uint32_t entry=g_esp;
    g_esp=(g_esp-80u)&~7u;
    uint32_t record=g_esp;
    memset(GPTR(record),0,72);
    MEM32(record+4)=c->operation==TV_BLIT ? (uint32_t)c->blend : MEM32(c->layer+392);
    uint32_t alpha; memcpy(&alpha,&c->alpha,4);
    MEM32(record+8)=c->operation==TV_BLIT ? alpha : MEM32(c->layer+396);
    float x=(float)c->x,y=(float)c->y;
    memcpy(GPTR(record+12),&x,4);memcpy(GPTR(record+16),&y,4);
    MEM32(record+20)=c->sx;MEM32(record+24)=c->sy;
    MEM32(record+28)=c->operation==TV_BLIT ? (uint32_t)c->width : (uint32_t)c->sw;
    MEM32(record+32)=c->operation==TV_BLIT ? (uint32_t)c->height : (uint32_t)c->sh;
    MEM32(record+36)=c->width;MEM32(record+40)=c->height;MEM32(record+44)=0xffffffffu;
    MEM32(record+48)=MEM32(c->resource+80);MEM32(record+52)=MEM32(c->resource+84);
    g_eax=c->resource+36;g_edi=record+56;
    void_call_guest(0x00508360u,0,NULL,1);
    MEM32(record+64)=MEM32(c->layer+400);
    uint32_t vector=c->layer+344;g_ecx=record;
    void_call_guest(0x004EF220u,1,&vector,1);
    uint32_t control=MEM32(record+60);
    if (control && __atomic_fetch_sub((uint32_t *)GPTR(control+4),1,__ATOMIC_SEQ_CST)==1) {
        void_call_guest_this(MEM32(MEM32(control)),control,0,NULL);
        if (__atomic_fetch_sub((uint32_t *)GPTR(control+8),1,__ATOMIC_SEQ_CST)==1)
            void_call_guest_this(MEM32(MEM32(control)+4),control,0,NULL);
    }
    g_esp=entry;
}

static void tv_emit(void *context, const tv_command *c)
{
    (void)context;
    if (c->operation == TV_CAPTURE) { MEM8(c->resource+120)=1; return; }
    if (c->operation == TV_BLEND) {
        MEM32(c->layer+392)=(uint32_t)c->blend;
        memcpy(GPTR(c->layer+396),&c->alpha,4);
        return;
    }
    tv_queue(c);
}

static int32_t tv_random(void *context)
{
    tv_bridge *b=context;
    uint32_t top=MEM32(b->vm+48);
    uint32_t result=void_call_guest(b->random,1,&b->vm,0);
    if (result != 1 || MEM32(b->vm+48) != top+1) void_fatal("TV rand callback violated its stack contract");
    uint32_t value=MEM32(b->vm+24)+8*top;
    if (MEM32(value) != 0x05000002u) void_fatal("TV rand callback did not return an integer");
    int32_t random=(int32_t)MEM32(value+4);
    uint32_t pop[]={b->vm,1}; void_call_guest(0x0052BC90u,2,pop,0);
    return random;
}

static int tv_active(void *context)
{
    tv_bridge *b=context;
    return MEM8(b->player+8) && !MEM8(b->player+244);
}

static void tv_frame_call(void)
{
    void_regs saved; void_regs_save(&saved);
    uint32_t vm=ARG(0);
    tv_scene s={0}; tv_bridge bridge={.vm=vm};
    s.main_layer=tv_instance(tv_at(vm,2),404);
    s.bar_layer=tv_instance(tv_at(vm,3),404);
    s.capture=tv_instance(tv_at(vm,4),124);
    s.light=tv_instance(tv_at(vm,5),88);
    s.bar_image=tv_instance(tv_at(vm,6),88);
    s.bar_light=tv_instance(tv_at(vm,7),88);
    bridge.player=tv_instance(tv_at(vm,12),248);
    tv_value random=tv_at(vm,13);
    int valid=s.main_layer && s.bar_layer && s.capture && s.light && s.bar_image && s.bar_light && bridge.player;
    if (valid) valid=MEM32(s.main_layer)==0x0065C1B0u && MEM32(s.bar_layer)==0x0065C1B0u &&
        MEM32(s.capture)==0x0065B5BCu && MEM32(s.light)==0x0065B774u &&
        MEM32(s.bar_image)==0x0065B774u && MEM32(s.bar_light)==0x0065B774u;
    valid &= random.type == 0x08000200u && random.bits && void_vm_readable(random.bits,64);
    if (valid) { bridge.random=MEM32(random.bits+60); valid &= bridge.random==0x00549F00u; }
    tv_array data;
    valid = valid && tv_array_view(tv_at(vm,8),&data) && data.count == 10;
    if (valid) {
        for (unsigned i=0;i<4;++i) valid &= tv_array_view(data.values[i],&s.chips[i]);
        for (unsigned i=0;i<4;++i) valid &= tv_array_view(data.values[i+4],&s.source[i]);
        valid &= tv_array_view(data.values[8],&s.bars) && tv_array_view(data.values[9],&s.frames);
        tv_value x=tv_at(vm,9),y=tv_at(vm,10),shader=tv_at(vm,11),alpha=tv_at(vm,14),sub=tv_at(vm,15);
        valid &= (x.type == 0x05000002u || x.type == 0x05000004u) &&
            (y.type == 0x05000002u || y.type == 0x05000004u) && shader.type == 0x05000002u &&
            alpha.type == 0x05000002u && sub.type == 0x05000002u;
        s.camera_x=tv_number(x); s.camera_y=tv_number(y); s.shader=(int32_t)shader.bits;
        s.alpha_blend=(int32_t)alpha.bits; s.subtract_blend=(int32_t)sub.bits;
        memcpy(&s.main_x,GPTR(s.main_layer+148),4); memcpy(&s.main_y,GPTR(s.main_layer+152),4);
        memcpy(&s.bar_x,GPTR(s.bar_layer+148),4); memcpy(&s.bar_y,GPTR(s.bar_layer+152),4);
        valid &= tv_scene_valid(&s);
    }
    if (valid) {
        static int reported;
        if (!reported++) void_log("[tv] native stage3C Update; rand %08X, TVs %u/%u/%u/%u, bars %u",bridge.random,
            s.chips[0].count/2,s.chips[1].count/2,s.chips[2].count/2,s.chips[3].count/2,s.bars.count/2);
        tv_callbacks cb={&bridge,tv_emit,tv_random,tv_active}; tv_frame(&s,&cb);
    } else {
        static int reported;
        if (!reported++) void_log("[tv] nonstandard TV inputs: original Update selected");
    }
    uint32_t result[]={vm,(uint32_t)valid}; void_call_guest(0x0052B7B0u,2,result,0);
    void_regs_load(&saved); g_eax=1; g_esp+=4;
}

int void_grief_tv_enabled(void)
{
    static int mode=-1;
    if (mode < 0) {
        mode=1;
        char path[1200]; snprintf(path,sizeof path,"%s/tv-native",void_data_dir());
        FILE *file=fopen(path,"rb");
        if (file) { mode=fgetc(file)=='1'; fclose(file); }
        void_log("[tv] stage3C Update: %s",mode ? "native" : "original");
    }
    return mode;
}

void void_grief_tv_register(uint32_t vm)
{
    uint32_t name=void_gstrdup("PortTVFrame");
    void_call_guest(0x0052B930u,1,&vm,0);
    uint32_t push[]={vm,name,0xffffffffu}; void_call_guest(0x0052B740u,3,push,0);
    uint32_t closure[]={vm,void_thunk(tv_frame_call,"PortTVFrame"),0}; void_call_guest(0x0052EA10u,3,closure,0);
    uint32_t slot[]={vm,(uint32_t)-3,0};
    if ((int32_t)void_call_guest(0x0052DBC0u,3,slot,0)<0) void_fatal("Cannot register native TV frame");
    uint32_t pop[]={vm,1}; void_call_guest(0x0052BC90u,2,pop,0);
    void_gfree(name);
}

static void_fn original_capture;

static uint32_t tv_com(uint32_t object,unsigned slot,unsigned count,const uint32_t *parameters)
{
    uint32_t args[6]={object};
    for (unsigned i=0;i<count;++i) args[i+1]=parameters[i];
    return void_call_guest(MEM32(MEM32(object)+4*slot),(int)count+1,args,1);
}

static void tv_capture(void)
{
    if (!void_grief_tv_enabled()) { original_capture();return; }
    uint32_t self=g_ecx;
    if (!MEM8(self+121) || !MEM8(self+120)) {g_esp+=4;return;}
    uint32_t entry=g_esp;
    g_esp=(g_esp-80u)&~7u;
    uint32_t scratch=g_esp,device=MEM32(0x006ABF34u);
    MEM32(scratch)=0;
    uint32_t get_back[]={0,0,0,scratch};tv_com(device,18,4,get_back);
    uint32_t back=MEM32(scratch);
    if (back) {
        uint32_t desc=scratch+28;tv_com(back,12,1,&desc);
        uint32_t handles[]={MEM32(self+80),MEM32(self+84)};
        if (handles[0] || handles[1]) {
            uint32_t found=void_call_guest_this(MEM32(MEM32(0x006AB158u)+12),0x006AB158u,2,handles);
            uint32_t texture=found ? MEM32(found) : 0;
            if (texture) {
                MEM32(scratch+4)=0;
                uint32_t query[]={0x00632980u,scratch+4};
                if ((int32_t)tv_com(texture,0,2,query)>=0 && MEM32(scratch+4)) {
                    texture=MEM32(scratch+4);MEM32(scratch+8)=0;
                    uint32_t level[]={0,scratch+8};
                    /* Both original GetSurfaceLevel calls are observable, including the first ignored HRESULT. */
                    tv_com(texture,18,2,level);
                    if ((int32_t)tv_com(texture,18,2,level)>=0) {
                        uint32_t surface=MEM32(scratch+8),rect=scratch+12;
                        MEM32(rect)=MEM32(rect+4)=0;
                        MEM32(rect+8)=MEM32(self+88)-1;MEM32(rect+12)=MEM32(self+92)-1;
                        uint32_t blit[]={back,rect,surface,rect,0};tv_com(device,34,5,blit);
                        tv_com(surface,2,0,NULL);
                    }
                    tv_com(texture,2,0,NULL);
                }
            }
        }
        tv_com(back,2,0,NULL);MEM8(self+120)=0;
    }
    g_esp=entry+4;
}

void void_grief_tv_init(void)
{
    original_capture=void_dispatch_find(0x004D7800u);
    if (!original_capture) void_fatal("TV capture is not lifted");
    void_dispatch_add(0x004D7800u,tv_capture);
}

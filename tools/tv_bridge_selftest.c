/* Production C against separately compiled production lifted bodies; mock only external ownership/D3D calls. */
#include "../app/native/host/grief_tv.c"
#include <assert.h>
#include <sys/mman.h>
#include <sys/stat.h>

uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_fs_base,g_cur_func;
uint32_t g_flag_k,g_flag_a,g_flag_b,g_flag_cf,g_backedges;
uint32_t g_icall_trace[ICALL_TRACE_SIZE],g_icall_from[ICALL_TRACE_SIZE],g_icall_trace_idx;
int void_fpu_cmp;
xmm_t g_xmm[8];uint64_t g_mm[8];
double g_st_win[RECOMP_X87_SLOTS];int64_t g_st_i64_win[RECOMP_X87_SLOTS];
uint8_t g_st_exact_win[RECOMP_X87_SLOTS];int g_fp_sp,g_fp_top;
uint16_t g_fpu_cw=0x37f;
void (*recomp_yield_hook)(void);
#if defined(__arm__)
static uint8_t storage[0x700000];uint8_t *g_guest_mem=storage;
#else
__asm__(".globl g_guest_mem\n\t.hidden g_guest_mem\n\t.set g_guest_mem, 0x2000000000");
#endif
extern void sub_004D7800(void),sub_004EE0C0(void),sub_004EE3D0(void);
static uint32_t failure,control,trace[2048],used,record[17];
static unsigned level_call;
static const uint32_t OBJECT=0x20000,LAYER=0x21000,RESOURCE=0x22000,CONTROL=0x23000;
static const uint32_t DEVICE=0x24000,BACK=0x24100,TEXTURE=0x24200,SURFACE=0x24300,REGISTRY=0x24400;

static void observe(uint32_t a,uint32_t b,uint32_t c) {assert(used+3<=2048);trace[used++]=a;trace[used++]=b;trace[used++]=c;}
void void_log(const char *format,...) {(void)format;}
_Noreturn void void_fatal(const char *format,...) {fprintf(stderr,"%s\n",format);abort();}
const char *void_data_dir(void) {return "/data/local/tmp/grief-tv-bridge-check";}
int void_vm_readable(uint32_t address,uint32_t size) {return address>=0x1000 && address<0x700000 && size<=0x700000-address;}
int void_vm_writable(uint32_t address,uint32_t size) {return void_vm_readable(address,size);}
static void return_call(uint32_t value,unsigned bytes) {g_eax=value;g_esp+=bytes+4;}
void sub_005A9C3D(void) {g_esp+=4;}
void sub_005AACBF(void) {g_eax=ARG(0);g_esp+=4;}
void sub_00508360(void) {
    assert(g_eax==RESOURCE+36);observe(100,RESOURCE,control);
    MEM32(g_edi)=0x12345678;MEM32(g_edi+4)=control;g_eax=g_edi;g_esp+=4;
}
void sub_004EF220(void) {
    assert(ARG(0)==LAYER+344);memcpy(record,GPTR(g_ecx),sizeof record);
    observe(101,ARG(0),0);g_eax=0x31415926;g_esp+=8;
}
static void destructor(void) {observe(102,g_ecx,0);g_esp+=4;}
static void weak_free(void) {observe(103,g_ecx,0);g_esp+=4;}
static void lookup(void) {assert(g_ecx==0x6ab158);observe(1,ARG(0),ARG(1));return_call(REGISTRY,8);}
static void back_buffer(void) {
    assert(ARG(0)==DEVICE && ARG(1)==0 && ARG(2)==0 && ARG(3)==0);
    observe(2,DEVICE,0);MEM32(ARG(4))=(failure&1)?0:BACK;return_call(0,20);
}
static void description(void) {observe(3,ARG(0),0);memset(GPTR(ARG(1)),0x37,32);return_call(0,8);}
static void query(void) {
    assert(ARG(0)==TEXTURE && ARG(1)==0x632980);observe(4,TEXTURE,ARG(1));
    MEM32(ARG(2))=(failure&4)?0:TEXTURE;return_call((failure&2)?0x80004005:0,12);
}
static void surface_level(void) {
    assert(ARG(0)==TEXTURE && ARG(1)==0);observe(5,TEXTURE,++level_call);
    MEM32(ARG(2))=SURFACE;return_call((failure&(level_call==1?8:16))?0x80004005:0,12);
}
static void blit(void) {
    assert(ARG(0)==DEVICE && ARG(1)==BACK && ARG(3)==SURFACE && ARG(5)==0);
    assert(!memcmp(GPTR(ARG(2)),GPTR(ARG(4)),16));
    observe(6,MEM32(ARG(2)+8),MEM32(ARG(2)+12));return_call(0,24);
}
static void release(void) {observe(7,ARG(0),0);return_call(0,4);}

recomp_func_t void_resolve(uint32_t va) {
    switch(va) {
    case 0x508360:return sub_00508360;case 0x4ef220:return sub_004EF220;
    case 0x700001:return destructor;case 0x700002:return weak_free;
    case 0x700003:return lookup;case 0x700004:return back_buffer;
    case 0x700005:return description;case 0x700006:return query;
    case 0x700007:return surface_level;case 0x700008:return blit;case 0x700009:return release;
    default:fprintf(stderr,"unexpected target %08X\n",va);abort();
    }
}
uint32_t void_call_guest(uint32_t va,int count,const uint32_t *args,int stdcall_pops) {
    uint32_t entry=g_esp,ebx=g_ebx,esi=g_esi,edi=g_edi,ebp=g_ebp;
    for(int i=count;i>0;--i)PUSH32(g_esp,args[i-1]);PUSH32(g_esp,RECOMP_RETADDR);
    void_resolve(va)();assert(g_esp==entry-(stdcall_pops?0:4u*count));g_esp=entry;
    g_ebx=ebx;g_esi=esi;g_edi=edi;g_ebp=ebp;return g_eax;
}
uint32_t void_call_guest_this(uint32_t va,uint32_t self,int n,const uint32_t *args) {g_ecx=self;return void_call_guest(va,n,args,1);}

static void reset(void) {
    memset(GPTR(0x1000),0,0x690000);
    memset(g_st_win,0,sizeof g_st_win);memset(g_st_i64_win,0,sizeof g_st_i64_win);
    memset(g_st_exact_win,0,sizeof g_st_exact_win);g_fp_sp=128;g_fp_top=0;g_fpu_cw=0x37f;void_fpu_cmp=0;
    g_eax=0x1111;g_ecx=OBJECT;g_edx=0x3333;g_ebx=0x4444;g_esi=0x5555;g_edi=0x6666;g_ebp=0x7777;
    g_esp=0x18000;MEM32(g_esp)=RECOMP_RETADDR;g_fs_base=0x1000;
    used=level_call=0;memset(record,0,sizeof record);
    MEM32(0x6abf34)=DEVICE;MEM32(0x6ab158)=0x25000;MEM32(0x25000+12)=0x700003;
    MEM32(DEVICE)=0x25100;MEM32(0x25100+72)=0x700004;MEM32(0x25100+136)=0x700008;
    MEM32(BACK)=0x25200;MEM32(0x25200+48)=0x700005;MEM32(0x25200+8)=0x700009;
    MEM32(TEXTURE)=0x25300;MEM32(0x25300)=0x700006;MEM32(0x25300+72)=0x700007;MEM32(0x25300+8)=0x700009;
    MEM32(SURFACE)=0x25400;MEM32(0x25400+8)=0x700009;
    MEM32(CONTROL)=0x25500;MEM32(0x25500)=0x700001;MEM32(0x25500+4)=0x700002;
    MEM32(REGISTRY)=(failure&32)?0:TEXTURE;
    MEM32(OBJECT+80)=9;MEM32(OBJECT+84)=10;MEM32(OBJECT+88)=800;MEM32(OBJECT+92)=600;
    MEM32(RESOURCE+80)=11;MEM32(RESOURCE+84)=12;
    MEM32(LAYER+392)=3;MEM32(LAYER+396)=0x3f000000;MEM32(LAYER+400)=37;
}

int main(void) {
#if defined(__aarch64__)
    assert(mmap((void *)VOID_GUEST_BASE,0x700000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==(void *)VOID_GUEST_BASE);
#endif
    mkdir(void_data_dir(),0775);char path[200];snprintf(path,sizeof path,"%s/tv-native",void_data_dir());
    FILE *file=fopen(path,"wb");assert(file);assert(fputs("1",file)>=0);assert(!fclose(file));
    assert(void_grief_tv_enabled());
    unsigned cases=0;
    for (failure=0;failure<64;++failure)for(unsigned flags=0;flags<4;++flags) {
        uint32_t expected[2048],n,flag;
        reset();MEM8(OBJECT+120)=flags&1;MEM8(OBJECT+121)=flags>>1;
        sub_004D7800();assert(g_esp==0x18004);assert(g_ebx==0x4444&&g_esi==0x5555&&g_edi==0x6666&&g_ebp==0x7777);
        n=used;memcpy(expected,trace,n*4);flag=MEM8(OBJECT+120);
        reset();MEM8(OBJECT+120)=flags&1;MEM8(OBJECT+121)=flags>>1;
        tv_capture();assert(g_esp==0x18004);assert(MEM8(OBJECT+120)==flag);
        if (used!=n || memcmp(trace,expected,n*4)) {
            fprintf(stderr,"capture failure %u flags %u: %u vs %u words\n",failure,flags,used,n);
            for(unsigned i=0;i<used||i<n;++i)fprintf(stderr,"%u %08X %08X\n",i,i<used?trace[i]:0,i<n?expected[i]:0);
            abort();
        }
        ++cases;
    }
    failure=0;
    for(unsigned operation=TV_STRETCH;operation<=TV_BLIT;++operation)for(unsigned owner=0;owner<4;++owner)
        for(int position=-512;position<=512;position+=17) {
            tv_command c={.operation=operation,.layer=LAYER,.resource=RESOURCE,.x=position,.y=-position,
                .width=98,.height=82,.sx=13,.sy=7,.sw=543,.sh=421,.blend=5,.alpha=0.625f};
            uint32_t expected[2048],expected_record[17],n,refs[2];
            reset();control=owner?CONTROL:0;MEM32(CONTROL+4)=owner-1;MEM32(CONTROL+8)=owner-1;
            uint32_t alpha;memcpy(&alpha,&c.alpha,4);
            uint32_t args[]={c.x,c.y,c.width,c.height,RESOURCE,c.sx,c.sy,
                operation==TV_STRETCH?(uint32_t)c.sw:(uint32_t)c.blend,operation==TV_STRETCH?(uint32_t)c.sh:alpha};
            g_ecx=LAYER;g_esp=0x18000;
            for(int i=8;i>=0;--i)PUSH32(g_esp,args[i]);PUSH32(g_esp,RECOMP_RETADDR);
            if(operation==TV_STRETCH)sub_004EE3D0();else sub_004EE0C0();assert(g_esp==0x18000);
            n=used;memcpy(expected,trace,n*4);memcpy(expected_record,record,sizeof record);
            refs[0]=MEM32(CONTROL+4);refs[1]=MEM32(CONTROL+8);
            reset();control=owner?CONTROL:0;MEM32(CONTROL+4)=owner-1;MEM32(CONTROL+8)=owner-1;
            tv_queue(&c);assert(g_esp==0x18000);
            if(memcmp(record,expected_record,sizeof record)) {
                for(unsigned i=0;i<17;++i)if(record[i]!=expected_record[i])fprintf(stderr,"record %u: %08X != %08X\n",i,record[i],expected_record[i]);abort();
            }
            if (used!=n || memcmp(trace,expected,n*4)) {
                fprintf(stderr,"queue op %u owner %u position %d: %u vs %u words\n",operation,owner,position,used,n);
                for(unsigned i=0;i<used||i<n;++i)fprintf(stderr,"%u %08X %08X\n",i,i<used?trace[i]:0,i<n?expected[i]:0);
                abort();
            }
            assert(MEM32(CONTROL+4)==refs[0]&&MEM32(CONTROL+8)==refs[1]);++cases;
        }
    reset();MEM32(0x31000+32)=LAYER;
    tv_value instance={0x0a008000u,0x31000};assert(tv_instance(instance,404)==LAYER);
    instance.type=0x05000002;assert(!tv_instance(instance,404));
    printf("PASS: %u lifted/native capture/queue cases; D3D callbacks, ownership, 68-byte records, ESP, instance ABI\n",cases);
    return 0;
}

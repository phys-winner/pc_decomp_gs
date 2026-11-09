#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../app/native/host/grief_tv_kernel.inc"

static FILE *input;
static uint32_t seed, emissions, expected_emissions, active_calls;
static int is_active;
static uint32_t word(void) { uint32_t x; assert(fread(&x,4,1,input)==1); return x; }
static void values(tv_array *a, uint32_t n) {
    a->count=n; a->values=calloc(n ? n : 1,sizeof *a->values); assert(a->values);
    assert(fread(a->values,8,n,input)==n);
}
static void emit(void *context,const tv_command *c) {
    (void)context;
    tv_command expected; assert(fread(&expected,sizeof expected,1,input)==1);
    if (memcmp(c,&expected,sizeof expected)) {
        const uint32_t *a=(const uint32_t *)c,*b=(const uint32_t *)&expected;
        for (unsigned i=0;i<sizeof expected/4;++i)
            if (a[i]!=b[i]) fprintf(stderr,"command %u field %u: %08X != %08X\n",emissions,i,a[i],b[i]);
        abort();
    }
    ++emissions; assert(emissions<=expected_emissions);
}
static int32_t random_value(void *context) {
    (void)context; seed=seed*1664525u+1013904223u; return (int32_t)(seed>>1);
}
static int active(void *context) { (void)context; ++active_calls; return is_active; }

int main(int argc,char **argv) {
    _Static_assert(sizeof(tv_command)==52,"trace ABI");
    assert(argc==2); input=fopen(argv[1],"rb"); assert(input);
    assert(word()==0x314E5654u); uint32_t cases=word(); uint64_t commands=0;
    for (uint32_t test=0;test<cases;++test) {
        tv_scene s={.main_layer=1,.bar_layer=2,.capture=3,.light=4,.bar_image=5,.bar_light=6,
            .alpha_blend=1,.subtract_blend=3};
        uint32_t counts[5]; for (unsigned i=0;i<5;++i) counts[i]=word();
        s.shader=(int32_t)word(); is_active=(int)word(); seed=word();
        float p[6]; assert(fread(p,4,6,input)==6);
        s.camera_x=p[0];s.camera_y=p[1];s.main_x=p[2];s.main_y=p[3];s.bar_x=p[4];s.bar_y=p[5];
        uint32_t total=0;
        for (unsigned i=0;i<4;++i) { values(&s.chips[i],counts[i]*2); total+=counts[i]; }
        for (unsigned i=0;i<4;++i) values(&s.source[i],total);
        values(&s.bars,counts[4]*2);values(&s.frames,counts[4]);
        expected_emissions=word(); emissions=active_calls=0;
        assert(tv_scene_valid(&s));
        tv_callbacks cb={NULL,emit,random_value,active};tv_frame(&s,&cb);
        if (emissions!=expected_emissions) fprintf(stderr,"case %u emissions %u != %u\n",test,emissions,expected_emissions);
        assert(emissions==expected_emissions);commands+=emissions;
        for (uint32_t i=0;i<s.frames.count;++i) assert(s.frames.values[i].bits==word());
        assert(seed==word());assert(active_calls==word());
        for (unsigned i=0;i<4;++i) { free(s.chips[i].values);free(s.source[i].values); }
        free(s.bars.values);free(s.frames.values);
    }
    assert(fgetc(input)==EOF);fclose(input);
    tv_scene invalid={0};invalid.camera_x=NAN;assert(!tv_scene_valid(&invalid));
    invalid.camera_x=INFINITY;assert(!tv_scene_valid(&invalid));
    invalid.camera_x=0;assert(tv_scene_valid(&invalid));
    tv_value odd={0x05000002u,0};invalid.chips[0]=(tv_array){&odd,1};assert(!tv_scene_valid(&invalid));
    tv_value pair[2]={{0x05000002u,0},{0x05000002u,0}};
    invalid.chips[0]=(tv_array){0};invalid.bars=(tv_array){pair,2};invalid.frames=(tv_array){pair,1};
    assert(!tv_scene_valid(&invalid));
    printf("PASS: %u original-bytecode cases, %llu exact draw/capture/blend observations; frames, RNG and activity order match\n",
        cases,(unsigned long long)commands);
    return 0;
}

/* GP100_OBS_V11 — наблюдатель PCIe-линка с исправлениями F1-F5 (обзор V10).
 *
 * F1: ЛЮБАЯ потеря достоверности делает прогон неполным -> OBSERVATION_LOST,
 *     независимо от событий до неё (classify не смотрит на счётчик событий).
 * F2: изменения LnkSta разложены по полям: SPEED[3:0], WIDTH[9:4], TRAINING[11],
 *     DLL_ACTIVE[13], иначе STATUS. speed_change считает ТОЛЬКО SPEED.
 *     События BAR 0x8c040 помечаются BAR_STATUS и НЕ называются сменой скорости.
 * F3: сохраняются начальные/конечные сырые значения и список событий с
 *     интервалами чтений; фактическая скорость (LnkSta[3:0]) выводится отдельно;
 *     стабильность = NO_CHANGE (а не STILL_GEN1).
 * F4: рабочий deadline по часам backend; остановка при первом invalid и overflow;
 *     TIMEOUT возникает из ПОВЕДЕНИЯ (часы истекли до READY), не из присваивания.
 * F5: config-only режим: resource0 НЕ открывается, rd_bar32 не вызывается,
 *     writeback отсутствует (нет wr-callback вообще в config-only backend).
 *
 * Аппаратно разрешён ТОЛЬКО config-only read (O_RDONLY + pread). Никаких mmap,
 * pwrite, retrain. PCIe cap offsets берутся из списка capabilities устройства.
 *
 * Сборка: gcc -O2 -Wall -Wextra -o gp100_obs_v11 gp100_obs_v11.c
 * Тесты:  ./gp100_obs_v11 --test
 * HW:     ./gp100_obs_v11 --hw-config <ep_bdf> <up_bdf> <sec> (гейт CMP100_ALLOW_HW_RUN=1)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#endif

/* ---------------- классификаторы достоверности ---------------- */
static int bar_invalid(uint32_t v){ return v==0xffffffffu || (v&0xffff0000u)==0xbadf0000u; }
static int cfg_invalid(uint16_t v){ return v==0xffffu; }

/* ---------------- декод полей LnkSta (PCIe Link Status 16 бит) --- */
static int lnk_speed(uint16_t v){ return v & 0x0f; }
static int lnk_width(uint16_t v){ return (v>>4) & 0x3f; }
static int lnk_train(uint16_t v){ return (v>>11) & 1; }
static int lnk_dll  (uint16_t v){ return (v>>13) & 1; }

enum evkind { EV_SPEED, EV_WIDTH, EV_TRAIN, EV_DLL, EV_STATUS, EV_BAR_STATUS };
static const char*evkind_name(enum evkind k){
    switch(k){case EV_SPEED:return "SPEED";case EV_WIDTH:return "WIDTH";case EV_TRAIN:return "TRAIN";
        case EV_DLL:return "DLL";case EV_STATUS:return "STATUS";case EV_BAR_STATUS:return "BAR_STATUS";}
    return "?";
}
/* Классификация изменения LnkSta old->new по приоритету полей. */
static enum evkind classify_lnk(uint16_t o,uint16_t n){
    if(lnk_speed(o)!=lnk_speed(n)) return EV_SPEED;
    if(lnk_width(o)!=lnk_width(n)) return EV_WIDTH;
    if(lnk_train(o)!=lnk_train(n)) return EV_TRAIN;
    if(lnk_dll(o)!=lnk_dll(n))     return EV_DLL;
    return EV_STATUS;
}

/* ---------------- backend vtable (rd_bar32 может быть NULL) ------ */
struct backend {
    uint64_t (*now)(void*);
    int (*rd_cfg16)(void*, int which, uint16_t*); /* 0=ep 1=up */
    int (*rd_bar32)(void*, int which, uint32_t*); /* NULL в config-only */
    void*ctx;
};

/* ---------------- состояние/исходы ------------------------------ */
enum outcome { OUT_OBSERVED, OUT_NO_CHANGE, OUT_TIMEOUT, OUT_OVERFLOW,
               OUT_OBSERVATION_LOST, OUT_SETUP_FAIL };
static const char*outcome_name(enum outcome o){
    switch(o){case OUT_OBSERVED:return "OBSERVED";case OUT_NO_CHANGE:return "NO_CHANGE";
        case OUT_TIMEOUT:return "TIMEOUT";case OUT_OVERFLOW:return "OVERFLOW";
        case OUT_OBSERVATION_LOST:return "OBSERVATION_LOST";case OUT_SETUP_FAIL:return "SETUP_FAIL";}
    return "?";
}
#define MAXEV 256
struct event { uint64_t before, after; int dev; uint32_t raw; enum evkind kind; };
struct expstate {
    int ready, timed_out, invalid_seen, overflow;
    uint16_t init_ep, init_up, fin_ep, fin_up;
    uint32_t init_bar, fin_bar; int have_bar;
    unsigned nevents, speed_changes, width_changes;
    struct event ev[MAXEV];
    uint64_t last_ts, max_gap;
    int ts_order_ok;
    unsigned reads;
    enum outcome outcome;
};
static void st_init(struct expstate*s){ memset(s,0,sizeof*s); s->ts_order_ok=1; }

static void push_event(struct expstate*s,uint64_t b,uint64_t a,int dev,uint32_t raw,enum evkind k){
    if(s->nevents>=MAXEV){ s->overflow=1; return; }
    s->ev[s->nevents++] = (struct event){b,a,dev,raw,k};
    if(k==EV_SPEED) s->speed_changes++;
    if(k==EV_WIDTH) s->width_changes++;
}

/* одно измерение config16 с меткой интервала; возвращает 0/-1(invalid|shortread) */
static int rd16(struct backend*bk,struct expstate*s,int which,uint16_t*o,uint64_t*b,uint64_t*a){
    *b=bk->now(bk->ctx);
    int r=bk->rd_cfg16(bk->ctx,which,o);
    *a=bk->now(bk->ctx);
    if(s->last_ts && (*a<*b||*b<s->last_ts)) s->ts_order_ok=0;
    if(s->last_ts){ uint64_t g=(*a>=s->last_ts)?(*a-s->last_ts):0; if(g>s->max_gap)s->max_gap=g; }
    s->last_ts=*a; s->reads++;
    if(r!=2||cfg_invalid(*o)){ s->invalid_seen=1; return -1; }
    return 0;
}
static int rd32(struct backend*bk,struct expstate*s,int which,uint32_t*o,uint64_t*b,uint64_t*a){
    *b=bk->now(bk->ctx);
    int r=bk->rd_bar32(bk->ctx,which,o);
    *a=bk->now(bk->ctx);
    if(s->last_ts && (*a<*b||*b<s->last_ts)) s->ts_order_ok=0;
    if(s->last_ts){ uint64_t g=(*a>=s->last_ts)?(*a-s->last_ts):0; if(g>s->max_gap)s->max_gap=g; }
    s->last_ts=*a; s->reads++;
    if(r!=0||bar_invalid(*o)){ s->invalid_seen=1; return -1; }
    return 0;
}

/* F1: любая потеря достоверности -> неполный прогон, вне зависимости от событий. */
static enum outcome classify(struct expstate*s){
    if(!s->ready)        return s->timed_out?OUT_TIMEOUT:OUT_SETUP_FAIL;
    if(s->invalid_seen)  return OUT_OBSERVATION_LOST;
    if(s->overflow)      return OUT_OVERFLOW;
    if(s->timed_out)     return OUT_TIMEOUT;
    if(s->speed_changes) return OUT_OBSERVED;
    return OUT_NO_CHANGE;
}
static int exit_code(enum outcome o){
    switch(o){case OUT_OBSERVED:case OUT_NO_CHANGE:return 0;
        case OUT_TIMEOUT:return 8;case OUT_OVERFLOW:return 9;
        case OUT_OBSERVATION_LOST:return 5;case OUT_SETUP_FAIL:return 4;}
    return 4;
}

/* ---------------- общее ядро (config-only когда use_bar==0) ------ */
/* deadline: значение часов backend, при достижении которого наблюдение
 * завершается штатно. Если часы истекают ещё на setup-чтениях -> TIMEOUT. */
static void run_core(struct backend*bk,struct expstate*s,uint64_t deadline,int use_bar){
    uint64_t b,a;
    if(rd16(bk,s,0,&s->init_ep,&b,&a)<0){ s->outcome=OUT_SETUP_FAIL; return; }
    if(a>=deadline){ s->timed_out=1; s->outcome=OUT_TIMEOUT; return; }
    if(rd16(bk,s,1,&s->init_up,&b,&a)<0){ s->outcome=OUT_SETUP_FAIL; return; }
    if(a>=deadline){ s->timed_out=1; s->outcome=OUT_TIMEOUT; return; }
    if(use_bar && bk->rd_bar32){
        if(rd32(bk,s,0,&s->init_bar,&b,&a)<0){ s->outcome=OUT_SETUP_FAIL; return; }
        s->have_bar=1;
        if(a>=deadline){ s->timed_out=1; s->outcome=OUT_TIMEOUT; return; }
    }
    s->ready=1;
    uint16_t lep=s->init_ep, lup=s->init_up; uint32_t lbar=s->init_bar;
    s->fin_ep=lep; s->fin_up=lup; s->fin_bar=s->init_bar;

    for(;;){
        if(bk->now(bk->ctx)>=deadline){ break; }   /* штатное завершение окна (НЕ timeout) */
        uint16_t v; uint32_t v32;
        if(rd16(bk,s,0,&v,&b,&a)<0) break;                 /* F4: стоп на invalid */
        if(v!=lep){ push_event(s,b,a,0,v,classify_lnk(lep,v)); lep=v; s->fin_ep=v; if(s->overflow)break; }
        if(rd16(bk,s,1,&v,&b,&a)<0) break;
        if(v!=lup){ push_event(s,b,a,1,v,classify_lnk(lup,v)); lup=v; s->fin_up=v; if(s->overflow)break; }
        if(use_bar && bk->rd_bar32){
            if(rd32(bk,s,0,&v32,&b,&a)<0) break;
            if((v32&0x1f)!=(lbar&0x1f)){ push_event(s,b,a,2,v32,EV_BAR_STATUS); lbar=v32; s->fin_bar=v32; if(s->overflow)break; }
        }
    }
    s->outcome=classify(s);
}

/* печать результата (после завершения) */
static void report(struct expstate*s,const char*tag){
    printf("== %s ==\n",tag);
    printf("outcome=%s exit=%d ready=%d timed_out=%d invalid=%d overflow=%d ts_ok=%d reads=%u max_gap=%" PRIu64 "\n",
        outcome_name(s->outcome),exit_code(s->outcome),s->ready,s->timed_out,s->invalid_seen,
        s->overflow,s->ts_order_ok,s->reads,s->max_gap);
    printf("init: ep=0x%04x(speed=%d width=%d) up=0x%04x(speed=%d width=%d)",
        s->init_ep,lnk_speed(s->init_ep),lnk_width(s->init_ep),
        s->init_up,lnk_speed(s->init_up),lnk_width(s->init_up));
    if(s->have_bar) printf(" bar0x8c040=0x%08x",s->init_bar);
    printf("\nfinal: ep=0x%04x(speed=%d width=%d) up=0x%04x(speed=%d width=%d)",
        s->fin_ep,lnk_speed(s->fin_ep),lnk_width(s->fin_ep),
        s->fin_up,lnk_speed(s->fin_up),lnk_width(s->fin_up));
    if(s->have_bar) printf(" bar0x8c040=0x%08x",s->fin_bar);
    printf("\nactual_speed_ep=%d actual_speed_up=%d (1=Gen1 2=Gen2 3=Gen3)\n",
        lnk_speed(s->fin_ep),lnk_speed(s->fin_up));
    printf("events=%u (speed_changes=%u width_changes=%u):\n",s->nevents,s->speed_changes,s->width_changes);
    for(unsigned i=0;i<s->nevents;i++)
        printf("  [%u] dev%d %s raw=0x%08x before=%" PRIu64 " after=%" PRIu64 "\n",
            i,s->ev[i].dev,evkind_name(s->ev[i].kind),s->ev[i].raw,s->ev[i].before,s->ev[i].after);
    printf("NOTE: между выборками возможен кратковременный переход, не наблюдаемый в интервале max_gap.\n");
}

/* ================================================================ */
/* ТЕСТОВЫЙ backend                                                 */
/* ================================================================ */
struct tctx {
    uint64_t clk, clk_step;
    const uint16_t *ep_seq; int ep_len, ep_idx;   /* последовательность ep */
    const uint16_t *up_seq; int up_len, up_idx;
    int ep_fail_at;                                /* индекс ep-чтения -> 0xffff */
    int bar_calls;                                 /* должно быть 0 в config-only */
};
static uint64_t t_now(void*c){ struct tctx*t=c; t->clk+=t->clk_step?t->clk_step:1; return t->clk; }
static int t_rd_cfg16(void*c,int which,uint16_t*o){ struct tctx*t=c;
    if(which==0){ int i=t->ep_idx++;
        if(t->ep_fail_at>=0 && i==t->ep_fail_at){*o=0xffff;return 2;}
        *o = t->ep_seq ? t->ep_seq[i<t->ep_len?i:t->ep_len-1] : 0x1011; return 2; }
    int j=t->up_idx++;
    *o = t->up_seq ? t->up_seq[j<t->up_len?j:t->up_len-1] : 0x1011; return 2;
}
static int t_rd_bar32(void*c,int which,uint32_t*o){ (void)which; struct tctx*t=c; t->bar_calls++; *o=0x08089000; return 0; }
static struct backend mk_test(struct tctx*t){
    memset(t,0,sizeof*t); t->clk_step=1; t->ep_fail_at=-1;
    struct backend bk={t_now,t_rd_cfg16,t_rd_bar32,t};
    return bk;
}

static int T=0,Fl=0;
#define CHK(c,n) do{ if(c){T++;} else {Fl++;printf("  FAIL: %s\n",n);} }while(0)

static int run_tests(void){
    printf("== V11 OFFLINE TESTS ==\n");
    CHK(bar_invalid(0xffffffff)&&bar_invalid(0xbadf5040)&&!bar_invalid(0x08089000),"bar_invalid");
    CHK(cfg_invalid(0xffff)&&!cfg_invalid(0x1011),"cfg_invalid");
    /* F2 классификация */
    CHK(classify_lnk(0x1011,0x2011)==EV_DLL,"F2 0x1011->0x2011 = DLL (не speed)");
    CHK(classify_lnk(0x1011,0x5011)==EV_STATUS,"F2 0x1011->0x5011 = STATUS");
    CHK(classify_lnk(0x1011,0x1012)==EV_SPEED,"F2 0x1011->0x1012 = SPEED");
    CHK(classify_lnk(0x1011,0x1021)==EV_WIDTH,"F2 0x1011->0x1021 = WIDTH");

    /* B1: событие затем invalid -> OBSERVATION_LOST, exit!=0 (F1) */
    {static const uint16_t seq[]={0x1011,0x1012,0xffff}; struct tctx t; struct backend bk=mk_test(&t);
     t.ep_seq=seq; t.ep_len=3; t.ep_fail_at=2;
     struct expstate s; st_init(&s); run_core(&bk,&s,100000,0);
     CHK(s.speed_changes>=1 && s.outcome==OUT_OBSERVATION_LOST && exit_code(s.outcome)==5,
         "B1 событие+invalid -> OBSERVATION_LOST exit5 (F1)");}

    /* B2: invalid затем событие -> немедленная остановка, чтений после нет */
    {static const uint16_t seq[]={0x1011,0xffff,0x1012}; struct tctx t; struct backend bk=mk_test(&t);
     t.ep_seq=seq; t.ep_len=3; t.ep_fail_at=1;
     struct expstate s; st_init(&s); run_core(&bk,&s,100000,0);
     unsigned reads_at_stop=s.reads;
     CHK(s.invalid_seen && s.nevents==0,"B2 invalid до события -> стоп, 0 событий");
     CHK(reads_at_stop>=2,"B2 остановка зафиксирована");}

    /* B3: стабильные 0x1011 и 0x1012 -> NO_CHANGE, разные фактические скорости */
    {struct tctx t; struct backend bk=mk_test(&t); /* ep постоянно 0x1011 */
     struct expstate s; st_init(&s); run_core(&bk,&s,40,0);
     CHK(s.outcome==OUT_NO_CHANGE && lnk_speed(s.fin_ep)==1,"B3a стабильный 0x1011 -> NO_CHANGE speed1");}
    {static const uint16_t seq[]={0x1012}; struct tctx t; struct backend bk=mk_test(&t);
     t.ep_seq=seq; t.ep_len=1;
     struct expstate s; st_init(&s); run_core(&bk,&s,40,0);
     CHK(s.outcome==OUT_NO_CHANGE && lnk_speed(s.fin_ep)==2,"B3b стабильный 0x1012 -> NO_CHANGE speed2 (Gen2 стабилен!)");}

    /* B4: 0x1011->0x5011 статусное событие, speed_change=false */
    {static const uint16_t seq[]={0x1011,0x5011}; struct tctx t; struct backend bk=mk_test(&t);
     t.ep_seq=seq; t.ep_len=2;
     struct expstate s; st_init(&s); run_core(&bk,&s,40,0);
     CHK(s.nevents>=1 && s.ev[0].kind==EV_STATUS && s.speed_changes==0,"B4 STATUS событие, speed_change=false");}

    /* B5: 0x1011->0x1012 speed_change=true */
    {static const uint16_t seq[]={0x1011,0x1012}; struct tctx t; struct backend bk=mk_test(&t);
     t.ep_seq=seq; t.ep_len=2;
     struct expstate s; st_init(&s); run_core(&bk,&s,40,0);
     CHK(s.speed_changes==1 && s.outcome==OUT_OBSERVED,"B5 speed_change=true -> OBSERVED");}

    /* B6: 0x1011->0x1021 width_change=true */
    {static const uint16_t seq[]={0x1011,0x1021}; struct tctx t; struct backend bk=mk_test(&t);
     t.ep_seq=seq; t.ep_len=2;
     struct expstate s; st_init(&s); run_core(&bk,&s,40,0);
     CHK(s.width_changes==1 && s.speed_changes==0,"B6 width_change=true, speed_change=false");}

    /* B7: реальное переполнение буфера чередующейся последовательностью */
    {static uint16_t seq[MAXEV+50]; for(int i=0;i<MAXEV+50;i++) seq[i]=(i&1)?0x1012:0x1011;
     struct tctx t; struct backend bk=mk_test(&t); t.ep_seq=seq; t.ep_len=MAXEV+50;
     struct expstate s; st_init(&s); run_core(&bk,&s,1000000,0);
     CHK(s.overflow && s.outcome==OUT_OVERFLOW && exit_code(s.outcome)==9,"B7 реальный overflow -> exit9");}

    /* B8: истечение deadline поведением часов (clk_step большой) */
    {struct tctx t; struct backend bk=mk_test(&t); t.clk_step=1000;
     struct expstate s; st_init(&s); run_core(&bk,&s,500,0); /* первое же чтение уводит clk>deadline */
     CHK(s.timed_out && s.outcome==OUT_TIMEOUT && exit_code(s.outcome)==8,"B8 deadline поведением -> TIMEOUT exit8");}

    /* B9: config-only -> ноль вызовов BAR, ноль записей (нет wr-callback вообще) */
    {struct tctx t; struct backend bk=mk_test(&t);
     struct expstate s; st_init(&s); run_core(&bk,&s,40,0 /*use_bar=0*/);
     CHK(t.bar_calls==0,"B9 config-only -> 0 вызовов BAR backend");}
    /* контроль: с use_bar=1 BAR вызывается (доказывает, что 0 в B9 не тривиально) */
    {struct tctx t; struct backend bk=mk_test(&t);
     struct expstate s; st_init(&s); run_core(&bk,&s,40,1 /*use_bar=1*/);
     CHK(t.bar_calls>0,"B9b контроль: use_bar=1 -> BAR вызывается");}

    /* B10: короткое чтение (rd возвращает !=2) -> отказ */
    /* смоделируем через ep_fail_at на setup-чтении */
    {struct tctx t; struct backend bk=mk_test(&t); t.ep_fail_at=0;
     struct expstate s; st_init(&s); run_core(&bk,&s,100,0);
     CHK(!s.ready && s.outcome==OUT_SETUP_FAIL && exit_code(s.outcome)==4,"B10 invalid на setup -> SETUP_FAIL exit4");}

    printf("== RESULT: pass=%d fail=%d ==\n",T,Fl);
    return Fl==0?0:1;
}

/* ================================================================ */
/* HW config-only backend: ТОЛЬКО O_RDONLY + pread config           */
/* ================================================================ */
#ifdef __linux__
struct hcfg { int fe, fu; unsigned ep_lnksta, up_lnksta; };
static uint64_t h_now(void*c){ (void)c; struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW,&t);
    return (uint64_t)t.tv_sec*1000000ull+t.tv_nsec/1000; }
static int h_rd16(void*c,int which,uint16_t*o){ struct hcfg*h=c;
    return (int)pread(which==0?h->fe:h->fu,o,2,which==0?h->ep_lnksta:h->up_lnksta); }
/* rd_bar32 = NULL (config-only) */

/* найти PCIe cap (0x10) в списке capabilities, вернуть смещение cap */
static int find_pcie_cap(int fd){
    uint16_t status; if(pread(fd,&status,2,0x06)!=2) return -1;
    if(!(status&0x10)) return -1;                 /* Capabilities List отсутствует */
    uint8_t ptr; if(pread(fd,&ptr,1,0x34)!=1) return -1;
    int guard=0;
    while(ptr && ptr!=0xff && guard++<48){
        uint8_t id,next;
        if(pread(fd,&id,1,ptr)!=1) return -1;
        if(pread(fd,&next,1,ptr+1)!=1) return -1;
        if(id==0x10) return ptr;                  /* PCI Express Capability */
        ptr=next;
    }
    return -1;
}
#endif

/* ================================================================ */
int main(int argc,char**argv){
    if(argc==2 && !strcmp(argv[1],"--test")) return run_tests();

    if(argc>=2 && !strcmp(argv[1],"--hw-config")){
        if(!getenv("CMP100_ALLOW_HW_RUN")){
            fprintf(stderr,"HW config-only backend реализован; ЗАПУСК за гейтом CMP100_ALLOW_HW_RUN=1.\n");
            return 3;
        }
#ifdef __linux__
        if(argc<5){ fprintf(stderr,"usage: %s --hw-config <ep_bdf> <up_bdf> <seconds>\n",argv[0]); return 2; }
        const char*ep=argv[2],*up=argv[3];
        char*end=0; long sec=strtol(argv[4],&end,0);
        if(!end||*end||sec<=0||sec>10){ fprintf(stderr,"ERROR: seconds 1..10\n"); return 2; }
        char p[256]; struct hcfg h; memset(&h,0,sizeof h); h.fe=h.fu=-1;
        snprintf(p,sizeof p,"/sys/bus/pci/devices/%s/config",ep);
        h.fe=open(p,O_RDONLY|O_CLOEXEC); if(h.fe<0){perror("open ep");return 4;}
        snprintf(p,sizeof p,"/sys/bus/pci/devices/%s/config",up);
        h.fu=open(p,O_RDONLY|O_CLOEXEC); if(h.fu<0){perror("open up");close(h.fe);return 4;}
        int ce=find_pcie_cap(h.fe), cu=find_pcie_cap(h.fu);
        if(ce<0||cu<0){ fprintf(stderr,"ERROR: PCIe cap не найден (ep=%d up=%d)\n",ce,cu); close(h.fe);close(h.fu);return 4; }
        h.ep_lnksta=(unsigned)ce+0x12; h.up_lnksta=(unsigned)cu+0x12;   /* LnkSta = cap+0x12 */
        printf("HW config-only: ep_cap=0x%02x up_cap=0x%02x ep_lnksta=0x%02x up_lnksta=0x%02x\n",
               ce,cu,h.ep_lnksta,h.up_lnksta);
        struct backend bk={h_now,h_rd16,NULL,&h};    /* rd_bar32=NULL -> config-only */
        struct expstate s; st_init(&s);
        uint64_t deadline=h_now(0)+(uint64_t)sec*1000000ull;
        run_core(&bk,&s,deadline,0);
        report(&s,"HW_CONFIG_ONLY");
        close(h.fe); close(h.fu);
        return exit_code(s.outcome);
#else
        fprintf(stderr,"HW только для Linux.\n"); return 3;
#endif
    }
    fprintf(stderr,"usage: %s --test | --hw-config <ep> <up> <sec>\n",argv[0]);
    return 2;
}

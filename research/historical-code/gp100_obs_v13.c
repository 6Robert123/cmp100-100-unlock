/* GP100_OBS_V13 — config-only наблюдатель PCIe-линка для раннего RM-окна.
 *
 * V12->V13 (mandatory fixes из обзора V12):
 *  M1: LnkCap читается ПЕРИОДИЧЕСКИ (каждый тик) вместе с LnkSta; изменения
 *      сохраняются с интервалами (cap_events, cap_changed).
 *  M2: ошибка/недостоверность чтения LnkCap (pread!=4 или 0xffffffff) ->
 *      invalid_seen -> OBSERVATION_LOST, ненулевой exit, без дальнейших чтений.
 *  M3: настоящий READY-сигнал наблюдателя ПОСЛЕ успешных исходных чтений:
 *      запись в ready-file + печать OBSERVER_READY (маркер оболочки не заменяет).
 *  M4: offline: временное изменение LnkCap с возвратом, короткое чтение,
 *      0xffffffff, ошибка после ранее зарегистрированного события.
 *
 * Наследие V12 (сохранено):
 *  C1: реальный период опроса 1 мс через sleep_until backend; при опоздании
 *      НЕТ серии догоняющих чтений (tick перевыравнивается по факту, missed++).
 *  C2: пер-устройственные метрики: число выборок, макс. интервал МЕЖДУ выборками
 *      одного и того же конца, before/after каждого чтения, фактическая длительность.
 *  C3: нарушение монотонности времени (ts_order_ok=0) -> ненулевой exit (в classify).
 *  C4: независимые флаги speed/width/training/dll/other_status (не приоритетный один).
 *  C5: тесты pread==0, ==1, ==-1 (разные от значения 0xffff), одновременное speed+width.
 *  C6: валидация capability pointer: диапазон [0x40..0xfc], выравнивание &0xfc,
 *      детекция циклов, границы читаемых полей (unit-тест на synthetic config).
 *  F5(сохр): аппаратный режим — только O_RDONLY+pread; ни BAR, ни write-callback.
 *
 * Сборка: gcc -O2 -Wall -Wextra -o gp100_obs_v13 gp100_obs_v13.c
 * Тесты:  ./gp100_obs_v13 --test
 * HW:     ./gp100_obs_v13 --hw-config <ep_bdf> <up_bdf> <sec> <period_ms> [ready_file]  (гейт CMP100_ALLOW_HW_RUN=1)
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

/* --------- достоверность и декод LnkSta --------- */
static int cfg_invalid(uint16_t v){ return v==0xffffu; }
static int lnk_speed(uint16_t v){ return v & 0x0f; }
static int lnk_width(uint16_t v){ return (v>>4) & 0x3f; }
static int lnk_train(uint16_t v){ return (v>>11) & 1; }
static int lnk_dll  (uint16_t v){ return (v>>13) & 1; }
#define LNK_KNOWN 0x2bffu   /* speed|width|train|dll поля */

struct chg { int speed,width,train,dll,other; };
static struct chg diff_lnk(uint16_t o,uint16_t n){
    struct chg c;
    c.speed = lnk_speed(o)!=lnk_speed(n);
    c.width = lnk_width(o)!=lnk_width(n);
    c.train = lnk_train(o)!=lnk_train(n);
    c.dll   = lnk_dll(o)!=lnk_dll(n);
    c.other = ((uint16_t)(o^n) & (uint16_t)~LNK_KNOWN) != 0;
    return c;
}
static int chg_any(struct chg c){ return c.speed||c.width||c.train||c.dll||c.other; }

/* --------- backend (config-only: НЕТ bar, НЕТ write) --------- */
struct backend {
    uint64_t (*now)(void*);
    void     (*sleep_until)(void*, uint64_t);
    int      (*rd_cfg16)(void*, int which, uint16_t*); /* 0=ep_lnksta 1=up_lnksta */
    int      (*rd_cfg32)(void*, int which, uint32_t*); /* 0=ep_lnkcap 1=up_lnkcap */
    void*ctx;
};

/* --------- состояние/исходы --------- */
enum outcome { OUT_OBSERVED, OUT_NO_CHANGE, OUT_TIMEOUT, OUT_OVERFLOW,
               OUT_OBSERVATION_LOST, OUT_SETUP_FAIL };
static const char*outcome_name(enum outcome o){
    switch(o){case OUT_OBSERVED:return "OBSERVED";case OUT_NO_CHANGE:return "NO_CHANGE";
        case OUT_TIMEOUT:return "TIMEOUT";case OUT_OVERFLOW:return "OVERFLOW";
        case OUT_OBSERVATION_LOST:return "OBSERVATION_LOST";case OUT_SETUP_FAIL:return "SETUP_FAIL";}
    return "?";
}
#define MAXEV 512
struct event { uint64_t before, after; int dev; uint16_t raw; struct chg c; };
struct expstate {
    int ready, timed_out, invalid_seen, overflow, ts_order_ok;
    uint16_t init_ls[2], fin_ls[2];        /* LnkSta ep/up */
    uint32_t init_lc[2], fin_lc[2];        /* LnkCap ep/up */
    int lnkcap_changed;
    unsigned samples[2]; uint64_t last_sample_ts[2], max_interval[2];
    uint32_t last_lc[2]; int cap_changed; unsigned cap_events;   /* M1 */
    unsigned nevents;
    int speed_changed, width_changed, training_changed, dll_changed, other_changed;
    struct event ev[MAXEV];
    uint64_t last_ts, t_begin, t_end;
    unsigned missed_ticks;
    enum outcome outcome;
};
static void st_init(struct expstate*s){ memset(s,0,sizeof*s); s->ts_order_ok=1; }

static void note_ts(struct expstate*s,uint64_t b,uint64_t a){
    if(s->last_ts && (a<b || b<s->last_ts)) s->ts_order_ok=0;
    s->last_ts=a;
}
/* чтение LnkSta одного устройства с пер-устройственной метрикой интервала */
static int sample_dev(struct backend*bk,struct expstate*s,int dev,uint16_t*out){
    uint64_t b=bk->now(bk->ctx);
    int r=bk->rd_cfg16(bk->ctx,dev,out);
    uint64_t a=bk->now(bk->ctx);
    note_ts(s,b,a);
    if(s->last_sample_ts[dev]){ uint64_t g=a-s->last_sample_ts[dev]; if(g>s->max_interval[dev])s->max_interval[dev]=g; }
    s->last_sample_ts[dev]=a; s->samples[dev]++;
    if(r!=2||cfg_invalid(*out)){ s->invalid_seen=1; return -1; }
    /* событие с независимыми флагами */
    uint16_t last=s->fin_ls[dev];
    if(*out!=last){
        struct chg c=diff_lnk(last,*out);
        if(chg_any(c)){
            if(s->nevents>=MAXEV){ s->overflow=1; }
            else s->ev[s->nevents++]=(struct event){b,a,dev,*out,c};
            s->speed_changed|=c.speed; s->width_changed|=c.width; s->training_changed|=c.train;
            s->dll_changed|=c.dll; s->other_changed|=c.other;
        }
        s->fin_ls[dev]=*out;
    }
    return 0;
}
/* M2: недостоверность LnkCap (0xffffffff) считается потерей достоверности */
static int cap_invalid(uint32_t v){ return v==0xffffffffu || (v&0xffff0000u)==0xbadf0000u; }
static int read_lnkcap(struct backend*bk,struct expstate*s,int dev,uint32_t*out){
    uint64_t b=bk->now(bk->ctx); int r=bk->rd_cfg32(bk->ctx,dev,out); uint64_t a=bk->now(bk->ctx);
    note_ts(s,b,a);
    if(r!=4||cap_invalid(*out)){ s->invalid_seen=1; return -1; }   /* M2 (rd_cfg32 -> число байт) */
    return 0;
}
/* M1: периодическая выборка LnkCap с детекцией изменения */
static int sample_cap(struct backend*bk,struct expstate*s,int dev){
    uint32_t v; if(read_lnkcap(bk,s,dev,&v)<0) return -1;
    if(v!=s->last_lc[dev]){ s->cap_events++; s->cap_changed=1; s->last_lc[dev]=v; s->fin_lc[dev]=v; }
    return 0;
}

/* C3: ts_order_ok=false учитывается */
static enum outcome classify(struct expstate*s){
    if(!s->ready)        return s->timed_out?OUT_TIMEOUT:OUT_SETUP_FAIL;
    if(!s->ts_order_ok)  return OUT_OBSERVATION_LOST;
    if(s->invalid_seen)  return OUT_OBSERVATION_LOST;
    if(s->overflow)      return OUT_OVERFLOW;
    if(s->speed_changed) return OUT_OBSERVED;
    return OUT_NO_CHANGE;
}
static int exit_code(enum outcome o){
    switch(o){case OUT_OBSERVED:case OUT_NO_CHANGE:return 0;case OUT_TIMEOUT:return 8;
        case OUT_OVERFLOW:return 9;case OUT_OBSERVATION_LOST:return 5;case OUT_SETUP_FAIL:return 4;}
    return 4;
}

/* --------- ядро: период period_us, окно duration_us, без догоняющих серий --- */
static void run_core(struct backend*bk,struct expstate*s,uint64_t duration_us,uint64_t period_us,
                     const char*ready_file){
    uint16_t v; uint32_t lc;
    s->t_begin=bk->now(bk->ctx); s->last_ts=s->t_begin;
    uint64_t deadline=s->t_begin+duration_us;
    /* setup: LnkCap+LnkSta обоих концов */
    for(int d=0;d<2;d++){
        if(read_lnkcap(bk,s,d,&lc)<0){ s->outcome=OUT_SETUP_FAIL; return; }
        s->init_lc[d]=s->fin_lc[d]=s->last_lc[d]=lc;
    }
    for(int d=0;d<2;d++){
        uint64_t b=bk->now(bk->ctx); int r=bk->rd_cfg16(bk->ctx,d,&v); uint64_t a=bk->now(bk->ctx);
        note_ts(s,b,a); s->last_sample_ts[d]=a; s->samples[d]++;
        if(bk->now(bk->ctx)>=deadline){ s->timed_out=1; s->outcome=OUT_TIMEOUT; return; }
        if(r!=2||cfg_invalid(v)){ s->invalid_seen=1; s->outcome=OUT_SETUP_FAIL; return; }
        s->init_ls[d]=s->fin_ls[d]=v;
    }
    s->ready=1;
    /* M3: READY только ПОСЛЕ успешных исходных чтений */
    if(ready_file){ FILE*rf=fopen(ready_file,"w"); if(rf){ fprintf(rf,"READY %" PRIu64 "\n",bk->now(bk->ctx)); fclose(rf); } }
    printf("OBSERVER_READY %" PRIu64 "\n",bk->now(bk->ctx)); fflush(stdout);

    uint64_t tick=s->t_begin;
    while(tick < deadline){
        tick += period_us;
        bk->sleep_until(bk->ctx,tick);
        uint64_t w=bk->now(bk->ctx);
        if(w > tick + period_us){ s->missed_ticks++; tick=w; }  /* C1: без догоняющих серий */
        if(w>=deadline) break;
        if(sample_dev(bk,s,0,&v)<0) break;   /* стоп на invalid */
        if(s->overflow) break;
        if(sample_dev(bk,s,1,&v)<0) break;
        if(s->overflow) break;
        if(sample_cap(bk,s,0)<0) break;      /* M1/M2: периодический LnkCap + invalid-стоп */
        if(sample_cap(bk,s,1)<0) break;
        if(!s->ts_order_ok) break;           /* C3 */
    }
    if(s->cap_changed) s->lnkcap_changed=1;
    s->t_end=bk->now(bk->ctx);
    s->outcome=classify(s);
}

static void report(struct expstate*s,const char*tag){
    printf("== %s ==\n",tag);
    printf("outcome=%s exit=%d ready=%d timed_out=%d invalid=%d overflow=%d ts_ok=%d\n",
        outcome_name(s->outcome),exit_code(s->outcome),s->ready,s->timed_out,s->invalid_seen,s->overflow,s->ts_order_ok);
    printf("duration_us=%" PRIu64 " missed_ticks=%u\n",(s->t_end>=s->t_begin)?(s->t_end-s->t_begin):0,s->missed_ticks);
    for(int d=0;d<2;d++)
        printf("dev%d(%s): samples=%u max_interval_us=%" PRIu64 " LnkCap %08x->%08x LnkSta %04x->%04x (speed %d->%d width %d->%d)\n",
            d,d?"upstream":"endpoint",s->samples[d],s->max_interval[d],s->init_lc[d],s->fin_lc[d],
            s->init_ls[d],s->fin_ls[d],lnk_speed(s->init_ls[d]),lnk_speed(s->fin_ls[d]),
            lnk_width(s->init_ls[d]),lnk_width(s->fin_ls[d]));
    printf("changed flags: speed=%d width=%d training=%d dll=%d other=%d lnkcap=%d cap_events=%u\n",
        s->speed_changed,s->width_changed,s->training_changed,s->dll_changed,s->other_changed,s->lnkcap_changed,s->cap_events);
    printf("events=%u:\n",s->nevents);
    for(unsigned i=0;i<s->nevents;i++){ struct event*e=&s->ev[i];
        printf("  [%u] dev%d raw=0x%04x [%s%s%s%s%s] before=%" PRIu64 " after=%" PRIu64 "\n",i,e->dev,e->raw,
            e->c.speed?"S":"",e->c.width?"W":"",e->c.train?"T":"",e->c.dll?"D":"",e->c.other?"O":"",e->before,e->after);
    }
    printf("NOTE: NO_CHANGE не доказывает отсутствие кратковременных переходов между выборками (max_interval_us).\n");
}

/* ================================================================ */
/* ТЕСТОВЫЙ backend                                                 */
/* ================================================================ */
struct tctx {
    uint64_t clk;
    const uint16_t*ep_seq; int ep_len, ep_idx;
    int ep_readret, ep_readret_at;     /* C5: подмена кода возврата pread */
    int cfg32_calls;
    const uint32_t*cap_seq; int cap_len, cap_idx;   /* M4: последовательность LnkCap ep(dev0) */
    int cap_readret, cap_readret_at;   /* M4: код возврата/значение pread для cap */
    uint32_t cap_badval;               /* M4: значение при cap_readret_at (напр. 0xffffffff) */
};
static uint64_t t_now(void*c){ struct tctx*t=c; return ++t->clk; }
static void t_sleep_until(void*c,uint64_t tgt){ struct tctx*t=c; if(tgt>t->clk)t->clk=tgt; }
static int t_rd16(void*c,int which,uint16_t*o){ struct tctx*t=c;
    if(which==0){ int i=t->ep_idx++;
        if(t->ep_readret_at>=0 && i==t->ep_readret_at){ *o=0x1011; return t->ep_readret; } /* 0/1/-1 */
        *o = t->ep_seq ? t->ep_seq[i<t->ep_len?i:t->ep_len-1] : 0x1011; return 2; }
    *o=0x1011; return 2; }
static int t_rd32(void*c,int which,uint32_t*o){ struct tctx*t=c; t->cfg32_calls++;
    if(which!=0){ *o=0x0261ad03; return 4; }              /* upstream cap постоянный */
    int i=t->cap_idx++;
    if(t->cap_readret_at>=0 && i==t->cap_readret_at){ *o=t->cap_badval; return t->cap_readret; } /* M4 short/err/badval */
    *o = t->cap_seq ? t->cap_seq[i<t->cap_len?i:t->cap_len-1] : 0x00453c11; return 4; }
static struct backend mk_test(struct tctx*t){
    memset(t,0,sizeof*t); t->ep_readret=2; t->ep_readret_at=-1; t->cap_readret=4; t->cap_readret_at=-1;
    struct backend bk={t_now,t_sleep_until,t_rd16,t_rd32,t}; return bk; }

/* ---- synthetic-config для теста find_pcie_cap (C6) ---- */
#ifdef __linux__
static int find_pcie_cap(int fd);   /* fwd */
static int make_cfg_file(const uint8_t*buf,size_t n){
    char tmpl[]="/tmp/gp100cfgXXXXXX"; int fd=mkstemp(tmpl); if(fd<0)return -1;
    if(write(fd,buf,n)!=(ssize_t)n){close(fd);unlink(tmpl);return -1;}
    unlink(tmpl); lseek(fd,0,SEEK_SET); return fd;
}
#endif

static int T=0,Fl=0;
#define CHK(c,n) do{ if(c){T++;} else {Fl++;printf("  FAIL: %s\n",n);} }while(0)

static int run_tests(void){
    printf("== V12 OFFLINE TESTS ==\n");
    /* C4 независимые флаги */
    {struct chg c=diff_lnk(0x1011,0x1022); CHK(c.speed&&c.width&&!c.train&&!c.dll,"C4 0x1011->0x1022 speed&width одновременно");}
    {struct chg c=diff_lnk(0x1011,0x2011); CHK(c.dll&&c.other&&!c.speed&&!c.width,"C4 0x1011->0x2011 dll&other");}
    {struct chg c=diff_lnk(0x1011,0x5011); CHK(c.other&&!c.speed&&!c.width&&!c.dll,"C4 0x1011->0x5011 только other");}

    /* B: speed event -> OBSERVED */
    {static const uint16_t seq[]={0x1011,0x1012}; struct tctx t; struct backend bk=mk_test(&t); t.ep_seq=seq;t.ep_len=2;
     struct expstate s; st_init(&s); run_core(&bk,&s,50,10,NULL);
     CHK(s.speed_changed && s.outcome==OUT_OBSERVED,"B speed_change -> OBSERVED");}
    /* одновременное speed+width */
    {static const uint16_t seq[]={0x1011,0x1022}; struct tctx t; struct backend bk=mk_test(&t); t.ep_seq=seq;t.ep_len=2;
     struct expstate s; st_init(&s); run_core(&bk,&s,50,10,NULL);
     CHK(s.speed_changed&&s.width_changed,"B одновременное speed+width зафиксировано");}
    /* стабильный Gen2 -> NO_CHANGE speed2 */
    {static const uint16_t seq[]={0x1012}; struct tctx t; struct backend bk=mk_test(&t); t.ep_seq=seq;t.ep_len=1;
     struct expstate s; st_init(&s); run_core(&bk,&s,50,10,NULL);
     CHK(s.outcome==OUT_NO_CHANGE && lnk_speed(s.fin_ls[0])==2,"B стабильный Gen2 -> NO_CHANGE speed2");}

    /* C5: pread==0 (short) на setup -> SETUP_FAIL */
    {struct tctx t; struct backend bk=mk_test(&t); t.ep_readret=0; t.ep_readret_at=0;
     struct expstate s; st_init(&s); run_core(&bk,&s,50,10,NULL);
     CHK(!s.ready && s.outcome==OUT_SETUP_FAIL,"C5 pread==0 -> SETUP_FAIL");}
    /* C5: pread==1 (short) в цикле -> OBSERVATION_LOST */
    {struct tctx t; struct backend bk=mk_test(&t); t.ep_readret=1; t.ep_readret_at=2;
     struct expstate s; st_init(&s); run_core(&bk,&s,200,10,NULL);
     CHK(s.invalid_seen && s.outcome==OUT_OBSERVATION_LOST && exit_code(s.outcome)==5,"C5 pread==1 в цикле -> OBSERVATION_LOST exit5");}
    /* C5: pread==-1 (error) в цикле -> OBSERVATION_LOST */
    {struct tctx t; struct backend bk=mk_test(&t); t.ep_readret=-1; t.ep_readret_at=2;
     struct expstate s; st_init(&s); run_core(&bk,&s,200,10,NULL);
     CHK(s.invalid_seen && s.outcome==OUT_OBSERVATION_LOST,"C5 pread==-1 в цикле -> OBSERVATION_LOST");}
    /* значение 0xffff (не короткое чтение, а недостоверное значение) -> отдельный путь */
    {static const uint16_t seq[]={0x1011,0xffff}; struct tctx t; struct backend bk=mk_test(&t); t.ep_seq=seq;t.ep_len=2;
     struct expstate s; st_init(&s); run_core(&bk,&s,200,10,NULL);
     CHK(s.invalid_seen && s.outcome==OUT_OBSERVATION_LOST,"0xffff значение != короткое чтение, но тоже invalid");}

    /* overflow реальный */
    {static uint16_t seq[MAXEV+80]; for(int i=0;i<MAXEV+80;i++) seq[i]=(i&1)?0x1012:0x1011;
     struct tctx t; struct backend bk=mk_test(&t); t.ep_seq=seq;t.ep_len=MAXEV+80;
     struct expstate s; st_init(&s); run_core(&bk,&s,1000000,1,NULL);
     CHK(s.overflow && s.outcome==OUT_OVERFLOW,"overflow -> OVERFLOW");}

    /* per-device метрика: обе стороны имеют samples>0 */
    {struct tctx t; struct backend bk=mk_test(&t);
     struct expstate s; st_init(&s); run_core(&bk,&s,50,10,NULL);
     CHK(s.samples[0]>0 && s.samples[1]>0,"per-device samples обе стороны");}

    /* C1: период соблюдается, нет догоняющих серий: samples ~ duration/period, не тысячи */
    {struct tctx t; struct backend bk=mk_test(&t);
     struct expstate s; st_init(&s); run_core(&bk,&s,100,10,NULL); /* 10 тиков */
     CHK(s.samples[0]<=12,"C1 без догоняющих серий (samples<=~duration/period)");}

    /* C6: find_pcie_cap на synthetic config */
#ifdef __linux__
    { uint8_t cfg[256]; memset(cfg,0,sizeof cfg);
      cfg[0x06]=0x10;              /* status: Capabilities List bit4 */
      cfg[0x34]=0x40;              /* cap ptr */
      cfg[0x40]=0x01; cfg[0x41]=0x50;  /* cap id 0x01 (PM), next 0x50 */
      cfg[0x50]=0x10; cfg[0x51]=0x00;  /* cap id 0x10 (PCIe), next 0 */
      int fd=make_cfg_file(cfg,sizeof cfg);
      CHK(fd>=0 && find_pcie_cap(fd)==0x50,"C6 находит PCIe cap 0x10"); if(fd>=0)close(fd);
    }
    { uint8_t cfg[256]; memset(cfg,0,sizeof cfg);   /* цикл */
      cfg[0x06]=0x10; cfg[0x34]=0x40;
      cfg[0x40]=0x01; cfg[0x41]=0x50; cfg[0x50]=0x05; cfg[0x51]=0x40; /* 0x50 -> 0x40 цикл, нет 0x10 */
      int fd=make_cfg_file(cfg,sizeof cfg);
      CHK(fd>=0 && find_pcie_cap(fd)<0,"C6 цикл -> не зависает, возврат <0"); if(fd>=0)close(fd);
    }
    { uint8_t cfg[256]; memset(cfg,0,sizeof cfg);   /* ptr вне диапазона / невыровнен */
      cfg[0x06]=0x10; cfg[0x34]=0x3f;               /* <0x40 недопустим */
      int fd=make_cfg_file(cfg,sizeof cfg);
      CHK(fd>=0 && find_pcie_cap(fd)<0,"C6 ptr<0x40 -> <0"); if(fd>=0)close(fd);
    }
#endif

    /* M4: временное изменение LnkCap с возвратом A->B->A -> cap_changed, cap_events>=2 */
    {static const uint32_t cs[]={0x00453c11,0x00453c12,0x00453c11}; struct tctx t; struct backend bk=mk_test(&t);
     t.cap_seq=cs; t.cap_len=3;
     struct expstate s; st_init(&s); run_core(&bk,&s,80,10,NULL);
     CHK(s.cap_changed && s.cap_events>=2,"M4 LnkCap A->B->A: cap_changed, cap_events>=2");}
    /* M4: короткое чтение LnkCap (pread!=4) в цикле -> OBSERVATION_LOST */
    {struct tctx t; struct backend bk=mk_test(&t); t.cap_readret=2; t.cap_readret_at=1; t.cap_badval=0x00453c11;
     struct expstate s; st_init(&s); run_core(&bk,&s,200,10,NULL);
     CHK(s.invalid_seen && s.outcome==OUT_OBSERVATION_LOST && exit_code(s.outcome)==5,"M4 короткое чтение LnkCap -> OBSERVATION_LOST");}
    /* M4: 0xffffffff в LnkCap -> OBSERVATION_LOST, без дальнейших чтений */
    {struct tctx t; struct backend bk=mk_test(&t); t.cap_readret=4; t.cap_readret_at=1; t.cap_badval=0xffffffff;
     struct expstate s; st_init(&s); run_core(&bk,&s,200,10,NULL);
     CHK(s.invalid_seen && s.outcome==OUT_OBSERVATION_LOST,"M4 0xffffffff LnkCap -> OBSERVATION_LOST");}
    /* M4: ошибка LnkCap ПОСЛЕ ранее зарегистрированного speed-события -> всё равно OBSERVATION_LOST (F1) */
    {static const uint16_t es[]={0x1011,0x1012,0x1012,0x1012}; static const uint32_t cs[]={0x00453c11,0x00453c11,0xffffffff};
     struct tctx t; struct backend bk=mk_test(&t); t.ep_seq=es;t.ep_len=4; t.cap_seq=cs;t.cap_len=3; t.cap_readret=4;t.cap_readret_at=2;t.cap_badval=0xffffffff;
     struct expstate s; st_init(&s); run_core(&bk,&s,200,10,NULL);
     CHK(s.speed_changed && s.invalid_seen && s.outcome==OUT_OBSERVATION_LOST,"M4 событие затем ошибка LnkCap -> OBSERVATION_LOST (F1)");}

    printf("== RESULT: pass=%d fail=%d ==\n",T,Fl);
    return Fl==0?0:1;
}

/* ================================================================ */
/* HW config-only backend                                           */
/* ================================================================ */
#ifdef __linux__
struct hcfg { int fe, fu; unsigned ep_lnksta, up_lnksta, ep_lnkcap, up_lnkcap; };
static uint64_t h_now(void*c){ (void)c; struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW,&t);
    return (uint64_t)t.tv_sec*1000000ull+t.tv_nsec/1000; }
static void h_sleep_until(void*c,uint64_t tgt){ (void)c; uint64_t n=h_now(0);
    if(tgt>n){ uint64_t d=tgt-n; struct timespec ts={ (time_t)(d/1000000ull),(long)((d%1000000ull)*1000ull) }; nanosleep(&ts,0);} }
static int h_rd16(void*c,int which,uint16_t*o){ struct hcfg*h=c;
    return (int)pread(which==0?h->fe:h->fu,o,2,which==0?h->ep_lnksta:h->up_lnksta); }
static int h_rd32(void*c,int which,uint32_t*o){ struct hcfg*h=c;
    return (int)pread(which==0?h->fe:h->fu,o,4,which==0?h->ep_lnkcap:h->up_lnkcap); }  /* -> число байт */

/* C6: валидация capability pointer */
static int find_pcie_cap(int fd){
    uint16_t status; if(pread(fd,&status,2,0x06)!=2) return -1;
    if(!(status&0x10)) return -1;
    uint8_t ptr; if(pread(fd,&ptr,1,0x34)!=1) return -1;
    ptr &= 0xfc;                                  /* выравнивание, младшие 2 бита reserved */
    uint8_t seen[256]; memset(seen,0,sizeof seen);
    int guard=0;
    while(ptr){
        if(ptr<0x40 || ptr>0xfc) return -1;       /* диапазон */
        if(seen[ptr]) return -1;                  /* цикл */
        seen[ptr]=1;
        uint8_t id,next;
        if(pread(fd,&id,1,ptr)!=1) return -1;
        if(pread(fd,&next,1,ptr+1)!=1) return -1;
        if(id==0x10) return ptr;
        ptr=next & 0xfc;
        if(guard++>48) return -1;
    }
    return -1;
}
#endif

/* ================================================================ */
int main(int argc,char**argv){
    if(argc==2 && !strcmp(argv[1],"--test")) return run_tests();

    if(argc>=2 && !strcmp(argv[1],"--hw-config")){
        if(!getenv("CMP100_ALLOW_HW_RUN")){
            fprintf(stderr,"HW config-only реализован; ЗАПУСК за гейтом CMP100_ALLOW_HW_RUN=1.\n"); return 3; }
#ifdef __linux__
        if(argc<6){ fprintf(stderr,"usage: %s --hw-config <ep> <up> <sec> <period_ms> [ready_file]\n",argv[0]); return 2; }
        const char*ep=argv[2],*up=argv[3]; char*e1=0,*e2=0;
        long sec=strtol(argv[4],&e1,0), pms=strtol(argv[5],&e2,0);
        const char*ready_file = (argc>=7)? argv[6] : NULL;
        if(!e1||*e1||sec<=0||sec>10||!e2||*e2||pms<1||pms>1000){ fprintf(stderr,"ERROR: sec 1..10, period_ms 1..1000\n"); return 2; }
        char p[256]; struct hcfg h; memset(&h,0,sizeof h); h.fe=h.fu=-1;
        snprintf(p,sizeof p,"/sys/bus/pci/devices/%s/config",ep); h.fe=open(p,O_RDONLY|O_CLOEXEC); if(h.fe<0){perror("open ep");return 4;}
        snprintf(p,sizeof p,"/sys/bus/pci/devices/%s/config",up); h.fu=open(p,O_RDONLY|O_CLOEXEC); if(h.fu<0){perror("open up");close(h.fe);return 4;}
        int ce=find_pcie_cap(h.fe), cu=find_pcie_cap(h.fu);
        if(ce<0||cu<0){ fprintf(stderr,"ERROR: PCIe cap не найден ep=%d up=%d\n",ce,cu); close(h.fe);close(h.fu);return 4; }
        h.ep_lnksta=(unsigned)ce+0x12; h.up_lnksta=(unsigned)cu+0x12;
        h.ep_lnkcap=(unsigned)ce+0x0c; h.up_lnkcap=(unsigned)cu+0x0c;
        printf("HW config-only: ep_cap=0x%02x up_cap=0x%02x period_ms=%ld sec=%ld\n",ce,cu,pms,sec);
        struct backend bk={h_now,h_sleep_until,h_rd16,h_rd32,&h};
        struct expstate s; st_init(&s);
        run_core(&bk,&s,(uint64_t)sec*1000000ull,(uint64_t)pms*1000ull,ready_file);
        report(&s,"HW_CONFIG_ONLY");
        close(h.fe); close(h.fu);
        return exit_code(s.outcome);
#else
        fprintf(stderr,"HW только для Linux.\n"); return 3;
#endif
    }
    fprintf(stderr,"usage: %s --test | --hw-config <ep> <up> <sec> <period_ms>\n",argv[0]);
    return 2;
}

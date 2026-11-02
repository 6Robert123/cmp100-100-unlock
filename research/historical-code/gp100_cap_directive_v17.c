/* GP100_CAP_DIRECTIVE_V17 — транзакционное ядро с исправлениями F1..F7 (обзор V16).
 * V16 СОХРАНЁН отдельно. Аппаратный backend НЕ реализован; запуск запрещён.
 *
 * F1: масочный RMW пишет ПОЛНОЕ слово -> сохранённый bit0 мог бы повторно подать
 *     directive. Введена классификация битов на регистр: cmd_field (директива),
 *     w1c_bits, ro_bits, known_bits. Любая НЕ-командная запись принудительно
 *     обнуляет cmd_field. Перед записью требуется cmd_field==0 (иначе STATE_UNSAFE).
 *     Утверждение "маска сама обеспечивает безопасный откат" ОТОЗВАНО.
 * F2: ожидания принимают БЮДЖЕТ, deadline=now+budget считается в начале ожидания,
 *     с проверкой переполнения. Часы с большим uptime тестируются.
 * F3: ВСЕ чтения (MMIO и config) идут через единого владельца, проверяются на
 *     ошибку и invalid. После access_lost backend не вызывается ни разу.
 *     Результат при access_lost не может быть SUCCESS.
 * F4: cap после ожидания СРАВНИВАЕТСЯ с ожидаемым; расхождение -> STATE_CHANGED,
 *     cap НЕ переписывается.
 * F5: статусы записи разделены: SUBMITTED / BUDGET_EXCEEDED / WRITE_FAILED /
 *     READ_FAILED / READBACK_MISMATCH. Самоочистка trigger допустима ТОЛЬКО после
 *     подтверждённой отправки. Раздельные бюджеты: поля / команды / откат.
 * F6: журнал владения по полям: откат только затронутых полей и только если
 *     текущее состояние совпадает с тем, что мы записали; конфликт -> RESTORE_BLOCKED.
 * F7: разделены результат ДО отката, восстановление полей и фактическое состояние
 *     ПОСЛЕ. Успех требует согласованных скорости И ширины на обоих концах.
 *
 * Сборка: gcc -O2 -Wall -Wextra -o gp100_cap_directive_v17 gp100_cap_directive_v17.c
 * Тесты:  ./gp100_cap_directive_v17 --test
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define R_DL   0x8b8c0u
#define R_LC   0x8c040u
#define R_CAP  0x8c1c0u
#define M_SAFE     0x00000004u
#define M_RATE     0x000c0000u
#define V_RATE_G2  0x00040000u
#define M_WIDTH    0x00700000u
#define V_WIDTH_X1 0x00700000u
#define M_CAP      0x00030000u
#define V_CAP_G2   0x00020000u      /* [17:16]=10b=2 */
#define F_LC_CMD   0x0000001fu      /* [4:0] ltssm directive/status — КОМАНДНОЕ поле */
#define B_TRIG     0x00000001u      /* change_speed */

/* --- классификация битов на регистр (F1) --- */
struct regclass { uint32_t addr, cmd_field, w1c_bits, ro_bits, known_bits; };
static const struct regclass RC[] = {
    { R_LC,  F_LC_CMD, 0u, F_LC_CMD, F_LC_CMD|M_RATE|M_WIDTH },
    { R_DL,  0u,       0u, 0u,       M_SAFE },
    { R_CAP, 0u,       0u, 0u,       M_CAP  },
};
static const struct regclass* rc_of(uint32_t a){
    for(unsigned i=0;i<sizeof RC/sizeof RC[0];i++) if(RC[i].addr==a) return &RC[i];
    return 0; }

enum wr_status { WR_SUBMITTED, WR_BUDGET_EXCEEDED, WR_WRITE_FAILED, WR_READ_FAILED,
                 WR_READBACK_MISMATCH, WR_STATE_UNSAFE, WR_NOT_ATTEMPTED };
static const char*wr_name(enum wr_status s){
    switch(s){case WR_SUBMITTED:return "SUBMITTED";case WR_BUDGET_EXCEEDED:return "BUDGET_EXCEEDED";
        case WR_WRITE_FAILED:return "WRITE_FAILED";case WR_READ_FAILED:return "READ_FAILED";
        case WR_READBACK_MISMATCH:return "READBACK_MISMATCH";case WR_STATE_UNSAFE:return "STATE_UNSAFE";
        case WR_NOT_ATTEMPTED:return "NOT_ATTEMPTED";} return "?"; }

enum result { R_SUCCESS_CANDIDATE, R_INCONCLUSIVE, R_NO_GEN2, R_TIMEOUT, R_STATE_CHANGED,
              R_INIT_INVALID, R_ACCESS_LOST, R_WRITE_PROBLEM, R_NOT_RUN };
static const char*res_name(enum result r){
    switch(r){case R_SUCCESS_CANDIDATE:return "SUCCESS_CANDIDATE";case R_INCONCLUSIVE:return "INCONCLUSIVE";
        case R_NO_GEN2:return "NO_GEN2";case R_TIMEOUT:return "TIMEOUT";case R_STATE_CHANGED:return "STATE_CHANGED";
        case R_INIT_INVALID:return "INIT_INVALID";case R_ACCESS_LOST:return "ACCESS_LOST";
        case R_WRITE_PROBLEM:return "WRITE_PROBLEM";case R_NOT_RUN:return "NOT_RUN";} return "?"; }
enum restore { RST_NOT_NEEDED, RST_RESTORED, RST_FAILED, RST_BLOCKED };
static const char*rst_name(enum restore r){
    switch(r){case RST_NOT_NEEDED:return "NOT_NEEDED";case RST_RESTORED:return "RESTORED";
        case RST_FAILED:return "RESTORE_FAILED";case RST_BLOCKED:return "RESTORE_BLOCKED";} return "?"; }

static int mmio_invalid(uint32_t v){ return v==0xffffffffu || (v&0xffff0000u)==0xbadf0000u; }
static int cfg_invalid16(uint16_t v){ return v==0xffffu; }

struct ops {
    int (*rd32)(void*,uint32_t,uint32_t*);
    int (*wr32)(void*,uint32_t,uint32_t);
    int (*rd_lnksta)(void*,int,uint16_t*);
    int (*rd_lnkcap)(void*,int,uint32_t*);
    uint64_t (*now)(void*);
    void*ctx;
};

/* --- владение полями (F6) --- */
struct owned { uint32_t addr, mask, written, baseline; int touched; };
#define MAXOWN 4
struct owner {
    struct ops*o; int access_lost; unsigned backend_calls_after_loss;
    unsigned field_writes, field_budget;
    unsigned cmd_writes,   cmd_budget;
    unsigned restore_writes, restore_budget;
    struct owned own[MAXOWN]; unsigned nown;
};
static void own_init(struct owner*w,struct ops*o,unsigned fb,unsigned cb,unsigned rb){
    memset(w,0,sizeof*w); w->o=o; w->field_budget=fb; w->cmd_budget=cb; w->restore_budget=rb; }

/* F3: единственная точка доступа. После потери — НИ ОДНОГО вызова backend. */
static int W_rd32(struct owner*w,uint32_t a,uint32_t*v){
    if(w->access_lost){ w->backend_calls_after_loss++; return -1; }
    if(w->o->rd32(w->o->ctx,a,v)!=0){ w->access_lost=1; return -1; }
    if(mmio_invalid(*v)){ w->access_lost=1; return -1; }
    return 0; }
static int W_lnksta(struct owner*w,int d,uint16_t*v){
    if(w->access_lost){ w->backend_calls_after_loss++; return -1; }
    if(w->o->rd_lnksta(w->o->ctx,d,v)!=0){ w->access_lost=1; return -1; }
    if(cfg_invalid16(*v)){ w->access_lost=1; return -1; }
    return 0; }
static int W_lnkcap(struct owner*w,int d,uint32_t*v){
    if(w->access_lost){ w->backend_calls_after_loss++; return -1; }
    if(w->o->rd_lnkcap(w->o->ctx,d,v)!=0){ w->access_lost=1; return -1; }
    if(mmio_invalid(*v)){ w->access_lost=1; return -1; }
    return 0; }

static struct owned* own_find(struct owner*w,uint32_t a){
    for(unsigned i=0;i<w->nown;i++) if(w->own[i].addr==a) return &w->own[i];
    if(w->nown>=MAXOWN) return 0;
    w->own[w->nown]=(struct owned){a,0,0,0,0}; return &w->own[w->nown++]; }

/* Единая запись. kind: 0=поле, 1=команда, 2=откат.
 * F1: не-командная запись ПРИНУДИТЕЛЬНО обнуляет cmd_field и не пишет 1 в w1c. */
static enum wr_status W_write(struct owner*w,uint32_t a,uint32_t mask,uint32_t val,int kind){
    if(w->access_lost){ w->backend_calls_after_loss++; return WR_WRITE_FAILED; }
    unsigned*cnt; unsigned budget;
    if(kind==1){ cnt=&w->cmd_writes; budget=w->cmd_budget; }
    else if(kind==2){ cnt=&w->restore_writes; budget=w->restore_budget; }
    else { cnt=&w->field_writes; budget=w->field_budget; }
    if(*cnt>=budget) return WR_BUDGET_EXCEEDED;                 /* F5: НЕ запись */
    const struct regclass*rc=rc_of(a); if(!rc) return WR_STATE_UNSAFE;
    uint32_t old; if(W_rd32(w,a,&old)!=0) return WR_READ_FAILED;
    /* F1: командное поле не должно быть взведено, если мы не подаём команду */
    if(kind!=1 && (old & rc->cmd_field)) return WR_STATE_UNSAFE;
    uint32_t want = (old & ~mask & ~rc->cmd_field & ~rc->w1c_bits) | (val & mask);
    if(kind==1) want |= (val & rc->cmd_field & mask);           /* команда — только явно */
    if(w->o->wr32(w->o->ctx,a,want)!=0){ w->access_lost=1; return WR_WRITE_FAILED; }
    (*cnt)++;
    uint32_t rb; if(W_rd32(w,a,&rb)!=0) return WR_READ_FAILED;
    /* команда может самоочищаться -> readback команды не проверяем */
    if(kind!=1 && (rb&mask)!=(want&mask)) return WR_READBACK_MISMATCH;
    /* владение фиксируют ТОЛЬКО полевые записи: команда транзиентна (самоочищается) */
    if(kind==0){ struct owned*ow=own_find(w,a);
        if(ow){ ow->mask|=mask; if(!ow->touched){ ow->baseline=old; ow->touched=1; } ow->written=want; } }
    return WR_SUBMITTED; }

/* F2: бюджет, не абсолютный deadline; overflow-safe */
static int W_wait_idle(struct owner*w,uint64_t budget_us){
    uint64_t t0=w->o->now(w->o->ctx);
    uint64_t dl = (t0 > UINT64_MAX-budget_us)? UINT64_MAX : t0+budget_us;
    for(;;){
        uint32_t v; if(W_rd32(w,R_LC,&v)!=0) return -1;
        if((v & F_LC_CMD)==0) return 0;
        if(w->o->now(w->o->ctx) >= dl) return 1;
    } }

enum mode { MODE_CONTROL, MODE_TREATMENT };
struct exp {
    enum mode mode;
    uint32_t init_dl, init_lc, init_cap;
    uint16_t init_ls[2], fin_ls[2], post_restore_ls[2];
    uint32_t init_lnkcap[2];
    uint32_t cap_before_dir, cap_after_wait;
    enum wr_status st_cap, st_safe, st_ratewidth, st_dir;
    int directive_submitted;
    enum result result_before_restore;
    enum restore restore;
    int link_restored;            /* F7: фактическое состояние линка после отката */
    int post_read_ok;
};

static void run_experiment(struct owner*w,struct exp*e,uint64_t idle_budget,uint64_t done_budget){
    e->st_cap=e->st_safe=e->st_ratewidth=e->st_dir=WR_NOT_ATTEMPTED;
    e->result_before_restore=R_NOT_RUN; e->restore=RST_NOT_NEEDED;
    /* 1. исходные чтения через владельца; любая недостоверность -> ноль записей */
    if(W_rd32(w,R_DL,&e->init_dl)!=0||W_rd32(w,R_LC,&e->init_lc)!=0||W_rd32(w,R_CAP,&e->init_cap)!=0){
        e->result_before_restore=R_INIT_INVALID; goto fin; }
    for(int d=0;d<2;d++){
        if(W_lnksta(w,d,&e->init_ls[d])!=0){ e->result_before_restore=R_INIT_INVALID; goto fin; }
        if(W_lnkcap(w,d,&e->init_lnkcap[d])!=0){ e->result_before_restore=R_INIT_INVALID; goto fin; } }
    e->fin_ls[0]=e->init_ls[0]; e->fin_ls[1]=e->init_ls[1];

    if(e->mode==MODE_TREATMENT){
        e->st_cap=W_write(w,R_CAP,M_CAP,V_CAP_G2,0);
        if(e->st_cap!=WR_SUBMITTED){ e->result_before_restore = w->access_lost?R_ACCESS_LOST:R_WRITE_PROBLEM; goto restore; } }
    e->st_safe=W_write(w,R_DL,M_SAFE,M_SAFE,0);
    if(e->st_safe!=WR_SUBMITTED){ e->result_before_restore = w->access_lost?R_ACCESS_LOST:R_WRITE_PROBLEM; goto restore; }
    { int r=W_wait_idle(w,idle_budget);
      if(r<0){ e->result_before_restore=R_ACCESS_LOST; goto restore; }
      if(r==1){ e->result_before_restore=R_TIMEOUT; goto restore; } }
    e->st_ratewidth=W_write(w,R_LC,M_RATE|M_WIDTH,V_RATE_G2|V_WIDTH_X1,0);
    if(e->st_ratewidth!=WR_SUBMITTED){ e->result_before_restore = w->access_lost?R_ACCESS_LOST:R_WRITE_PROBLEM; goto restore; }
    /* cap непосредственно перед командой */
    if(W_rd32(w,R_CAP,&e->cap_before_dir)!=0){ e->result_before_restore=R_ACCESS_LOST; goto restore; }
    { uint32_t exp_cap = (e->mode==MODE_TREATMENT)? V_CAP_G2 : (e->init_cap & M_CAP);
      if((e->cap_before_dir & M_CAP)!=(exp_cap & M_CAP)){ e->result_before_restore=R_STATE_CHANGED; goto restore; } }
    /* ОДНА команда */
    e->st_dir=W_write(w,R_LC,B_TRIG,B_TRIG,1);
    if(e->st_dir!=WR_SUBMITTED){ e->result_before_restore = w->access_lost?R_ACCESS_LOST:R_WRITE_PROBLEM; goto restore; }
    e->directive_submitted=1;                                   /* F5: только после подтверждённой отправки */
    { int r=W_wait_idle(w,done_budget);
      if(r<0){ e->result_before_restore=R_ACCESS_LOST; goto restore; }
      if(r==1){ e->result_before_restore=R_TIMEOUT; goto restore; } }
    /* F4: cap после ожидания СРАВНИВАЕТСЯ */
    if(W_rd32(w,R_CAP,&e->cap_after_wait)!=0){ e->result_before_restore=R_ACCESS_LOST; goto restore; }
    { uint32_t exp_cap = (e->mode==MODE_TREATMENT)? V_CAP_G2 : (e->init_cap & M_CAP);
      if((e->cap_after_wait & M_CAP)!=(exp_cap & M_CAP)){ e->result_before_restore=R_STATE_CHANGED; goto restore; } }
    for(int d=0;d<2;d++) if(W_lnksta(w,d,&e->fin_ls[d])!=0){ e->result_before_restore=R_ACCESS_LOST; goto restore; }
    /* F7: успех требует скорость И ширину на ОБОИХ концах */
    { int s0=e->fin_ls[0]&0xf, s1=e->fin_ls[1]&0xf;
      int w0=(e->fin_ls[0]>>4)&0x3f, w1=(e->fin_ls[1]>>4)&0x3f;
      if(s0==2&&s1==2&&w0==1&&w1==1) e->result_before_restore=R_SUCCESS_CANDIDATE;
      else if(s0!=s1||w0!=w1)        e->result_before_restore=R_INCONCLUSIVE;
      else                           e->result_before_restore=R_NO_GEN2; }

restore:
    /* F6: только затронутые поля; только при подтверждённом владении */
    if(w->access_lost){ e->restore=RST_BLOCKED; goto fin; }
    { int any=0, ok=1, conflict=0;
      for(unsigned i=0;i<w->nown;i++){ struct owned*ow=&w->own[i]; if(!ow->touched) continue; any=1;
        uint32_t cur; if(W_rd32(w,ow->addr,&cur)!=0){ conflict=1; break; }
        if((cur & ow->mask)!=(ow->written & ow->mask)){ conflict=1; break; }   /* чужое изменение */
        if(W_write(w,ow->addr,ow->mask,ow->baseline,2)!=WR_SUBMITTED){ ok=0; } }
      if(w->access_lost||conflict) e->restore=RST_BLOCKED;
      else if(!any)                e->restore=RST_NOT_NEEDED;
      else                         e->restore= ok?RST_RESTORED:RST_FAILED; }
    /* F7: фактическое состояние ПОСЛЕ отката — отдельно от восстановления полей */
    if(!w->access_lost){
        e->post_read_ok=1;
        for(int d=0;d<2;d++) if(W_lnksta(w,d,&e->post_restore_ls[d])!=0){ e->post_read_ok=0; break; }
        if(e->post_read_ok)
            e->link_restored = (e->post_restore_ls[0]==e->init_ls[0] && e->post_restore_ls[1]==e->init_ls[1]); }
fin:
    if(w->access_lost && e->result_before_restore==R_SUCCESS_CANDIDATE) e->result_before_restore=R_ACCESS_LOST;
}
static int exit_code(struct exp*e){
    if(e->restore==RST_BLOCKED||e->restore==RST_FAILED) return 6;
    switch(e->result_before_restore){
        case R_SUCCESS_CANDIDATE: case R_NO_GEN2: return (e->post_read_ok&&!e->link_restored)?10:0;
        case R_INCONCLUSIVE: return 3; case R_TIMEOUT: return 8; case R_STATE_CHANGED: return 7;
        case R_INIT_INVALID: return 4; case R_ACCESS_LOST: return 5;
        case R_WRITE_PROBLEM: return 9; case R_NOT_RUN: return 4; }
    return 4; }

/* ===================== MOCK ===================== */
struct mock {
    uint32_t dl,lc,cap; uint16_t ls[2]; uint32_t lnkcap[2];
    uint64_t clk, clk_start;
    int trig_self_clear;          /* 0 -> trigger НЕ самоочищается */
    int phys_directives;          /* сколько раз физически записан bit0=1 */
    int fail_rd_at, rd_calls; int fail_wr_at, wr_calls;
    int fail_lnksta_at, lnksta_calls;
    uint32_t mismatch_xor; int mismatch_at;
    int cap_change_after_first_rb;    /* cap уезжает после успешного первого readback */
    int cap_change_after_wait;        /* cap уезжает после ожидания */
    int conflict_in_mask;             /* чужое изменение внутри нашей маски перед откатом */
    int idle_never;
    uint16_t ls_after[2]; int use_ls_after;
    int did_dir;
};
static uint64_t m_now(void*c){ struct mock*m=c; return m->clk_start + (++m->clk); }
static uint32_t* m_reg(struct mock*m,uint32_t a){
    if(a==R_DL) return &m->dl;
    if(a==R_LC) return &m->lc;
    if(a==R_CAP) return &m->cap;
    return 0; }
static int m_rd(void*c,uint32_t a,uint32_t*v){ struct mock*m=c; int i=m->rd_calls++;
    if(m->fail_rd_at>=0 && i==m->fail_rd_at){ *v=0xbadf0000; return 0; }
    uint32_t*p=m_reg(m,a); if(!p){ *v=0; return 0; }
    if(a==R_LC && m->idle_never){ *v=(*p)|0x10; return 0; }
    *v=*p; return 0; }
static int m_wr(void*c,uint32_t a,uint32_t val){ struct mock*m=c; int i=m->wr_calls++;
    if(m->fail_wr_at>=0 && i==m->fail_wr_at) return -1;
    uint32_t*p=m_reg(m,a); if(!p) return 0;
    if(a==R_LC && (val&B_TRIG)){ m->phys_directives++; m->did_dir=1; }
    *p=val;
    if(m->mismatch_at>=0 && i==m->mismatch_at) *p = val ^ m->mismatch_xor;
    if(a==R_LC && (val&B_TRIG)){
        if(m->trig_self_clear) *p &= ~B_TRIG;
        if(m->use_ls_after){ m->ls[0]=m->ls_after[0]; m->ls[1]=m->ls_after[1]; }
        if(m->cap_change_after_wait) m->cap=(m->cap&~M_CAP)|0x00010000;
        if(m->conflict_in_mask) m->lc = (m->lc & ~M_RATE) | 0x00080000; /* чужой RATE */
    }
    if(a==R_CAP && m->cap_change_after_first_rb) m->cap=(val&~M_CAP)|0x00010000;
    return 0; }
static int m_ls(void*c,int d,uint16_t*v){ struct mock*m=c; int i=m->lnksta_calls++;
    if(m->fail_lnksta_at>=0 && i==m->fail_lnksta_at){ *v=0xffff; return 0; }
    *v=m->ls[d]; return 0; }
static int m_lc_cap(void*c,int d,uint32_t*v){ struct mock*m=c; *v=m->lnkcap[d]; return 0; }
static struct ops mk_mock(struct mock*m){
    memset(m,0,sizeof*m);
    m->dl=0x00640003; m->lc=0x08089000; m->cap=0x00110036;
    m->ls[0]=0x1011; m->ls[1]=0x1011; m->lnkcap[0]=0x00453c11; m->lnkcap[1]=0x0261ad03;
    m->trig_self_clear=1; m->fail_rd_at=-1; m->fail_wr_at=-1; m->fail_lnksta_at=-1;
    m->mismatch_at=-1; m->mismatch_xor=0x00010000;
    m->clk_start=1700000000000ull;          /* F2: часы далеко от нуля */
    struct ops o={m_rd,m_wr,m_ls,m_lc_cap,m_now,m}; return o; }

/* ===================== TESTS ===================== */
static int T=0,F=0;
#define CHK(c,n) do{ if(c){T++;} else {F++;printf("  FAIL: %s\n",n);} }while(0)
/* заявленные бюджеты: поля 2 (control) / 3 (treatment); команда 1; откат 3 */
static void setup(struct owner*w,struct ops*o,enum mode md){
    own_init(w,o,(md==MODE_TREATMENT)?3:2,1,3); }

static int run_tests(void){
    printf("== V17 TRANSACTION TESTS ==\n");

    /* F2: часы далеко от нуля — ожидания работают (не мгновенный timeout) */
    { struct mock m; struct ops o=mk_mock(&m); struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      CHK(e.result_before_restore!=R_TIMEOUT,"F2 большой uptime: не ложный TIMEOUT");
      CHK(e.directive_submitted==1,"F2 команда отправлена"); }

    /* F1: trigger НЕ самоочищается -> откат не должен подать вторую команду */
    { struct mock m; struct ops o=mk_mock(&m); m.trig_self_clear=0;
      struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      CHK(m.phys_directives<=1,"F1 физических directive-команд не больше одной");
      CHK(e.restore==RST_BLOCKED||e.restore==RST_FAILED||e.restore==RST_RESTORED,"F1 откат классифицирован"); }

    /* F1b: cmd_field взведено ПЕРЕД полевой записью -> STATE_UNSAFE, без записи */
    { struct mock m; struct ops o=mk_mock(&m); m.lc |= B_TRIG;
      struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_CONTROL); e.mode=MODE_CONTROL; run_experiment(&w,&e,1000,1000);
      CHK(m.phys_directives==0,"F1b ни одной команды при взведённом cmd_field"); }

    /* F5: исчерпание бюджета прямо перед командой -> directive НЕ отправлен */
    { struct mock m; struct ops o=mk_mock(&m); struct owner w; struct exp e; memset(&e,0,sizeof e);
      own_init(&w,&o,3,0,3);              /* cmd_budget=0 */
      e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      CHK(e.st_dir==WR_BUDGET_EXCEEDED,"F5 BUDGET_EXCEEDED отдельным статусом");
      CHK(e.directive_submitted==0,"F5 directive_submitted=false");
      CHK(m.phys_directives==0,"F5 физической команды не было"); }

    /* F3: отказ на первом чтении -> ноль записей, ноль backend-вызовов после потери */
    { struct mock m; struct ops o=mk_mock(&m); m.fail_rd_at=0;
      struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      CHK(e.result_before_restore==R_INIT_INVALID,"F3 INIT_INVALID");
      CHK(m.wr_calls==0,"F3 ноль физических записей");
      CHK(w.backend_calls_after_loss==0,"F3 ноль backend-вызовов после потери доступа"); }

    /* F3b: недостоверный LnkSta на исходном чтении */
    { struct mock m; struct ops o=mk_mock(&m); m.fail_lnksta_at=0;
      struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      CHK(e.result_before_restore==R_INIT_INVALID && m.wr_calls==0,"F3b invalid LnkSta -> ноль записей"); }

    /* F4: cap уезжает ПОСЛЕ ожидания -> STATE_CHANGED, cap не переписывается */
    { struct mock m; struct ops o=mk_mock(&m); m.cap_change_after_wait=1;
      struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      CHK(e.result_before_restore==R_STATE_CHANGED,"F4 cap после ожидания -> STATE_CHANGED");
      CHK(w.field_writes<=3,"F4 cap не переписан сверх бюджета"); }

    /* F4b: cap уезжает после первого readback (перед командой) */
    { struct mock m; struct ops o=mk_mock(&m); m.cap_change_after_first_rb=1;
      struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      CHK(e.st_cap==WR_READBACK_MISMATCH||e.result_before_restore==R_STATE_CHANGED,"F4b изменение cap перехвачено до команды");
      CHK(m.phys_directives==0,"F4b команда не подана"); }

    /* F6: конфликт внутри нашей маски перед откатом -> RESTORE_BLOCKED */
    { struct mock m; struct ops o=mk_mock(&m); m.conflict_in_mask=1;
      struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      CHK(e.restore==RST_BLOCKED,"F6 чужое изменение в маске -> RESTORE_BLOCKED");
      CHK(exit_code(&e)==6,"F6 exit=6"); }

    /* F6b: отказ на первом шаге -> LC/DL не получают лишних восстановительных записей */
    { struct mock m; struct ops o=mk_mock(&m); m.fail_wr_at=0;   /* падает cap-запись */
      struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      CHK(w.restore_writes==0,"F6b нет восстановительных записей для нетронутых полей");
      CHK(m.lc==0x08089000 && m.dl==0x00640003,"F6b LC/DL не изменялись"); }

    /* F7: успех требует ширину; скорость Gen2 при неверной ширине -> INCONCLUSIVE */
    { struct mock m; struct ops o=mk_mock(&m); m.use_ls_after=1; m.ls_after[0]=0x1012; m.ls_after[1]=0x1022;
      struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      CHK(e.result_before_restore==R_INCONCLUSIVE,"F7 расхождение ширины -> INCONCLUSIVE"); }
    { struct mock m; struct ops o=mk_mock(&m); m.use_ls_after=1; m.ls_after[0]=0x1012; m.ls_after[1]=0x1012;
      struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      CHK(e.result_before_restore==R_SUCCESS_CANDIDATE,"F7 скорость+ширина на обоих -> SUCCESS_CANDIDATE");
      CHK(e.post_read_ok,"F7 состояние после отката прочитано отдельно"); }

    /* F7b: поля восстановлены, но линк не вернулся -> отдельный признак и exit 10 */
    { struct mock m; struct ops o=mk_mock(&m); m.use_ls_after=1; m.ls_after[0]=0x1012; m.ls_after[1]=0x1012;
      struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      CHK(e.restore==RST_RESTORED && !e.link_restored,"F7b поля RESTORED, но линк НЕ восстановлен — различаются");
      CHK(exit_code(&e)==10,"F7b exit=10 (линк не восстановлен)"); }

    /* базовый отрицательный путь: Gen1 + полный откат */
    { struct mock m; struct ops o=mk_mock(&m); struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_CONTROL); e.mode=MODE_CONTROL; run_experiment(&w,&e,1000,1000);
      CHK(e.result_before_restore==R_NO_GEN2,"BASE control -> NO_GEN2");
      CHK(e.restore==RST_RESTORED && e.link_restored,"BASE поля и линк восстановлены");
      CHK(exit_code(&e)==0,"BASE exit=0"); }

    printf("== RESULT: pass=%d fail=%d ==\n",T,F);
    return F==0?0:1; }

/* Трасса фактических backend-вызовов для критических отказов */
static void trace_case(const char*name,struct mock*m,struct owner*w,struct exp*e){
    printf("%-28s rd=%d wr=%d lnksta=%d phys_directives=%d after_loss=%d | "
           "st_cap=%s st_safe=%s st_rw=%s st_dir=%s | dir_submitted=%d | %s | %s | link_restored=%d exit=%d\n",
        name,m->rd_calls,m->wr_calls,m->lnksta_calls,m->phys_directives,w->backend_calls_after_loss,
        wr_name(e->st_cap),wr_name(e->st_safe),wr_name(e->st_ratewidth),wr_name(e->st_dir),
        e->directive_submitted,res_name(e->result_before_restore),rst_name(e->restore),
        e->link_restored,exit_code(e)); }
static int run_trace(void){
    printf("== V17 BACKEND CALL TRACES ==\n");
    { struct mock m; struct ops o=mk_mock(&m); m.fail_rd_at=0; struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      trace_case("F3 init read fail",&m,&w,&e); }
    { struct mock m; struct ops o=mk_mock(&m); struct owner w; struct exp e; memset(&e,0,sizeof e);
      own_init(&w,&o,3,0,3); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      trace_case("F5 cmd budget=0",&m,&w,&e); }
    { struct mock m; struct ops o=mk_mock(&m); m.trig_self_clear=0; struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      trace_case("F1 trigger no self-clear",&m,&w,&e); }
    { struct mock m; struct ops o=mk_mock(&m); m.lc|=B_TRIG; struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_CONTROL); e.mode=MODE_CONTROL; run_experiment(&w,&e,1000,1000);
      trace_case("F1b cmd_field preset",&m,&w,&e); }
    { struct mock m; struct ops o=mk_mock(&m); m.conflict_in_mask=1; struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      trace_case("F6 conflict in mask",&m,&w,&e); }
    { struct mock m; struct ops o=mk_mock(&m); m.cap_change_after_wait=1; struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_TREATMENT); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000,1000);
      trace_case("F4 cap moved after wait",&m,&w,&e); }
    { struct mock m; struct ops o=mk_mock(&m); struct owner w; struct exp e; memset(&e,0,sizeof e);
      setup(&w,&o,MODE_CONTROL); e.mode=MODE_CONTROL; run_experiment(&w,&e,1000,1000);
      trace_case("BASE control (negative)",&m,&w,&e); }
    return 0; }

int main(int argc,char**argv){
    if(argc==2 && !strcmp(argv[1],"--test")) return run_tests();
    if(argc==2 && !strcmp(argv[1],"--trace")) return run_trace();
    if(argc>=2 && !strcmp(argv[1],"--hw")){
        fprintf(stderr,"V17: аппаратный backend НЕ реализован; запуск запрещён (F1 write-семантика GP100 не обоснована).\n");
        return 3; }
    fprintf(stderr,"usage: %s --test\n",argv[0]);
    (void)wr_name; (void)res_name; (void)rst_name;
    return 2; }

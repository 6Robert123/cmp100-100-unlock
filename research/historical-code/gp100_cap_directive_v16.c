/* GP100_CAP_DIRECTIVE_V16 — испытатель гипотезы H_CAP_GPU_DIRECTIVE.
 *
 * Гипотеза: влияет ли ПРЕДВАРИТЕЛЬНАЯ настройка внутреннего cap 0x8c1c0
 * (mask 0x00030000, value 0x00020000 -> поле [17:16]=2) на результат
 * последующего GPU-side change_speed directive (0x8c040 bit0).
 * Контроль = последовательность V2 без cap-записи. Лечение = она же + ОДНА cap-запись.
 *
 * ЖЁСТКИЕ ПРАВИЛА (V16):
 *  - Аппаратный запуск ЗАПРЕЩЁН по умолчанию (гейт CMP100_ALLOW_HW_RUN=1); в V16 не запускался.
 *  - Никаких ASPM / PUNITS / bridge retrain / x16 / Gen3 — это другие переменные.
 *  - Одна cap-запись, один directive на попытку; счётчик записей с жёстким лимитом.
 *  - Непрерывной гонки записей НЕТ. Удержание проверяется readback'ом, не переписыванием.
 *  - Откат ТОЛЬКО изменённых полей маской (RMW). Целый LINK_CONFIG обратно НЕ пишется
 *    (bit0 = trigger, [4:0] = status).
 *  - Один владелец аппаратных операций: после access_lost любые операции отказывают.
 *
 * Сборка: gcc -O2 -Wall -Wextra -o gp100_cap_directive_v16 gp100_cap_directive_v16.c
 * Тесты:  ./gp100_cap_directive_v16 --test
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- адреса и поля (кодировки — ГИПОТЕЗА для GP100, см. отчёт) ---- */
#define R_DL      0x8b8c0u
#define R_LC      0x8c040u          /* LINK_CONFIG */
#define R_CAP     0x8c1c0u
#define M_SAFE    0x00000004u       /* DL safe timing bit2 */
#define M_RATE    0x000c0000u       /* [19:18] */
#define V_RATE_G2 0x00040000u       /* =01b (Gen2/5.0) по nouveau gk104 */
#define M_WIDTH   0x00700000u       /* [22:20] */
#define V_WIDTH_X1 0x00700000u      /* =7 (x1) */
#define M_CAP     0x00030000u       /* [17:16] */
#define V_CAP_G2  0x00020000u       /* =10b = 2  (ИСПРАВЛЕНО: не 01b) */
#define M_TRIG    0x00000001u       /* change_speed directive */
#define M_LTSSM   0x0000001fu       /* [4:0] статус/директива */

enum mode { MODE_CONTROL, MODE_TREATMENT };
enum result { R_SUCCESS_CANDIDATE, R_INCONCLUSIVE, R_NO_GEN2, R_TIMEOUT,
              R_STATE_CHANGED, R_INIT_INVALID, R_ACCESS_LOST, R_WRITE_VERIFY_FAIL };
enum restore { RST_NOT_NEEDED, RST_RESTORED, RST_FAILED, RST_BLOCKED };
static const char*result_name(enum result r){
    switch(r){case R_SUCCESS_CANDIDATE:return "SUCCESS_CANDIDATE";case R_INCONCLUSIVE:return "INCONCLUSIVE";
        case R_NO_GEN2:return "NO_GEN2";case R_TIMEOUT:return "TIMEOUT";case R_STATE_CHANGED:return "STATE_CHANGED";
        case R_INIT_INVALID:return "INIT_INVALID";case R_ACCESS_LOST:return "ACCESS_LOST";
        case R_WRITE_VERIFY_FAIL:return "WRITE_VERIFY_FAIL";} return "?"; }
static const char*restore_name(enum restore r){
    switch(r){case RST_NOT_NEEDED:return "NOT_NEEDED";case RST_RESTORED:return "RESTORED";
        case RST_FAILED:return "RESTORE_FAILED";case RST_BLOCKED:return "RESTORE_BLOCKED";} return "?"; }

static int mmio_invalid(uint32_t v){ return v==0xffffffffu || (v&0xffff0000u)==0xbadf0000u; }
static int cfg_invalid16(uint16_t v){ return v==0xffffu; }

/* ---- слой аппаратных операций ---- */
struct ops {
    int (*rd32)(void*,uint32_t addr,uint32_t*out);
    int (*wr32)(void*,uint32_t addr,uint32_t val);
    int (*rd_lnksta)(void*,int dev,uint16_t*out);   /* 0=ep 1=up */
    int (*rd_lnkcap)(void*,int dev,uint32_t*out);
    uint64_t (*now)(void*);
    void*ctx;
};

/* ---- журнал записей ---- */
struct wrec { uint32_t addr, old, mask, want, readback; int ok; };
#define MAXW 12
/* ---- единственный владелец аппаратных операций ---- */
struct owner {
    struct ops*o; int access_lost; unsigned writes, max_writes;
    struct wrec j[MAXW]; unsigned nj;
};
static int own_rd(struct owner*w,uint32_t a,uint32_t*v){
    if(w->access_lost) return -1;
    if(w->o->rd32(w->o->ctx,a,v)!=0){ w->access_lost=1; return -1; }
    if(mmio_invalid(*v)){ w->access_lost=1; return -1; }
    return 0;
}
/* masked RMW write + обязательный readback-verify; журналируется всегда */
static int own_wr_masked(struct owner*w,uint32_t a,uint32_t mask,uint32_t val){
    if(w->access_lost) return -1;
    if(w->writes>=w->max_writes){ fprintf(stderr,"write budget exceeded\n"); return -1; }
    uint32_t old; if(own_rd(w,a,&old)<0) return -1;
    uint32_t want=(old&~mask)|(val&mask);
    if(w->o->wr32(w->o->ctx,a,want)!=0){ w->access_lost=1; return -1; }
    w->writes++;
    uint32_t rb=0; int rbok = (own_rd(w,a,&rb)==0);
    int ok = rbok && ((rb&mask)==(want&mask));
    if(w->nj<MAXW) w->j[w->nj++]=(struct wrec){a,old,mask,want,rb,ok};
    return ok?0:-1;
}

/* ---- состояние опыта ---- */
struct exp {
    enum mode mode;
    uint32_t init_dl, init_lc, init_cap;
    uint16_t init_ls[2], fin_ls[2];
    uint32_t init_lnkcap[2], fin_lnkcap[2];
    uint32_t cap_before_directive, cap_after_wait;
    int directive_issued, directive_readback_bit;
    uint32_t lc_after_directive;
    enum result result; enum restore restore;
    int idle_ok, complete_ok;
    unsigned nj_before_restore;
};

/* ожидание (0x8c040 & 0x1f)==0 с deadline */
static int wait_idle(struct owner*w,uint64_t deadline_us){
    uint32_t v;
    for(;;){
        if(own_rd(w,R_LC,&v)<0) return -1;
        if((v&M_LTSSM)==0) return 0;
        if(w->o->now(w->o->ctx)>=deadline_us) return 1;   /* timeout */
    }
}

/* Основная последовательность. НЕ выполняет аппаратный запуск сама по себе —
 * вызывается и тестами (mock), и будущим hw backend. */
static void run_experiment(struct owner*w,struct exp*e,uint64_t idle_budget_us,uint64_t done_budget_us){
    memset(&e->result,0,0);
    e->restore=RST_NOT_NEEDED;
    /* 1. исходные чтения; любая недостоверность -> НОЛЬ записей */
    if(own_rd(w,R_DL,&e->init_dl)<0 || own_rd(w,R_LC,&e->init_lc)<0 || own_rd(w,R_CAP,&e->init_cap)<0){
        e->result=R_INIT_INVALID; return; }
    for(int d=0;d<2;d++){
        if(w->o->rd_lnksta(w->o->ctx,d,&e->init_ls[d])!=0 || cfg_invalid16(e->init_ls[d])){ e->result=R_INIT_INVALID; return; }
        if(w->o->rd_lnkcap(w->o->ctx,d,&e->init_lnkcap[d])!=0){ e->result=R_INIT_INVALID; return; }
    }
    e->fin_ls[0]=e->init_ls[0]; e->fin_ls[1]=e->init_ls[1];
    e->fin_lnkcap[0]=e->init_lnkcap[0]; e->fin_lnkcap[1]=e->init_lnkcap[1];

    /* 2. TREATMENT: ровно ОДНА cap-запись ДО directive */
    if(e->mode==MODE_TREATMENT){
        if(own_wr_masked(w,R_CAP,M_CAP,V_CAP_G2)<0){
            e->result = w->access_lost? R_ACCESS_LOST : R_WRITE_VERIFY_FAIL; goto restore; }
    }
    /* 3. safe timing */
    if(own_wr_masked(w,R_DL,M_SAFE,M_SAFE)<0){
        e->result = w->access_lost? R_ACCESS_LOST : R_WRITE_VERIFY_FAIL; goto restore; }
    /* 4. idle-wait с deadline */
    { int r=wait_idle(w,idle_budget_us);
      if(r<0){ e->result=R_ACCESS_LOST; goto restore; }
      if(r==1){ e->idle_ok=0; e->result=R_TIMEOUT; goto restore; }   /* directive НЕ выполняется */
      e->idle_ok=1; }
    /* 5. rate=Gen2 + width=x1 одной RMW-записью */
    if(own_wr_masked(w,R_LC,M_RATE|M_WIDTH,V_RATE_G2|V_WIDTH_X1)<0){
        e->result = w->access_lost? R_ACCESS_LOST : R_WRITE_VERIFY_FAIL; goto restore; }
    /* 6. проверка cap НЕПОСРЕДСТВЕННО перед directive; при неожиданном изменении — STOP */
    if(own_rd(w,R_CAP,&e->cap_before_directive)<0){ e->result=R_ACCESS_LOST; goto restore; }
    { uint32_t expect = (e->mode==MODE_TREATMENT)? V_CAP_G2 : (e->init_cap & M_CAP);
      if((e->cap_before_directive & M_CAP) != (expect & M_CAP)){
          e->result=R_STATE_CHANGED; goto restore; }   /* cap НЕ переписываем */
    }
    /* 7. ОДИН GPU-directive */
    if(own_wr_masked(w,R_LC,M_TRIG,M_TRIG)<0){
        if(w->access_lost){ e->result=R_ACCESS_LOST; goto restore; }
        /* trigger-бит может самоочищаться -> readback-mismatch тут не фатален,
           но фиксируем факт: срабатывание НЕ подтверждено */
    }
    e->directive_issued=1;
    if(own_rd(w,R_LC,&e->lc_after_directive)==0) e->directive_readback_bit=(e->lc_after_directive&M_TRIG)?1:0;
    /* 8. ожидание завершения */
    { int r=wait_idle(w,done_budget_us);
      if(r<0){ e->result=R_ACCESS_LOST; goto restore; }
      e->complete_ok = (r==0);
      if(r==1){ e->result=R_TIMEOUT; goto restore; } }
    /* 9. cap после ожидания + финальные LnkSta/LnkCap */
    own_rd(w,R_CAP,&e->cap_after_wait);
    for(int d=0;d<2;d++){
        if(w->o->rd_lnksta(w->o->ctx,d,&e->fin_ls[d])!=0||cfg_invalid16(e->fin_ls[d])){ e->result=R_ACCESS_LOST; goto restore; }
        w->o->rd_lnkcap(w->o->ctx,d,&e->fin_lnkcap[d]);
    }
    { int s0=e->fin_ls[0]&0xf, s1=e->fin_ls[1]&0xf;
      if(s0==2&&s1==2) e->result=R_SUCCESS_CANDIDATE;
      else if(s0!=s1)  e->result=R_INCONCLUSIVE;
      else             e->result=R_NO_GEN2; }

restore:
    e->nj_before_restore = w->nj;
    /* 10. откат ТОЛЬКО изменённых полей маской. При потере доступа — BLOCKED, без слепых записей. */
    if(w->access_lost){ e->restore=RST_BLOCKED; return; }
    if(w->nj==0){ e->restore=RST_NOT_NEEDED; return; }
    { int ok=1;
      w->max_writes += 3;   /* бюджет на откат */
      if(own_wr_masked(w,R_LC,M_RATE|M_WIDTH,e->init_lc)<0) ok=0;
      if(own_wr_masked(w,R_DL,M_SAFE,e->init_dl)<0) ok=0;
      if(e->mode==MODE_TREATMENT && own_wr_masked(w,R_CAP,M_CAP,e->init_cap)<0) ok=0;
      if(w->access_lost) e->restore=RST_BLOCKED;
      else e->restore = ok? RST_RESTORED : RST_FAILED; }
}
static int exit_code(struct exp*e){
    if(e->restore==RST_BLOCKED||e->restore==RST_FAILED) return 6;
    switch(e->result){
        case R_SUCCESS_CANDIDATE: case R_NO_GEN2: return 0;
        case R_INCONCLUSIVE: return 3;
        case R_TIMEOUT: return 8;
        case R_STATE_CHANGED: return 7;
        case R_INIT_INVALID: return 4;
        case R_ACCESS_LOST: return 5;
        case R_WRITE_VERIFY_FAIL: return 9;
    } return 4;
}

/* ================= MOCK backend с инъекцией отказов ================= */
struct mock {
    uint32_t dl, lc, cap;
    uint16_t ls[2]; uint32_t lnkcap[2];
    uint64_t clk;
    int fail_rd_at, rd_calls;          /* -1 нет */
    int fail_wr_at, wr_calls;
    int readback_mismatch_at;          /* индекс записи, где readback не совпадёт */
    uint32_t mismatch_xor;             /* какой бит испортить (должен лежать ВНУТРИ маски записи) */
    int cap_change_before_directive;   /* подменить cap перед directive */
    int idle_never;                    /* (lc&0x1f) никогда не 0 */
    int access_lost_after_wr;          /* -1 нет: после N-й записи доступ теряется */
    int foreign_bit_after;             /* выставить посторонний бит в lc после directive */
    uint16_t ls_after[2]; int use_ls_after;
};
static uint64_t m_now(void*c){ struct mock*m=c; return ++m->clk; }
static uint32_t* m_reg(struct mock*m,uint32_t a){
    if(a==R_DL) return &m->dl;
    if(a==R_LC) return &m->lc;
    if(a==R_CAP) return &m->cap;
    return 0; }
static int m_rd(void*c,uint32_t a,uint32_t*v){ struct mock*m=c;
    int i=m->rd_calls++;
    if(m->access_lost_after_wr>=0 && m->wr_calls>m->access_lost_after_wr){ *v=0xffffffff; return 0; }
    if(m->fail_rd_at>=0 && i==m->fail_rd_at){ *v=0xbadf0000; return 0; }
    uint32_t*p=m_reg(m,a); if(!p){ *v=0; return 0; }
    if(a==R_LC && m->idle_never){ *v=(*p)|0x10; return 0; }
    *v=*p; return 0; }
static int m_wr(void*c,uint32_t a,uint32_t val){ struct mock*m=c;
    int i=m->wr_calls++;
    if(m->fail_wr_at>=0 && i==m->fail_wr_at) return -1;
    uint32_t*p=m_reg(m,a); if(!p) return 0;
    *p=val;
    if(m->readback_mismatch_at>=0 && i==m->readback_mismatch_at) *p = val ^ (m->mismatch_xor?m->mismatch_xor:0x00040000u);
    if(a==R_LC && (val&M_TRIG)){
        *p &= ~M_TRIG;                                   /* trigger самоочищается */
        if(m->foreign_bit_after) *p |= 0x00000200;       /* посторонний бит появился */
        if(m->use_ls_after){ m->ls[0]=m->ls_after[0]; m->ls[1]=m->ls_after[1]; }
    }
    if(a==R_CAP && m->cap_change_before_directive) *p = (val & ~M_CAP) | 0x00010000; /* cap уехал */
    return 0; }
static int m_ls(void*c,int d,uint16_t*v){ struct mock*m=c; *v=m->ls[d]; return 0; }
static int m_lc(void*c,int d,uint32_t*v){ struct mock*m=c; *v=m->lnkcap[d]; return 0; }
static struct ops mk_mock(struct mock*m){
    memset(m,0,sizeof*m);
    m->dl=0x00640003; m->lc=0x08089000; m->cap=0x00110036;   /* реальные baseline из лога V2 */
    m->ls[0]=0x1011; m->ls[1]=0x1011; m->lnkcap[0]=0x00453c11; m->lnkcap[1]=0x0261ad03;
    m->fail_rd_at=-1; m->fail_wr_at=-1; m->readback_mismatch_at=-1; m->access_lost_after_wr=-1;
    struct ops o={m_rd,m_wr,m_ls,m_lc,m_now,m}; return o; }
static void mk_owner(struct owner*w,struct ops*o,unsigned budget){
    memset(w,0,sizeof*w); w->o=o; w->max_writes=budget; }

/* ================= тесты ================= */
static int T=0,F=0;
#define CHK(c,n) do{ if(c){T++;} else {F++;printf("  FAIL: %s\n",n);} }while(0)
static unsigned cap_writes_pre(struct owner*w,struct exp*e){ unsigned n=0,lim=e->nj_before_restore?e->nj_before_restore:w->nj;
    for(unsigned i=0;i<lim;i++){ if(w->j[i].addr==R_CAP) n++; }
    return n; }
static int had_trigger(struct owner*w){ for(unsigned i=0;i<w->nj;i++) if(w->j[i].addr==R_LC&&w->j[i].mask==M_TRIG) return 1; return 0; }

static int run_tests(void){
    printf("== V16 CAP+DIRECTIVE OFFLINE TESTS ==\n");
    /* C1: 0x20000 под 0x30000 -> поле 2; прочие биты сохраняются */
    { uint32_t old=0x00110036, want=(old&~M_CAP)|V_CAP_G2;
      CHK(((want&M_CAP)>>16)==2,"C1 поле [17:16] == 2");
      CHK((want&~M_CAP)==(old&~M_CAP),"C1 прочие биты сохранены");
      CHK(((old&M_CAP)>>16)==1,"C1 исходное поле было 1 (изменение реально)"); }

    /* C2: control не пишет cap; treatment пишет ровно один раз ДО directive */
    { struct mock m; struct ops o=mk_mock(&m); struct owner w; struct exp e; memset(&e,0,sizeof e);
      mk_owner(&w,&o,8); e.mode=MODE_CONTROL; run_experiment(&w,&e,1000000,1000000);
      CHK(cap_writes_pre(&w,&e)==0,"C2 control: НОЛЬ cap-записей"); }
    { struct mock m; struct ops o=mk_mock(&m); struct owner w; struct exp e; memset(&e,0,sizeof e);
      mk_owner(&w,&o,8); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000000,1000000);
      CHK(cap_writes_pre(&w,&e)==1,"C2 treatment: РОВНО одна cap-запись (без учёта отката)");
      CHK(w.j[0].addr==R_CAP,"C2 cap-запись ПЕРВАЯ, до directive");
      CHK(((w.j[0].readback&M_CAP)>>16)==2,"C2 cap readback = 2"); }

    /* C3: ошибка начального чтения -> НОЛЬ записей */
    { struct mock m; struct ops o=mk_mock(&m); m.fail_rd_at=0; struct owner w; struct exp e; memset(&e,0,sizeof e);
      mk_owner(&w,&o,8); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000000,1000000);
      CHK(e.result==R_INIT_INVALID && w.writes==0,"C3 init invalid -> ноль записей");
      CHK(exit_code(&e)!=0,"C3 exit!=0"); }

    /* C4a: readback mismatch на cap -> directive НЕ выполняется */
    { struct mock m; struct ops o=mk_mock(&m); m.readback_mismatch_at=0; m.mismatch_xor=0x00010000; struct owner w; struct exp e; memset(&e,0,sizeof e);
      mk_owner(&w,&o,8); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000000,1000000);
      CHK(!had_trigger(&w),"C4a readback mismatch -> directive не выполнен");
      CHK(e.result==R_WRITE_VERIFY_FAIL,"C4a результат WRITE_VERIFY_FAIL"); }
    /* C4b: cap изменился перед directive -> STATE_CHANGED, cap НЕ переписан */
    { struct mock m; struct ops o=mk_mock(&m); m.cap_change_before_directive=1; struct owner w; struct exp e; memset(&e,0,sizeof e);
      mk_owner(&w,&o,8); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000000,1000000);
      CHK(e.result==R_STATE_CHANGED || e.result==R_WRITE_VERIFY_FAIL,"C4b изменение cap перехвачено");
      CHK(!had_trigger(&w),"C4b directive не выполнен"); }

    /* C5: idle timeout -> directive НЕ выполняется */
    { struct mock m; struct ops o=mk_mock(&m); m.idle_never=1; struct owner w; struct exp e; memset(&e,0,sizeof e);
      mk_owner(&w,&o,8); e.mode=MODE_TREATMENT; run_experiment(&w,&e,50,50);
      CHK(e.result==R_TIMEOUT && !had_trigger(&w),"C5 idle timeout -> без directive"); }

    /* C6: потеря доступа после записи -> нет слепого отката/перезапуска */
    { struct mock m; struct ops o=mk_mock(&m); m.access_lost_after_wr=0; struct owner w; struct exp e; memset(&e,0,sizeof e);
      mk_owner(&w,&o,8); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000000,1000000);
      CHK(w.access_lost && e.restore==RST_BLOCKED,"C6 access lost -> RESTORE_BLOCKED");
      CHK(exit_code(&e)==6,"C6 exit=6 (неполный опыт)");
      CHK(!had_trigger(&w),"C6 directive не выполнялся после потери доступа"); }

    /* C7: посторонний бит, изменившийся во время опыта, НЕ затирается откатом */
    { struct mock m; struct ops o=mk_mock(&m); m.foreign_bit_after=1; struct owner w; struct exp e; memset(&e,0,sizeof e);
      mk_owner(&w,&o,8); e.mode=MODE_CONTROL; run_experiment(&w,&e,1000000,1000000);
      CHK((m.lc & 0x00000200)!=0,"C7 посторонний бит сохранён после отката");
      CHK((m.lc & (M_RATE|M_WIDTH))==(0x08089000u & (M_RATE|M_WIDTH)),"C7 управляющие поля восстановлены");
      CHK(e.restore==RST_RESTORED,"C7 RESTORED"); }

    /* C8: оба конца Gen2 -> SUCCESS_CANDIDATE; расхождение -> INCONCLUSIVE */
    { struct mock m; struct ops o=mk_mock(&m); m.use_ls_after=1; m.ls_after[0]=0x1012; m.ls_after[1]=0x1012;
      struct owner w; struct exp e; memset(&e,0,sizeof e);
      mk_owner(&w,&o,8); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000000,1000000);
      CHK(e.result==R_SUCCESS_CANDIDATE,"C8a оба Gen2 -> SUCCESS_CANDIDATE"); }
    { struct mock m; struct ops o=mk_mock(&m); m.use_ls_after=1; m.ls_after[0]=0x1012; m.ls_after[1]=0x1011;
      struct owner w; struct exp e; memset(&e,0,sizeof e);
      mk_owner(&w,&o,8); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000000,1000000);
      CHK(e.result==R_INCONCLUSIVE,"C8b расхождение концов -> INCONCLUSIVE"); }

    /* C9: Gen1 после завершения + успешный откат -> NO_GEN2 (НЕ доказательство невозможности) */
    { struct mock m; struct ops o=mk_mock(&m); struct owner w; struct exp e; memset(&e,0,sizeof e);
      mk_owner(&w,&o,8); e.mode=MODE_TREATMENT; run_experiment(&w,&e,1000000,1000000);
      CHK(e.result==R_NO_GEN2 && e.restore==RST_RESTORED,"C9 Gen1 + откат -> NO_GEN2/RESTORED");
      CHK(exit_code(&e)==0,"C9 exit=0 (опыт полон, результат отрицательный)"); }

    printf("== RESULT: pass=%d fail=%d ==\n",T,F);
    return F==0?0:1;
}

int main(int argc,char**argv){
    if(argc==2 && !strcmp(argv[1],"--test")) return run_tests();
    if(argc>=2 && !strcmp(argv[1],"--hw")){
        if(!getenv("CMP100_ALLOW_HW_RUN")){
            fprintf(stderr,"V16: аппаратный запуск ЗАПРЕЩЁН на этом этапе (нет гейта).\n"); return 3; }
        fprintf(stderr,"V16: hardware backend НЕ реализован намеренно (гейт не пройден).\n"); return 3; }
    fprintf(stderr,"usage: %s --test    (аппаратный режим закрыт в V16)\n",argv[0]);
    (void)result_name; (void)restore_name;
    return 2;
}

/**
 * @file gui_lvgl.c
 * @brief LVGL 红绿灯 GUI (320×480 竖屏) —— 单线程, 参照 lvgldemo 模式
 *
 *  lvgldemo 流程:
 *    lv_init() → lv_nuttx_init() → 创建UI → while(1){ lv_timer_handler(); usleep(idle); }
 *
 *  我们沿用同样思路:
 *    gui_lvgl_init()     → 初始化 LVGL + 创建所有控件
 *    gui_lvgl_update()   → 更新控件状态 (主循环每一 tick 调一次)
 *    gui_lvgl_sleep_ms() → 代替 sleep, 边睡边调用 lv_timer_handler()
 */
#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <lvgl/lvgl.h>

#include "gui_lvgl.h"
#include "signal_controller.h"

/* ---- 颜色 ---- */
#define C_GREEN   lv_color_hex(0x00C853)
#define C_RED     lv_color_hex(0xD50000)
#define C_YELLOW  lv_color_hex(0xFFD600)
#define C_GRAY    lv_color_hex(0x424242)
#define C_BG      lv_color_hex(0x0F1419)
#define C_PANEL   lv_color_hex(0x1A2332)
#define C_TEXT    lv_color_hex(0xE0E0E0)
#define C_DIM     lv_color_hex(0x8899AA)
#define C_HEADER  lv_color_hex(0x1E2D3D)
#define C_BLUE    lv_color_hex(0x4FC3F7)

/* 竖屏 */
#define SCR_W   320
#define SCR_H   480
#define MARGIN  6

/* 区域 */
#define H_TOP   34
#define H_HEAD  18
#define GAP     2
#define H_ROW  ((SCR_H - (H_TOP+H_HEAD) - 64 - GAP*5) / 4)
#define H_BOT   64
#define Y_TOP   0
#define Y_HEAD (Y_TOP+H_TOP)
#define Y_BOT  (SCR_H-H_BOT)

/* 行内列 */
#define X_DIR   0
#define W_DIR   28
#define X_L    (X_DIR+W_DIR+4)
#define W_L     60
#define X_S    (X_L+W_L+4)
#define W_S     60
#define X_R    (X_S+W_S+4)
#define W_R     60
#define X_Q    (X_R+W_R+4)
#define W_Q     84
#define DOT_SZ 24

static const int   ORDER[4] = {ROAD_E, ROAD_S, ROAD_W, ROAD_N};
static const char *NAMES[4] = {"E","S","W","N"};

static lv_obj_t *g_phase, *g_remain, *g_tick;
static lv_obj_t *g_bot1, *g_bot2;
static lv_obj_t *g_light[4][3];
static lv_obj_t *g_queue[4];
static lv_style_t sty_grn, sty_red, sty_yel, sty_gry;
static bool g_ok;

/* ---- 工具 ---- */
static lv_obj_t *ml(lv_obj_t *p, const char *t, lv_color_t c, int x, int y)
{ lv_obj_t *l=lv_label_create(p); lv_label_set_text(l,t);
  lv_obj_set_style_text_color(l,c,LV_PART_MAIN); lv_obj_set_pos(l,x,y); return l; }

static void mk_styles(void)
{
#define S(name,clr) lv_style_init(&name);lv_style_set_bg_color(&name,clr); \
  lv_style_set_bg_opa(&name,LV_OPA_COVER);lv_style_set_radius(&name,5);lv_style_set_border_width(&name,0)
  S(sty_grn, C_GREEN); S(sty_red, C_RED); S(sty_yel, C_YELLOW); S(sty_gry, C_GRAY);
#undef S
}

static void mk_top(lv_obj_t *scr)
{
  lv_obj_t *b=lv_obj_create(scr); lv_obj_set_size(b,SCR_W,H_TOP); lv_obj_set_pos(b,0,Y_TOP);
  lv_obj_set_style_bg_color(b,C_PANEL,LV_PART_MAIN); lv_obj_set_style_border_width(b,0,LV_PART_MAIN);
  lv_obj_set_style_pad_all(b,3,LV_PART_MAIN);
  g_phase=ml(b,"INIT...",C_BLUE,4,4); g_remain=ml(b,"--",C_YELLOW,160,4);
  g_tick=ml(b,"T:0",C_DIM,270,4);
}

static void mk_head(lv_obj_t *scr)
{
  lv_obj_t *h=lv_obj_create(scr); lv_obj_set_size(h,SCR_W-MARGIN*2,H_HEAD); lv_obj_set_pos(h,MARGIN,Y_HEAD);
  lv_obj_set_style_bg_color(h,C_HEADER,LV_PART_MAIN); lv_obj_set_style_border_width(h,0,LV_PART_MAIN);
  lv_obj_set_style_radius(h,3,LV_PART_MAIN); lv_obj_set_style_pad_all(h,0,LV_PART_MAIN);
  ml(h,"",C_DIM,MARGIN+X_DIR,1); ml(h,"L",C_DIM,MARGIN+X_L+28,1);
  ml(h,"S",C_DIM,MARGIN+X_S+28,1); ml(h,"R",C_DIM,MARGIN+X_R+28,1);
  ml(h,"Q",C_DIM,MARGIN+X_Q+36,1);
}

static void mk_row(lv_obj_t *scr, int idx, int road)
{
  int y=Y_HEAD+H_HEAD+GAP+idx*(H_ROW+GAP);
  lv_obj_t *p=lv_obj_create(scr); lv_obj_set_size(p,SCR_W-MARGIN*2,H_ROW);
  lv_obj_set_pos(p,MARGIN,y); lv_obj_set_style_bg_color(p,C_PANEL,LV_PART_MAIN);
  lv_obj_set_style_border_width(p,1,LV_PART_MAIN); lv_obj_set_style_border_color(p,lv_color_hex(0x2C3A4A),LV_PART_MAIN);
  lv_obj_set_style_radius(p,3,LV_PART_MAIN); lv_obj_set_style_pad_all(p,0,LV_PART_MAIN);
  ml(p,NAMES[road],C_TEXT,X_DIR+4,(H_ROW-16)/2);
  int xs[3]={X_L,X_S,X_R};
  for(int ln=0;ln<3;ln++){ lv_obj_t *d=lv_obj_create(p); lv_obj_set_size(d,DOT_SZ,DOT_SZ);
    lv_obj_set_pos(d,xs[ln]+16,(H_ROW-DOT_SZ)/2); lv_obj_add_style(d,&sty_gry,LV_PART_MAIN);
    lv_obj_clear_flag(d,LV_OBJ_FLAG_SCROLLABLE); g_light[road][ln]=d; }
  g_queue[road]=ml(p,"0",C_TEXT,X_Q+30,(H_ROW-16)/2);
}

static void mk_bot(lv_obj_t *scr)
{
  lv_obj_t *b=lv_obj_create(scr); lv_obj_set_size(b,SCR_W,H_BOT); lv_obj_set_pos(b,0,Y_BOT);
  lv_obj_set_style_bg_color(b,C_PANEL,LV_PART_MAIN); lv_obj_set_style_border_width(b,0,LV_PART_MAIN);
  lv_obj_set_style_pad_all(b,3,LV_PART_MAIN);
  g_bot1=ml(b,"Ready.",C_DIM,4,6); g_bot2=ml(b,"",C_DIM,4,28);
}

/* ==================== 公共接口 ==================== */

void gui_lvgl_init(void)
{
  if(g_ok) return;
  if(!lv_is_initialized()){ lv_init(); lv_nuttx_dsc_t i; lv_nuttx_result_t r;
    lv_nuttx_dsc_init(&i);
#ifdef CONFIG_INPUT_TOUCHSCREEN
    i.input_path="/dev/input0";
#endif
    lv_nuttx_init(&i,&r); if(!r.disp){ printf("[GUI] disp fail\n"); return; } }

  lv_obj_t *scr=lv_scr_act(); lv_obj_set_style_bg_color(scr,C_BG,LV_PART_MAIN);
  mk_styles(); mk_top(scr); mk_head(scr);
  for(int i=0;i<4;i++) mk_row(scr,i,ORDER[i]); mk_bot(scr);

  for(int w=0;w<200;w++){ lv_timer_handler(); usleep(5000); }

  g_ok=true; printf("[GUI] OK\n");
}

void gui_lvgl_update(const SignalDecision *d, int tick)
{
  if(!g_ok||!d) return;
  bool tr=(d->phase==PHASE_YELLOW||d->phase==PHASE_ALL_RED);
  int mask=d->allowed_lanes_mask; char buf[64];

  lv_label_set_text(g_phase,phase_to_string(d->phase));
  int rm=d->duration-d->phase_elapsed; if(rm<0)rm=0;
  snprintf(buf,sizeof(buf),"%ds",rm); lv_label_set_text(g_remain,buf);
  snprintf(buf,sizeof(buf),"T:%d",tick); lv_label_set_text(g_tick,buf);

  int total=0;
  for(int i=0;i<4;i++){ int road=ORDER[i]; int qt=0;
    for(int ln=0;ln<3;ln++){ bool ok=(mask>>(road*3+ln))&1;
      lv_obj_set_style_bg_color(g_light[road][ln],
        ln==LANE_RIGHT?C_GREEN: (ok?C_GREEN:C_RED), LV_PART_MAIN);
      qt+=d->road_lanes[road][ln].total; }
    snprintf(buf,sizeof(buf),"%d",qt); lv_label_set_text(g_queue[road],buf); total+=qt; }
  snprintf(buf,sizeof(buf),"NS:%ds EW:%ds Total:%d",
    d->waiting_seconds[CORRIDOR_NS],d->waiting_seconds[CORRIDOR_EW],total);
  lv_label_set_text(g_bot1,buf); lv_label_set_text(g_bot2,d->reason[0]?d->reason:"-");
}

void gui_lvgl_sleep_ms(int ms)
{
  int loops=ms/5; if(loops<1)loops=1;
  for(int i=0;i<loops;i++){ lv_timer_handler(); usleep(5000); }
}

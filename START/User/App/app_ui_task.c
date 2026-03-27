/**
 * @file    app_ui_task.c
 * @brief   UI display task implementation
 */
#include "main.h"

#include "ai_beamforming.h"
#include "app_camera.h"
#include "app_display.h"
#include "app_main_task.h"
#include "app_perf.h"
#include "app_touch.h"
#include "app_ui_cli.h"
#include "app_user_config.h"
#include "LCD/ltdc.h"

#include <stdio.h>
#include <string.h>

#if (APP_LVGL_ENABLE != 0u)
#include "app_lvgl_ui.h"
#include "lvgl/lvgl.h"

void lv_port_disp_init(void);
void lv_port_disp_reconfigure(void);
void lv_port_indev_init(void);
#endif

/**
 * @brief UI 婵炴挸寮堕悡瀣触鎼达綆浼傞柟鍨С缂嶆梻鎮伴…鎺旂闁惧繑鑹鹃崵閬嶅极閹峰被鈧啴鏁?
 *
 * 闁告牕鎳庨幆鍫熺▔婢跺鍤嬮柛鎴ｅГ閺嗙喖骞愰崶顒佸珱闁挎稑鑻崹搴ㄥ礆椤愩埄鍤犻幖瀛樻缁?
 *  - is_ready : 闁哄被鍎撮妤呭及閸撗佷粵閻忕偛鍊瑰Σ鎼佸触閿曗偓閸戯紕鈧懓鏈崹姘跺礆濠靛棭娼楅柛?
 *  - init     : 闁圭瑳鍡╂斀闁哄嫬澧介妵姘变沪閸屾艾鐏ュ┑顔碱儏鐎垫煡鏁嶉崼婵堢暤缂佹稑顧€缁辨繈宕ｉ鐐叉濠㈣泛绉烽惃鐔兼偨椤帞绀?
 *  - render   : 闁圭瑳鍡╂斀濞戞挴鍋撻悽顖嗗嫯顩柡灞炬尭閼荤喖骞撻幇顏呭攭闁?LCD
 *
 * 闂侇偅淇虹换鍐礄閼恒儲娈堕柟绋挎喘閹凤繝鎳撳畝鍕闁烩晛鐡ㄧ敮瀵告嫬閸愵亝鏆忛柨娑樻湰濠€顓㈠级閵夈儱璁查柡鍐Х缁辨娊宕氶崶銊ュ簥闁告帡顣︾粭澶愬触鐏炴崘顩柡灞炬尭閻ゅ嫰鎮崇敮顔剧濠?GPU 闁告梻濞€閳ь剛鍠庨幃妤冪博椤栥倗绀嗛柕?
 */
typedef struct
{
    uint8_t (*is_ready)(void);          /**< 閺夆晜鏌ㄥú?1 閻炴稏鍔庨妵姘跺及閸撗佷粵缁绢収鍏涘▎銏狀啅閹绘帗鐨戠紓渚囦悍缁辨繈宕ｉ娆庣鞍鐎殿喒鍋撳┑顔碱儐鐟曞棝寮?*/
    void    (*init)(void);              /**< 闁告帗绻傞～鎰板礌閺嶃劍鈻旂紒鈧崫鍕勾闁挎稒鐡圕D 闁哄啳娉涚花顓㈠Υ娑撱彆DC 闂佹澘绉堕悿鍡涘Υ娴ｅ憡濮庣紓鍌涙尭閸熷灝銆掗崨瀛樼ォ */
    void    (*render)(const Sound_Pos_t   *pos,        /**< 濠㈠湱澧楃花顔芥媴瀹ュ洨鏋傞柨娑樼墛閺岀喐鎷呭鍫健+闁煎厖绮欓崳娲晬?*/
                      const SRP_VisFrame_t *vis_frame, /**< SRP 闁绘埈鍘兼慨蹇涘炊閹冭閻熸瑥妫楃€佃尪绠涢銈呭季 */
                      uint32_t             frame_seq,  /**< UI 閻㈩垎鍐闁告瑥鍤栫槐婵嬫偨閵娿倗鑹鹃柡鍌氭搐閻⊙囧礆闁垮鐓€闁告帒妫濋。?*/
                      uint8_t              sai_dma_active); /**< SAI DMA 婵炲弶妲掔粚顒勫冀閸パ呯 */
} App_UiRendererOps_t;

/* -------------------------------------------------------------------------- */
/* 闁告挸绉撮幃婊勭珶閻楀牊顫栭柨娑欑搱egacy 婵炴挸寮堕悡瀣触鎼达綆浼傞柣銊ュ缁椾焦绋夐鍕澖闁绘粍婢橀崵閬嶅极鐢喚绀勯悗瑙勭煯缁犵喖宕烽妸銈囩憮闁哄倻娅㈢槐?                        */
/* -------------------------------------------------------------------------- */
static uint8_t s_ui_legacy_is_ready(void);   /**< 闁哄被鍎撮?App_Display 闁哄嫷鍨伴幆浣轰焊鏉堚晛宕?*/
static void    s_ui_legacy_init(void);        /**< 閻犲鍟伴弫?App_Display_Init() */
static void    s_ui_legacy_render(const Sound_Pos_t *pos,
                                  const SRP_VisFrame_t *vis_frame,
                                  uint32_t frame_seq,
                                  uint8_t sai_dma_active); /**< 閻犲鍟伴弫?App_Display_Render() */

#if (APP_LVGL_ENABLE != 0u)
static uint8_t s_ui_lvgl_is_ready(void);
static void    s_ui_lvgl_init(void);
static void    s_ui_lvgl_render(const Sound_Pos_t *pos,
                                const SRP_VisFrame_t *vis_frame,
                                uint32_t frame_seq,
                                uint8_t sai_dma_active);
#endif

/** @brief Legacy 婵炴挸寮堕悡瀣触鎼达綆浼傞柟鍨С缂嶆梻鎮伴…鎺旂const闁挎稑鐬肩槐顏嗘嫚閹寸偞鍩傜痪顓у枛閻ｉ箖鏁嶇仦鐣屾憼闁?Flash闁?*/
static const App_UiRendererOps_t s_ui_renderer_legacy_ops = {
    s_ui_legacy_is_ready,   /**< is_ready 闁告垼濮ら弳鐔煎箰閸ヮ剚瀚?*/
    s_ui_legacy_init,        /**< init 闁告垼濮ら弳鐔煎箰閸ヮ剚瀚?*/
    s_ui_legacy_render       /**< render 闁告垼濮ら弳鐔煎箰閸ヮ剚瀚?*/
};

#if (APP_LVGL_ENABLE != 0u)
static const App_UiRendererOps_t s_ui_renderer_lvgl_ops = {
    s_ui_lvgl_is_ready,
    s_ui_lvgl_init,
    s_ui_lvgl_render
};

static uint8_t s_ui_lvgl_ready = 0u;
static uint8_t s_ui_lvgl_core_inited = 0u;
static uint8_t s_ui_lvgl_ports_inited = 0u;
static TickType_t s_ui_lvgl_last_tick = 0u;
#endif

/** @brief 鐟滅増鎸告晶鐘测攽閳ь剙煤閼姐倖鐣辨繛鎾冲级閻撳宕ユ惔锝庝紓闁圭娲幏锟犳晬瀹€鍕笡閻犱降鍊栫€垫岸宕?Legacy 闁告艾娴烽?*/
static const App_UiRendererOps_t *s_ui_renderer = &s_ui_renderer_legacy_ops;

/** @brief 鐟滅増鎸告晶鐘诲触鎼达綆浼傞柡瀣煯婵″洭宕愮涵椋庣闁活潿鍔嬬花顒佸緞閺嶎厼鍔ラ柡灞诲劥椤曟鏁嶉崷鎼峱_UiRenderer_GetBackend闁?*/
static volatile App_UiRenderBackend_t s_ui_backend = APP_UI_RENDER_BACKEND_LEGACY;

static uint32_t s_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo)           /* 濞达絽绨肩花顒佺▔鐎ｎ喗顎欓柨娑欐皑濞插潡骞掗妷銊х闁搞儳鍋樼粭鍛存⒔閹邦兘鍋?*/
    {
        return lo;
    }
    if (v > hi)           /* 濡ゅ倹眉缁剚绋夋繝鍥€欓柨娑欐皑濞插潡骞掗妷銊х闁搞儳鍋樼粭鍌炴⒔閹邦兘鍋?*/
    {
        return hi;
    }
    return v;             /* 闁革负鍔忕€垫牠宕堕弶鎴濇暥闁挎稒鑹剧敮顐﹀磹閼规壆绠查柛?*/
}

/**
 * @brief   闁告帒娲﹀畷?UI 婵炴挸寮堕悡瀣触鎼达綆浼傞柨娑樼墢閸ゅ海绮欑€ｎ亞鏆旈柛蹇ｇ厜缁?
 * @details 闁革负鍔嬫径宥夋偩鐏炶棄闅橀柛鎰噹閸ㄥ繘骞戦姀鐘叉瘣闁轰胶澧楃€垫岸鏌﹂崼锝冣偓鍐晬鐎涚 濞寸姾顕ф慨鐔哥▔鐎ｂ晝顏辨繛鍠°倗娈堕柣?render() 闁哄啫澧庨弫鎾诲极閸稈鍋?
 *          鐟滅増鎸告晶鐘崇閸涱喗鏆滈柟?LEGACY 闁告艾娴烽顒勬晬瀹€鍕垫殨闁伙絾鐟﹀﹢顓㈠级閵夛箑鈷栭悘鐐存穿缁辨瑦淇?GPU 闁告梻濞€閳ь剛鍠庨幃妤冪博椤栥倗绀嗛柕?
 * @param   backend  闁烩晩鍠楅悥锝夊触鎼达綆浼傞柡瀣煯婵″洭宕?
 */
void App_UiRenderer_SetBackend(App_UiRenderBackend_t backend)
{
    taskENTER_CRITICAL();  /* 濞戞挸顕弲顐﹀礌閻氬绐楀ǎ鍥ㄧ箚閻﹀骞愰崶顒佸珱闁告帒娲﹀畷鏌ュ储閻旈鎽嶉柟顑秶绀夐梻鍐ㄥ级椤?UI 濞寸姾顕ф慨鐔烘嫚鐠囨彃鐓傞柛妤€锕ら崹蹇涘箲閵忋垹笑闁?*/
    switch (backend)
    {
#if (APP_LVGL_ENABLE != 0u)
        case APP_UI_RENDER_BACKEND_LVGL:
            s_ui_renderer = &s_ui_renderer_lvgl_ops;
            s_ui_backend  = APP_UI_RENDER_BACKEND_LVGL;
            break;
#endif
        case APP_UI_RENDER_BACKEND_LEGACY:  /* 闁告帒娲﹀畷鏌ュ礆?Legacy 閺夌儐鍨▎銏犮€掗崣澶屽帬闁告艾娴烽?*/
        default:                            /* 闁哄牜浜為悡锟犲触鎼达綆浼傚☉鏃傚枎濞叉牠鏌呴埀顒勫礆?Legacy闁挎稑鐗嗛悾銊╁礂閵娿倗绠介幖瀛樻穿缁?*/
            s_ui_renderer = &s_ui_renderer_legacy_ops;   /* 闁哄洤鐡ㄩ弻濠囧箼瀹ュ嫮绋婇悶娑栧妽鐎垫岸鏌?*/
            s_ui_backend  = APP_UI_RENDER_BACKEND_LEGACY; /* 闁哄洤鐡ㄩ弻濠囧几濮橆偄顩柛?*/
            break;
    }
    taskEXIT_CRITICAL();
}

/**
 * @brief   閻犲洩顕цぐ鍥亹閹惧啿顤?UI 婵炴挸寮堕悡瀣触鎼达綆浼傞柡瀣煯婵″洭宕愮涵椋庣缂佹崘娉曢埢鑲┾偓鐟邦槸閸欏繘鏁?
 * @return  鐟滅増鎸告晶鐘诲触鎼达綆浼傞柡瀣煯婵″洭宕?
 */
App_UiRenderBackend_t App_UiRenderer_GetBackend(void)
{
    App_UiRenderBackend_t backend;
    taskENTER_CRITICAL();
    backend = s_ui_backend;
    taskEXIT_CRITICAL();
    return backend;
}

/* -------------------------------------------------------------------------- */
/* Legacy 婵炴挸寮堕悡瀣触鎼达綆浼傞悗鍦仧楠炲洭鏁嶉崼銏＄函闁规亽鍎甸埀顒€绻嬬槐鍫曞礆?App_Display 婵☆垪鈧櫕鍋ラ柨?                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief   Legacy 闁告艾娴烽?is_ready 閻庡湱鍋熼獮?
 * @return  App_Display_IsReady() 闁汇劌瀚换鎴﹀炊閻愭祴鍋撶涵椋庣1=鐎瑰憡褰冮崹鍨叏鐎ｎ亜顕ч悗鐟版湰閸ㄦ岸鏁?=闁哄牜浜滃銊х磼椤忓海绀?
 */
static uint8_t s_ui_legacy_is_ready(void)
{
    return App_Display_IsReady();  /* 闁烩晛鐡ㄧ敮鎾蓟閵夘煈鍤?App_Display 闁告帗绻傞～鎰板礌閺嶎偄笑闁?*/
}

/**
 * @brief   Legacy 闁告艾娴烽?init 閻庡湱鍋熼獮?
 * @details 閻犲鍟伴弫?App_Display_Init() 闁告帗绻傞～鎰板礌?LCD 闁哄啳娉涚花顓㈠Υ娑撱彆DC闁靛棔绀侀幎姘辩磽閹惧啿鏆遍柛鏍ㄤ航閳?
 *          闁?LCD 鐎瑰憡褰冨銊х磼椤忓嫬鐏熷☉鎾规閳规牠骞欏鍕▕闁挎稑婀弍p_Display_Init 闁告劕鎳橀崕鎾嵁閸屾粎鎼煎璺哄閹﹪鏁嶆径鍫氬亾?
 */
static void s_ui_legacy_init(void)
{
    App_LvglUi_SetOverlayEnabled(0u);
    App_Display_Init();  /* 闁告帗绻傞～鎰板礌閺嶃劍鈻旂紒鈧崫鍕勾闁挎稑鑻€垫﹢骞?LCD 濡炵懓宕慨鈺呭Υ娑撱彆DC 闂佹澘绉堕悿鍡涘Υ娴ｅ憡濮庣紓鍌涙尭閸熷灝銆掗崨瀛樼ォ */
}

/**
 * @brief   Legacy 闁告艾娴烽?render 閻庡湱鍋熼獮鍥晬閸儮鍋撹箛搴ｇ倞闁告瑥鍊归弳鐔煎礆?App_Display_Render闁?
 * @param   pos           濠㈠湱澧楃花顔芥媴瀹ュ洨鏋傞柨娑樼墛閺岀喐鎷呭鍫健+闁煎厖绮欓崳娲晬婢舵稓绀夐柣顫妺缁剛绱掑Ο鍝勭厬闁告ぞ绀侀悺褔宕楁径瀣灱
 * @param   vis_frame     SRP 闁绘埈鍘兼慨蹇涘炊閹冭閻熸瑥妫楃€佃尪绠涢銈呭季闁挎稑鐬奸弫銈嗙鎼淬垼顩柡灞炬崄閸庢寮查婊冨姲闁告梹绋戝ù?
 * @param   frame_seq     UI 閻㈩垎鍐闁告瑥鍤栫槐婵囩瑹濞戞瑦鐎悗娑欘殔閸╂盯寮弶鍨€诲Λ鐗堝灴閳ь剚妲掔欢顐ｆ媴鐠恒劍鏆?
 * @param   sai_dma_active  SAI DMA 闁哄嫷鍨伴幆浣该洪弰蹇曗攬闁挎稑鐬奸弫銈嗙鎼淬垺鈻旂紒鈧?"闁哄啰濞€閻撹埖锛愰幋婊€绻嗛柛? 闁圭粯鍔楅妵?
 */
static void s_ui_legacy_render(const Sound_Pos_t *pos,
                               const SRP_VisFrame_t *vis_frame,
                               uint32_t frame_seq,
                               uint8_t sai_dma_active)
{
    /* 闁烩晛鐡ㄧ敮瀛樻姜椤掆偓瑜板倿骞嶉埀顒勫嫉婢跺﹤妫橀柡浣规緲閸?App_Display 婵炴挸寮堕悡瀣熼垾铏仴闁挎稑鏈Λ銈嗭紣濠靛棭妯嗗璺哄閹?*/
    App_CameraFrame_t camera_frame = {0};

    (void)App_Camera_AcquireLatestFrame(&camera_frame);
    App_Display_Render(pos, vis_frame, &camera_frame, frame_seq, sai_dma_active);
    App_Camera_ReleaseFrame(&camera_frame);
}

#if (APP_LVGL_ENABLE != 0u)
static uint8_t s_ui_lvgl_is_ready(void)
{
    return s_ui_lvgl_ready;
}

static void s_ui_lvgl_init(void)
{
    if (App_Display_IsReady() == 0u)
    {
        return;
    }

    if (s_ui_lvgl_core_inited == 0u)
    {
        lv_init();
        s_ui_lvgl_core_inited = 1u;
    }

    if (s_ui_lvgl_ports_inited == 0u)
    {
        lv_port_disp_init();
        lv_port_indev_init();
        s_ui_lvgl_ports_inited = 1u;
    }
    else
    {
        lv_port_disp_reconfigure();
    }

    s_ui_lvgl_last_tick = xTaskGetTickCount();
    s_ui_lvgl_ready = 1u;
    App_LvglUi_Init();
    App_LvglUi_SetOverlayEnabled(1u);
}

static void s_ui_lvgl_render(const Sound_Pos_t *pos,
                             const SRP_VisFrame_t *vis_frame,
                             uint32_t frame_seq,
                             uint8_t sai_dma_active)
{
    App_CameraFrame_t camera_frame = {0};
    TickType_t now;
    uint32_t delta_ms;

    if (s_ui_lvgl_ready == 0u)
    {
        return;
    }

    now = xTaskGetTickCount();
    delta_ms = (uint32_t)(now - s_ui_lvgl_last_tick) * (uint32_t)portTICK_PERIOD_MS;
    s_ui_lvgl_last_tick = now;

    if (delta_ms != 0u)
    {
        /* Feed LVGL directly from the RTOS tick so no extra timer is needed. */
        lv_tick_inc(delta_ms);
    }

    App_LvglUi_Process();
    (void)lv_timer_handler();

    (void)App_Camera_AcquireLatestFrame(&camera_frame);
    App_Display_Render(pos, vis_frame, &camera_frame, frame_seq, sai_dma_active);
    App_Camera_ReleaseFrame(&camera_frame);
}
#endif

/**
 * @brief   閻犱緤绱曢悾?UI 濞寸姾顕ф慨鐔兼儍閸曨剝顩柡灞炬尭閹冲棝寮甸悤鍌滅FreeRTOS ticks闁?
 * @details 閻忓繐妫涘ú浼村冀閸パ勫闁绘粌娲﹀畷鑼不濡炴崘绀?vTaskDelayUntil 闁汇劌瀚幊鍡涘嫉?ticks闁?
 *            period_ms = round(1000 / fps)  闁挎稑鐗嗗ú鎾绘嚋瀹ュ嫮瀹夐柛蹇嬪劵缁变即宕?fps/2 闁告劕绉瑰▍搴㈢?fps闁?
 *            ticks = pdMS_TO_TICKS(period_ms)
 *
 *          缂佲偓鏉炴壆浼愰柨娑欑摬ps=20 -> period_ms=50ms -> 50 ticks (1ms/tick)
 *                fps=30 -> period_ms=33ms -> 33 ticks
 *                fps=5  -> period_ms=200ms -> 200 ticks
 *
 *          濞ｅ洦绻嗛惁?ticks >= 1闁挎稑鐭傚Σ璇差潰?vTaskDelayUntil(0) 閻庝絻澹堥崵褎绂掔拠鎻掝潳濞戞挸绉烽鈧柛?CPU闁?
 *
 * @return  婵炴挸寮堕悡瀣川閵婏附鍩傞柨娑樿嫰瀹曠喐鎷呭蹇曠獥FreeRTOS ticks
 */
static uint32_t s_ui_period_ticks(void)
{
#if (APP_LVGL_ENABLE != 0u)
    if (App_UiRenderer_GetBackend() == APP_UI_RENDER_BACKEND_LVGL)
    {
        TickType_t lvgl_ticks = pdMS_TO_TICKS(APP_LVGL_HANDLER_PERIOD_MS);

        if (lvgl_ticks == 0u)
        {
            lvgl_ticks = 1u;
        }
        return (uint32_t)lvgl_ticks;
    }
#endif

    /* 閻犲洩顕цぐ鍥儎椤旂晫鍨奸悽顖嗗懎鑺虫鐐茬埣閹稿憡鎷呭蹇曠闂傚啫寮堕娑欐交閹邦垼鏀介柡鍐ㄧ埣閸樸倗绱旈娆炬蕉閻犱礁澧介悿鍡涘礆閹峰苯鐦遍柛銉︽綑椤?*/
    uint32_t fps = s_clamp_u32(App_RuntimeConfig_GetUiTargetFps(), UI_FPS_MIN, UI_FPS_MAX);

    /* 闁搞儲绋栭崹妤佺閺傚灝寮抽柟骞垮灮閻ｅ鏁嶅顒€顫?fps/2 闁告劕绉甸弳锝夋⒔閵堝繒绀夐柣鈺冾焾缂嶅绂嶆惔鈽嗗殸 1000/fps 闁稿鑹惧ú鎾绘嚋瀹ュ嫮瀹夐柛?*/
    uint32_t period_ms = (1000u + (fps / 2u)) / fps;

    TickType_t ticks = pdMS_TO_TICKS(period_ms);  /* 婵綆鍋嗛～妤佹姜?FreeRTOS ticks */

    if (ticks == 0u)    /* 闁哄鑳堕顒勫箚閸涱厼鏋屽ǎ鍥ㄧ箖婵垽鏁嶉崸顪祌tTICK_PERIOD_MS > period_ms 闁哄啳娉涜ぐ鏌ユ嚄閹存帟绀?0闁?*/
    {
        ticks = 1u;     /* 闁煎嘲鍟块惃顖氼嚈閹壆绠?1 濞?tick闁挎稑濂旂换姘辨嫚娴ｉ攱宕查柛鏂衡偓鍐叉瘔閻?CPU */
    }
    return (uint32_t)ticks;
}

#define ui_cli_poll App_UiCli_Poll

/**
 * @brief   UI 闁哄嫬澧介妵姘鐠囨彃顫?
 * @details 閺夌儐鍠涢妤佹媴瀹ュ洨鏋傞梻鍐枎閸亪鐛捄鍝勭厱闁哄倻澧楀Ο澶岀矆閻氬绐楅柛娆愮墬濞撳爼寮０浣虹Т缂?-> 闊浂鍋嗛崣?SRP 闁告瑯鍨甸～瀣礌閺嶃劍娈堕柟?-> 婵炴挸寮堕悡瀣綇閹惧啿姣夐柕?
 *
 * 闁稿繑濞婇弫顓㈡倷閻у摜绐?
 * - 闁哄嫬澧介妵姘跺嫉椤忓嫭鐨戠紓渚囦簼濡炲倿骞?`UI_RETRY_INIT_MS` 闁告稏鍔嶅﹢锟犳煂瀹ュ牏妲搁柛鎺撶箓椤劙宕犻弽銉㈠亾?
 * - 婵絽绻愰幎姘閸涱剙鈻忛柣顫姂濡诧箓宕氬鎹愬幀闁汇劌瀚〒鍫曞触鎼存繄顏遍柡澶嗏偓鑼Т缂傚喚鍠楅弳鐔煎箲椤曞棛绀夐梺顒€鐏濋崢?UI 闁割偄妫涜ⅶ闁?
 * - 闁革负鍔嬫径宥夋偩鐏炶棄闅橀柛鎰噹椤︽煡宕?SRP 闁告瑯鍨甸～瀣礌閺嵮勫渐闁绘挆宥囩闂侇剙鐏濋崢銈囨嫚鐠囨彃鏅哥紒鏃傚仒缁ㄣ倝濡?
 *
 * @param   pvParameters  FreeRTOS 濞寸姾顕ф慨鐔煎矗閸屾稒娈堕柨娑樼墛濠€顓熸媴鐠恒劍鏆忛柨?
 */
void UI_Display_Task(void *pvParameters)
{
    (void)pvParameters;  /* 濞寸姾顕ф慨鐔煎矗閸屾稒娈堕柡鍫簷婵炲洭鎮介…鎺旂婵炴垵鐗撳▍搴ｆ媰閿曗偓閹?*/

    /* ---- 濞寸姾顕ф慨鐔轰沪閳ь剟鏌堥妸褍笑闁诡兛绀佽ぐ澶愭煂?---- */

    /** @brief 鐟滅増鎸婚鍏肩鎼淬劍袝闁告帗顨呰ぐ鍥礆閹殿喗鐣卞鍦缁喗鎷呭鍥╂瀭闁挎稑鐗嗚ぐ鏌ユ嚄閸婄噥娼跺鑸电椤愯偐鎷犵拠鎻掔悼閻熸洖妫涘ú濠囨晬鐏炶棄绲块柡鍫氬亾闁哄倹婢橀埀顒傘€嬬槐?*/
    Sound_Pos_t draw_pos = {0.0f, 0.0f, 0.0f};

    /** @brief 闁哄牃鍋撻柛姘凹缁旀潙鈻庨埄鍐ㄧ亣闁告梻鍠嶇划鐘绘⒓閻斿嘲鐏欓悹鍥嚙閸╁矂鎯冮崟顐矗婵犙勫姃缂嶅懐绱旈鍡欑闂傚啰鍠庨崹顏呯▔閾忓厜鏁勯柡鍐硾椤︽煡鎮介妸锔诲妰闁稿﹨鍋愰幋椋庣磼椤撶喕顩柡灞炬惈缁?*/
    Sound_Pos_t last_pos = {0.0f, 0.0f, 0.0f};

    /** @brief SRP 闁绘埈鍘兼慨蹇涘炊閹冭閻熸瑥妫楃€佃尪绠涢銈呭季闁挎稑鐗呯划鐘绘閹剁瓔鏆ュù鐘侯嚙婵喐绋夌€靛憡娅曢柛鏍劋鐎氬湱鎷瑰┑鎾剁闂侇剙鐏濋崢銈囨嫚鐠囨彃鏅哥紒鏃傚仒缁ㄣ倝鏁?*/
    SRP_VisFrame_t vis_snapshot = {0};

    /** @brief UI 閻㈩垎鍐闁告瑥鍤栫槐婵喰掕箛鏃囶洬闁哄本鎸风粩瀵告暜瑜旈埀顒佸笒椤ゅ啴鏁嶇仦鑲╄繑闁哄倸娲ら悺褔宕氶柨瀣厐闁告帒妫濋。鍫曟焻閺勫繒甯嗗ù锝堟硶閺?*/
    uint32_t ui_frame_seq = 0u;

    /** @brief 濞戞挸锕ラ鑲╂嫚鐠囨彃绲?g_audio_frame_seq_isr 闁哄啫澧庡▓鎴﹀磹绾绀夐柣顫妺缁剙螞閳ь剙霉?SAI DMA 闁哄嫷鍨伴幆浣圭瀹ュ懏韬弶鈺傚姌椤?*/
    uint32_t last_audio_isr_seq = 0u;

    /** @brief 閺夆晝鍋熼悽缁樺緞濮橆剚濮?SAI DMA 閹兼潙绻愯ぐ鍧楀嫉椤忓嫬缍侀柛鏍ㄧ墱濞堟垹鏁閺嗙喖鏁嶉崼锝嗗涧闁告帊鍗冲Σ鍥磹閻撳孩鍊甸悹浣靛€撶拹鐔煎籍閻樿崵鍙惧Λ鐗堝灱缁额參宕楅妷顖滅 */
    uint8_t audio_idle_frames = 0xFFu;  /* 闁告帗绻傞～鎰板礌閺嶏箒绀嬮柡鍫氬亾濠㈠爢鍐ｅ亾绾绀夐悷娆欑畱瑜板倹锛冮弽褎濮庨柛姘捣閻濇盯宕￠崘鑼Ш闂?*/

    /** @brief vTaskDelayUntil 闁汇劌瀚划椋庘偓鐢垫嚀閺佹粓鏌嬮幒鎾搭槯闁告帊绱槐娆愮┍濠靛﹦妲堥悽顖嗗啯鍣柡鍫㈠枔鑿欓悗瑙勭啲缁辨繃绋夊鍛秬婵炴挸寮堕悡瀣嚀濡や焦顦х憸鏉垮船閹肩兘鏁?*/
    TickType_t next_render_wake;

    /** @brief 濞戞挸锕ラ鑲╀焊濠靛﹦妲搁柛鎺撶箓椤劙宕犻弽銊︹枖缂佲偓閸濆嫮婀撮柣?tick 闁稿﹦銆嬬槐娆撴⒔閹邦剙鐓戦梺鎻掔Х閻︻垱锛愰幋鐘茶姵闁?*/
    TickType_t last_init_try = 0u;

    /** @brief 濞戞挸锕ラ鑲╂媼閺夎法绉块柣?DMA2D 閻℃帒鎳忓鍌滄媼閳╁啯娈堕柨娑樼灱閺併倖绂嶆惔鈥崇秮闁告牗鐗楅ˉ鍛圭€ｅ墎绀勫ù鐘叉噹濠€?UI_DEBUG_LOG 婵☆垪鈧磭纭€闁瑰灚鎸稿畵鍐晬?*/
    uint32_t last_dma2d_timeout = 0u;
    App_UiRenderBackend_t current_backend = APP_UI_RENDER_BACKEND_LEGACY;
    App_UiRenderBackend_t last_backend = App_UiRenderer_GetBackend();

    /* ---- 濞寸姾顕ф慨鐔煎触椤栨艾袟闁哄啫鐖奸々璇测枎閳ュ磭姣﹂悹鍥ㄦ礀閸ㄥ灚鎱ㄧ€ｎ亜顕ч柡鍕⒔閵囨氨浠?---- */
    /* 闁?LCD 閻忓繑纰嶅﹢顓犱焊鏉堚晛宕曢柨娑樼墕椤┭囧籍鐠鸿櫣纰嶉柛鎺撶箓椤劙宕犻弽銊﹀紦閻庣懓鏈崹姘舵晬婢舵稓绀夐柛蹇撶墕閻ㄥ墽鎷犻弴鐐茬仴濠殿喖顑呯€?*/
    if ((s_ui_renderer != NULL) && (s_ui_renderer->is_ready() == 0u))
    {
        s_ui_renderer->init();  /* 闁告帗绻傞～鎰板礌?LCD 濡炵懓宕慨鈺呭Υ娑撱彆DC 闂佹澘绉堕悿鍡涘Υ娴ｅ湱顏搁梻鍡氭硾閹舵氨绱撻幘鍐叉毐 */
    }
    last_init_try    = xTaskGetTickCount();  /* 閻犱焦婢樼紞宥夊礆濠靛棭娼楅柛鏍ㄧ墪閻ㄥ墽鎷犻弴鐔割槯闁?*/
    next_render_wake = last_init_try;        /* 缂佹鍏涚粩瀵告暜瑜忛悵娑㈠础閾忣偉顩柡?*/

    /* ================================================================
     * 濞寸姾顕ф慨鐔哥▔鐠囧弶鍎曢柣婊庡灲缁辨瑥顫濋梹鎰憹闂侇偀鍋撻柛鎴犲皑缁?
     * ================================================================ */
    for (;;)
    {
        uint32_t t_loop;       /* 闁哄牜鍓欓幎姘嚗椤忓棗绠氶柡浣虹節缂嶅鎳撳Δ浣诡槯閻犱讲鍓濆鍌滄導妞嬪骸浠?*/
        uint8_t  sai_dma_active; /* SAI DMA 婵炲弶妲掔粚顒勫冀閸パ呯闁挎稑鐗呯槐鍓佺磼濞嗘劘顩柡灞炬尭濞呮帡寮伴崜褋浠涢梻濠冨▕椤ｅ爼鎮╅懜纰樺亾娓氬﹦绀?*/

        /* ---- 婵縿鍎甸?1闁挎稒鑹鹃ˇ鈺呮偠?CLI 閺夊牊鎸搁崣鍡涙晬閸︾潪RT 闁告稒鍨濋幎銈囨偘瀹€瀣 ---- */
        /* 婵絽绻愰幎姘舵焾閸婄喓鏋傞悹?CLI闁挎稑濂旂换姘辨嫚娴ｇ柉顩柛娆欑到閹斥剝绂掗妶鍛儥閹煎瓨鏌ㄥ▎銏℃交?<= 1 閻㈩垎鍐╁櫙闁哄牏鍣︾槐娆撳焿?0ms@20fps闁?*/
        ui_cli_poll();
        App_Touch_Poll();

        current_backend = App_UiRenderer_GetBackend();

        if (current_backend != last_backend)
        {
            last_backend = current_backend;
#if (APP_LVGL_ENABLE != 0u)
            if ((current_backend == APP_UI_RENDER_BACKEND_LVGL) && (s_ui_renderer != NULL))
            {
                s_ui_renderer->init();
            }
            else
#endif
            if ((s_ui_renderer != NULL) && (s_ui_renderer->is_ready() == 0u))
            {
                s_ui_renderer->init();
            }
            last_init_try = xTaskGetTickCount();
        }

        /* ---- 婵縿鍎甸?2闁挎稒纰嶅Ο澶岀矆閸濆嫬鐏ュ┑顔碱儏鐎垫煡鏌屽鍫㈡Ц闂侇偅妲掔欢?---- */
        /* 闁兼眹鍎插Ο澶岀矆閸濆嫮婀撮柡鍫簻濮樸劎绱掗搴ｇ濠?LCD 闁告帗绻傞～鎰板礌閺嵮佷杭閻犳劑鍎荤槐姘舵晬鐏炲墽妲ㄩ梻?UI_RETRY_INIT_MS(1000ms) 闂佹彃绉烽惁顖涚▔閳ь剙鈻?*/
        if ((s_ui_renderer != NULL) && (s_ui_renderer->is_ready() == 0u))
        {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_init_try) >= pdMS_TO_TICKS(UI_RETRY_INIT_MS))
            {
#if UI_DEBUG_LOG
                /* 閻犲鍟抽惁顖毼熼垾宕囩濞戞挸顑嗘晶锕傚础閺夊灝鐏ュ┑顔碱儏鐎靛弶寰勬潏顐バ曢柡鍐硾閹洤螣閳ヨ櫕鍋ラ柣銊ュ濡礁鈻撻棃娑掑亾绾绀夐弶鍫濇噹婵亞鈧鐭紞鍛兜椤戞寧顐介梻鍌ゅ櫍椤?*/
                printf("UI: retry init (app=0x%08lX err=%lu lcd=%lu ltdc=%lu)\r\n",
                       (unsigned long)g_display_init_stage,   /* App_Display 闁告帗绻傞～鎰板礌閺嶎厽鈻夋繛?*/
                       (unsigned long)g_display_init_error,   /* App_Display 闂佹寧鐟ㄩ銈夋儘?*/
                       (unsigned long)g_lcd_init_stage,       /* LCD 濡炵懓宕慨鈺呭礆濠靛棭娼楅柛鏍ㄧ墵濡礁鈻?*/
                       (unsigned long)g_ltdc_init_stage);     /* LTDC 闁硅矇鍐ㄧ厬闁革絻鍔岄崹鍨叏鐎ｎ亜顕ч梻鍐煐椤?*/
#endif
                s_ui_renderer->init();   /* 闂佹彃绉烽惁顖炲礆濠靛棭娼楅柛?*/
                last_init_try = now;     /* 闁哄洤鐡ㄩ弻濠囨煂瀹ュ牏妲搁柡鍐硾閸?*/
            }
            taskYIELD();  /* 闁告垿缂氶鈧?CPU闁挎稑濂旂粭澶屾啺娴ｈ　鏁勯弶鐑嗗墰閻℃垵顕ラ崪鍐閻犱讲鏅犻悡鑸碉紣閹存粍宕查柛鏂哄墲濠€渚€寮垫潪鎵獥閺夆晜鍔橀、?*/
            continue;     /* 闁搞儳鍋涢崺灞筋嚗椤忓棗绠氬銈呯埣閸庢挳鏌屽鍡樼厐闁告帇鍊栭弻?*/
        }

        /* ---- 婵縿鍎甸?3闁挎稒纰嶉埀顑棗鍘寸紓浣哄枙椤撴悂鏌呴幒鎴澔濞戞挸閰ｉ埀顒傚枔瀹稿ジ骞嶉幘鍐茬オ ---- */
        App_Perf_CountUiLoop();         /* UI 鐎甸偊浜為獮鍡欐媼閳╁啯娈?+1闁挎稑鐗忛弫銈嗙?perf rate 闂侇偆鍠撳鑲╂媼閿涘嫮鏆柨?*/
        App_Perf_MaybePrintRates();     /* 闁兼眹鍎撮幓顏堝礆閻楀牆鈪甸柛妤佹緲閹冲棝寮甸悤鍌滅1s闁挎稑顧€缁辨繃娼忛幘鍐叉瘔闂侇偆鍠撳鑲╃磼閻旀椿鍚€ */
        t_loop = App_Perf_BeginCycles(); /* 閻犱焦婢樼紞宥夊嫉椤掆偓閹舵艾顕ラ鍡楃畾鐎殿喒鍋撳┑顔碱儐濡炲倿宕?*/
        sai_dma_active = 0u;

        /* ---- 婵縿鍎甸?4闁挎稒纰嶇粔鐑芥嚀濡炲墽绉寸紓鍐惧櫍濡诧箓宕氬Δ瀣闁告瑦鐗楀〒鍫曞棘閺夊尅绱ｆ繝褎鍔掔紞鍛磾?---- */
        /* 濞达綀娉曢弫?timeout=0闁挎稑鐗撳顏堟⒓鐠囧樊鏁氶柨娑橆檧缁辨繈鎳熼妷锔斤骏闁哄倻澧楅弳鐔煎箲椤旂厧鐏熷璺虹Ф閺?last_pos 缂備綀鍛暰婵炴挸寮堕悡瀣晬閸粎绠介柟闀愮閹舵岸鎮抽崶鈹ｆ梻鈧鐔槐?*/
        {
            /* LVGL overlays the legacy scene, so the scene data still needs to update. */
            if (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
            {
                last_pos = draw_pos;           /* 濞ｅ洦绻傞悺銊╁嫉閳ь剟寮０浣虹Т缂?*/
                g_ui_queue_rx_count++;         /* 缂備胶鍠曢鎼佸箣閹邦剙顫犻柟鎭掑劜閺佺懓鈻庨埄鍐╂ */

                /* 闁兼眹鍎靛Σ锕傚礆濡炴崘鍘弶鈺偵戝﹢渚€寮寸€涙ɑ鐓€闁汇劌瀚紞鍛磾椤曞棛绀勯柡瀣╃閻垶骞嗛崨顓炴瀸闁挎稑顧€缁辨繄绱掕閻㈣鈽夐崼锝傚亾濡ゅ啯绾柛鎺斿缁旇崵绮氶悮瀵哥闁告瑦鐗楀〒鍫曞触鎼存繄顏卞☉?*/
                while (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
                {
                    last_pos = draw_pos;       /* 闁归晲鑳堕悽鑽ゆ啺閸℃瑦纾伴柨娑樺缁绘氨鎷犳担绋跨悼闁告帞澧楀〒鍫曞棘閹殿喗鐣?*/
                    g_ui_queue_rx_count++;
                }
            }
            else
            {
                /* 闂傚啰鍠庨崹顏呯▔閾忓厜鏁勯柨娑樼墦閻撹埖锛愰幋婊勫床闁告柡鈧磭姣ｉ柡鍫簷妤犲洭鎮介悢鍛婄厐缂備焦鎸婚悘澶愭晬婢舵稓绀夊璺虹Ф閺?last_pos 濞戞挸绉靛ú鍧楀棘?*/
                g_ui_queue_timeout_count++;  /* 缂備胶鍠曢鎼佸籍閻樿櫕鐓€闁轰胶澧楀畵浣烘暜瑜庨弳鐔兼晬閸噥鍔€閻㈩垰鎽滈獮鍥╂寬閳藉懐绐楃憸?UI FPS > 闂傚﹥濞婇。鍓佹暜瑜忓?/ decim 闁哄啳顔愮槐?*/
            }
        }

        ui_frame_seq++;  /* 闂侇偅甯掗·?UI 閻㈩垎鍐闁告瑥鍤栫槐娆愬閻樼數鑸舵繛鎾冲级閻撳宕抽妸褎鏆忓ù婊冮閸ㄥ孩锛愰幋婵嗙厱闁哄倻澧楅弸鍐偓娑欘殘閻℃垿宕楅崘顏嗩槺闁?*/

        /* ---- 婵縿鍎甸?5闁挎稒纰嶉ˉ鍛?SAI DMA 婵炲弶妲掔粚顒勫箑?---- */
        /* 闂侇偅淇虹换鍐儎閹寸偟銈?g_audio_frame_seq_isr 闁哄嫷鍨伴幆渚€寮垫径濠傜秮闁告牗鐗楀鐢稿礆閵堝棙鐒?SAI DMA 闁哄嫷鍨伴幆渚€宕烽妸銊х閻?*/
        {
            uint32_t audio_seq = g_audio_frame_seq_isr;  /* 閻犲洩顕цぐ?ISR 閻㈩垎鍐闁告瑥鍤栫槐妾漮latile闁?*/
            if (audio_seq != last_audio_isr_seq)          /* 閹兼潙绻愯ぐ鍧楀嫉婢跺﹤缍侀柛鏍ㄧ壄缁辩檳MA 濞寸姴绉村﹢顏勵啅閵夈倗绋?*/
            {
                last_audio_isr_seq = audio_seq;  /* 闁哄洤鐡ㄩ弻濠囧春閸濆嫬娅欓柛?*/
                audio_idle_frames  = 0u;          /* 闂佹彃绉堕悿鍡欑矚濞差亝锛濋悹浣插墲閺嗙喖鏁嶉崼锝冣偓鍐矆?DMA 婵炲弶妲掔粚顒勬晬?*/
            }
            else if (audio_idle_frames < 0xFFu)   /* 閹兼潙绻愯ぐ鍧楀嫉椤忓嫬缍侀柨娑欘儚MA 闁告瑯鍨甸崗姗€宕戝鍕靛壘闁挎稑鐬奸悿顔剧矓椤栨壕鏁勯梻鍌涘絻閹舵岸寮?*/
            {
                audio_idle_frames++;  /* 濡ゆ宕幏鎵媼閳╁啯娈堕柨娑樺缁楀鈹冮姀鐘叉瘔 */
            }
            /* 闁兼眹鍎寸换娑氱磼椤撶姭鏁勯梻鍌涘絻閹舵岸寮?<= 闂傚啫鐗嗛埀顒傘€嬬槐婵堟媼閵堝嫯绀?SAI DMA 婵炲弶妲掔粚顒勬晬濞戞粎孝閺夆晛娲Σ鍥磹閼归偊鍚囧☉鎾跺劋濡倝妫呴幎绛嬫殽閺夊牊鎸搁崣?*/
            sai_dma_active = (audio_idle_frames <= APP_DISPLAY_SAI_ACTIVE_HOLD_FRAMES) ? 1u : 0u;
        }

        /* ---- 婵縿鍎甸?6闁挎稒鑹惧﹢顏呯▔鐎靛憡娅曢柛鏍ф惈閸炲瓨寰勫鍛厬 SRP 闁告瑯鍨甸～瀣礌閺嵮勫渐闁?---- */
        /* SRP 闁告瑯鍨甸～瀣礌閺嶃劍娈堕柟璇″櫙缁辨瑩鎮滈鐐差潝闁搞儳鍋撻弳鐔煎箲椤曞棛绀嗛柣銏や憾閻撹埖锛愰幋婊勫床闁告柡鈧啿鏅搁柛蹇嬪劵缁辨紛I 濞寸姾顕ф慨鐔烘嫚鐠囨彃绲块柨娑樿嫰閻°劑宕烽妸褏褰藉ù婊冾槶閳?
         * 濞达綀娉曢弫銈嗙▔鐎靛憡娅曢柛鏍〃缁绘岸骞庨妶鍜佹Щ闁告帟鍩栭幖閿嬫媴濠婃劗绀夐梻鍐ㄥ级椤掓稓鎷犵拠鎻掔厒濞戞搩鍙冨Λ鍧楁偐閼哥鍋撴笟濠勭闁告锕ョ€氬湱鎷瑰┑鍫熺暠闁轰胶澧楀畵渚€鏁嶆径鍫氬亾?
         * 濠㈣泛绉撮崺妤呭触鎼粹剝褰ラ柣鎾楀倻鐟㈤梻濠冨▕椤ｈ埖绂掔拠鎻掝潳閻熸瑱缍€閳ь剨璁ｇ槐婵嗐€掗崣澶屽帬闁哄牏鍠栧Λ鎸庣▔瀹ュ懎鏅欓梻鍥ｅ亾閻熸洑鐒︾€垫棃寮垫径澶婎槻闁伙絽鑻亸顖炲Υ?*/
        {
            uint32_t t_sec = App_Perf_BeginCycles();
            (void)AI_SRP_GetLatestVisualizationFrame(&vis_snapshot);
            App_Perf_EndCycles(APP_PERF_SEC_UI_SNAPSHOT, t_sec);
        }

        /* ---- 婵縿鍎甸?7闁挎稒淇洪惃鐔兼偨閵婏箒顩柡灞炬尭閹绮╅娑樷挃閻炴稑濂旂粩瀵告暜瑜庣憰鍡涘蓟?---- */
        /* 婵炴挸寮堕悡瀣规担琛℃煠闁挎稑鐗嗗﹢?App_Display_Render 闁告劕鎳橀崕鎾晬婢舵稓绐?
         *   鐟滅増甯婄粩鎾礌閺嶎偄鍔伴柛鏃€绋戝ù?-> colormap 闁哄嫮濮撮惃?-> 闁告瑥鐬奸崵搴ㄥ箑?闁哄牃鍋撻弶鈺傚灴閸嬶箓骞撻幒鎴斿亾?-> 妤犵偠娅曠划?->
         *   缂備焦锚閸╂宕℃担鍝ユ憻闁稿繐顦伴悥?-> 缂備焦锚閸╂宕搁幇顓犲灱閺?-> 閻熸洖妫涘ú濠囧棘閸パ呮憻 -> DMA2D blit 闁?LCD */
        {
            uint32_t t_sec = App_Perf_BeginCycles();
            s_ui_renderer->render(&last_pos,       /* 濠㈠湱澧楃花顔芥媴瀹ュ洨鏋?*/
                                  &vis_snapshot,   /* SRP 闁绘埈鍘兼慨蹇涘炊閹勫渐闁?*/
                                  ui_frame_seq,    /* 閻㈩垎鍐闁告瑥鍤栫槐娆撳棘閸パ呮憻闁告帒妫濋。鍫曟偨椤帞绀?*/
                                  sai_dma_active); /* SAI DMA 闁绘鍩栭埀?*/
            App_Perf_EndCycles(APP_PERF_SEC_UI_RENDER, t_sec);
        }
        g_ui_render_count++;  /* 婵炴挸寮堕悡瀣暜瑜戦鎼佸极?+1闁挎稑鐗嗚ぐ鏌ユ焻濮樺磭绠栭悹瀣暢閻︻垶宕抽妸顭戞綆閻?UI 閻庡湱鍋ゅ顖涙交閹邦垼鏀介悽顖嗗懎鑺抽柨?*/

        /* ---- 婵縿鍎甸?8闁挎稒顑廙A2D 閻℃帒鎳忓鍌炲矗濡搫顕ф俊顐熷亾婵炴潙顑戠槐娆撳矗椤栫偐鍋撴径搴ｆ閻犲洦娲樺Λ鈺勭疀濡ゅ绀?---- */
        if (g_ltdc_dma2d_timeout_count != last_dma2d_timeout)  /* 閻℃帒鎳忓鍌滄媼閳╁啯娈堕柡鍫濐槸椤ゅ啴宕?*/
        {
#if UI_DEBUG_LOG
            /* 闁瑰灚鎸稿畵鍐惥閸涱喗顦уǎ鍥ｅ墲娴煎懘鏁嶇仦钘夌盎闁告柡鏅為惁鏍棘?DMA2D 缁绢収鍏涘▎銏ゅ箣閺嶃劍顦ч幖鏉戠箻濡埖锛?*/
            printf("UI: DMA2D timeout=%lu panel=0x%04X\r\n",
                   (unsigned long)g_ltdc_dma2d_timeout_count,  /* 闁哄牃鍋撻柡鍌涘缁夋挳寮幆閭﹀悁闁?*/
                   (unsigned int)g_ltdc_panel_id);              /* LCD 闂傚牄鍨哄?ID */
#endif
            last_dma2d_timeout = g_ltdc_dma2d_timeout_count;  /* 闁哄洤鐡ㄩ弻濠囧春閸濆嫬娅欓柛?*/
        }

        /* ---- 婵縿鍎甸?9闁挎稒姘ㄧ划銊╁级閻斿憡鎷遍悽顖嗗棭鍚€闁哄啳娉涢懟鐔虹驳婢跺﹦绐″☉鎾愁儎缁斿鏁閺佹粓鏌嬮幒鎾搭槯闁?---- */
        App_Perf_EndCycles(APP_PERF_SEC_UI_LOOP, t_loop);  /* 閻犱焦婢樼紞宥夊嫉椤掆偓閹舵岸骞€閺勫繆鍋撳Δ浣诡槯 */

        /* vTaskDelayUntil闁挎稒姘ㄧ划椋庘偓浣冾潐濡炲倿姊婚弶鎴烆偨閺夆晝鍣︾槐婵嬫嚊椤忓嫬袟閻炴稏鍎辨导鈺併€掗崣澶屽帬闁肩増顨嗗鍌炴晬鐏炶偐绠介悹鍥︾閹舵岸宕ㄩ妸锔藉焸闁诡厽甯掗悾楣冨Υ?
         * 闁兼眹鍎茬憰鍡涘蓟閹惧瓨顦ч梻鍌滎棎缁夊瓨娼婚崶锔绢伇濞戞搩浜滈幎姘跺川閵婏附鍩傞柨娑橆唵ext_render_wake 濞村吋淇洪～锕傚箳閵娿劎绠诲ù鐘劦娴尖晠宕楀鍫㈩槹鐎点倖鍎肩换婊堝Υ?
         * 闁烩晛鎲￠惁?vTaskDelay闁挎稑鐗忓ù澶屸偓鐢垫嚀濞嗐垺娼婚悤鍌滅闁挎稑鑻ぐ鏌ュ嫉婢跺娅忛梻鍐ㄥ级椤掓稓鏁瀹稿ジ姊捐箛姘鳖槹閺夌偠濮ょ亸婵嬪礉閵娿劉鍋撶仦鍓х＝缂佸鐪归埀?*/
        vTaskDelayUntil(&next_render_wake, (TickType_t)s_ui_period_ticks());
    }
}

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
#include "app_task_cfg.h"
#include "app_ui_cli.h"
#include "LCD/ltdc.h"

#include <stdio.h>

/**
 * @brief UI 濞撳弶鐓嬮崥搴ｎ伂閹垮秳缍旂悰顭掔礄閾忔艾鍤遍弫鎷屻€冮�?
 *
 * 閸栧懎鎯堟稉澶夐嚋閸戣姤鏆熼幐鍥嫛閿涘苯鍨庨崚顐㈩嚠鎼存棑�?
 *  - is_ready : 閺屻儴顕楅弰鍓с仛鐏炲倹妲搁崥锕€鍑＄€瑰本鍨氶崚婵嗩潗閸?
 *  - init     : 閹笛嗩攽閺勫墽銇氱仦鍌氬灥婵瀵查敍鍫濈畵缁涘绱濋崣顖炲櫢婢跺秷鐨熼悽顭掔�?
 *  - render   : 閹笛嗩攽娑撯偓鐢勮閺屾挸鑻熼幓鎰唉�?LCD
 *
 * 闁俺绻冮崙鑺ユ殶閹稿洭鎷￠懓宀勬姜閻╁瓨甯寸拫鍐暏閿涘本婀弶銉ュ讲閺冪姷绱抽崚鍥ㄥ床閸掗绗夐崥灞捐閺屾挸鐤勯悳甯礄�?GPU 閸旂娀鈧喎鎮楃粩顖ょ礆閵?
 */
typedef struct
{
    uint8_t (*is_ready)(void);          /**< 鏉╂柨娲?1 鐞涖劎銇氶弰鍓с仛绾兛娆㈠鎻掓皑缂侇亷绱濋崣顖欎簰瀵偓婵瑕嗛�?*/
    void    (*init)(void);              /**< 閸掓繂顫愰崠鏍ㄦ▔缁€鍝勭湴閿涙瓈CD 閺冭泛绨妴涓㏕DC 闁板秶鐤嗛妴浣告姎缂傛挸鍟垮〒鍛存祩 */
    void    (*render)(const Sound_Pos_t   *pos,        /**< 婢圭増绨担宥囩枂閿涘牊鏌熸担宥堫潡+閼充粙鍣洪敍?*/
                      const SRP_VisFrame_t *vis_frame, /**< SRP 閻戭厼濮忛崶鎯у讲鐟欏棗瀵茶箛顐ゅ弾 */
                      uint32_t             frame_seq,  /**< UI 鐢冪碍閸欏嚖绱濋悽銊ょ艾閺傚洤鐡ч崚閿嬫煀閸掑棝顣?*/
                      uint8_t              sai_dma_active); /**< SAI DMA 濞叉槒绌弽鍥х箶 */
} App_UiRendererOps_t;

/* -------------------------------------------------------------------------- */
/* 閸撳秴鎮滄竟鐗堟閿涙瓈egacy 濞撳弶鐓嬮崥搴ｎ伂閻ㄥ嫪绗佹稉顏勭杽閻滄澘鍤遍弫甯礄鐎规矮绠熼崷銊ょ瑓閺傜櫢绱?                        */
/* -------------------------------------------------------------------------- */
static uint8_t s_ui_legacy_is_ready(void);   /**< 閺屻儴顕?App_Display 閺勵垰鎯佺亸杈╁�?*/
static void    s_ui_legacy_init(void);        /**< 鐠嬪啰鏁?App_Display_Init() */
static void    s_ui_legacy_render(const Sound_Pos_t *pos,
                                  const SRP_VisFrame_t *vis_frame,
                                  uint32_t frame_seq,
                                  uint8_t sai_dma_active); /**< 鐠嬪啰鏁?App_Display_Render() */

/** @brief Legacy 濞撳弶鐓嬮崥搴ｎ伂閹垮秳缍旂悰顭掔礄const閿涘瞼绱拠鎴炴埂绾喖鐣鹃敍灞界摠�?Flash�?*/
static const App_UiRendererOps_t s_ui_renderer_legacy_ops = {
    s_ui_legacy_is_ready,   /**< is_ready 閸戣姤鏆熼幐鍥�?*/
    s_ui_legacy_init,        /**< init 閸戣姤鏆熼幐鍥�?*/
    s_ui_legacy_render       /**< render 閸戣姤鏆熼幐鍥�?*/
};

/** @brief 瑜版挸澧犲┑鈧ú鑽ゆ畱濞撳弶鐓嬮崥搴ｎ伂閹稿洭鎷￠敍宀勭帛鐠併倖瀵氶�?Legacy 閸氬海顏?*/
static const App_UiRendererOps_t *s_ui_renderer = &s_ui_renderer_legacy_ops;

/** @brief 瑜版挸澧犻崥搴ｎ伂閺嬫矮濡囬崐纭风礉閻劋绨径鏍劥閺屻儴顕楅敍鍦損p_UiRenderer_GetBackend�?*/
static volatile App_UiRenderBackend_t s_ui_backend = APP_UI_RENDER_BACKEND_LEGACY;

static uint32_t s_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo)           /* 娴ｅ簼绨稉瀣閿涙氨娲块幒銉ㄧ箲閸ョ偘绗呴梽鎰�?*/
    {
        return lo;
    }
    if (v > hi)           /* 妤傛ü绨稉濠囨閿涙氨娲块幒銉ㄧ箲閸ョ偘绗傞梽鎰�?*/
    {
        return hi;
    }
    return v;             /* 閸︺劏瀵栭崶鏉戝敶閿涙艾甯崐鑹扮箲閸?*/
}

/**
 * @brief   閸掑洦宕?UI 濞撳弶鐓嬮崥搴ｎ伂閿涘牏鍤庣粙瀣暔閸忣煉�?
 * @details 閸︺劋澶嶉悾灞藉隘閸愬懎鍨忛幑銏犲毐閺佺増瀵氶柦鍫ｃ€冮敍瀛禝 娴犺濮熸稉瀣╃濞喡ょ殶閻?render() 閺冨墎鏁撻弫鍫涒�?
 *          瑜版挸澧犳禒鍛暜閹?LEGACY 閸氬海顏敍宀勵暕閻ｆ瑦婀弶銉﹀⒖鐏炴洩绱欐�?GPU 閸旂娀鈧喎鎮楃粩顖ょ礆閵?
 * @param   backend  閻╊喗鐖ｉ崥搴ｎ伂閺嬫矮濡囬�?
 */
void App_UiRenderer_SetBackend(App_UiRenderBackend_t backend)
{
    taskENTER_CRITICAL();  /* 娑撳鏅崠鐚寸窗娣囨繆鐦夐幐鍥嫛閸掑洦宕查崢鐔风摍閹嶇礉闂冨弶�?UI 娴犺濮熺拠璇插煂閸楀﹤鍨忛幑銏㈠Ц�?*/
    switch (backend)
    {
        case APP_UI_RENDER_BACKEND_LEGACY:  /* 閸掑洦宕查崚?Legacy 鏉烆垯娆㈠〒鍙夌厠閸氬海顏?*/
        default:                            /* 閺堫亞鐓￠崥搴ｎ伂娑旂喎娲栭柅鈧崚?Legacy閿涘牆鐣ㄩ崗銊ょ箽鎼存洩�?*/
            s_ui_renderer = &s_ui_renderer_legacy_ops;   /* 閺囧瓨鏌婇幙宥勭稊鐞涖劍瀵氶�?*/
            s_ui_backend  = APP_UI_RENDER_BACKEND_LEGACY; /* 閺囧瓨鏌婇弸姘閸?*/
            break;
    }
    taskEXIT_CRITICAL();
}

/**
 * @brief   鐠囪褰囪ぐ鎾冲�?UI 濞撳弶鐓嬮崥搴ｎ伂閺嬫矮濡囬崐纭风礄缁捐法鈻肩€瑰鍙忛�?
 * @return  瑜版挸澧犻崥搴ｎ伂閺嬫矮濡囬�?
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
/* Legacy 濞撳弶鐓嬮崥搴ｎ伂鐎圭偟骞囬敍鍫㈡纯閹恒儵鈧繋绱堕崚?App_Display 濡€虫健閿?                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief   Legacy 閸氬海顏?is_ready 鐎圭偟骞?
 * @return  App_Display_IsReady() 閻ㄥ嫯绻戦崶鐐测偓纭风礄1=瀹告彃鍨垫慨瀣鐎瑰本鍨氶�?=閺堫亜姘ㄧ紒顏庣�?
 */
static uint8_t s_ui_legacy_is_ready(void)
{
    return App_Display_IsReady();  /* 閻╁瓨甯撮弻銉�?App_Display 閸掓繂顫愰崠鏍Ц�?*/
}

/**
 * @brief   Legacy 閸氬海顏?init 鐎圭偟骞?
 * @details 鐠嬪啰鏁?App_Display_Init() 閸掓繂顫愰崠?LCD 閺冭泛绨妴涓㏕DC閵嗕礁鎶氱紓鎾冲暱閸栨亽�?
 *          �?LCD 瀹告彃姘ㄧ紒顏勫灟娑撹櫣鈹栭幙宥勭稊閿涘湏pp_Display_Init 閸愬懘鍎撮獮鍌滅搼婢跺嫮鎮婇敍澶堚偓?
 */
static void s_ui_legacy_init(void)
{
    App_Display_Init();  /* 閸掓繂顫愰崠鏍ㄦ▔缁€鍝勭湴閿涘苯瀵橀�?LCD 妞瑰崬濮╅妴涓㏕DC 闁板秶鐤嗛妴浣告姎缂傛挸鍟垮〒鍛存祩 */
}

/**
 * @brief   Legacy 閸氬海顏?render 鐎圭偟骞囬敍鍫モ偓蹇庣炊閸欏倹鏆熼崚?App_Display_Render�?
 * @param   pos           婢圭増绨担宥囩枂閿涘牊鏌熸担宥堫潡+閼充粙鍣洪敍澶涚礉閻劋绨紒妯哄煑閸椾礁鐡ч崗澶嬬垼
 * @param   vis_frame     SRP 閻戭厼濮忛崶鎯у讲鐟欏棗瀵茶箛顐ゅ弾閿涘瞼鏁ゆ禍搴㈣閺屾捁鍎楅弲顖滃劰閸旀稑娴?
 * @param   frame_seq     UI 鐢冪碍閸欏嚖绱濇笟娑欐瀮鐎涙鍩涢弬鏉垮瀻妫版垿鈧槒绶担璺ㄦ�?
 * @param   sai_dma_active  SAI DMA 閺勵垰鎯佸ú鏄忕┈閿涘瞼鏁ゆ禍搴㈡▔缁�?"閺冪娀鐓舵０鎴滀繆閸? 閹绘劗銇?
 */
static void s_ui_legacy_render(const Sound_Pos_t *pos,
                               const SRP_VisFrame_t *vis_frame,
                               uint32_t frame_seq,
                               uint8_t sai_dma_active)
{
    /* 閻╁瓨甯存潪顒€褰傞幍鈧張澶婂棘閺佹澘�?App_Display 濞撳弶鐓嬪Ο鈥虫健閿涘本妫ゆ０婵嗩樆婢跺嫮�?*/
    App_CameraFrame_t camera_frame = {0};

    (void)App_Camera_AcquireLatestFrame(&camera_frame);
    App_Display_Render(pos, vis_frame, &camera_frame, frame_seq, sai_dma_active);
    App_Camera_ReleaseFrame(&camera_frame);
}

/**
 * @brief   鐠侊紕鐣?UI 娴犺濮熼惃鍕閺屾挸鎳嗛張鐕傜礄FreeRTOS ticks�?
 * @details 鐏忓棛娲伴弽鍥ф姎閻滃洦宕茬粻妞捐�?vTaskDelayUntil 閻ㄥ嫬鎳嗛張?ticks�?
 *            period_ms = round(1000 / fps)  閿涘牆娲撻懜宥勭安閸忋儻绱伴�?fps/2 閸愬秹娅庢禒?fps�?
 *            ticks = pdMS_TO_TICKS(period_ms)
 *
 *          缁€杞扮伐閿涙瓲ps=20 -> period_ms=50ms -> 50 ticks (1ms/tick)
 *                fps=30 -> period_ms=33ms -> 33 ticks
 *                fps=5  -> period_ms=200ms -> 200 ticks
 *
 *          娣囨繆鐦?ticks >= 1閿涘矂妲诲?vTaskDelayUntil(0) 鐎佃壈鍤ф禒璇插娑撳秷顔€閸?CPU�?
 *
 * @return  濞撳弶鐓嬮崨銊︽埂閿涘苯宕熸担宥忕窗FreeRTOS ticks
 */
static uint32_t s_ui_period_ticks(void)
{
    /* 鐠囪褰囬惄顔界垼鐢呭芳楠炲爼鎸告担宥忕礉闂冨弶顒涙潻鎰攽閺冨爼鍘ょ純顔款潶鐠佸墽鐤嗛崚鎷屽瘱閸ユ潙�?*/
    uint32_t fps = s_clamp_u32(App_RuntimeConfig_GetUiTargetFps(), UI_FPS_MIN, UI_FPS_MAX);

    /* 閸ユ稖鍨楁禍鏂垮弳閹广垻鐣婚敍姘�?fps/2 閸愬秵鏆ｉ梽銈忕礉閻╃缍嬫禍搴☆嚠 1000/fps 閸嬫艾娲撻懜宥勭安閸?*/
    uint32_t period_ms = (1000u + (fps / 2u)) / fps;

    TickType_t ticks = pdMS_TO_TICKS(period_ms);  /* 濮ｎ偆顫楁潪?FreeRTOS ticks */

    if (ticks == 0u)    /* 閺嬩胶顏幆鍛枌娣囨繃濮㈤敍鍧rtTICK_PERIOD_MS > period_ms 閺冭泛褰查懗鎴掕�?0�?*/
    {
        ticks = 1u;     /* 閼峰啿鐨鎯扮�?1 �?tick閿涘奔绻氱拠浣锋崲閸斺€冲毉�?CPU */
    }
    return (uint32_t)ticks;
}

#define ui_cli_poll App_UiCli_Poll

/**
 * @brief   UI 閺勫墽銇氭禒璇插�?
 * @details 鏉烆喛顕楁担宥囩枂闂冪喎鍨獮璺哄煕閺傜増妯夌粈鐚寸窗閸欐牗娓堕弬棰佺秴�?-> 韫囶偆鍙?SRP 閸欘垵顫嬮崠鏍ㄦ殶閹?-> 濞撳弶鐓嬫潏鎾冲毉閵?
 *
 * 閸忔娊鏁悙鐧哥�?
 * - 閺勫墽銇氶張顏勬皑缂侇亝妞傞�?`UI_RETRY_INIT_MS` 閸涖劍婀￠柌宥堢槸閸掓繂顫愰崠鏍モ偓?
 * - 濮ｅ繐鎶氭禒鍛▏閻劑妲﹂崚妞捐厬閻ㄥ嫭娓堕崥搴濈閺夆€茬秴缂冾喗鏆熼幑顕嗙礉闁灝鍘?UI 閸棛袧�?
 * - 閸︺劋澶嶉悾灞藉隘閸愬懎顦查�?SRP 閸欘垵顫嬮崠鏍ф彥閻撗嶇礉闁灝鍘ょ拠璇插晸缁旂偘绨ら�?
 *
 * @param   pvParameters  FreeRTOS 娴犺濮熼崣鍌涙殶閿涘牊婀担璺ㄦ暏閿?
 */
void UI_Display_Task(void *pvParameters)
{
    (void)pvParameters;  /* 娴犺濮熼崣鍌涙殶閺堫亙濞囬悽顭掔礉濞戝牓娅庣拃锕€�?*/

    /* ---- 娴犺濮熺仦鈧柈銊уЦ閹礁褰夐柌?---- */

    /** @brief 瑜版挻顐兼禒搴ㄦЕ閸掓褰囬崚鎵畱婢圭増绨担宥囩枂閿涘牆褰查懗鍊燁潶婢舵碍顐肩拠璇插絿鐟曞棛娲婇敍灞藉絿閺堚偓閺傛澘鈧》绱?*/
    Sound_Pos_t draw_pos = {0.0f, 0.0f, 0.0f};

    /** @brief 閺堚偓閸氬簼绔村▎鈩冨灇閸旂喍绮犻梼鐔峰灙鐠囪鍩岄惃鍕紣濠ф劒缍呯純顕嗙礄闂冪喎鍨稉铏光敄閺冭泛顦查悽銊︻劃閸婅偐鎴风紒顓熻閺屾搫�?*/
    Sound_Pos_t last_pos = {0.0f, 0.0f, 0.0f};

    /** @brief SRP 閻戭厼濮忛崶鎯у讲鐟欏棗瀵茶箛顐ゅ弾閿涘牅绮犻棅鎶筋暥娴犺濮熸稉瀵告櫕閸栫儤瀚圭拹婵撶礉闁灝鍘ょ拠璇插晸缁旂偘绨ら�?*/
    SRP_VisFrame_t vis_snapshot = {0};

    /** @brief UI 鐢冪碍閸欏嚖绱濆В蹇旇閺屾挷绔寸敮褔鈧帒顤冮敍灞肩返閺傚洤鐡ч崚閿嬫煀閸掑棝顣堕柅鏄忕帆娴ｈ法�?*/
    uint32_t ui_frame_seq = 0u;

    /** @brief 娑撳﹥顐肩拠璇插�?g_audio_frame_seq_isr 閺冨墎娈戦崐纭风礉閻劋绨Λ鈧ù?SAI DMA 閺勵垰鎯佹禒宥呮躬鏉╂劘�?*/
    uint32_t last_audio_isr_seq = 0u;

    /** @brief 鏉╃偟鐢绘径姘�?SAI DMA 鎼村繐褰块張顏勫綁閸栨牜娈戠敮褎鏆熼敍鍫ｆ彧閸掍即妲囬崐鐓庢倵鐠併倓璐熼弮鐘荤叾妫版垼绶崗銉礆 */
    uint8_t audio_idle_frames = 0xFFu;  /* 閸掓繂顫愰崠鏍﹁礋閺堚偓婢堆冣偓纭风礉鐟欙箑褰傛＃鏍ф姎閸氬海鐝涢崡鍐茬秺�?*/

    /** @brief vTaskDelayUntil 閻ㄥ嫮绮风€电懓鏁滈柋鎺撴閸掍紮绱欐穱婵婄槈鐢冩噯閺堢喓菙鐎规熬绱濇稉宥呭綀濞撳弶鐓嬮懓妤佹瑜板崬鎼烽�?*/
    TickType_t next_render_wake;

    /** @brief 娑撳﹥顐肩亸婵婄槸閸掓繂顫愰崠鏍ㄦ▔缁€鍝勭湴閻?tick 閸婄》绱欓梽鎰煑闁插秷鐦０鎴犲芳�?*/
    TickType_t last_init_try = 0u;

    /** @brief 娑撳﹥顐肩拋鏉跨秿閻?DMA2D 鐡掑懏妞傜拋鈩冩殶閿涘瞼鏁ゆ禍搴″綁閸栨牗顥呭ù瀣剁礄娴犲懎婀?UI_DEBUG_LOG 濡€崇础閹垫挸宓冮敍?*/
    uint32_t last_dma2d_timeout = 0u;

    /* ---- 娴犺濮熼崥顖氬З閺冨爼顩诲▎鈥崇毦鐠囨洖鍨垫慨瀣閺勫墽銇氱�?---- */
    /* �?LCD 鐏忔碍婀亸杈╁崕閿涘牆顩ч弮璺虹碍閸掓繂顫愰崠鏍ㄦ弓鐎瑰本鍨氶敍澶涚礉閸忓牆鐨剧拠鏇炲灥婵�?*/
    if ((s_ui_renderer != NULL) && (s_ui_renderer->is_ready() == 0u))
    {
        s_ui_renderer->init();  /* 閸掓繂顫愰崠?LCD 妞瑰崬濮╅妴涓㏕DC 闁板秶鐤嗛妴浣圭闂嗚泛鎶氱紓鎾冲暱 */
    }
    last_init_try    = xTaskGetTickCount();  /* 鐠佹澘缍嶉崚婵嗩潗閸栨牕鐨剧拠鏇熸�?*/
    next_render_wake = last_init_try;        /* 缁楊兛绔寸敮褏鐝涢崡铏閺?*/

    /* ================================================================
     * 娴犺濮熸稉璇叉儕閻滎垽绱欏闀愮瑝闁偓閸戠尨�?
     * ================================================================ */
    for (;;)
    {
        uint32_t t_loop;       /* 閺堫剙鎶氬顏嗗箚閺佺繝缍嬮懓妤佹鐠佲剝妞傜挧椋庡�?*/
        uint8_t  sai_dma_active; /* SAI DMA 濞叉槒绌弽鍥х箶閿涘牅绱剁紒娆愯閺屾挸娅掗弰鍓с仛闂婃娊顣堕悩鑸碘偓渚婄�?*/

        /* ---- 濮濄儵顎?1閿涙艾顦╅悶?CLI 鏉堟挸鍙嗛敍鍦睞RT 閸涙垝鎶ょ悰宀嬬礆 ---- */
        /* 濮ｅ繐鎶氶柈鍊熺枂鐠?CLI閿涘奔绻氱拠浣疯閸欙絽鎳℃禒銈呮惙鎼存柨娆㈡潻?<= 1 鐢冩噯閺堢噦绱欓埉?0ms@20fps�?*/
        ui_cli_poll();

        /* ---- 濮濄儵顎?2閿涙碍妯夌粈鍝勫灥婵瀵查柌宥堢槸闁槒绶?---- */
        /* 閼汇儲妯夌粈鍝勭湴閺堫亜姘ㄧ紒顏庣礄�?LCD 閸掓繂顫愰崠鏍с亼鐠愩儻绱氶敍灞剧槨闂?UI_RETRY_INIT_MS(1000ms) 闁插秷鐦稉鈧�?*/
        if ((s_ui_renderer != NULL) && (s_ui_renderer->is_ready() == 0u))
        {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_init_try) >= pdMS_TO_TICKS(UI_RETRY_INIT_MS))
            {
#if UI_DEBUG_LOG
                /* 鐠嬪啳鐦Ο鈥崇础娑撳澧﹂崡鏉垮灥婵瀵叉径杈Е閺冭泛鎮囧Ο鈥虫健閻ㄥ嫰妯佸▓闈涒偓纭风礉鏉堝懎濮€规矮缍呯涵顑挎闂傤噣�?*/
                printf("UI: retry init (app=0x%08lX err=%lu lcd=%lu ltdc=%lu)\r\n",
                       (unsigned long)g_display_init_stage,   /* App_Display 閸掓繂顫愰崠鏍▉濞?*/
                       (unsigned long)g_display_init_error,   /* App_Display 闁挎瑨顕ら惍?*/
                       (unsigned long)g_lcd_init_stage,       /* LCD 妞瑰崬濮╅崚婵嗩潗閸栨牠妯佸�?*/
                       (unsigned long)g_ltdc_init_stage);     /* LTDC 閹貉冨煑閸ｃ劌鍨垫慨瀣闂冭埖�?*/
#endif
                s_ui_renderer->init();   /* 闁插秷鐦崚婵嗩潗閸?*/
                last_init_try = now;     /* 閺囧瓨鏌婇柌宥堢槸閺冭泛�?*/
            }
            taskYIELD();  /* 閸戦缚顔�?CPU閿涘奔绗夌憰浣衡敄鏉烆剛鐡戝鍜冪礉鐠佲晠鐓舵０鎴滄崲閸斺剝婀侀張杞扮窗鏉╂劘顢?*/
            continue;     /* 閸ョ偛鍩屽顏嗗箚妞ゅ爼鍎撮柌宥嗘煀閸掋倖鏌?*/
        }

        /* ---- 濮濄儵顎?3閿涙碍鈧嗗厴缂佺喕顓搁柅鎺戭杻娑撳酣鈧喓宸奸幍鎾冲祪 ---- */
        App_Perf_CountUiLoop();         /* UI 瀵邦亞骞嗙拋鈩冩�?+1閿涘牏鏁ゆ禍?perf rate 闁喓宸肩拋锛勭暬閿?*/
        App_Perf_MaybePrintRates();     /* 閼汇儴鎻崚鐗堝ⅵ閸楁澘鎳嗛張鐕傜礄1s閿涘绱濇潏鎾冲毉闁喓宸肩紒鐔活吀 */
        t_loop = App_Perf_BeginCycles(); /* 鐠佹澘缍嶉張顒€鎶氬顏嗗箚瀵偓婵妞傞�?*/

        /* ---- 濮濄儵顎?4閿涙碍绉烽懓妞剧秴缂冾噣妲﹂崚妤嬬礉閸欐牗娓堕弬鏉匡紣濠ф劒缍呯純?---- */
        /* 娴ｈ法鏁?timeout=0閿涘牓娼梼璇差敚閿涘绱濋懟銉︽￥閺傜増鏆熼幑顔煎灟婢跺秶�?last_pos 缂佈呯敾濞撳弶鐓嬮敍鍫滅箽閹镐礁鎶氶悳鍥┣旂€规熬绱?*/
        if (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
        {
            last_pos = draw_pos;           /* 娣囨繂鐡ㄩ張鈧弬棰佺秴�?*/
            g_ui_queue_rx_count++;         /* 缂佺喕顓搁幋鎰閹恒儲鏁瑰▎鈩冩殶 */

            /* 閼汇儵妲﹂崚妞捐厬鏉╂ɑ婀侀弴瀛樻煀閻ㄥ嫪缍呯純顕嗙礄閺嬩礁鐨幆鍛枌閿涘绱濈紒褏鐢诲☉鍫ｂ偓妤冩纯閸掔増绔荤粚鐚寸礉閸欐牗娓堕崥搴濈娑?*/
            while (xQueueReceive(xPositionQueue, &draw_pos, 0u) == pdPASS)
            {
                last_pos = draw_pos;       /* 閹镐胶鐢荤憰鍡欐磰閿涘奔绻氱拠浣稿絿閸掔増娓堕弬鎵�?*/
                g_ui_queue_rx_count++;
            }
        }
        else
        {
            /* 闂冪喎鍨稉铏光敄閿涘牓鐓舵０鎴滄崲閸斺€崇毣閺堫亙楠囬悽鐔告煀缂佹挻鐏夐敍澶涚礉婢跺秶�?last_pos 娑撳秵娲块弬?*/
            g_ui_queue_timeout_count++;  /* 缂佺喕顓搁弮鐘虫煀閺佺増宓佺敮褎鏆熼敍鍫燁劀鐢摜骞囩挒鈽呯窗瑜?UI FPS > 闂婃娊顣剁敮褏宸?/ decim 閺冭绱?*/
        }

        ui_frame_seq++;  /* 闁帒顤?UI 鐢冪碍閸欏嚖绱欐导鐘电舶濞撳弶鐓嬮崳銊ф暏娴滃骸鍨庢０鎴濆煕閺傜増鏋冪€涙鐡戦崗鍐�?*/

        /* ---- 濮濄儵顎?5閿涙碍顥呭�?SAI DMA 濞叉槒绌幀?---- */
        /* 闁俺绻冮惄鎴炵�?g_audio_frame_seq_isr 閺勵垰鎯侀張澶婂綁閸栨牗娼甸崚銈嗘�?SAI DMA 閺勵垰鎯侀崷銊ㄧ箥�?*/
        {
            uint32_t audio_seq = g_audio_frame_seq_isr;  /* 鐠囪褰?ISR 鐢冪碍閸欏嚖绱檝olatile�?*/
            if (audio_seq != last_audio_isr_seq)          /* 鎼村繐褰块張澶婂綁閸栨牭绱癉MA 娴犲秴婀銉ょ�?*/
            {
                last_audio_isr_seq = audio_seq;  /* 閺囧瓨鏌婇崺鍝勫櫙閸?*/
                audio_idle_frames  = 0u;          /* 闁插秶鐤嗙粚娲＝鐠佲剝鏆熼敍鍫ｃ€冪粈?DMA 濞叉槒绌敍?*/
            }
            else if (audio_idle_frames < 0xFFu)   /* 鎼村繐褰块張顏勫綁閿涙MA 閸欘垵鍏橀崑婊勵剾閿涘瞼鐤粔顖溾敄闂傛彃鎶氶�?*/
            {
                audio_idle_frames++;  /* 妤楀崬鎷扮拋鈩冩殶閿涘奔绗夊┃銏犲毉 */
            }
            /* 閼汇儴绻涚紒顓犫敄闂傛彃鎶氶�?<= 闂冨牆鈧》绱濈拋銈勮�?SAI DMA 濞叉槒绌敍娑滅Т鏉╁洭妲囬崐鑹邦吇娑撶儤妫ら棅鎶筋暥鏉堟挸鍙?*/
            sai_dma_active = (audio_idle_frames <= APP_DISPLAY_SAI_ACTIVE_HOLD_FRAMES) ? 1u : 0u;
        }

        /* ---- 濮濄儵顎?6閿涙艾婀稉瀵告櫕閸栧搫鍞存径宥呭煑 SRP 閸欘垵顫嬮崠鏍ф彥�?---- */
        /* SRP 閸欘垵顫嬮崠鏍ㄦ殶閹诡噯绱欓悜顓炲閸ョ偓鏆熼幑顕嗙礆閻㈤亶鐓舵０鎴滄崲閸斺€冲晸閸忋儻绱漊I 娴犺濮熺拠璇插絿閿涘苯鐡ㄩ崷銊х彽娴滃�?
         * 娴ｈ法鏁ゆ稉瀵告櫕閸栬桨绻氶幎銈咁槻閸掕埖鎼锋担婊愮礉闂冨弶顒涚拠璇插煂娑擃參妫块悩鑸碘偓渚婄礄閸楀﹥瀚圭拹婵堟畱閺佺増宓侀敍澶堚偓?
         * 婢跺秴鍩楅崥搴℃彥閻撗傜瑢闂婃娊顣舵禒璇插鐟欙綀鈧讣绱濆〒鍙夌厠閺堢喖妫挎稉宥呭晙闂団偓鐟曚焦瀵旈張澶夊閻ｅ苯灏妴?*/
        {
            uint32_t t_sec = App_Perf_BeginCycles();
            (void)AI_SRP_GetLatestVisualizationFrame(&vis_snapshot);
            App_Perf_EndCycles(APP_PERF_SEC_UI_SNAPSHOT, t_sec);
        }

        /* ---- 濮濄儵顎?7閿涙俺鐨熼悽銊﹁閺屾挸鎮楃粩顖涘⒔鐞涘奔绔寸敮褎瑕嗛弻?---- */
        /* 濞撳弶鐓嬪ù浣衡柤閿涘牆婀?App_Display_Render 閸愬懘鍎撮敍澶涚�?
         *   瑜版帊绔撮崠鏍劰閸旀稑娴?-> colormap 閺勭姴鐨?-> 閸欏瞼鍤庨幀?閺堚偓鏉╂垿鍋﹂幓鎺戔偓?-> 楠炶櫕绮?->
         *   缂佹ê鍩楅崡浣哥摟閸忓鐖?-> 缂佹ê鍩楅崸鎰垼�?-> 鐟曞棛娲婇弬鍥х摟 -> DMA2D blit �?LCD */
        {
            uint32_t t_sec = App_Perf_BeginCycles();
            s_ui_renderer->render(&last_pos,       /* 婢圭増绨担宥囩�?*/
                                  &vis_snapshot,   /* SRP 閻戭厼濮忛崶鎯ф彥�?*/
                                  ui_frame_seq,    /* 鐢冪碍閸欏嚖绱欓弬鍥х摟閸掑棝顣堕悽顭掔�?*/
                                  sai_dma_active); /* SAI DMA 閻樿埖鈧?*/
            App_Perf_EndCycles(APP_PERF_SEC_UI_RENDER, t_sec);
        }
        g_ui_render_count++;  /* 濞撳弶鐓嬬敮褑顓搁弫?+1閿涘牆褰查柅姘崇箖鐠嬪啳鐦崳銊潎�?UI 鐎圭偤妾潻鎰攽鐢呭芳閿?*/

        /* ---- 濮濄儵顎?8閿涙MA2D 鐡掑懏妞傞崣妯哄濡偓濞村绱欓崣顖炩偓澶庣殶鐠囨洘妫╄箛妤嬬�?---- */
        if (g_ltdc_dma2d_timeout_count != last_dma2d_timeout)  /* 鐡掑懏妞傜拋鈩冩殶閺堝顤冮�?*/
        {
#if UI_DEBUG_LOG
            /* 閹垫挸宓冪搾鍛娣団剝浼呴敍灞藉簻閸斺晞鐦栭弬?DMA2D 绾兛娆㈤幋鏍ㄦ鎼村繘妫舵�?*/
            printf("UI: DMA2D timeout=%lu panel=0x%04X\r\n",
                   (unsigned long)g_ltdc_dma2d_timeout_count,  /* 閺堚偓閺傛媽绉撮弮鎯邦吀�?*/
                   (unsigned int)g_ltdc_panel_id);              /* LCD 闂堛垺婢?ID */
#endif
            last_dma2d_timeout = g_ltdc_dma2d_timeout_count;  /* 閺囧瓨鏌婇崺鍝勫櫙閸?*/
        }

        /* ---- 濮濄儵顎?9閿涙氨绮ㄩ弶鐔告拱鐢嗩吀閺冭泛鑻熺粵澶婄窡娑撳绔寸敮褍鏁滈柋鎺撴�?---- */
        App_Perf_EndCycles(APP_PERF_SEC_UI_LOOP, t_loop);  /* 鐠佹澘缍嶉張顒€鎶氶幀鏄忊偓妤佹 */

        /* vTaskDelayUntil閿涙氨绮风€佃妞傞梻鏉戞鏉╃噦绱濋懛顏勫З鐞涖儱浼╁〒鍙夌厠閼版妞傞敍灞肩箽鐠囦礁鎶氶崨銊︽埂閹帒鐣鹃妴?
         * 閼汇儲瑕嗛弻鎾存闂傜绉存潻鍥︾娑擃亜鎶氶崨銊︽埂閿涘ext_render_wake 娴兼俺顫﹂幒銊ㄧ箻娴犮儵浼╅崗宥堢瀵ゆ儼绻滈妴?
         * 閻╁憡鐦?vTaskDelay閿涘牏娴夌€电懓娆㈡潻鐕傜礆閿涘苯褰查張澶嬫櫏闂冨弶顒涚敮褏宸奸梾蹇氱鏉炶姤灏濋崝銊ㄢ偓灞剧磽缁夋眹鈧?*/
        vTaskDelayUntil(&next_render_wake, (TickType_t)s_ui_period_ticks());
    }
}

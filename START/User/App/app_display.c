/**
 * @file    app_display.c
 * @brief   婢规澘顒熼幋鎰剼閺勫墽銇氬Ο鈥虫健鐎圭偟骞?
 * @details
 * 閺堫剚鏋冩禒鎯扮鐠愶絾濡哥粻妤佺《鐏炲倻绮伴崙铏规畱 SRP-PHAT 閸欘垵顫嬮崠鏍波閺嬫粣绱濇潪顒佸床閹?LCD 娑撳﹦婀″锝呭讲鐟欎胶娈?
 * 娑撯偓鐢冩禈閸嶅繈鈧倹娓剁紒鍫㈡暰闂堛垻鏁遍崶娑㈠劥閸掑棛绮嶉幋鎰剁窗
 * 1. 閻戭厼濮忛崶鎾呯窗鐏炴洜銇氱粚娲？閼充粙鍣洪崚鍡楃閵? * 2. 閸椾礁鐡ч崙鍡樻Е閿涙艾鐫嶇粈鐑樻付缂佸牆鐣炬担宥堢翻閸戦缚顫楁惔锔衡偓? * 3. 瀹勬澘鈧吋顢嬮敍姘潔缁€鍝勭秼閸撳秴濮涢悳鍥ф簚娑擃厾娈戦張鈧铏瑰仯娴ｅ秶鐤嗛妴? * 4. 閺傚洦婀版笟褎鐖敍姘潔缁€鍝勬綏閺嶅洢鈧浇鍏橀柌蹇嬧偓浣鼓佸蹇嬧偓涓廙A/娴溿倖宕茬粵澶庣槚閺傤厺淇婇幁顖樷偓? *
 * 閺佺繝閲滃〒鍙夌厠闁炬崘鐭鹃崣顖欎簰閹稿顩ф稉瀣€庢惔蹇曟倞鐟欙綇绱?
 * `SRP_VisFrame_t`
 * -> 鐏忓棛鈻堥悿蹇旀偝缁便垻缍夐弽濂稿櫢闁插洦鐗辨稉铏诡焼鐎靛棙妯夌粈鍝勬簚
 * -> 閸欘垶鈧婀撮幎濠勭矎閹兼粎鍌ㄧ紒鎾寸亯闁插秵鏌婇摶宥呮値閸掔増妯夌粈鍝勬簚
 * -> 鐎电懓婧€閸嬫艾閽╁鎴濇嫲閸斻劍鈧礁缍婃稉鈧崠? * -> 閸掑棗娼￠崘娆忓弳 LTDC 閸氬骸褰寸紓鎾冲暱
 * -> 閸欑姴濮炴潏瑙勵攱閵嗕礁鍣弰鐔粹偓浣稿槻閸婂吋顢嬮崪灞炬瀮閺? * -> 閸掗攱鏌婄紒妯哄煑闂冪喎鍨獮鍓佹暤鐠囧嘲澧犻崥搴″酱缂傛挸鍟挎禍銈嗗床
 *
 * 闂冨懓顕板楦款唴閿? * - `App_Display_Init` 閸忚櫕鏁為崚婵嗩潗閸栨牓鈧礁绔风仦鈧拋锛勭暬閸滃瞼绱﹂崘鑼拨鐎规哎鈧? * - `s_prepare_field` 閸忚櫕鏁炵粻妤佺《缂佹挻鐏夋俊鍌欑秿閸欐ɑ鍨氶崣顖滅帛閸掕泛婧€閵? * - `s_update_norm_field` 閸忚櫕鏁為崝銊︹偓浣藉瘱閸ユ潙甯囩紓鈺€绗屾禍顔煎缁嬪啿鐣鹃張鍝勫煑閵? * - `s_render_field_rows` 閸忚櫕鏁炴俊鍌欑秿閹?8bit 閻戭厼濮忛崶楣冣偓浣风瑐鐏炲繐绠烽妴? * - `App_Display_Render` 閸忚櫕鏁炲В蹇撴姎鐎瑰本鏆ｉ弮璺虹碍娑撳骸鐤勯弮鑸碘偓褍褰囬懜宥冣偓? */
#include "app_display.h"
#include "app_main_task.h"
#include "app_perf.h"
#include "app_spectrum.h"
#include "app_touch_test.h"

#include "LCD/lcd.h"
#include "LCD/ltdc.h"
#include "LCD/dma2d_accel.h"
#include "ai_config.h"
#include "mpu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>


/* 閺勫墽銇氭稉顓㈡？閸﹁櫣娴夐崗鍐茬暞閿? * 鏉╂瑩鍣烽惃鍕ㄢ偓娓噄eld閳ユ繀绗夐弰?LCD 鐎圭偤妾崓蹇曠閿涘矁鈧本妲哥紒妯哄煑閸撳秶娈戞稉顓㈡？缂冩垶鐗搁妴? * 鏉╂瑦鐗辩拋鎹愵吀閻ㄥ嫬銈芥径鍕Ц閿? * - 閸掑棜椴搁悳鍥ㄧ槷缁?缂佸棙鎮崇槐銏㈢秹閺嶅ジ鐝敍宀冨喕娴犮儴骞忓妤勭窛楠炶櫕绮﹂惃鍕劰閸旀稑娴橀敍? * - 閸掑棜椴搁悳鍥у嫉鏉╂粌鐨禍搴㈡殻鐏炲繘鈧劕鍎氱槐鐘差槱閻炲棴绱濋懞鍌滄阜 RAM 閸滃矁顓哥粻妤呭櫤閿? * - 閸氬海鐢婚弮鐘侯啈闁插洨鏁ら張鈧潻鎴﹀仸鏉╂ɑ妲搁崣宀€鍤庨幀褎鏂佹径褝绱濋柈鑺ユ箒缂佺喍绔撮惃鍕殶閹诡喗娼靛┃鎰┾偓?*/
#define APP_DISPLAY_FIELD_PIXELS      (APP_DISPLAY_FIELD_W * APP_DISPLAY_FIELD_H)
#define APP_DISPLAY_BLUR_KERNEL_LEN   (2u * APP_DISPLAY_SMOOTH_RADIUS + 1u)
#define APP_DISPLAY_FINE_KERNEL_LEN   (2u * APP_DISPLAY_FINE_KERNEL_RADIUS + 1u)
#define APP_DISPLAY_CAMERA_CACHE_ADDR 0xC0600000u
#define APP_DISPLAY_CAMERA_CACHE_BYTES (APP_DISPLAY_CAMERA_VIEW_W * APP_DISPLAY_CAMERA_VIEW_H * 2u)
#define APP_DISPLAY_CAMERA_CACHE_LIMIT 0xC2000000u
#define APP_DISPLAY_SPECTRUM_AXIS_LABEL_W 36u
#define APP_DISPLAY_SPECTRUM_MARGIN_L    4u
#define APP_DISPLAY_SPECTRUM_MARGIN_R    4u
#define APP_DISPLAY_SPECTRUM_MARGIN_T    4u
#define APP_DISPLAY_SPECTRUM_MARGIN_B    18u
#define APP_DISPLAY_SPECTRUM_GUIDE_DIVS  4u
#define APP_DISPLAY_SPECTRUM_MIN_MAG     1.0e-12f
#if ((APP_CAMERA_ENABLE != 0u) && ((APP_DISPLAY_CAMERA_CACHE_ADDR + APP_DISPLAY_CAMERA_CACHE_BYTES) > APP_DISPLAY_CAMERA_CACHE_LIMIT))
#error "Camera display cache must stay inside SDRAM"
#endif

/* ---------------------------------------------------------------------------
 * 濡€虫健閻樿埖鈧? * ---------------------------------------------------------------------------
 * 娑撳鍨棃娆愨偓浣稿綁闁插繘鍏樼仦鐐扮艾閳ユ粍妯夌粈鐑樐侀崸妤冾潌閺堝濮搁幀浣测偓婵撶礉娑撳秵妲哥粻妤佺《閻樿埖鈧焦婀伴煬顐犫偓? * 閸欘垱瀵滈悽銊┾偓鏂垮瀻娑撳搫鍤戠紒鍕剁窗
 * - 閻㈢喎鎳￠崨銊︽埂閻樿埖鈧緤绱伴崚婵嗩潗閸栨牗妲搁崥锕€鐣幋鎰┾偓浣告躬閸濐亙绔村銉ャ亼鐠? * - 鐢啫鐪悩鑸碘偓渚婄窗閻戭厼濮忛崶鎯у隘閸╃喎鎷伴弬鍥ㄦ拱閸栧搫鐓欓崷?LCD 娑撳﹦娈戦惌鈺佽埌閼煎啫娲?
 * - 閸斻劍鈧胶濮搁幀渚婄窗EMA 瀹勬澘鈧鈧礁娅旀竟鏉跨俺閵嗕焦鏋冮張顒€鍩涢弬鎷屽Ν濞翠椒淇婇幁? * - 缂傛挸鐡?LUT 閻樿埖鈧緤绱版稉鍝勫櫤鐏忔垿鈧劕鎶氶柌宥咁槻鐠侊紕鐣婚懓灞肩箽閻ｆ瑧娈戞潏鍛И閺佺増宓?
 */
static uint8_t s_ready = 0u;
volatile uint32_t g_display_init_stage = 0u;
volatile uint32_t g_display_init_error = 0u;

static uint16_t s_map_x0 = 0u;
static uint16_t s_map_y0 = 0u;
static uint16_t s_map_x1 = 0u;
static uint16_t s_map_y1 = 0u;
static uint16_t s_text_x = 0u;
static uint16_t s_camera_x0 = 0u;
static uint16_t s_camera_y0 = 0u;
static uint16_t s_camera_x1 = 0u;
static uint16_t s_camera_y1 = 0u;
static uint16_t s_ui_x1 = 0u;

static float s_peak_ema = APP_DISPLAY_EMA_MIN_PEAK;
static float s_last_noise_floor = 0.0f;
static uint8_t s_kernel_ready = 0u;
static uint8_t s_norm_lut_valid = 0u;
static float s_norm_lut_db_floor = APP_DISPLAY_DYNAMIC_DB_FLOOR;
static float s_norm_lut_gamma = APP_DISPLAY_DYNAMIC_GAMMA;
static uint32_t s_fb_addr_a = 0u;
static uint32_t s_fb_addr_b = 0u;
static uint16_t s_cache_map_w = 0u;
static uint16_t s_cache_map_h = 0u;
static uint16_t s_camera_cache_map_w = 0u;
static uint16_t s_camera_cache_map_h = 0u;
static uint16_t s_camera_cache_src_w = 0u;
static uint16_t s_camera_cache_src_h = 0u;
static uint16_t *const s_camera_cache_pixels = (uint16_t *)APP_DISPLAY_CAMERA_CACHE_ADDR;
static uint32_t s_camera_cache_seq = 0u;
static uint8_t s_camera_cache_valid = 0u;
static uint16_t s_camera_freeze_w = 0u;
static uint16_t s_camera_freeze_h = 0u;
static uint16_t s_camera_freeze_stride = 0u;
static uint8_t s_camera_freeze_valid = 0u;
static uint32_t s_dbg_camera_path_count = 0u;
static uint32_t s_dbg_camera_overlay_count = 0u;
static uint32_t s_dbg_camera_input_seq = 0u;
static App_Display_CameraView_t s_camera_view_mode = APP_DISPLAY_CAMERA_VIEW_OVERLAY;
static float s_spectrum_ema[APP_SPECTRUM_BIN_COUNT];
static App_SpectrumFrame_t s_last_spectrum_frame;
static float s_spectrum_ref_mag = APP_DISPLAY_SPECTRUM_MIN_MAG;
static uint8_t s_spectrum_ema_valid = 0u;
static uint8_t s_spectrum_frame_valid = 0u;

/* 瀹搞儰缍旂紓鎾冲暱閸栭缚顕╅弰搴窗
 * - `s_field_a` / `s_field_b`
 *   娣囨繂鐡ㄥù顔惧仯閺勫墽銇氶崷鎭掆偓鍌欑閹碘偓娴犮儰濞囬悽銊ュ蓟缂傛挸鍟块敍灞炬Ц娑撹桨绨￠崡椋幮?楠炶櫕绮﹂弮鍫曚缉閸忓秮鈧粏绔熺拠鏄忕珶閸愭瑢鈧? *   鐎佃壈鍤х紒鎾寸亯濮光剝鐓嬮妴? * - `s_field_norm_u8`
 *   娣囨繂鐡ㄨぐ鎺嶇閸栨牕鎮楅惃?8bit 瀵搫瀹抽崶鎾呯礉閺勵垱娓剁紒鍫㈡絻閼规彃鎷版稉濠傜潌閻ㄥ嫮娲块幒銉ㄧ翻閸忋儯鈧? * - `s_blit_buf` / `s_blit_l8_buf`
 *   娣囨繂鐡ㄩ崚鍡楁健濞撳弶鐓嬮弮鍓佹畱娑撳瓨妞傜悰灞芥健閺佺増宓侀敍灞藉悑妞?DMA2D/LTDC 閸旂娀鈧喕鐭惧鍕嫲鏉烆垯娆㈤崶鐐衡偓鈧捄顖氱窞閵?*/
__SECTION_AXI_SRAM static float s_field_a[APP_DISPLAY_FIELD_PIXELS];
__SECTION_AXI_SRAM static float s_field_b[APP_DISPLAY_FIELD_PIXELS];
__SECTION_AXI_SRAM static uint8_t s_field_norm_u8[APP_DISPLAY_FIELD_PIXELS];
__SECTION_D2_SRAM static uint16_t s_blit_buf[APP_DISPLAY_MAX_LINE_PIXELS * APP_DISPLAY_BLIT_ROWS_MAX];
__SECTION_D2_SRAM static uint8_t s_blit_l8_buf[APP_DISPLAY_MAX_LINE_PIXELS * APP_DISPLAY_BLIT_ROWS_MAX];
static uint8_t s_norm_ratio_lut[APP_DISPLAY_NORM_RATIO_LUT_SIZE + 1u];
static uint16_t s_row_near_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_row_y0_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_row_y1_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_row_wy256_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_col_near_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_col_x0_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_col_x1_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_col_wx256_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_camera_row_near_cache[APP_DISPLAY_MAX_LINE_PIXELS];
static uint16_t s_camera_col_near_cache[APP_DISPLAY_MAX_LINE_PIXELS];

/* 妫板嫯顓哥粻妤勩€冮敍? * - `s_blur_kernel`閿涙矮绔寸紒鏉戦挬濠婃垶鐗抽敍灞肩返閸欘垰鍨庣粋缁樐佺化濠佸▏閻? * - `s_fine_kernel`閿涙矮绨╃紒瀵哥矎閹兼粎鍌ㄩ幍鈺傛殠閺嶉潻绱濋幎濠勭矎缂冩垶鐗搁悙纭呭厴闁插繆鈧粍鎷婚垾婵嗘礀閺勫墽銇氶崷? * - `s_heat_lut`閿涙碍濡?0..255 瀵搫瀹抽弰鐘茬殸娑?RGB565 閻戭厼濮忛崶楣冾杹閼?*/
static float s_blur_kernel[APP_DISPLAY_BLUR_KERNEL_LEN];
static float s_fine_kernel[APP_DISPLAY_FINE_KERNEL_LEN * APP_DISPLAY_FINE_KERNEL_LEN];
static uint16_t s_heat_lut[APP_DISPLAY_HEAT_LUT_SIZE];

static App_Display_Mode_t s_mode = (App_Display_Mode_t)APP_DISPLAY_DEFAULT_MODE;
static App_Display_RuntimeCfg_t s_cfg = {
#if (APP_DISPLAY_DEFAULT_MODE == 0u)
    APP_DISPLAY_MODE_FAST_EMA_ATTACK,
    APP_DISPLAY_MODE_FAST_EMA_DECAY,
    APP_DISPLAY_MODE_FAST_DB_FLOOR,
    APP_DISPLAY_MODE_FAST_FINE_GAIN,
    APP_DISPLAY_MODE_FAST_GAMMA,
    APP_DISPLAY_MODE_FAST_NOISE_GATE_RATIO,
    APP_DISPLAY_MODE_FAST_NOISE_ADAPT_GAIN,
    APP_DISPLAY_MODE_FAST_SMOOTH_PASSES,
    APP_DISPLAY_MODE_FAST_FINE_FUSION_ENABLE,
    APP_DISPLAY_MODE_FAST_DRAW_COARSE_GRID,
    (APP_DISPLAY_MODE_FAST_INTERP_BILINEAR != 0u) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST,
    (APP_DISPLAY_MODE_FAST_NORM_FULL != 0u) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST,
    APP_DISPLAY_MODE_FAST_TEXT_REFRESH_DIV,
    APP_DISPLAY_MODE_FAST_BLIT_ROWS
#elif (APP_DISPLAY_DEFAULT_MODE == 2u)
    APP_DISPLAY_MODE_CLEAN_EMA_ATTACK,
    APP_DISPLAY_MODE_CLEAN_EMA_DECAY,
    APP_DISPLAY_MODE_CLEAN_DB_FLOOR,
    APP_DISPLAY_MODE_CLEAN_FINE_GAIN,
    APP_DISPLAY_MODE_CLEAN_GAMMA,
    APP_DISPLAY_MODE_CLEAN_NOISE_GATE_RATIO,
    APP_DISPLAY_MODE_CLEAN_NOISE_ADAPT_GAIN,
    APP_DISPLAY_MODE_CLEAN_SMOOTH_PASSES,
    APP_DISPLAY_MODE_CLEAN_FINE_FUSION_ENABLE,
    APP_DISPLAY_MODE_CLEAN_DRAW_COARSE_GRID,
    (APP_DISPLAY_MODE_CLEAN_INTERP_BILINEAR != 0u) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST,
    (APP_DISPLAY_MODE_CLEAN_NORM_FULL != 0u) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST,
    APP_DISPLAY_MODE_CLEAN_TEXT_REFRESH_DIV,
    APP_DISPLAY_MODE_CLEAN_BLIT_ROWS
#else
    APP_DISPLAY_EMA_ATTACK,
    APP_DISPLAY_EMA_DECAY,
    APP_DISPLAY_DYNAMIC_DB_FLOOR,
    APP_DISPLAY_FINE_GAIN,
    APP_DISPLAY_DYNAMIC_GAMMA,
    APP_DISPLAY_NOISE_GATE_RATIO,
    APP_DISPLAY_NOISE_ADAPT_GAIN,
    APP_DISPLAY_SMOOTH_PASSES,
    APP_DISPLAY_FINE_FUSION_ENABLE,
    APP_DISPLAY_DRAW_COARSE_GRID,
    (APP_DISPLAY_BILINEAR_SAMPLING != 0u) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST,
    (APP_DISPLAY_MODE_BALANCED_NORM_FULL != 0u) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST,
    APP_DISPLAY_TEXT_REFRESH_DIV,
    APP_DISPLAY_BLIT_ROWS_MAX
#endif
};

/* 閸戠姳缍嶇紓鎾崇摠閸掗攱鏌婇崙鑺ユ殶閸︺劍鏋冩禒璺烘倵閸楀﹥顔岀€规矮绠熼妴? * 鐎瑰啳绀嬬拹锝嗗Ω LCD 閸ф劖鐖ｉ崚鐗堟▔缁€鍝勬簚閸ф劖鐖ｉ惃鍕Ё鐏忓嫰顣╅崗鍫㈢暬婵傚鈧?*/
void s_refresh_render_map_cache(uint16_t map_w, uint16_t map_h);
static void s_refresh_camera_scale_cache(uint16_t map_w, uint16_t map_h, uint16_t src_w, uint16_t src_h);
static uint32_t s_display_frame_budget_ms(void);
static uint8_t s_flush_temp_draw(void);
static void s_submit_rgb565_block(uint16_t sx,
                                  uint16_t sy,
                                  uint16_t ex,
                                  uint16_t ey,
                                  uint16_t *pixels);
static void s_clean_dcache_by_addr(const void *addr, uint32_t size);
static uint16_t s_blend_rgb565(uint16_t bg, uint16_t fg, uint8_t alpha);
static uint8_t s_overlay_alpha_from_norm(uint8_t norm);
static void s_clear_scene_gutters(void);
static void s_update_camera_cache_from_frame(const App_CameraFrame_t *camera_frame);
static uint8_t s_blit_camera_cache_region(uint16_t dst_x0,
                                          uint16_t dst_y0,
                                          uint16_t width,
                                          uint16_t height,
                                          uint16_t src_x0,
                                          uint16_t src_y0);
static void s_render_camera_frame_rows(const App_CameraFrame_t *camera_frame);

/* 闁氨鏁ら柦鍏呯秴瀹搞儱鍙块敍? * 閺勫墽銇氶柧鎹愮熅娑擃厼鐡ㄩ崷銊ャ亣闁插繆鈧粎鏁ら幋宄板讲鐠嬪啫寮弫鎵斥偓婵嗘嫲閳ユ粍璇為悙纭呮祮閺佸瓨鏆熼垾婵堟畱鏉╁洨鈻奸敍? * 闁藉厖缍呴崙鑺ユ殶閻劋绨穱婵婄槈閺佺増宓佹稉宥勭窗鐡掑﹨绻冮崥鍫熺《鏉堝湱鏅妴?*/
static float s_clamp_f32(float v, float lo, float hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

/* 鐏?8 娴ｅ秵妫ょ粭锕€褰块崐濂告閸掕泛婀?`[lo, hi]` 閼煎啫娲块崘鍛偓? * 娑撴槒顩﹂悽銊ょ艾鏉╂劘顢戦弮鍫曞帳缂冾喚娈戦弸姘/鐏忓繗瀵栭崶鏉戝棘閺侀鎱ㄥ锝冣偓?*/
static uint8_t s_clamp_u8(uint8_t v, uint8_t lo, uint8_t hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

/* 鐏忓棙婀佺粭锕€褰挎稉顓㈡？閸婂ジ妾洪崚璺哄煂 16 娴ｅ秵妫ょ粭锕€褰块崥鍫熺《閼煎啫娲块妴? * 鐢摜鏁ゆ禍搴″剼缁辩姴娼楅弽鍥吀缁犳鐣幋鎰倵閻ㄥ嫬鐣ㄩ崗銊ㄦ儰閸﹁埇鈧?*/
static uint16_t s_clamp_u16(int32_t v, uint16_t lo, uint16_t hi)
{
    if (v < (int32_t)lo)
    {
        return lo;
    }
    if (v > (int32_t)hi)
    {
        return hi;
    }
    return (uint16_t)v;
}

/* 閹?8bit 閻?R/G/B 娑撳鈧岸浜鹃崢瀣級娑?RGB565閵? * 鏉╂瑦妲告潪顖欐閸ョ偤鈧偓閻偓閼硅尪鐭惧鍕▏閻劎娈戦崺铏诡攨妫版粏澹婇幍鎾冲瘶閸戣姤鏆熼妴?*/
static uint16_t s_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8u) << 8) |
                      ((uint16_t)(g & 0xFCu) << 3) |
                      ((uint16_t)b >> 3));
}

/* 鏉堟挸鍙?`0.0f ~ 1.0f` 閻ㄥ嫮鍎规惔锔界槷娓氬绱濇潏鎾冲毉鐎电懓绨查惃?RGB565 閻戭厼濮忛崶楣冾杹閼瑰眰鈧? * 閸戣姤鏆熼崘鍛村劥闁俺绻?5 娑擃亣澹婇弽鍥ㄥ付閸掑墎鍋ｉ崑姘瀻濞堢數鍤庨幀褎褰冮崐绗衡偓?*/
static uint16_t s_heat_color(float t)
{
    /* 妫版粏澹婂〒鎰綁闁插洨鏁ょ亸鎴﹀櫤閹貉冨煑閻愬湱鍤庨幀褎褰冮崐纭风礉閼板奔绗夐弰顖濈箥鐞涘本妞傜拋锛勭暬婢跺秵娼呴懝鎻掓禈閵?     * 鏉╂瑦鐗遍弮銏狀啇閺勬捁鐨熼懝璇х礉娑旂喍绌舵禍搴℃倵缂侇參顣╅悽鐔稿灇 256 缁?LUT閵?*/
    typedef struct
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } rgb8_t;

    static const rgb8_t k_stops[5] = {
        {  7u,  10u,  15u},
        { 19u,  28u,  38u},
        { 33u,  58u,  66u},
        {176u,  92u,  36u},
        {255u,  34u,  20u}
    };

    float x = s_clamp_f32(t, 0.0f, 1.0f) * 4.0f;
    uint32_t seg = (uint32_t)x;
    float k;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if (seg >= 4u)
    {
        return s_rgb565(k_stops[4].r, k_stops[4].g, k_stops[4].b);
    }

    k = x - (float)seg;
    r = (uint8_t)((1.0f - k) * (float)k_stops[seg].r + k * (float)k_stops[seg + 1u].r);
    g = (uint8_t)((1.0f - k) * (float)k_stops[seg].g + k * (float)k_stops[seg + 1u].g);
    b = (uint8_t)((1.0f - k) * (float)k_stops[seg].b + k * (float)k_stops[seg + 1u].b);
    return s_rgb565(r, g, b);
}

/* 閺嬪嫬缂?256 缁狙呭劰閸旀稑娴樻０婊嗗閺屻儲澹樼悰銊ｂ偓? * 閸氬海鐢婚崣顏囶洣閹峰灝宸辨惔锕€鈧棿缍旀稉铏瑰偍瀵洖宓嗛崣顖滄纯閹恒儱绶遍崚鐗堟▔缁€娲杹閼瑰眰鈧?*/
static void s_build_heat_lut(void)
{
    /* 妫板嫬鍘涢幎?0..255 閻ㄥ嫮鍎规惔锔剧搼缁狙嗘祮閹广垺鍨?RGB565閿涘矂浼╅崗宥夆偓鎰剼缁辩娀鍣告径宥囩暬妫版粏澹婇妴?*/
    uint32_t i;

    for (i = 0u; i < APP_DISPLAY_HEAT_LUT_SIZE; i++)
    {
        s_heat_lut[i] = s_heat_color((float)i / 255.0f);
    }
}

/* 閺嬪嫬缂撻弰鍓с仛濡€虫健閹碘偓闂団偓閻ㄥ嫬鍙忛柈銊︾壋閸戣姤鏆熼妴? * 閸栧懏瀚敍? * - 娑撯偓缂佸瓨膩缁﹥鐗抽敍姘返濡亜鎮?缁鹃潧鎮滈崣顖氬瀻缁傝閽╁? * - 娴滃瞼娣紒鍡氱€洪崥鍫熺壋閿涙矮绶电紒鍡樻偝缁便垼鍏橀柌蹇斿⒖閺? *
 * 鐠囥儱鍤遍弫鏉垮徔婢跺洠鈧粌褰ч崚婵嗩潗閸栨牔绔村▎鈾€鈧繄娈戞穱婵囧Б閵?*/
static void s_build_kernels(void)
{
    /* 閺堫剙鍤遍弫鏉垮涧闂団偓閹笛嗩攽娑撯偓濞嗏槄绱?
     * - 閺嬪嫬缂撴稉鈧紒鏉戦挬濠婃垶鐗抽敍灞肩返濡亜鎮?缁鹃潧鎮滄稉銈嗩偧閸楅袧婢跺秶鏁?
     * - 閺嬪嫬缂撴禍宀€娣紒鍡氱€洪崥鍫熺壋閿涘本濡哥粋缁樻殠缂佸棗鍢查崐鍏煎⒖鐏炴洑璐熼弴瀛樻鐟欏倸鐧傞惃鍕厴闁插繐娲?*/
    uint32_t i;
    uint32_t j;
    float sum = 0.0f;

    if (s_kernel_ready != 0u)
    {
        return;
    }

    for (i = 0u; i < APP_DISPLAY_BLUR_KERNEL_LEN; i++)
    {
        int32_t dx = (int32_t)i - (int32_t)APP_DISPLAY_SMOOTH_RADIUS;
        float w = expf(-((float)(dx * dx)) / (2.0f * APP_DISPLAY_SMOOTH_SIGMA * APP_DISPLAY_SMOOTH_SIGMA));
        s_blur_kernel[i] = w;
        sum += w;
    }
    if (sum > 0.0f)
    {
        for (i = 0u; i < APP_DISPLAY_BLUR_KERNEL_LEN; i++)
        {
            s_blur_kernel[i] /= sum;
        }
    }

    sum = 0.0f;
    for (i = 0u; i < APP_DISPLAY_FINE_KERNEL_LEN; i++)
    {
        for (j = 0u; j < APP_DISPLAY_FINE_KERNEL_LEN; j++)
        {
            int32_t dx = (int32_t)i - (int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS;
            int32_t dy = (int32_t)j - (int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS;
            float d2 = (float)(dx * dx + dy * dy);
            float w = expf(-d2 / (2.0f * APP_DISPLAY_FINE_KERNEL_SIGMA * APP_DISPLAY_FINE_KERNEL_SIGMA));
            s_fine_kernel[i * APP_DISPLAY_FINE_KERNEL_LEN + j] = w;
            sum += w;
        }
    }
    if (sum > 0.0f)
    {
        for (i = 0u; i < APP_DISPLAY_FINE_KERNEL_LEN * APP_DISPLAY_FINE_KERNEL_LEN; i++)
        {
            s_fine_kernel[i] /= sum;
        }
    }

    s_kernel_ready = 1u;
}

/* 閹跺﹥膩瀵繑鐏囨稉鎹愭祮閹诡澀璐熼惌顓熺垼缁涙儳鐡х粭锔胯閿涘奔瀵岀憰浣虹舶娓氀嗙珶閺嶅繑妯夌粈杞板▏閻劊鈧?*/
const char *App_Display_ModeName(App_Display_Mode_t mode)
{
    switch (mode)
    {
        case APP_DISPLAY_MODE_FAST:
            return "FAST";
        case APP_DISPLAY_MODE_CLEAN:
            return "CLEAN";
        case APP_DISPLAY_MODE_BALANCED:
        default:
            return "BAL";
    }
}

/* 閹跺﹥褰冮崐鍏寄佸蹇氭祮閹诡澀璐熼惌顓熺垼缁涙儳鐡х粭锔胯閵?*/
const char *App_Display_InterpName(App_Display_Interp_t interp)
{
    if (interp == APP_DISPLAY_INTERP_BILINEAR)
    {
        return "BIL";
    }
    return "NEAR";
}

/* 閹跺﹤缍婃稉鈧崠鏍佸蹇氭祮閹诡澀璐熼惌顓熺垼缁涙儳鐡х粭锔胯閵?*/
const char *App_Display_NormName(App_Display_Norm_t norm)
{
    if (norm == APP_DISPLAY_NORM_FULL)
    {
        return "FULL";
    }
    return "FAST";
}

const char *App_Display_CameraViewName(App_Display_CameraView_t view_mode)
{
    switch (view_mode)
    {
        case APP_DISPLAY_CAMERA_VIEW_CAMERA_ONLY:
            return "CAM";
        case APP_DISPLAY_CAMERA_VIEW_CAMERA_FREEZE:
            return "FRZ";
        case APP_DISPLAY_CAMERA_VIEW_HEAT_ONLY:
            return "HEAT";
        case APP_DISPLAY_CAMERA_VIEW_OVERLAY:
        default:
            return "OVLY";
    }
}

/* 閺嶈宓佹０鍕啎濡€崇础鐟佸懓娴囨稉鈧紒鍕腹閼芥劕寮弫鑸偓? * 濞夈劍鍓版潻娆撳櫡閸欘亝妲告繅顐㈠帠 `cfg`閿涘瞼婀″锝囨晸閺佸牅绮涢棁鈧拫鍐暏 `App_Display_SetConfig`閵?*/
static void s_load_mode_defaults(App_Display_Mode_t mode, App_Display_RuntimeCfg_t *cfg)
{
    /* 妫板嫯顔曞Ο鈥崇础娑撳秵妲哥粻鈧崡鏇犳畱閳ユ粌宕熼崣鍌涙殶瀵偓閸忔枼鈧繐绱濋懓灞炬Ц閺佸绮嶉崣鍌涙殶閸楀繐鎮撶拫鍐╂殻閵?     * 鏉╂瑦鐗遍崣顖欎簰娣囨繆鐦夐悽銊﹀煕閸掑洦宕插Ο鈥崇础閸氬函绱濋弰鍓с仛鐟欏倹鍔呴弰顖涘灇娴ｆ挾閮撮崣妯哄閻ㄥ嫨鈧?*/
    if (cfg == NULL)
    {
        return;
    }

    switch (mode)
    {
        case APP_DISPLAY_MODE_FAST:
            cfg->ema_attack = APP_DISPLAY_MODE_FAST_EMA_ATTACK;
            cfg->ema_decay = APP_DISPLAY_MODE_FAST_EMA_DECAY;
            cfg->db_floor = APP_DISPLAY_MODE_FAST_DB_FLOOR;
            cfg->fine_gain = APP_DISPLAY_MODE_FAST_FINE_GAIN;
            cfg->gamma = APP_DISPLAY_MODE_FAST_GAMMA;
            cfg->noise_gate_ratio = APP_DISPLAY_MODE_FAST_NOISE_GATE_RATIO;
            cfg->noise_adapt_gain = APP_DISPLAY_MODE_FAST_NOISE_ADAPT_GAIN;
            cfg->smooth_passes = APP_DISPLAY_MODE_FAST_SMOOTH_PASSES;
            cfg->fine_fusion_enable = APP_DISPLAY_MODE_FAST_FINE_FUSION_ENABLE;
            cfg->draw_coarse_grid = APP_DISPLAY_MODE_FAST_DRAW_COARSE_GRID;
            cfg->interp_mode = (APP_DISPLAY_MODE_FAST_INTERP_BILINEAR != 0u) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST;
            cfg->norm_mode = (APP_DISPLAY_MODE_FAST_NORM_FULL != 0u) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST;
            cfg->text_refresh_div = APP_DISPLAY_MODE_FAST_TEXT_REFRESH_DIV;
            cfg->blit_rows = APP_DISPLAY_MODE_FAST_BLIT_ROWS;
            break;

        case APP_DISPLAY_MODE_CLEAN:
            cfg->ema_attack = APP_DISPLAY_MODE_CLEAN_EMA_ATTACK;
            cfg->ema_decay = APP_DISPLAY_MODE_CLEAN_EMA_DECAY;
            cfg->db_floor = APP_DISPLAY_MODE_CLEAN_DB_FLOOR;
            cfg->fine_gain = APP_DISPLAY_MODE_CLEAN_FINE_GAIN;
            cfg->gamma = APP_DISPLAY_MODE_CLEAN_GAMMA;
            cfg->noise_gate_ratio = APP_DISPLAY_MODE_CLEAN_NOISE_GATE_RATIO;
            cfg->noise_adapt_gain = APP_DISPLAY_MODE_CLEAN_NOISE_ADAPT_GAIN;
            cfg->smooth_passes = APP_DISPLAY_MODE_CLEAN_SMOOTH_PASSES;
            cfg->fine_fusion_enable = APP_DISPLAY_MODE_CLEAN_FINE_FUSION_ENABLE;
            cfg->draw_coarse_grid = APP_DISPLAY_MODE_CLEAN_DRAW_COARSE_GRID;
            cfg->interp_mode = (APP_DISPLAY_MODE_CLEAN_INTERP_BILINEAR != 0u) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST;
            cfg->norm_mode = (APP_DISPLAY_MODE_CLEAN_NORM_FULL != 0u) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST;
            cfg->text_refresh_div = APP_DISPLAY_MODE_CLEAN_TEXT_REFRESH_DIV;
            cfg->blit_rows = APP_DISPLAY_MODE_CLEAN_BLIT_ROWS;
            break;

        case APP_DISPLAY_MODE_BALANCED:
        default:
            cfg->ema_attack = APP_DISPLAY_EMA_ATTACK;
            cfg->ema_decay = APP_DISPLAY_EMA_DECAY;
            cfg->db_floor = APP_DISPLAY_DYNAMIC_DB_FLOOR;
            cfg->fine_gain = APP_DISPLAY_FINE_GAIN;
            cfg->gamma = APP_DISPLAY_DYNAMIC_GAMMA;
            cfg->noise_gate_ratio = APP_DISPLAY_NOISE_GATE_RATIO;
            cfg->noise_adapt_gain = APP_DISPLAY_NOISE_ADAPT_GAIN;
            cfg->smooth_passes = APP_DISPLAY_SMOOTH_PASSES;
            cfg->fine_fusion_enable = APP_DISPLAY_FINE_FUSION_ENABLE;
            cfg->draw_coarse_grid = APP_DISPLAY_DRAW_COARSE_GRID;
            cfg->interp_mode = (APP_DISPLAY_BILINEAR_SAMPLING != 0u) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST;
            cfg->norm_mode = (APP_DISPLAY_MODE_BALANCED_NORM_FULL != 0u) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST;
            cfg->text_refresh_div = APP_DISPLAY_TEXT_REFRESH_DIV;
            cfg->blit_rows = APP_DISPLAY_BLIT_ROWS_MAX;
            break;
    }
}

/* 閸愭瑥鍙嗘潻鎰攽閺冨爼鍘ょ純顔衡偓? * 閸忔娊鏁悙鐧哥窗
 * - 鐎佃澧嶉張澶嬫殶閸婄厧浠涢崥鍫熺《閸栨椽妫块柦鍏呯秴
 * - 鐎电懓绔风亸鏂跨磻閸忓啿浠?0/1 瑜版帊绔撮崠? * - 闁板秶鐤嗛崣妯哄閸氬簼濞囪ぐ鎺嶇閸?LUT 婢惰鲸鏅ラ敍宀€鈥樻穱婵嗘倵缂侇厽瀵滈弬鏉垮棘閺佷即鍣稿?*/
void App_Display_SetConfig(const App_Display_RuntimeCfg_t *cfg)
{
    /* 閹碘偓閺堝绻嶇悰灞炬闁板秶鐤嗛崷銊ㄧ箹闁插瞼绮烘稉鈧崑姘崇珶閻ｅ奔鎱ㄥ锝忕礉闁灝鍘ら棃鐐寸《闁板秶鐤嗛幎濠傛倵缂侇厽瑕嗛弻鎾圭熅瀵?     * 閹恒劌鍙嗛張顏勭暰娑斿濮搁幀渚婄礉娓氬顩х拹鐔烘畱 gamma閵嗕浇绻冩径褏娈?blit 鐞涘本鏆熺粵澶堚偓?*/
    if (cfg == NULL)
    {
        return;
    }

    s_cfg.ema_attack = s_clamp_f32(cfg->ema_attack, 0.01f, 1.0f);
    s_cfg.ema_decay = s_clamp_f32(cfg->ema_decay, 0.01f, 1.0f);
    s_cfg.db_floor = s_clamp_f32(cfg->db_floor, -80.0f, -6.0f);
    s_cfg.fine_gain = s_clamp_f32(cfg->fine_gain, 0.0f, 3.0f);
    s_cfg.gamma = s_clamp_f32(cfg->gamma, 0.5f, 2.5f);
    s_cfg.noise_gate_ratio = s_clamp_f32(cfg->noise_gate_ratio, 0.0f, 0.6f);
    s_cfg.noise_adapt_gain = s_clamp_f32(cfg->noise_adapt_gain, 0.0f, 6.0f);
    s_cfg.smooth_passes = s_clamp_u8(cfg->smooth_passes, 0u, 3u);
    s_cfg.fine_fusion_enable = (cfg->fine_fusion_enable != 0u) ? 1u : 0u;
    s_cfg.draw_coarse_grid = (cfg->draw_coarse_grid != 0u) ? 1u : 0u;
    s_cfg.interp_mode = (cfg->interp_mode == APP_DISPLAY_INTERP_BILINEAR) ? APP_DISPLAY_INTERP_BILINEAR : APP_DISPLAY_INTERP_NEAREST;
    s_cfg.norm_mode = (cfg->norm_mode == APP_DISPLAY_NORM_FULL) ? APP_DISPLAY_NORM_FULL : APP_DISPLAY_NORM_FAST;
    s_cfg.text_refresh_div = s_clamp_u8(cfg->text_refresh_div, 1u, 20u);
    s_cfg.blit_rows = s_clamp_u8(cfg->blit_rows, 1u, APP_DISPLAY_BLIT_ROWS_MAX);
    s_norm_lut_valid = 0u;
}

/* 鐠囪褰囪ぐ鎾冲閻㈢喐鏅ラ柊宥囩枂閸掓媽鐨熼悽銊ㄢ偓鍛絹娓氭稓娈戠紒鎾寸€担鎾茶厬閵?*/
void App_Display_GetConfig(App_Display_RuntimeCfg_t *cfg)
{
    if (cfg != NULL)
    {
        *cfg = s_cfg;
    }
}

void App_Display_GetDebugStats(App_Display_DebugStats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    stats->camera_view_mode = (uint8_t)s_camera_view_mode;
    stats->camera_path_count = s_dbg_camera_path_count;
    stats->camera_overlay_count = s_dbg_camera_overlay_count;
    stats->camera_input_seq = s_dbg_camera_input_seq;
    stats->camera_cache_seq = s_camera_cache_seq;
    stats->camera_cache_valid = s_camera_cache_valid;
}

void App_Display_SetCameraView(App_Display_CameraView_t view_mode)
{
    if ((view_mode != APP_DISPLAY_CAMERA_VIEW_OVERLAY) &&
        (view_mode != APP_DISPLAY_CAMERA_VIEW_CAMERA_ONLY) &&
        (view_mode != APP_DISPLAY_CAMERA_VIEW_HEAT_ONLY) &&
        (view_mode != APP_DISPLAY_CAMERA_VIEW_CAMERA_FREEZE))
    {
        view_mode = APP_DISPLAY_CAMERA_VIEW_OVERLAY;
    }

    s_camera_view_mode = view_mode;
    s_camera_freeze_w = 0u;
    s_camera_freeze_h = 0u;
    s_camera_freeze_stride = 0u;
    s_camera_freeze_valid = 0u;
}

App_Display_CameraView_t App_Display_GetCameraView(void)
{
    return s_camera_view_mode;
}

/* 閸掑洦宕查弰鍓с仛濡€崇础閵? * 閼汇儴绶崗銉╂姜濞夋洘膩瀵骏绱濋崚娆忔礀闁偓閸?`APP_DISPLAY_MODE_BALANCED`閵?*/
void App_Display_SetMode(App_Display_Mode_t mode)
{
    App_Display_RuntimeCfg_t mode_cfg;

    if ((mode != APP_DISPLAY_MODE_FAST) &&
        (mode != APP_DISPLAY_MODE_BALANCED) &&
        (mode != APP_DISPLAY_MODE_CLEAN))
    {
        mode = APP_DISPLAY_MODE_BALANCED;
    }

    s_load_mode_defaults(mode, &mode_cfg);
    s_mode = mode;
    App_Display_SetConfig(&mode_cfg);
}

/* 鏉╂柨娲栬ぐ鎾冲閺勫墽銇氬Ο鈥崇础閵?*/
App_Display_Mode_t App_Display_GetMode(void)
{
    return s_mode;
}

/* 鏉╂柨娲栭弰鍓с仛濡€虫健閺勵垰鎯佸鎻掔暚閹存劕鍨垫慨瀣閵?*/
uint8_t App_Display_IsReady(void)
{
    return s_ready;
}

/* 瀵倹顒炴繅顐㈠帠閻晛鑸伴崠鍝勭厵閵? * 娴兼ê鍘涚挧?LTDC/DMA2D 瀵倹顒炵捄顖氱窞閿涙稑銇戠拹銉︽閸ョ偤鈧偓閸?`lcd_fill`閵?*/
static void s_fill_rect_async(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color)
{
    /* 娴兼ê鍘涚亸婵婄槸瀵倹顒為崝鐘烩偓鐔凤綖閸忓拑绱遍懟銉ョ俺鐏炲倷绗夐弨顖涘瘮閹存牗褰佹禍銈呫亼鐠愩儻绱濋崚娆忔礀闁偓閸掓澘鎮撳銉ㄨ拫娴犳儼鐭惧鍕┾偓?     * 鏉╂瑦鐗辨稉濠傜湴閺冪娀娓堕崗鍐茬妇瑜版挸澧犻獮鍐插酱閺勵垰鎯侀崗宄邦槵 DMA2D/LTDC 閸旂娀鈧喕鍏橀崝娑栤偓?*/
    if ((x0 > x1) || (y0 > y1))
    {
        return;
    }

    if (ltdc_fill_async(x0, y0, x1, y1, color) == 0u)
    {
        lcd_fill(x0, y0, x1, y1, color);
    }
}

/* 缂佹ê鍩楀鏉戦挬缁惧尅绱濋張顒冨窛娑撳﹥妲告妯哄娑?1 閻ㄥ嫮鐓╄ぐ銏狅綖閸忓懌鈧?*/
static void s_draw_hline_async(uint16_t x0, uint16_t y, uint16_t x1, uint32_t color)
{
    s_fill_rect_async(x0, y, x1, y, color);
}

/* 缂佹ê鍩楅崹鍌滄纯缁惧尅绱濋張顒冨窛娑撳﹥妲哥€硅棄瀹虫稉?1 閻ㄥ嫮鐓╄ぐ銏狅綖閸忓懌鈧?*/
static void s_draw_vline_async(uint16_t x, uint16_t y0, uint16_t y1, uint32_t color)
{
    s_fill_rect_async(x, y0, x, y1, color);
}

/* 缂佹ê鍩楃粚鍝勭妇閻晛鑸版潏瑙勵攱閵? * 闁俺绻冮崶娑欐蒋鏉堝湱绮嶉崥鍫ｂ偓灞惧灇閿涘奔绗夐崡鏇犲婵夘偄鍘栭崘鍛村劥閸栧搫鐓欓妴?*/
static void s_draw_rect_async(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color)
{
    s_draw_hline_async(x0, y0, x1, color);
    s_draw_hline_async(x0, y1, x1, color);
    s_draw_vline_async(x0, y0, y1, color);
    s_draw_vline_async(x1, y0, y1, color);
}

/* 鐠囧棗鍩嗚ぐ鎾冲閸氬骸褰寸紓鎾冲暱鐏炵偘绨崫顏冪娑擃亝蝎娴ｅ秲鈧? * 鏉╂柨娲栭崐鑲╁鐎规熬绱?
 * - `0`閿涙艾鎮楅崣鎵处閸愯尙鐡戞禍?A
 * - `1`閿涙艾鎮楅崣鎵处閸愯尙鐡戞禍?B
 * - `0xFF`閿涙碍妫ゅ▔鏇＄槕閸?*/
static uint8_t s_backbuf_slot(void)
{
    /* 閺嶈宓侀崥搴″酱缂傛挸鍟块崷鏉挎絻閸掋倖鏌囪ぐ鎾冲濮濓絽婀紒妯哄煑閻ㄥ嫭妲?A 鏉╂ɑ妲?B閵?     * 閺傚洦婀伴棃銏℃緲閸掗攱鏌婇懞鍌涚ウ闁槒绶笟婵婄鏉╂瑤閲滃Σ鎴掔秴娣団剝浼呴敍宀勪缉閸忓秴寮荤紓鎾冲暱閸掑洦宕查崥搴ゅΝ濞翠胶濮搁幀渚€鏁婃稊渚库偓?*/
    uint32_t back_addr = ltdc_get_backbuf_addr();

    if (back_addr == s_fb_addr_a)
    {
        return 0u;
    }
    if (back_addr == s_fb_addr_b)
    {
        return 1u;
    }
    return 0xFFu;
}

/* 閹绘劒姘﹂張顒€鎶氱紒妯哄煑缂佹挻鐏夐妴? * 閸栧懏瀚崘鎻掑煕鎼存洖鐪扮紒妯哄煑闂冪喎鍨禒銉ュ挤閻㈠疇顕?front/back swap閵?*/
static uint32_t s_display_frame_budget_ms(void)
{
    uint32_t fps = App_RuntimeConfig_GetUiTargetFps();
    if (fps < UI_FPS_MIN)
    {
        fps = UI_FPS_MIN;
    }
    if (fps > UI_FPS_MAX)
    {
        fps = UI_FPS_MAX;
    }
    return (1000u + fps - 1u) / fps;
}
static uint8_t s_flush_temp_draw(void)
{
    if (ltdc_draw_flush(APP_DISPLAY_DMA2D_TIMEOUT) != 0u)
    {
        DMA2D_Accel_Reset();
        return 0u;
    }
    return 1u;
}
static void s_submit_rgb565_block(uint16_t sx,
                                  uint16_t sy,
                                  uint16_t ex,
                                  uint16_t ey,
                                  uint16_t *pixels)
{
    if ((pixels == NULL) || (sx > ex) || (sy > ey))
    {
        return;
    }
    if (ltdc_color_fill_async(sx, sy, ex, ey, pixels) == 0u)
    {
        return;
    }
    if (s_flush_temp_draw() != 0u)
    {
        return;
    }
    DMA2D_Accel_Reset();
    lcd_color_fill(sx, sy, ex, ey, pixels);
}

static void s_clean_dcache_by_addr(const void *addr, uint32_t size)
{
#if (__DCACHE_PRESENT == 1U)
    uintptr_t start_addr;
    uintptr_t end_addr;
    uintptr_t aligned_addr;
    uint32_t aligned_size;

    if ((addr == NULL) || (size == 0u))
    {
        return;
    }

    start_addr = (uintptr_t)addr;
    end_addr = start_addr + (uintptr_t)size;
    aligned_addr = start_addr & ~(uintptr_t)31u;
    aligned_size = (uint32_t)(((end_addr + 31u) & ~(uintptr_t)31u) - aligned_addr);
    SCB_CleanDCache_by_Addr((uint32_t *)aligned_addr, (int32_t)aligned_size);
#else
    (void)addr;
    (void)size;
#endif
}

static uint16_t s_blend_rgb565(uint16_t bg, uint16_t fg, uint8_t alpha)
{
    uint32_t a = (uint32_t)alpha;
    uint32_t inv = 255u - a;
    uint32_t bg_r = (bg >> 11) & 0x1Fu;
    uint32_t bg_g = (bg >> 5) & 0x3Fu;
    uint32_t bg_b = bg & 0x1Fu;
    uint32_t fg_r = (fg >> 11) & 0x1Fu;
    uint32_t fg_g = (fg >> 5) & 0x3Fu;
    uint32_t fg_b = fg & 0x1Fu;
    uint32_t out_r = (bg_r * inv + fg_r * a + 127u) / 255u;
    uint32_t out_g = (bg_g * inv + fg_g * a + 127u) / 255u;
    uint32_t out_b = (bg_b * inv + fg_b * a + 127u) / 255u;

    return (uint16_t)((out_r << 11) | (out_g << 5) | out_b);
}
static uint8_t s_overlay_alpha_from_norm(uint8_t norm)
{
    uint32_t scaled;
    uint32_t curved;

    if (norm <= 24u)
    {
        return 0u;
    }

    scaled = ((uint32_t)(norm - 24u) * 255u + 115u) / 231u;
    if (scaled > 255u)
    {
        scaled = 255u;
    }
    curved = (scaled * scaled + 127u) / 255u;
    if (curved > 255u)
    {
        curved = 255u;
    }
    return (uint8_t)((curved * ((uint32_t)APP_CAMERA_OVERLAY_ALPHA_MAX / 2u) + 127u) / 255u);
}
static void s_clear_scene_gutters(void)
{
    uint16_t left_w = s_text_x;
    uint16_t screen_h = lcddev.height;

    if ((left_w == 0u) || (screen_h == 0u) ||
        (s_map_x1 < s_map_x0) || (s_map_y1 < s_map_y0))
    {
        return;
    }

    if (s_map_y0 > 0u)
    {
        s_fill_rect_async(0u, 0u, (uint16_t)(left_w - 1u), (uint16_t)(s_map_y0 - 1u), BLACK);
    }
    if (((uint32_t)s_map_y1 + 1u) < (uint32_t)screen_h)
    {
        s_fill_rect_async(0u,
                          (uint16_t)(s_map_y1 + 1u),
                          (uint16_t)(left_w - 1u),
                          (uint16_t)(screen_h - 1u),
                          BLACK);
    }
    if (s_map_x0 > 0u)
    {
        s_fill_rect_async(0u, s_map_y0, (uint16_t)(s_map_x0 - 1u), s_map_y1, BLACK);
    }
    if (((uint32_t)s_map_x1 + 1u) < (uint32_t)left_w)
    {
        s_fill_rect_async((uint16_t)(s_map_x1 + 1u),
                          s_map_y0,
                          (uint16_t)(left_w - 1u),
                          s_map_y1,
                          BLACK);
    }
}
static void s_commit_frame(void)
{
    /* 閹绘劒姘﹂梼鑸殿唽閸掑棔琚卞銉窗
     * 1. 缁涘绶?閸掗攱鏌婃惔鏇炵湴缂佹ê鍩楅梼鐔峰灙
     * 2. 閼汇儱缍嬮崜宥嗙梾閺堝婀€瑰本鍨氭禍銈嗗床閿涘苯鍟€閻㈠疇顕稉鈧▎?front/back swap
     *
     * 閼?DMA2D 鐠侯垰绶炲鍌氱埗鐡掑懏妞傞敍灞肩窗娑撹濮╂径宥勭秴閸旂娀鈧喎娅掗敍宀勬Щ濮濄垹鎮楃紒顓炴姎闂€鎸庢埂閸椻剝顒撮妴?*/
    if (ltdc_draw_flush(APP_DISPLAY_DMA2D_TIMEOUT) != 0u)
    {
        DMA2D_Accel_Reset();
        return;
    }

    if (ltdc_is_swap_pending() == 0u)
    {
        ltdc_request_swap();
    }
}

/* 閸掓繂顫愰崠鏍ㄦ▔缁€鐑樐侀崸妞尖偓? * 鏉╂瑦妲搁弫缈犻嚋濡€虫健閻ㄥ嫪绗傞悽闈涘弳閸欙綇绱濈拹鐔荤煑閿? * - 瀵ら缚銆?
 * - 閸掓繂顫愰崠?LCD
 * - 鐠侊紕鐣荤敮鍐ㄧ湰
 * - 閼惧嘲褰囩敮褏绱﹂崘鎻掓勾閸р偓
 * - 缂佹ê鍩楁＃鏍ф姎鏉堣顢嬮崪宀冨剹閺?*/
void App_Display_Init(void)
{
    uint16_t draw_w;
    uint16_t draw_h;
    uint16_t camera_w;
    uint16_t camera_h;
    uint16_t heat_w;
    uint16_t heat_h;
    g_display_init_stage = 1u;
    g_display_init_error = 0u;
    s_ready = 0u;
    s_build_kernels();
    s_build_heat_lut();
    s_peak_ema = APP_DISPLAY_EMA_MIN_PEAK;
    s_last_noise_floor = 0.0f;
    s_fb_addr_a = 0u;
    s_fb_addr_b = 0u;
    s_cache_map_w = 0u;
    s_cache_map_h = 0u;
    s_camera_cache_map_w = 0u;
    s_camera_cache_map_h = 0u;
    s_camera_cache_src_w = 0u;
    s_camera_cache_src_h = 0u;
    s_camera_cache_seq = 0u;
    s_camera_cache_valid = 0u;
    s_camera_freeze_w = 0u;
    s_camera_freeze_h = 0u;
    s_camera_freeze_stride = 0u;
    s_camera_freeze_valid = 0u;
    s_dbg_camera_path_count = 0u;
    s_dbg_camera_overlay_count = 0u;
    s_dbg_camera_input_seq = 0u;
    s_camera_view_mode = APP_DISPLAY_CAMERA_VIEW_OVERLAY;
    s_norm_lut_valid = 0u;
    memset(s_spectrum_ema, 0, sizeof(s_spectrum_ema));
    memset(&s_last_spectrum_frame, 0, sizeof(s_last_spectrum_frame));
    s_spectrum_ref_mag = APP_DISPLAY_SPECTRUM_MIN_MAG;
    s_spectrum_ema_valid = 0u;
    s_spectrum_frame_valid = 0u;
    App_Display_SetMode((App_Display_Mode_t)APP_DISPLAY_DEFAULT_MODE);
    g_display_init_stage = 2u;
    lcd_init();
    g_display_init_stage = 3u;
    draw_w = lcddev.width;
    draw_h = lcddev.height;
    if ((draw_w == 0u) || (draw_h == 0u))
    {
        g_display_init_error = 1u;
        g_display_init_stage = 0xE001u;
        return;
    }
    if ((draw_w <= APP_DISPLAY_UI_PANEL_W) || (draw_h < 32u))
    {
        g_display_init_error = 2u;
        g_display_init_stage = 0xE002u;
        return;
    }
    camera_w = (uint16_t)(draw_w - APP_DISPLAY_UI_PANEL_W);
    camera_h = draw_h;
    heat_w = (camera_w < APP_DISPLAY_HEAT_VIEW_W) ? camera_w : (uint16_t)APP_DISPLAY_HEAT_VIEW_W;
    heat_h = (camera_h < APP_DISPLAY_HEAT_VIEW_H) ? camera_h : (uint16_t)APP_DISPLAY_HEAT_VIEW_H;
    if ((heat_w < 32u) || (heat_h < 32u))
    {
        g_display_init_error = 3u;
        g_display_init_stage = 0xE003u;
        return;
    }
    s_map_x0 = (uint16_t)((camera_w - heat_w) / 2u);
    s_map_y0 = (uint16_t)((camera_h - heat_h) / 2u);
    s_map_x1 = (uint16_t)(s_map_x0 + heat_w - 1u);
    s_map_y1 = (uint16_t)(s_map_y0 + heat_h - 1u);
    s_camera_x0 = s_map_x0;
    s_camera_y0 = s_map_y0;
    s_camera_x1 = s_map_x1;
    s_camera_y1 = s_map_y1;
    s_text_x = camera_w;
    s_ui_x1 = (uint16_t)(draw_w - 1u);
    s_refresh_render_map_cache((uint16_t)(s_map_x1 - s_map_x0 + 1u),
                               (uint16_t)(s_map_y1 - s_map_y0 + 1u));
    g_display_init_stage = 4u;
    s_fb_addr_a = ltdc_get_frontbuf_addr();
    s_fb_addr_b = ltdc_get_backbuf_addr();
    DMA2D_Accel_LoadClutFromRgb565(s_heat_lut, APP_DISPLAY_HEAT_LUT_SIZE);
    s_fill_rect_async(0u, 0u, (uint16_t)(draw_w - 1u), (uint16_t)(draw_h - 1u), BLACK);
    s_draw_rect_async(s_map_x0, s_map_y0, s_map_x1, s_map_y1, WHITE);
    if (s_text_x > 0u)
    {
        s_draw_vline_async((uint16_t)(s_text_x - 1u), 0u, (uint16_t)(draw_h - 1u), WHITE);
    }
    s_commit_frame();
    s_ready = 1u;
    g_display_init_stage = 0x8000u;
}

/* 鐎靛湱鐣诲▔鏇炲閻滃洤鈧厧浠涢張澶嬫櫏閹嗙箖濠娿們鈧? * 閸欘亙绻氶悾娆屸偓婊勬箒闂勬劒绗栨径褌绨?0閳ユ繄娈戦崐纭风礉閸忔湹缍戦崗銊╁劥鐟欏棔璐?0閵?*/
static float s_power_mag(float v)
{
    /* 缁犳纭舵潏鎾冲毉娑擃叀瀚㈤崙铏瑰箛 NaN閵嗕浮nf 閹存牞绀嬮崐纭风礉鏉╂瑩鍣风紒鐔剁鐟欏棔璐熼弮鐘虫櫏閼充粙鍣洪妴?     * 閺勫墽銇氬Ο鈥虫健閸欘亝甯撮崣妞烩偓婊勬箒闂勬劒绗栭棃鐐剁閳ユ繄娈戦崝鐔哄芳閸婄鈧?*/
    if (!isfinite(v) || (v <= 0.0f))
    {
        return 0.0f;
    }
    return v;
}

/* 閹稿绱拠鎴炴埂瀵偓閸忚櫕濡哥粻妤佺《鐟欐帒瀹抽崸鎰垼閺勭姴鐨犻崚鐗堟▔缁€鍝勬綏閺嶅洨閮撮妴?*/
static void s_apply_output_remap(float *x_angle, float *y_angle)
{
    /* 閺勫墽銇氶崸鎰垼缁绗岀粻妤佺《閸ф劖鐖ｇ化璇插讲閼虫垝绗夌€瑰苯鍙忔稉鈧懛娣偓?     * 鏉╂瑩鍣烽幐澶岀椽鐠囨垶婀″鈧崗铏⒔鐞涘奔姘﹂幑銏ｉ叡/缂堟槒娴嗘潪杈剧礉娣囨繆鐦夐悽濠氭桨閺傜懓鎮滅粭锕€鎮庣仦蹇撶鐎瑰顥婇弬鐟扮础閵?*/
#if (SRP_OUTPUT_SWAP_XY != 0u)
    {
        float t = *x_angle;
        *x_angle = *y_angle;
        *y_angle = t;
    }
#endif
#if (SRP_OUTPUT_INVERT_X != 0u)
    *x_angle = -*x_angle;
#endif
#if (SRP_OUTPUT_INVERT_Y != 0u)
    *y_angle = -*y_angle;
#endif
}

/* 閹笛嗩攽娑?`s_apply_output_remap` 閻╃寮介惃鍕綏閺嶅洦妲х亸鍕┾偓?*/
static void s_inverse_output_remap(float *x_angle, float *y_angle)
{
    /* 娑?`s_apply_output_remap` 閻╃寮介敍宀€鏁ゆ禍搴濈矤閺勫墽銇氶崸鎰垼閸ョ偛鍩岀粻妤佺《閸樼喎顫愰崸鎰垼缁眹鈧?     * 閸忕鐎烽悽銊┾偓鏃€妲搁敍姘秼閹存垳婊戦幐澶嗏偓婊冪潌楠炴洑绗傞惃鍕厙娑擃亪鍣伴弽椋庡仯閳ユ繂寮介弻銉х煐缂冩垶鐗搁崐鍏兼閿涘矂娓剁憰浣稿帥閸嬫岸鈧棙妲х亸鍕┾偓?*/
#if (SRP_OUTPUT_INVERT_Y != 0u)
    *y_angle = -*y_angle;
#endif
#if (SRP_OUTPUT_INVERT_X != 0u)
    *x_angle = -*x_angle;
#endif
#if (SRP_OUTPUT_SWAP_XY != 0u)
    {
        float t = *x_angle;
        *x_angle = *y_angle;
        *y_angle = t;
    }
#endif
}

/* 閹跺﹦鐭栭幖婊呭偍缂冩垶鐗搁柌宥夊櫚閺嶅嘲鍩岄弰鍓с仛娑擃參妫块崷?`s_field_a`閵? * 濮ｅ繋閲滈崷铏瑰仯闁粙鈧俺绻冪憴鎺戝閸欏秵鐓￠崶鐐电煐缂冩垶鐗搁敍灞借嫙閸嬫艾寮荤痪鎸庘偓褎褰冮崐绗衡偓?*/
static void s_resample_coarse_to_field(const SRP_VisFrame_t *vis_frame)
{
    /* 缁鎮崇槐銏㈢秹閺嶈偐鍋ｉ弫鎷岀窛鐏忔埊绱濇稉宥夆偓鍌氭値閻╁瓨甯存稉濠傜潌閿涘苯鎯侀崚娆庣窗閸涘牏骞囬弰搴㈡▔濡娲忛弽?閸ф濮告潏鍦櫕閵?     * 鏉╂瑩鍣烽惃鍕粵濞夋洘妲搁敍?     * 1. 闁秴宸婚弰鍓с仛娑擃參妫块崷铏规畱濮ｅ繋閲滈柌鍥ㄧ壉閻?     * 2. 閹跺﹨顕氶悙鐟邦嚠鎼存梻娈戦弰鍓с仛鐟欐帒瀹抽崣宥嗗腹閸ョ偟鐣诲▔鏇☆潡鎼达箑娼楅弽?     * 3. 閸︺劎鐭栫純鎴炵壐娑撳﹤浠涢崣宀€鍤庨幀褎褰冮崐?     * 4. 瀵版鍩屾稉鈧稉顏嗩焼鐎靛棎鈧浇绻涚紒顓犳畱濞搭喚鍋ｉ崷?`s_field_a`
     *
     * 鏉╂瑤绔村銉︽Ц閳ユ粎鐣诲▔鏇犵秹閺嶅皷鈧繂鍩岄垾婊勬▔缁€铏圭秹閺嶅皷鈧繄娈戠粭顑跨鎼囱勊夐妴?*/
    uint32_t y;
    uint32_t x;
    float span = (float)(COARSE_ANGLE_MAX_DEG - COARSE_ANGLE_MIN_DEG);
    float inv_span;

    if (span <= 1.0e-6f)
    {
        memset(s_field_a, 0, sizeof(s_field_a));
        return;
    }
    inv_span = 1.0f / span;

    for (y = 0u; y < APP_DISPLAY_FIELD_H; y++)
    {
        /* 瑜版挸澧犵悰灞筋嚠鎼存梻娈戦弰鍓с仛閸ㄥ倻娲跨憴鎺戝閵?*/
        float phi_disp = (float)COARSE_ANGLE_MAX_DEG
                       - ((float)y * span / (float)(APP_DISPLAY_FIELD_H - 1u));
        for (x = 0u; x < APP_DISPLAY_FIELD_W; x++)
        {
            /* 瑜版挸澧犻崚妤€顕惔鏃傛畱閺勫墽銇氬鏉戦挬鐟欐帒瀹抽妴?*/
            float theta_disp = (float)COARSE_ANGLE_MIN_DEG
                             + ((float)x * span / (float)(APP_DISPLAY_FIELD_W - 1u));
            /* 閸氬海鐢绘导姘Ω閺勫墽銇氱憴鎺戝闁棗褰夐幑銏犳礀缁犳纭剁憴鎺戝閸ф劖鐖ｇ化姹団偓?*/
            float theta_raw = theta_disp;
            float phi_raw = phi_disp;
            /* `tx/py` 閺勵垳鐭栫純鎴炵壐娑擃厾娈戝ù顔惧仯閸ф劖鐖ｉ妴?*/
            float tx;
            float py;
            uint32_t t0;
            uint32_t t1;
            uint32_t p0;
            uint32_t p1;
            float wt;
            float wp;
            uint32_t idx00;
            uint32_t idx01;
            uint32_t idx10;
            uint32_t idx11;
            float v00;
            float v01;
            float v10;
            float v11;
            float vt0;
            float vt1;

            s_inverse_output_remap(&theta_raw, &phi_raw);

            /* 鏉╃偟鐢荤憴鎺戝 -> 缁缍夐弽鍏艰癁閻愬湱鍌ㄥ鏇樷偓?*/
            tx = (theta_raw - (float)COARSE_ANGLE_MIN_DEG) * inv_span * (float)(COARSE_GRID_SIZE - 1u);
            py = (phi_raw - (float)COARSE_ANGLE_MIN_DEG) * inv_span * (float)(COARSE_GRID_SIZE - 1u);
            tx = s_clamp_f32(tx, 0.0f, (float)(COARSE_GRID_SIZE - 1u));
            py = s_clamp_f32(py, 0.0f, (float)(COARSE_GRID_SIZE - 1u));

            /* 閹垫儳鍩岄崣宀€鍤庨幀褎褰冮崐鑲╂畱閸ユ稐閲滈柇鑽ゅ仯閸欏﹤鍙鹃弶鍐櫢閵?*/
            t0 = (uint32_t)tx;
            p0 = (uint32_t)py;
            t1 = (t0 + 1u < COARSE_GRID_SIZE) ? (t0 + 1u) : t0;
            p1 = (p0 + 1u < COARSE_GRID_SIZE) ? (p0 + 1u) : p0;
            wt = tx - (float)t0;
            wp = py - (float)p0;

            idx00 = t0 * COARSE_GRID_SIZE + p0;
            idx01 = t1 * COARSE_GRID_SIZE + p0;
            idx10 = t0 * COARSE_GRID_SIZE + p1;
            idx11 = t1 * COARSE_GRID_SIZE + p1;

            v00 = s_power_mag(vis_frame->power[idx00]);
            v01 = s_power_mag(vis_frame->power[idx01]);
            v10 = s_power_mag(vis_frame->power[idx10]);
            v11 = s_power_mag(vis_frame->power[idx11]);
            /* 閸忓牊铆閸氭垶褰冮崐纭风礉閸愬秶鏃遍崥鎴炲絻閸婄鈧?*/
            vt0 = v00 * (1.0f - wt) + v01 * wt;
            vt1 = v10 * (1.0f - wt) + v11 * wt;
            s_field_a[y * APP_DISPLAY_FIELD_W + x] = vt0 * (1.0f - wp) + vt1 * wp;
        }
    }
}

/* 鐏忓棛绮忛幖婊呭偍缂佹挻鐏夐摶宥呮値閸ョ偞妯夌粈杞拌厬闂傛潙婧€閵? * 閸欘亣鐎洪崥鍫濆繁鎼达箒鍐绘径鐔肩彯閻ㄥ嫮绮忛悙鐧哥礉楠炶埖瀵滈悡褌绨╃紒瀛樼壋閸氭垵鎳嗛崶瀛樺⒖閺侊絻鈧?*/
static void s_apply_fine_fusion(const SRP_VisFrame_t *vis_frame)
{
#if (APP_DISPLAY_FINE_FUSION_ENABLE != 0u)
    /* 缂佸棙鎮崇槐銏ｇ€洪崥鍫㈡畱閻╊喚娈戞稉宥嗘Ц闁插秴缂撻弫鏉戠炊閸ユ拝绱濋懓灞炬Ц鐎电懓鐪柈銊ュ繁瀹勪即妾潻鎴Ｋ夐崗鍛矎閼哄倶鈧?     * 闁槒绶稉濠傚涧婢跺嫮鎮婄搾鍐差檮瀵櫣娈戠紒鍡欑秹閺嶈偐鍋ｉ敍灞借嫙閻劋绔存稉顏冪癌缂佸瓨鐗抽幎濠傜暊娴狀剚澧块弫锝呭煂閺勫墽銇氶崷鐚寸礉
     * 鏉╂瑦鐗遍弮銏ｅ厴瀵搫瀵插畡鏉库偓濂告鏉╂垹绮ㄩ弸鍕剁礉閸欏牅绗夐懛鍏呯艾閹跺﹤鎬ラ崳顏勶紣閸忋劑娼伴幎顒勭彯閵?*/
    uint32_t i;
    float peak = 0.0f;
    float theta_span = (float)(COARSE_ANGLE_MAX_DEG - COARSE_ANGLE_MIN_DEG);
    float min_keep;

    if ((s_cfg.fine_fusion_enable == 0u) || (theta_span <= 1.0e-6f))
    {
        return;
    }

    for (i = 0u; i < SRP_GRID_TOTAL; i++)
    {
        float mag = s_power_mag(vis_frame->power[i]);
        if (mag > peak)
        {
            peak = mag;
        }
    }
    if (peak <= APP_DISPLAY_DYNAMIC_MIN_PEAK)
    {
        return;
    }
    min_keep = peak * APP_DISPLAY_FINE_MIN_RATIO;

    for (i = COARSE_TOTAL; i < SRP_GRID_TOTAL; i++)
    {
        float mag = s_power_mag(vis_frame->power[i]);
        float theta;
        float phi;
        float u;
        float v;
        int32_t cx;
        int32_t cy;
        int32_t ky;
        int32_t kx;

        if (mag < min_keep)
        {
            /* 鏉╁洦鎶ら幒澶庣箖瀵京娈戠紒鍡樻偝缁便垻鍋ｉ妴?*/
            continue;
        }
        theta = vis_frame->theta_deg[i];
        phi = vis_frame->phi_deg[i];
        s_apply_output_remap(&theta, &phi);
        u = (theta - (float)COARSE_ANGLE_MIN_DEG) / theta_span;
        v = ((float)COARSE_ANGLE_MAX_DEG - phi) / theta_span;
        if ((u < 0.0f) || (u > 1.0f) || (v < 0.0f) || (v > 1.0f))
        {
            continue;
        }
        cx = (int32_t)(u * (float)(APP_DISPLAY_FIELD_W - 1u) + 0.5f);
        cy = (int32_t)(v * (float)(APP_DISPLAY_FIELD_H - 1u) + 0.5f);

        /* 娴犮儳绮忛幖婊呭偍閻愰€涜礋娑擃厼绺鹃敍灞惧Ω閼充粙鍣洪幍鈺傛殠閸掓澘鎳嗛崶瀛樻▔缁€鍝勬簚閸嶅繒绀岄妴?*/
        for (ky = -(int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS; ky <= (int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS; ky++)
        {
            int32_t fy = cy + ky;
            uint32_t ky_idx;
            if ((fy < 0) || (fy >= (int32_t)APP_DISPLAY_FIELD_H))
            {
                continue;
            }
            ky_idx = (uint32_t)(ky + (int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS);
            for (kx = -(int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS; kx <= (int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS; kx++)
            {
                int32_t fx = cx + kx;
                uint32_t kx_idx;
                float w;
                uint32_t fidx;
                if ((fx < 0) || (fx >= (int32_t)APP_DISPLAY_FIELD_W))
                {
                    continue;
                }
                kx_idx = (uint32_t)(kx + (int32_t)APP_DISPLAY_FINE_KERNEL_RADIUS);
                w = s_fine_kernel[ky_idx * APP_DISPLAY_FINE_KERNEL_LEN + kx_idx];
                fidx = (uint32_t)fy * APP_DISPLAY_FIELD_W + (uint32_t)fx;
                /* 瑜版挸澧犻弽鍛婃綀闁插秴顕惔鏃傛畱閼充粙鍣烘晶鐐哄櫤閵?*/
                s_field_a[fidx] += s_cfg.fine_gain * mag * w;
            }
        }
    }
#else
    (void)vis_frame;
#endif
}

/* 鐎佃妯夌粈杞拌厬闂傛潙婧€閹笛嗩攽娑撯偓濞嗏€冲讲閸掑棛顬囬獮铏拨閵? * 濡亜鎮滅紒鎾寸亯閸忓牆鍟撻崚?`s_field_b`閿涘苯鍟€缁鹃潧鎮滈崘娆忔礀 `s_field_a`閵?*/
static void s_apply_blur_once(void)
{
#if (APP_DISPLAY_SMOOTH_ENABLE != 0u)
    /* 娑撯偓濞嗏€崇暚閺佹潙閽╁鎴犳暠娑撱倖顒炵紒鍕灇閿?     * - 閸忓牊铆閸氭垵宓庣粔顖ょ礉閹跺﹦绮ㄩ弸婊冨晸閸?`s_field_b`
     * - 閸愬秶鏃遍崥鎴濆祹缁夘垽绱濋幎濠勭波閺嬫粌鍟撻崶?`s_field_a`
     *
     * 鏉╂瑦妲搁崗绋跨€烽惃鍕讲閸掑棛顬囬崡椋幮濋崘娆愮《閿涘瞼娴夊В鏃傛纯閹恒儰绨╃紒鏉戝祹缁夘垽绱濇潻鎰暬闁插繑娲挎担搴涒偓?*/
    uint32_t y;
    uint32_t x;
    int32_t k;

    for (y = 0u; y < APP_DISPLAY_FIELD_H; y++)
    {
        for (x = 0u; x < APP_DISPLAY_FIELD_W; x++)
        {
            float acc = 0.0f;
            /* 濡亜鎮滈崡椋幮濋敍宀冪珶閻ｅ奔缍呯純顕€鍣伴悽銊ャ仚閸欐牜鐡ラ悾銉ｂ偓?*/
            for (k = -(int32_t)APP_DISPLAY_SMOOTH_RADIUS; k <= (int32_t)APP_DISPLAY_SMOOTH_RADIUS; k++)
            {
                int32_t xi = (int32_t)x + k;
                uint32_t xi_c = (uint32_t)((xi < 0) ? 0 : ((xi >= (int32_t)APP_DISPLAY_FIELD_W) ? ((int32_t)APP_DISPLAY_FIELD_W - 1) : xi));
                uint32_t wk = (uint32_t)(k + (int32_t)APP_DISPLAY_SMOOTH_RADIUS);
                acc += s_field_a[y * APP_DISPLAY_FIELD_W + xi_c] * s_blur_kernel[wk];
            }
            s_field_b[y * APP_DISPLAY_FIELD_W + x] = acc;
        }
    }

    for (y = 0u; y < APP_DISPLAY_FIELD_H; y++)
    {
        for (x = 0u; x < APP_DISPLAY_FIELD_W; x++)
        {
            float acc = 0.0f;
            /* 缁鹃潧鎮滈崡椋幮濋敍宀冪珶閻ｅ苯鎮撻弽鐑藉櫚閻劌銇欓崣鏍摜閻ｃ儯鈧?*/
            for (k = -(int32_t)APP_DISPLAY_SMOOTH_RADIUS; k <= (int32_t)APP_DISPLAY_SMOOTH_RADIUS; k++)
            {
                int32_t yi = (int32_t)y + k;
                uint32_t yi_c = (uint32_t)((yi < 0) ? 0 : ((yi >= (int32_t)APP_DISPLAY_FIELD_H) ? ((int32_t)APP_DISPLAY_FIELD_H - 1) : yi));
                uint32_t wk = (uint32_t)(k + (int32_t)APP_DISPLAY_SMOOTH_RADIUS);
                acc += s_field_b[yi_c * APP_DISPLAY_FIELD_W + x] * s_blur_kernel[wk];
            }
            s_field_a[y * APP_DISPLAY_FIELD_W + x] = acc;
        }
    }
#endif
}

/* 閸╄桨绨ぐ鎾冲閸欘垵顫嬮崠鏍ф姎閿涘苯鍣径鍥︾瀵姴鐣弫瀵告畱濞搭喚鍋ｉ弰鍓с仛閸︽亽鈧? * 鏉╂柨娲栭崐闂磋礋閸﹁桨鑵戦惃鍕付婢堆冨槻閸婄》绱濋悽銊ょ艾閸氬海鐢婚崝銊︹偓浣哥秺娑撯偓閸栨牓鈧?*/
static float s_prepare_field(const SRP_VisFrame_t *vis_frame)
{
    /* 閸╄桨绨張鈧弬鎵畱 SRP 韫囶偆鍙庨敍灞剧€杞扮瀵姭鈧粌褰查惄瀛樺复鏉╂稑鍙嗛弰鍓с仛闁炬崘鐭鹃垾婵堟畱濞搭喚鍋ｉ崷鎭掆偓?     * 鏉╂瑦妲哥粻妤佺《鏉堟挸鍤崚鐗堟▔缁€娲偓鏄忕帆娑斿妫块惃鍕壋韫囧啯藟閹恒儲顒炴銈忕礉闁艾鐖堕崠鍛儓閿?     * - 缁缍夐弽濂稿櫢闁插洦鐗?
     * - 閸欘垶鈧娈戠紒鍡欑秹閺嶈壈鐎洪崥?     * - 閼汇儱鍏卞▎鈥抽挬濠?     * - 閺堚偓缂佸牆鍢查崐鍏煎絹閸?     *
     * 鏉╂柨娲栭崐?`peak` 娴兼矮缍旀稉鍝勬倵缂侇厼濮╅幀浣哥秺娑撯偓閸栨牜娈戦崣鍌濃偓鍐翻閸忋儯鈧?*/
    uint32_t i;
    float peak = 0.0f;

    s_resample_coarse_to_field(vis_frame);
    s_apply_fine_fusion(vis_frame);
#if (APP_DISPLAY_SMOOTH_ENABLE != 0u)
    {
        uint8_t p;
        for (p = 0u; p < s_cfg.smooth_passes; p++)
        {
            s_apply_blur_once();
        }
    }
#endif
    for (i = 0u; i < APP_DISPLAY_FIELD_PIXELS; i++)
    {
        float v = s_field_a[i];
        if (!isfinite(v) || (v < 0.0f))
        {
            v = 0.0f;
            s_field_a[i] = 0.0f;
        }
        if (v > peak)
        {
            peak = v;
        }
    }
    return peak;
}

/* 鐎瑰本鏆ｇ划鎯у瑜版帊绔撮崠鏍у毐閺佽埇鈧? * 鏉堟挸鍙嗛弰顖滄祲鐎电懓寮懓鍐ㄥ槻閸婅偐娈戝В鏂剧伐閿涘矁绶崙鐑樻Ц 0..255 閻忔澘瀹抽妴?*/
static uint8_t s_compute_norm_full(float ratio)
{
    /* 鐎瑰本鏆ｈぐ鎺嶇閸栨牞鐭惧鍕剁窗
     * - 閸忓牊濡哥痪鎸庘偓褑鍏橀柌蹇旂槷鏉烆剚宕查崚?dB 缁屾椽妫?
     * - 閸愬秵瀵?`db_floor` 閹搭亝鏌?
     * - 閸愬秵妲х亸鍕礀 0..1
     * - 閺堚偓閸氬骸顨?gamma 鐠嬪啯鏆ｇ憴鍌涘妳閿涘苯鑻熼柌蹇撳閸?0..255 */
    float db;
    float t;
    uint32_t q;

    db = 20.0f * log10f(s_clamp_f32(ratio, 1.0e-9f, 1.0f));
    if (db <= s_cfg.db_floor)
    {
        return 0u;
    }
    if (db >= 0.0f)
    {
        return 255u;
    }

    t = s_clamp_f32((db - s_cfg.db_floor) / (-s_cfg.db_floor), 0.0f, 1.0f);
    if (fabsf(s_cfg.gamma - 1.0f) > 0.02f)
    {
        t = powf(t, s_cfg.gamma);
    }
    q = (uint32_t)(t * 255.0f + 0.5f);
    return (q > 255u) ? 255u : (uint8_t)q;
}

/* 閸掗攱鏌婅箛顐︹偓鐔风秺娑撯偓閸?LUT閵? * 閸欘亝婀侀崷?`gamma` 閹?`db_floor` 閸欐ê瀵查弮鑸靛闁插秵鏌婇弸鍕紦閵?*/
static void s_refresh_norm_lut(void)
{
    /* 韫囶偊鈧喎缍婃稉鈧崠鏍佸蹇庣瑓閿涘畭ratio -> 閻忔澘瀹抽崐绯?閻ㄥ嫬鍙х化璇插涧閸欐牕鍠呮禍?gamma 閸?db_floor閵?     * 閸欘亣顩︽潻娆庤⒈娑擃亜寮弫鐗堢梾閸欐﹫绱滾UT 鐏忓崬褰叉禒銉﹀瘮缂侇厼顦查悽銊ｂ偓?*/
    uint32_t i;
    float gamma;
    float db_floor;

    if (s_cfg.norm_mode != APP_DISPLAY_NORM_FAST)
    {
        return;
    }

    gamma = s_cfg.gamma;
    db_floor = s_cfg.db_floor;
    if ((s_norm_lut_valid != 0u) &&
        (fabsf(gamma - s_norm_lut_gamma) < 1.0e-4f) &&
        (fabsf(db_floor - s_norm_lut_db_floor) < 1.0e-4f))
    {
        return;
    }

    for (i = 0u; i <= APP_DISPLAY_NORM_RATIO_LUT_SIZE; i++)
    {
        float ratio = (float)i / (float)APP_DISPLAY_NORM_RATIO_LUT_SIZE;
        if (ratio < 1.0e-9f)
        {
            ratio = 1.0e-9f;
        }
        s_norm_ratio_lut[i] = s_compute_norm_full(ratio);
    }

    s_norm_lut_gamma = gamma;
    s_norm_lut_db_floor = db_floor;
    s_norm_lut_valid = 1u;
}

/* 韫囶偊鈧喎缍婃稉鈧崠鏍ㄧ叀鐞涖劌鍤遍弫鑸偓?*/
static uint8_t s_norm_fast_lookup(float ratio)
{
    /* 韫囶偊鈧喐膩瀵繋绗呮稉宥呭晙闁劕鍎氱槐鐘侯吀缁?log10f/powf閿涘矁鈧本妲搁惄瀛樺复閺屻儴銆冮妴?*/
    uint32_t idx;

    if (ratio <= 0.0f)
    {
        return 0u;
    }

    ratio = s_clamp_f32(ratio, 0.0f, 1.0f);
    idx = (uint32_t)(ratio * (float)APP_DISPLAY_NORM_RATIO_LUT_SIZE + 0.5f);
    if (idx > APP_DISPLAY_NORM_RATIO_LUT_SIZE)
    {
        idx = APP_DISPLAY_NORM_RATIO_LUT_SIZE;
    }
    return s_norm_ratio_lut[idx];
}

/* 閸掗攱鏌?LCD 閸嶅繒绀岄崚鐗堟▔缁€鍝勬簚閸ф劖鐖ｉ惃鍕Ё鐏忓嫮绱︾€涙ǜ鈧? * 鏉╂瑦鐗卞〒鍙夌厠闂冭埖顔岄崣顖欎簰闁灝鍘ゅВ蹇撴姎闁插秴顦叉潻娑滎攽閸ф劖鐖ｉ幑銏㈢暬閵?*/
void s_refresh_render_map_cache(uint16_t map_w, uint16_t map_h)
{
    /* 妫板嫯顓哥粻妞烩偓娣烠D 閸嶅繒绀岄崸鎰垼 -> 閺勫墽銇氶崷鍝勬綏閺嶅洠鈧繄娈戦弰鐘茬殸閸忓磭閮撮妴?     * 閺堚偓鏉╂垿鍋﹂崪灞藉蓟缁炬寧鈧傝⒈缁夊秷鐭惧鍕厴娴兼氨鏁ら崚鎷岀箹娴滄稓绱︾€涙ǜ鈧?     *
     * 缂傛挸鐡ㄩ崥搴礉闁劕鎶氶崘鍛湴瀵邦亞骞嗙亸鍙樼瑝闂団偓鐟曚礁寮芥径宥呬粵閿?     * - 濞搭喚鍋ｉ梽銈嗙《
     * - 鏉堝湱鏅柦鍏呯秴
     * - 閸欏瞼鍤庨幀褎娼堥柌宥嗗床缁?     *
     * 鏉╂瑥顕弫鏉戠潌閻戭厼濮忛崶楣冣偓鎰姎缂佹ê鍩楅惃鍕偓褑鍏樼敮顔煎И瀵板牏娲块幒銉ｂ偓?*/
    uint16_t x;
    uint16_t y;

    if ((map_w == 0u) || (map_h == 0u) ||
        (map_w > APP_DISPLAY_MAX_LINE_PIXELS) ||
        (map_h > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }

    if ((s_cache_map_w == map_w) && (s_cache_map_h == map_h))
    {
        return;
    }

    for (x = 0u; x < map_w; x++)
    {
        float fx;
        uint16_t x0;
        uint16_t x1;
        uint16_t wx;

        /* `fx` 鐞涖劎銇?LCD 瑜版挸澧犻崚妤€顕惔鏂垮煂閺勫墽銇氶崷杞拌厬閻ㄥ嫭璇為悙鐟板灙閸ф劖鐖ｉ妴?*/
        fx = (map_w > 1u)
           ? ((float)x * (float)(APP_DISPLAY_FIELD_W - 1u) / (float)(map_w - 1u))
           : 0.0f;
        x0 = (uint16_t)fx;
        if (x0 >= APP_DISPLAY_FIELD_W)
        {
            x0 = APP_DISPLAY_FIELD_W - 1u;
        }
        x1 = (x0 + 1u < APP_DISPLAY_FIELD_W) ? (uint16_t)(x0 + 1u) : x0;
        wx = (uint16_t)((fx - (float)x0) * 256.0f + 0.5f);
        if (wx > 256u)
        {
            wx = 256u;
        }

        /* 閸氬本妞傜紓鎾崇摠閺堚偓鏉╂垿鍋︾槐銏犵穿娑撳骸寮荤痪鎸庘偓褎褰冮崐鍏煎闂団偓閻ㄥ嫬涔忛崣鎶藉仸閻愬箍鈧焦娼堥柌宥冣偓?*/
        s_col_x0_cache[x] = x0;
        s_col_x1_cache[x] = x1;
        s_col_wx256_cache[x] = wx;
        s_col_near_cache[x] = (wx >= 128u) ? x1 : x0;
    }

    for (y = 0u; y < map_h; y++)
    {
        float fy;
        uint16_t y0;
        uint16_t y1;
        uint16_t wy;

        /* `fy` 鐞涖劎銇?LCD 瑜版挸澧犵悰灞筋嚠鎼存柨鍩岄弰鍓с仛閸﹁桨鑵戦惃鍕癁閻愮顢戦崸鎰垼閵?*/
        fy = (map_h > 1u)
           ? ((float)y * (float)(APP_DISPLAY_FIELD_H - 1u) / (float)(map_h - 1u))
           : 0.0f;
        y0 = (uint16_t)fy;
        if (y0 >= APP_DISPLAY_FIELD_H)
        {
            y0 = APP_DISPLAY_FIELD_H - 1u;
        }
        y1 = (y0 + 1u < APP_DISPLAY_FIELD_H) ? (uint16_t)(y0 + 1u) : y0;
        wy = (uint16_t)((fy - (float)y0) * 256.0f + 0.5f);
        if (wy > 256u)
        {
            wy = 256u;
        }

        /* 閸氬本妞傜紓鎾崇摠閺堚偓鏉╂垿鍋︾槐銏犵穿娑撳骸寮荤痪鎸庘偓褎褰冮崐鍏煎闂団偓閻ㄥ嫪绗傛稉瀣仸閻愬箍鈧焦娼堥柌宥冣偓?*/
        s_row_y0_cache[y] = y0;
        s_row_y1_cache[y] = y1;
        s_row_wy256_cache[y] = wy;
        s_row_near_cache[y] = (wy >= 128u) ? y1 : y0;
    }

    s_cache_map_w = map_w;
    s_cache_map_h = map_h;
}

/* 鐠侊紕鐣昏ぐ鎺嶇閸栨牕鎮楅惃?8bit 閺勫墽銇氶崷鎭掆偓? * 閸愬懘鍎存导姘強鐠佲€虫珨婢规澘绨抽妴浣瑰⒏闂勩倛鍎楅弲顖ょ礉楠炶埖鐗撮幑顔肩秼閸撳秵膩瀵繘鈧瀚ㄨ箛顐︹偓?鐎瑰本鏆ｇ捄顖氱窞閵?*/
static void s_refresh_camera_scale_cache(uint16_t map_w, uint16_t map_h, uint16_t src_w, uint16_t src_h)
{
    uint16_t x;
    uint16_t y;

    if ((map_w == 0u) || (map_h == 0u) || (src_w == 0u) || (src_h == 0u) ||
        (map_w > APP_DISPLAY_MAX_LINE_PIXELS) || (map_h > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }

    if ((s_camera_cache_map_w == map_w) &&
        (s_camera_cache_map_h == map_h) &&
        (s_camera_cache_src_w == src_w) &&
        (s_camera_cache_src_h == src_h))
    {
        return;
    }

    for (x = 0u; x < map_w; x++)
    {
        uint32_t src_x = (map_w > 1u)
                       ? ((uint32_t)x * (uint32_t)(src_w - 1u) / (uint32_t)(map_w - 1u))
                       : 0u;
        s_camera_col_near_cache[x] = (src_x >= src_w) ? (uint16_t)(src_w - 1u) : (uint16_t)src_x;
    }

    for (y = 0u; y < map_h; y++)
    {
        uint32_t src_y = (map_h > 1u)
                       ? ((uint32_t)y * (uint32_t)(src_h - 1u) / (uint32_t)(map_h - 1u))
                       : 0u;
        s_camera_row_near_cache[y] = (src_y >= src_h) ? (uint16_t)(src_h - 1u) : (uint16_t)src_y;
    }

    s_camera_cache_map_w = map_w;
    s_camera_cache_map_h = map_h;
    s_camera_cache_src_w = src_w;
    s_camera_cache_src_h = src_h;
}

static void s_update_camera_cache_from_frame(const App_CameraFrame_t *camera_frame)
{
    uint16_t camera_w = (uint16_t)(s_camera_x1 - s_camera_x0 + 1u);
    uint16_t camera_h = (uint16_t)(s_camera_y1 - s_camera_y0 + 1u);
    uint16_t src_stride;
    uint16_t y;
    if ((camera_frame == NULL) ||
        (camera_frame->pixels == NULL) ||
        (camera_frame->valid == 0u) ||
        (camera_frame->width == 0u) ||
        (camera_frame->height == 0u) ||
        (camera_w == 0u) ||
        (camera_h == 0u) ||
        (camera_w > APP_DISPLAY_CAMERA_VIEW_W) ||
        (camera_h > APP_DISPLAY_CAMERA_VIEW_H) ||
        (camera_w > APP_DISPLAY_MAX_LINE_PIXELS) ||
        (camera_h > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }
    if ((s_camera_cache_valid != 0u) &&
        (s_camera_cache_seq == camera_frame->seq) &&
        (s_camera_cache_map_w == camera_w) &&
        (s_camera_cache_map_h == camera_h) &&
        (s_camera_cache_src_w == camera_frame->width) &&
        (s_camera_cache_src_h == camera_frame->height))
    {
        return;
    }
    src_stride = (camera_frame->stride != 0u) ? camera_frame->stride : camera_frame->width;
    s_refresh_camera_scale_cache(camera_w, camera_h, camera_frame->width, camera_frame->height);
    for (y = 0u; y < camera_h; y++)
    {
        const uint16_t *src_cam = &camera_frame->pixels[(uint32_t)s_camera_row_near_cache[y] * (uint32_t)src_stride];
        uint16_t *dst = &s_camera_cache_pixels[(uint32_t)y * (uint32_t)camera_w];
        uint16_t x;
        for (x = 0u; x < camera_w; x++)
        {
            dst[x] = src_cam[s_camera_col_near_cache[x]];
        }
    }
    s_clean_dcache_by_addr(s_camera_cache_pixels,
                           (uint32_t)camera_w * (uint32_t)camera_h * 2u);
    s_camera_cache_seq = camera_frame->seq;
    s_camera_cache_valid = 1u;
}
static uint8_t s_blit_camera_cache_region(uint16_t dst_x0,
                                          uint16_t dst_y0,
                                          uint16_t width,
                                          uint16_t height,
                                          uint16_t src_x0,
                                          uint16_t src_y0)
{
    uint16_t camera_w = (uint16_t)(s_camera_x1 - s_camera_x0 + 1u);
    uint16_t camera_h = (uint16_t)(s_camera_y1 - s_camera_y0 + 1u);
    uint16_t dst_x1;
    uint16_t dst_y1;
    const uint16_t *src;
    uint16_t row;
    if ((s_camera_cache_valid == 0u) ||
        (width == 0u) ||
        (height == 0u) ||
        (camera_w == 0u) ||
        (camera_h == 0u))
    {
        return 0u;
    }
    if (((uint32_t)src_x0 + (uint32_t)width > (uint32_t)camera_w) ||
        ((uint32_t)src_y0 + (uint32_t)height > (uint32_t)camera_h))
    {
        return 0u;
    }
    dst_x1 = (uint16_t)(dst_x0 + width - 1u);
    dst_y1 = (uint16_t)(dst_y0 + height - 1u);
    src = &s_camera_cache_pixels[(uint32_t)src_y0 * (uint32_t)camera_w + (uint32_t)src_x0];
    if (ltdc_copy_async(dst_x0, dst_y0, dst_x1, dst_y1, src, camera_w) != 0u)
    {
        if (s_flush_temp_draw() != 0u)
        {
            return 1u;
        }
        DMA2D_Accel_Reset();
    }
    else
    {
        return 1u;
    }

    for (row = 0u; row < height; row++)
    {
        lcd_color_fill(dst_x0,
                       (uint16_t)(dst_y0 + row),
                       dst_x1,
                       (uint16_t)(dst_y0 + row),
                       (uint16_t *)&src[(uint32_t)row * (uint32_t)camera_w]);
    }
    return 1u;
}
static void s_render_camera_rows(const App_CameraFrame_t *camera_frame)
{
    uint16_t camera_w = (uint16_t)(s_camera_x1 - s_camera_x0 + 1u);
    uint16_t camera_h = (uint16_t)(s_camera_y1 - s_camera_y0 + 1u);
    if ((camera_frame == NULL) ||
        (camera_frame->pixels == NULL) ||
        (camera_frame->valid == 0u) ||
        (camera_frame->width == 0u) ||
        (camera_frame->height == 0u) ||
        (camera_w == 0u) ||
        (camera_h == 0u) ||
        (camera_w > APP_DISPLAY_CAMERA_VIEW_W) ||
        (camera_h > APP_DISPLAY_CAMERA_VIEW_H))
    {
        return;
    }
    s_update_camera_cache_from_frame(camera_frame);
    if (s_camera_cache_valid == 0u)
    {
        return;
    }
    (void)s_blit_camera_cache_region(s_camera_x0, s_camera_y0, camera_w, camera_h, 0u, 0u);
}

static void s_render_camera_frame_rows(const App_CameraFrame_t *camera_frame)
{
    uint16_t map_w = (uint16_t)(s_map_x1 - s_map_x0 + 1u);
    uint16_t map_h = (uint16_t)(s_map_y1 - s_map_y0 + 1u);
    uint16_t fit_w;
    uint16_t fit_h;
    uint16_t fit_x0;
    uint16_t fit_y0;
    uint16_t fit_x1;
    uint16_t fit_y1;
    uint16_t src_x0;
    uint16_t src_y0;
    uint16_t src_stride;
    uint8_t blit_rows = s_clamp_u8(s_cfg.blit_rows, 1u, APP_DISPLAY_BLIT_ROWS_MAX);
    uint16_t y_blk;

    if ((camera_frame == NULL) ||
        (camera_frame->pixels == NULL) ||
        (camera_frame->valid == 0u) ||
        (camera_frame->width == 0u) ||
        (camera_frame->height == 0u) ||
        (map_w == 0u) ||
        (map_h == 0u) ||
        (map_w > APP_DISPLAY_MAX_LINE_PIXELS) ||
        (map_h > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }

    src_stride = (camera_frame->stride != 0u) ? camera_frame->stride : camera_frame->width;
    fit_w = (map_w < camera_frame->width) ? map_w : camera_frame->width;
    fit_h = (map_h < camera_frame->height) ? map_h : camera_frame->height;
    if ((fit_w == 0u) || (fit_h == 0u))
    {
        return;
    }

    src_x0 = (uint16_t)(((uint32_t)camera_frame->width - (uint32_t)fit_w) / 2u);
    src_y0 = (uint16_t)(((uint32_t)camera_frame->height - (uint32_t)fit_h) / 2u);
    fit_x0 = (uint16_t)(s_map_x0 + ((uint32_t)map_w - (uint32_t)fit_w) / 2u);
    fit_y0 = (uint16_t)(s_map_y0 + ((uint32_t)map_h - (uint32_t)fit_h) / 2u);
    fit_x1 = (uint16_t)(fit_x0 + fit_w - 1u);
    fit_y1 = (uint16_t)(fit_y0 + fit_h - 1u);

    s_fill_rect_async(s_map_x0, s_map_y0, s_map_x1, s_map_y1, BLACK);
    s_camera_cache_seq = camera_frame->seq;
    s_camera_cache_valid = 1u;

    for (y_blk = 0u; y_blk < fit_h; y_blk = (uint16_t)(y_blk + blit_rows))
    {
        uint16_t rows = fit_h - y_blk;
        uint16_t row;

        if (rows > blit_rows)
        {
            rows = blit_rows;
        }

        for (row = 0u; row < rows; row++)
        {
            uint16_t src_y = (uint16_t)(src_y0 + y_blk + row);
            const uint16_t *src = &camera_frame->pixels[(uint32_t)src_y * (uint32_t)src_stride + (uint32_t)src_x0];
            uint16_t *dst = &s_blit_buf[(uint32_t)row * (uint32_t)fit_w];
            memcpy(dst, src, (size_t)fit_w * sizeof(uint16_t));
        }

        s_submit_rgb565_block(fit_x0,
                              (uint16_t)(fit_y0 + y_blk),
                              fit_x1,
                              (uint16_t)(fit_y0 + y_blk + rows - 1u),
                              s_blit_buf);
    }
}

static uint8_t s_capture_frozen_camera_frame(const App_CameraFrame_t *camera_frame)
{
    uint16_t src_stride;
    uint16_t row;

    if ((camera_frame == NULL) ||
        (camera_frame->pixels == NULL) ||
        (camera_frame->valid == 0u) ||
        (camera_frame->width == 0u) ||
        (camera_frame->height == 0u) ||
        (camera_frame->width > APP_DISPLAY_CAMERA_VIEW_W) ||
        (camera_frame->height > APP_DISPLAY_CAMERA_VIEW_H))
    {
        return 0u;
    }

    src_stride = (camera_frame->stride != 0u) ? camera_frame->stride : camera_frame->width;
    if (src_stride < camera_frame->width)
    {
        return 0u;
    }

    for (row = 0u; row < camera_frame->height; row++)
    {
        memcpy(&s_camera_cache_pixels[(uint32_t)row * (uint32_t)camera_frame->width],
               &camera_frame->pixels[(uint32_t)row * (uint32_t)src_stride],
               (size_t)camera_frame->width * sizeof(uint16_t));
    }

    s_camera_freeze_w = camera_frame->width;
    s_camera_freeze_h = camera_frame->height;
    s_camera_freeze_stride = camera_frame->width;
    s_camera_freeze_valid = 1u;
    s_camera_cache_seq = camera_frame->seq;
    s_camera_cache_valid = 1u;
    s_clean_dcache_by_addr(s_camera_cache_pixels,
                           (uint32_t)s_camera_freeze_w * (uint32_t)s_camera_freeze_h * 2u);
    return 1u;
}

static void s_render_field_alpha_rows(const App_CameraFrame_t *camera_frame, uint16_t color565)
{
    uint16_t map_w = (uint16_t)(s_map_x1 - s_map_x0 + 1u);
    uint16_t map_h = (uint16_t)(s_map_y1 - s_map_y0 + 1u);
    uint16_t src_stride;
    uint8_t blit_rows = s_clamp_u8(s_cfg.blit_rows, 1u, APP_DISPLAY_BLIT_ROWS_MAX);
    uint8_t use_bilinear = (s_cfg.interp_mode == APP_DISPLAY_INTERP_BILINEAR) ? 1u : 0u;
    uint16_t y_blk;
    (void)color565;

    if ((map_w == 0u) ||
        (map_h == 0u) ||
        (camera_frame == NULL) ||
        (camera_frame->pixels == NULL) ||
        (camera_frame->valid == 0u) ||
        (camera_frame->width == 0u) ||
        (camera_frame->height == 0u) ||
        (map_w > APP_DISPLAY_MAX_LINE_PIXELS) ||
        (map_h > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }

    src_stride = (camera_frame->stride != 0u) ? camera_frame->stride : camera_frame->width;
    s_refresh_render_map_cache(map_w, map_h);
    s_refresh_camera_scale_cache(map_w, map_h, camera_frame->width, camera_frame->height);
    s_camera_cache_seq = camera_frame->seq;
    s_camera_cache_valid = 1u;

    for (y_blk = 0u; y_blk < map_h; y_blk = (uint16_t)(y_blk + blit_rows))
    {
        uint16_t rows = map_h - y_blk;
        uint16_t row;

        if (rows > blit_rows)
        {
            rows = blit_rows;
        }

        for (row = 0u; row < rows; row++)
        {
            uint16_t y = (uint16_t)(y_blk + row);
            uint16_t x;
            uint8_t *dst = &s_blit_l8_buf[(uint32_t)row * (uint32_t)map_w];

            if (use_bilinear != 0u)
            {
                uint16_t y0 = s_row_y0_cache[y];
                uint16_t y1 = s_row_y1_cache[y];
                uint16_t wy = s_row_wy256_cache[y];
                uint16_t wy0 = (uint16_t)(256u - wy);
                const uint8_t *src0 = &s_field_norm_u8[(uint32_t)y0 * APP_DISPLAY_FIELD_W];
                const uint8_t *src1 = &s_field_norm_u8[(uint32_t)y1 * APP_DISPLAY_FIELD_W];

                for (x = 0u; x < map_w; x++)
                {
                    uint16_t x0 = s_col_x0_cache[x];
                    uint16_t x1 = s_col_x1_cache[x];
                    uint16_t wx = s_col_wx256_cache[x];
                    uint16_t wx0 = (uint16_t)(256u - wx);
                    uint32_t v00 = src0[x0];
                    uint32_t v01 = src0[x1];
                    uint32_t v10 = src1[x0];
                    uint32_t v11 = src1[x1];
                    uint32_t vx0 = v00 * wx0 + v01 * wx;
                    uint32_t vx1 = v10 * wx0 + v11 * wx;
                    uint32_t q = (vx0 * wy0 + vx1 * wy + 32768u) >> 16;
                    if (q > 255u)
                    {
                        q = 255u;
                    }
                    dst[x] = (uint8_t)q;
                }
            }
            else
            {
                uint16_t y_idx = s_row_near_cache[y];
                const uint8_t *src = &s_field_norm_u8[(uint32_t)y_idx * APP_DISPLAY_FIELD_W];

                for (x = 0u; x < map_w; x++)
                {
                    dst[x] = src[s_col_near_cache[x]];
                }
            }
        }

        for (row = 0u; row < rows; row++)
        {
            const uint8_t *src = &s_blit_l8_buf[(uint32_t)row * (uint32_t)map_w];
            uint16_t src_y = s_camera_row_near_cache[(uint32_t)y_blk + (uint32_t)row];
            const uint16_t *bg = &camera_frame->pixels[(uint32_t)src_y * (uint32_t)src_stride];
            uint16_t *dst = &s_blit_buf[(uint32_t)row * (uint32_t)map_w];
            uint16_t x;

            for (x = 0u; x < map_w; x++)
            {
                uint8_t alpha = s_overlay_alpha_from_norm(src[x]);
                uint16_t bg_px = bg[s_camera_col_near_cache[x]];
                dst[x] = (alpha == 0u) ? bg_px : s_blend_rgb565(bg_px, s_heat_lut[src[x]], alpha);
            }
        }
        s_submit_rgb565_block(s_map_x0,
                              (uint16_t)(s_map_y0 + y_blk),
                              s_map_x1,
                              (uint16_t)(s_map_y0 + y_blk + rows - 1u),
                              s_blit_buf);
    }
}

static void s_update_norm_field(float field_peak, uint32_t frame_seq)
{
    /* 閹跺﹥璇為悙纭呭厴闁插繐婧€鏉烆剚宕叉稉?8bit 瀵搫瀹抽崷鎭掆偓?     * 鏉╂瑦妲歌ぐ鍗炴惙鐟欏倹鍔呴張鈧崗鎶芥暛閻ㄥ嫭顒炴銈勭娑撯偓閿涘苯娲滄稉?SRP 閸旂喓宸奸崝銊︹偓浣藉瘱閸ユ潙绶㈡径褝绱濋懓灞肩瑬闂呭繐鎶氬▔銏犲З閵?     *
     * 瑜版挸澧犵粵鏍殣閸掑棔璐熼崶娑樼湴閿?     * 1. 閻?`s_peak_ema` 娴ｆ粈璐熼獮铏拨閸欏倽鈧啫鍢查崐纭风礉閸戝繐鐨禍顔煎閸撗呭創閹舵牕濮?
     * 2. 閺嶈宓侀崶鍝勭暰濮ｆ柧绶ラ崪宀冨剹閺咁垰閽╅崸鍥р偓鐓庡彙閸氬奔鍙婄粻妤€娅旀竟鏉跨俺
     * 3. 閹跺﹨绉存潻鍥ф珨婢规澘绨抽惃鍕箒閺佸牐鍏橀柌蹇旀Ё鐏忓嫬鍩?0..255
     * 4. 閺嶈宓佸Ο鈥崇础闁瀚ㄩ垾婊勭叀鐞涖劌鎻╅柅鐔荤熅瀵板嫧鈧繃鍨ㄩ垾婊冨弿缁儳瀹抽崗顒€绱＄捄顖氱窞閳?     *
     * 婵″倹鐏夎ぐ鎾冲鐢勭梾閺堝婀侀弫鍫濆槻閸婄》绱濇潻妯圭窗閺嶈宓侀柊宥囩枂闁瀚ㄦ潏鎾冲毉濞村鐦崶鐐攳閹存牜鍑芥鎴濇禈閵?*/
    uint32_t i;
    float ref;
    float floor_linear;
    float bg_sum = 0.0f;
    uint32_t bg_cnt = 0u;

    if ((field_peak <= APP_DISPLAY_DYNAMIC_MIN_PEAK) && (APP_DISPLAY_IDLE_TEST_PATTERN != 0u))
    {
        for (i = 0u; i < APP_DISPLAY_FIELD_H; i++)
        {
            uint32_t x;
            for (x = 0u; x < APP_DISPLAY_FIELD_W; x++)
            {
                uint32_t phase = (frame_seq >> 2) & 1u;
                s_field_norm_u8[i * APP_DISPLAY_FIELD_W + x] = ((((x >> 3) + (i >> 3) + phase) & 1u) != 0u) ? 48u : 12u;
            }
        }
        s_last_noise_floor = 0.0f;
        return;
    }

    if (field_peak <= APP_DISPLAY_DYNAMIC_MIN_PEAK)
    {
        memset(s_field_norm_u8, 0, sizeof(s_field_norm_u8));
        s_last_noise_floor = 0.0f;
        return;
    }

    ref = (s_peak_ema < APP_DISPLAY_DYNAMIC_MIN_PEAK) ? APP_DISPLAY_DYNAMIC_MIN_PEAK : s_peak_ema;
    for (i = 0u; i < APP_DISPLAY_FIELD_PIXELS; i++)
    {
        /* 瀹勬澘鈧棿绔撮崡濠佷簰娑撳娈戦崠鍝勭厵鐞氼偂缍旀稉楦垮剹閺咁垰鈧瑩鈧绱濋悽銊︽降娴兼媽顓搁崳顏勶紣鎼存洏鈧?*/
        if (s_field_a[i] < (field_peak * 0.5f))
        {
            bg_sum += s_field_a[i];
            bg_cnt++;
        }
    }
    /* 閸忓牊瀵滈崶鍝勭暰濮ｆ柧绶ョ紒娆忓毉閸╄櫣顢呴崳顏勶紣鎼存洏鈧?*/
    floor_linear = field_peak * s_cfg.noise_gate_ratio;
    if (bg_cnt > 0u)
    {
        /* 閸愬秶鏁ら懗灞炬珯楠炲啿娼庨崐闂村強鐠佲€茬娑擃亣鍤滈柅鍌氱安閸ｎ亜锛愭惔鏇礉閸欐牔琚遍懓鍛纯婢堆呮畱闁絼閲滈妴?*/
        float bg_floor = (bg_sum / (float)bg_cnt) * s_cfg.noise_adapt_gain;
        if (bg_floor > floor_linear)
        {
            floor_linear = bg_floor;
        }
    }
    floor_linear = s_clamp_f32(floor_linear, 0.0f, field_peak);
    s_last_noise_floor = floor_linear;

    if (s_cfg.norm_mode == APP_DISPLAY_NORM_FAST)
    {
        s_refresh_norm_lut();
        for (i = 0u; i < APP_DISPLAY_FIELD_PIXELS; i++)
        {
            float v = s_field_a[i] - floor_linear;
            if (v <= 0.0f)
            {
                /* 娴ｅ簼绨崳顏勶紣鎼存洜娈戦崓蹇曠閻╁瓨甯撮崢瀣灇 0閵?*/
                s_field_norm_u8[i] = 0u;
                continue;
            }
            /* `v / ref` 閺勵垳娴夌€电懓寮懓鍐ㄥ槻閸婅偐娈戦懗浠嬪櫤濮ｆ柧绶ラ妴?*/
            s_field_norm_u8[i] = s_norm_fast_lookup(v / ref);
        }
        return;
    }

    for (i = 0u; i < APP_DISPLAY_FIELD_PIXELS; i++)
    {
        float v = s_field_a[i] - floor_linear;
        if (v <= 0.0f)
        {
            s_field_norm_u8[i] = 0u;
            continue;
        }
        /* 鐎瑰本鏆ｅΟ鈥崇础閻╁瓨甯撮柅鎰剼缁辩姾铔嬮崗顒€绱＄捄顖氱窞閵?*/
        s_field_norm_u8[i] = s_compute_norm_full(v / ref);
    }
}

/* 閹跺﹥鎸夐獮瀹狀潡鎼达箒娴嗛幑顫礋閻戭厼濮忛崶鎯у隘閸╃喎鍞撮惃?X 閸ф劖鐖ｉ妴?*/
static uint16_t s_angle_to_x(float angle)
{
    /* 閹跺﹥鎸夐獮瀹狀潡鎼达妇鍤庨幀褎妲х亸鍕煂閻戭厼濮忛崶鍓х叐瑜般垹鍞撮惃?X 閸嶅繒绀岄崸鎰垼閵?*/
    float ratio;
    uint16_t width = (uint16_t)(s_map_x1 - s_map_x0 + 1u);
    float span = (float)(COARSE_ANGLE_MAX_DEG - COARSE_ANGLE_MIN_DEG);
    int32_t x;

    if (span < 1.0e-6f)
    {
        return s_map_x0;
    }
    ratio = s_clamp_f32((angle - (float)COARSE_ANGLE_MIN_DEG) / span, 0.0f, 1.0f);
    x = (int32_t)(s_map_x0 + ratio * (float)(width - 1u));
    return s_clamp_u16(x, s_map_x0, s_map_x1);
}

/* 閹跺﹤鐎惄纾嬵潡鎼达箒娴嗛幑顫礋閻戭厼濮忛崶鎯у隘閸╃喎鍞撮惃?Y 閸ф劖鐖ｉ妴?*/
static uint16_t s_angle_to_y(float angle)
{
    /* 閹跺﹤鐎惄纾嬵潡鎼达附妲х亸鍕煂閻戭厼濮忛崶鍓х叐瑜般垹鍞撮惃?Y 閸嶅繒绀岄崸鎰垼閵?     * 閻㈠彉绨仦蹇撶 Y 鏉炴潙鎮滄稉瀣杻婢堆嶇礉閸ョ姵顒濇潻娆撳櫡娴ｈ法鏁ら垾婊勬付婢堆嗩潡閸︺劋绗傞弬鍏夆偓婵堟畱閺勭姴鐨犻弬鐟扮础閵?*/
    float ratio;
    uint16_t height = (uint16_t)(s_map_y1 - s_map_y0 + 1u);
    float span = (float)(COARSE_ANGLE_MAX_DEG - COARSE_ANGLE_MIN_DEG);
    int32_t y;

    if (span < 1.0e-6f)
    {
        return s_map_y0;
    }
    ratio = s_clamp_f32(((float)COARSE_ANGLE_MAX_DEG - angle) / span, 0.0f, 1.0f);
    y = (int32_t)(s_map_y0 + ratio * (float)(height - 1u));
    return s_clamp_u16(y, s_map_y0, s_map_y1);
}

/* 鐏?8bit 閻戭厼濮忛崶鐐瘻鐞涘苯娼￠弬鐟扮础缂佹ê鍩楅崚鏉挎倵閸欐壆绱﹂崘灞傗偓?*/
static void s_render_field_rows(void)
{
    /* 鐏忓棗缍婃稉鈧崠鏍ф倵閻ㄥ嫮鍎归崝娑樻禈閸掑棗娼￠崘娆忓弳閸氬骸褰寸紓鎾冲暱閵?     * 闁插洨鏁ら垾婊勫瘻閼汇儱鍏辩悰灞肩閸фせ鈧繄娈戦崘娆愮《閿涘苯甯崶鐘虫箒娑撳绱?
     * - 閹貉冨煑娑撳瓨妞傜紓鎾冲暱婢堆冪毈閿涘奔绗夎箛鍛礋閺佹潙绱堕崶鎯у櫙婢跺洤鍙忕亸鍝勵嚟娑擃參妫块崠?     * - 閺囨挳鈧倸鎮?DMA2D/LTDC 鏉╂瑧琚崸妞剧炊鏉堟挻甯撮崣?     * - 鏉烆垯娆㈤崶鐐衡偓鈧弮鏈电瘍閼宠棄顦查悽銊ユ倱娑撯偓婵傛绁︾粙?     *
     * 閸ф鍞存径鍕倞妞ゅ搫绨稉鐚寸窗
     * 1. 閺嶈宓侀幓鎺戔偓鍏寄佸蹇ョ礉閹?`s_field_norm_u8` 閺€鎯с亣閸掓澘缍嬮崜?LCD 鐞涘苯娼?
     * 2. 閼汇儲鏁幐?L8 + CLUT 閸旂娀鈧噦绱濋崚娆戞纯閹恒儲褰佹禍?8bit 閺佺増宓?
     * 3. 閸氾箑鍨幍瀣З閺?`s_heat_lut` 鏉烆剚鍨?RGB565 閸愬秴鍟撶仦?*/
    uint16_t map_w = (uint16_t)(s_map_x1 - s_map_x0 + 1u);
    uint16_t map_h = (uint16_t)(s_map_y1 - s_map_y0 + 1u);
    uint8_t blit_rows = s_clamp_u8(s_cfg.blit_rows, 1u, APP_DISPLAY_BLIT_ROWS_MAX);
    uint8_t use_bilinear = (s_cfg.interp_mode == APP_DISPLAY_INTERP_BILINEAR) ? 1u : 0u;
    uint16_t y_blk;

    if ((map_w == 0u) ||
        (map_h == 0u) ||
        (map_w > APP_DISPLAY_MAX_LINE_PIXELS) ||
        (map_h > APP_DISPLAY_MAX_LINE_PIXELS))
    {
        return;
    }
    s_refresh_render_map_cache(map_w, map_h);

    for (y_blk = 0u; y_blk < map_h; y_blk = (uint16_t)(y_blk + blit_rows))
    {
        uint16_t rows = map_h - y_blk;
        uint16_t row;

        if (rows > blit_rows)
        {
            rows = blit_rows;
        }

        for (row = 0u; row < rows; row++)
        {
            uint16_t y = (uint16_t)(y_blk + row);
            uint16_t x;
            uint8_t *dst = &s_blit_l8_buf[(uint32_t)row * (uint32_t)map_w];

            if (use_bilinear != 0u)
            {
                /* 閸欏瞼鍤庨幀褑鐭惧鍕剁窗娴ｈ法鏁ゆ０鍕处鐎涙ê娼楅弽鍥ф嫲閺夊啴鍣告潻娑滎攽 2x2 閹绘帒鈧鈧?*/
                uint16_t y0 = s_row_y0_cache[y];
                uint16_t y1 = s_row_y1_cache[y];
                uint16_t wy = s_row_wy256_cache[y];
                uint16_t wy0 = (uint16_t)(256u - wy);
                const uint8_t *src0 = &s_field_norm_u8[(uint32_t)y0 * APP_DISPLAY_FIELD_W];
                const uint8_t *src1 = &s_field_norm_u8[(uint32_t)y1 * APP_DISPLAY_FIELD_W];

                for (x = 0u; x < map_w; x++)
                {
                    uint16_t x0 = s_col_x0_cache[x];
                    uint16_t x1 = s_col_x1_cache[x];
                    uint16_t wx = s_col_wx256_cache[x];
                    uint16_t wx0 = (uint16_t)(256u - wx);
                    uint32_t v00 = src0[x0];
                    uint32_t v01 = src0[x1];
                    uint32_t v10 = src1[x0];
                    uint32_t v11 = src1[x1];
                    uint32_t vx0 = v00 * wx0 + v01 * wx;
                    uint32_t vx1 = v10 * wx0 + v11 * wx;
                    uint32_t q = (vx0 * wy0 + vx1 * wy + 32768u) >> 16;
                    /* 缂佹挻鐏夐悶鍡氼啈娑撳﹨鎯ら崷?0..255閿涘奔绮涙穱婵嗙暓閸嬫矮绔村▎陇顥嗛崜顏傗偓?*/
                    dst[x] = (q > 255u) ? 255u : (uint8_t)q;
                }
            }
            else
            {
                /* 閺堚偓鏉╂垿鍋︾捄顖氱窞閿涙氨娲块幒銉﹀瘻缂傛挸鐡ㄧ槐銏犵穿閸欐牕鈧鈧?*/
                uint16_t y_idx = s_row_near_cache[y];
                const uint8_t *src = &s_field_norm_u8[(uint32_t)y_idx * APP_DISPLAY_FIELD_W];
                for (x = 0u; x < map_w; x++)
                {
                    dst[x] = src[s_col_near_cache[x]];
                }
            }
        }

        if (ltdc_l8_fill_async(s_map_x0,
                               (uint16_t)(s_map_y0 + y_blk),
                               s_map_x1,
                               (uint16_t)(s_map_y0 + y_blk + rows - 1u),
                               s_blit_l8_buf,
                               map_w) == 0u)
        {
            /* 閼汇儰绗夐懗鐣屾纯閹恒儲瀵?8bit 鐠嬪啳澹婇弶鑳熅瀵板嫭褰佹禍銈忕礉閸掓瑦澧滈崝銊ㄦ祮 RGB565閵?*/
            for (row = 0u; row < rows; row++)
            {
                uint8_t *src = &s_blit_l8_buf[(uint32_t)row * (uint32_t)map_w];
                uint16_t *dst = &s_blit_buf[(uint32_t)row * (uint32_t)map_w];
                uint16_t x;

                for (x = 0u; x < map_w; x++)
                {
                    dst[x] = s_heat_lut[src[x]];
                }
            }

            s_submit_rgb565_block(s_map_x0,
                                  (uint16_t)(s_map_y0 + y_blk),
                                  s_map_x1,
                                  (uint16_t)(s_map_y0 + y_blk + rows - 1u),
                                  s_blit_buf);
        }
        else if (s_flush_temp_draw() != 0u)
        {
            continue;
        }
    }

}
/* 缂佹ê鍩楁笟褑绔熼弬鍥ㄦ拱鐠囧﹥鏌囬崠鍝勭厵閵?*/
static App_FreqBand_t s_spectrum_clamp_band(App_FreqBand_t band, uint16_t bin_count)
{
    uint16_t last_bin;

    if (bin_count == 0u)
    {
        return App_Spectrum_DefaultBand();
    }

    last_bin = (uint16_t)(bin_count - 1u);
    if (band.start_bin > last_bin)
    {
        band.start_bin = last_bin;
    }
    if (band.end_bin > last_bin)
    {
        band.end_bin = last_bin;
    }
    if (band.end_bin < band.start_bin)
    {
        band.end_bin = band.start_bin;
    }

    return band;
}

static uint16_t s_spectrum_display_min_bin(uint16_t bin_count)
{
    uint16_t min_bin = App_Spectrum_HzToBin(APP_SPECTRUM_DISPLAY_MIN_HZ);

    if (bin_count == 0u)
    {
        return 0u;
    }
    if (min_bin == 0u)
    {
        min_bin = 1u;
    }
    if (min_bin >= bin_count)
    {
        min_bin = (uint16_t)(bin_count - 1u);
    }

    return min_bin;
}

static float s_spectrum_axis_min_hz(uint16_t bin_count)
{
    return App_Spectrum_BinToHz(s_spectrum_display_min_bin(bin_count));
}

static float s_spectrum_axis_max_hz(uint16_t bin_count)
{
    if (bin_count == 0u)
    {
        return DELTA_F;
    }

    return App_Spectrum_BinToHz((uint16_t)(bin_count - 1u));
}

static float s_spectrum_freq_norm(float hz, float min_hz, float max_hz)
{
    float clamped_hz = s_clamp_f32(hz, min_hz, max_hz);

    if (max_hz <= min_hz)
    {
        return 0.0f;
    }

    #if (APP_SPECTRUM_FREQ_SCALE_MODE != 0u)
    {
        float log_min = logf(min_hz);
        float log_max = logf(max_hz);

        return (logf(clamped_hz) - log_min) / (log_max - log_min);
    }
    #else
    return (clamped_hz - min_hz) / (max_hz - min_hz);
    #endif
}

static uint16_t s_spectrum_freq_to_y(float hz,
                                     uint16_t plot_y0,
                                     uint16_t plot_h,
                                     float min_hz,
                                     float max_hz)
{
    float norm;
    float y_pos;

    if (plot_h <= 1u)
    {
        return plot_y0;
    }

    norm = s_spectrum_freq_norm(hz, min_hz, max_hz);
    if (APP_SPECTRUM_LOW_FREQ_AT_BOTTOM != 0u)
    {
        y_pos = (float)plot_y0 + ((float)(plot_h - 1u) * (1.0f - norm));
    }
    else
    {
        y_pos = (float)plot_y0 + ((float)(plot_h - 1u) * norm);
    }

    return (uint16_t)(y_pos + 0.5f);
}

static uint16_t s_spectrum_bin_to_y(uint16_t bin,
                                    uint16_t plot_y0,
                                    uint16_t plot_h,
                                    float min_hz,
                                    float max_hz)
{
    return s_spectrum_freq_to_y(App_Spectrum_BinToHz(bin), plot_y0, plot_h, min_hz, max_hz);
}

static uint16_t s_spectrum_db_to_bar_x(float rel_db,
                                       uint16_t plot_x0,
                                       uint16_t plot_w)
{
    float norm;

    if (plot_w <= 1u)
    {
        return plot_x0;
    }

    norm = (rel_db - APP_SPECTRUM_DB_FLOOR) / (0.0f - APP_SPECTRUM_DB_FLOOR);
    norm = s_clamp_f32(norm, 0.0f, 1.0f);
    return (uint16_t)(plot_x0 + ((float)(plot_w - 1u) * norm) + 0.5f);
}

static void s_spectrum_format_freq_label(char *buf, size_t buf_size, float hz)
{
    if ((buf == NULL) || (buf_size == 0u))
    {
        return;
    }

    if (hz >= 1000.0f)
    {
        float khz = hz / 1000.0f;

        if ((khz >= 10.0f) || (fabsf(khz - floorf(khz + 0.5f)) < 0.05f))
        {
            (void)snprintf(buf, buf_size, "%0.0fk", (double)khz);
        }
        else
        {
            (void)snprintf(buf, buf_size, "%0.1fk", (double)khz);
        }
    }
    else
    {
        (void)snprintf(buf, buf_size, "%0.0f", (double)hz);
    }
}

static void s_spectrum_draw_freq_tick(uint16_t panel_x0,
                                      uint16_t plot_x0,
                                      uint16_t plot_x1,
                                      uint16_t plot_y0,
                                      uint16_t plot_y1,
                                      uint16_t y,
                                      float hz,
                                      uint32_t color)
{
    char label[16];
    int32_t label_y = (int32_t)y - 8;

    if ((y < plot_y0) || (y > plot_y1))
    {
        return;
    }

    s_spectrum_format_freq_label(label, sizeof(label), hz);
    lcd_draw_line(plot_x0, y, plot_x1, y, color);

    if (label_y < 0)
    {
        label_y = 0;
    }
    if ((uint32_t)label_y + 16u > lcddev.height)
    {
        label_y = (int32_t)lcddev.height - 16;
    }

    lcd_show_string((uint16_t)(panel_x0 + 2u),
                    (uint16_t)label_y,
                    (uint16_t)(APP_DISPLAY_SPECTRUM_AXIS_LABEL_W - 4u),
                    16u,
                    16u,
                    label,
                    WHITE);
}

static void s_spectrum_draw_guides(uint16_t panel_x0,
                                   uint16_t plot_x0,
                                   uint16_t plot_x1,
                                   uint16_t plot_y0,
                                   uint16_t plot_y1,
                                   float min_hz,
                                   float max_hz)
{
    uint16_t plot_w = (uint16_t)(plot_x1 - plot_x0 + 1u);
    uint16_t plot_h = (uint16_t)(plot_y1 - plot_y0 + 1u);
    uint32_t i;
    char db_floor_label[16];
    static const float k_log_ticks[] = {500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};
    static const float k_lin_ticks[] = {6000.0f, 12000.0f, 18000.0f};

    for (i = 1u; i < APP_DISPLAY_SPECTRUM_GUIDE_DIVS; i++)
    {
        uint16_t x = (uint16_t)(plot_x0 + (((uint32_t)(plot_w - 1u) * i) / APP_DISPLAY_SPECTRUM_GUIDE_DIVS));

        lcd_draw_line(x, plot_y0, x, plot_y1, GRAYBLUE);
    }

    s_spectrum_draw_freq_tick(panel_x0,
                              plot_x0,
                              plot_x1,
                              plot_y0,
                              plot_y1,
                              s_spectrum_freq_to_y(min_hz, plot_y0, plot_h, min_hz, max_hz),
                              min_hz,
                              WHITE);
    s_spectrum_draw_freq_tick(panel_x0,
                              plot_x0,
                              plot_x1,
                              plot_y0,
                              plot_y1,
                              s_spectrum_freq_to_y(max_hz, plot_y0, plot_h, min_hz, max_hz),
                              max_hz,
                              WHITE);

    if (APP_SPECTRUM_FREQ_SCALE_MODE != 0u)
    {
        for (i = 0u; i < (sizeof(k_log_ticks) / sizeof(k_log_ticks[0])); i++)
        {
            float hz = k_log_ticks[i];

            if ((hz <= min_hz) || (hz >= max_hz))
            {
                continue;
            }
            s_spectrum_draw_freq_tick(panel_x0,
                                      plot_x0,
                                      plot_x1,
                                      plot_y0,
                                      plot_y1,
                                      s_spectrum_freq_to_y(hz, plot_y0, plot_h, min_hz, max_hz),
                                      hz,
                                      GRAYBLUE);
        }
    }
    else
    {
        for (i = 0u; i < (sizeof(k_lin_ticks) / sizeof(k_lin_ticks[0])); i++)
        {
            float hz = k_lin_ticks[i];

            if ((hz <= min_hz) || (hz >= max_hz))
            {
                continue;
            }
            s_spectrum_draw_freq_tick(panel_x0,
                                      plot_x0,
                                      plot_x1,
                                      plot_y0,
                                      plot_y1,
                                      s_spectrum_freq_to_y(hz, plot_y0, plot_h, min_hz, max_hz),
                                      hz,
                                      GRAYBLUE);
        }
    }

    (void)snprintf(db_floor_label, sizeof(db_floor_label), "%0.0fdB", (double)APP_SPECTRUM_DB_FLOOR);
    lcd_show_string(plot_x0,
                    (uint16_t)(plot_y1 + 2u),
                    42u,
                    16u,
                    16u,
                    db_floor_label,
                    WHITE);
    {
        static char peak_label[] = "0dB";

        lcd_show_string((uint16_t)(plot_x1 - 28u),
                        (uint16_t)(plot_y1 + 2u),
                        28u,
                        16u,
                        16u,
                        peak_label,
                        WHITE);
    }
}

static void s_draw_overlay(const Sound_Pos_t *pos,
                           const App_SpectrumFrame_t *spectrum_frame,
                           float field_peak,
                           uint8_t sai_dma_active)
{
    App_FreqBand_t active_band = App_Spectrum_DefaultBand();
    App_FreqBand_t preview_band = active_band;
    uint16_t panel_x0 = s_text_x;
    uint16_t panel_x1 = s_ui_x1;
    uint16_t plot_x0;
    uint16_t plot_x1;
    uint16_t plot_y0;
    uint16_t plot_y1;
    uint16_t plot_w;
    uint16_t plot_h;
    uint16_t bin_count = APP_SPECTRUM_BIN_COUNT;
    uint16_t min_bin = 1u;
    uint16_t peak_bin = 1u;
    float current_peak_mag = APP_DISPLAY_SPECTRUM_MIN_MAG;
    float display_ref_mag;
    uint32_t prev_back_color;
#if (APP_SPECTRUM_INFO_ENABLE != 0u)
    float peak_hz = 0.0f;
#endif
    float min_hz;
    float max_hz;

    if ((panel_x0 >= lcddev.width) || (panel_x1 < panel_x0))
    {
        return;
    }

    prev_back_color = g_back_color;
    g_back_color = BLACK;

    lcd_fill(panel_x0, 0u, panel_x1, (uint16_t)(lcddev.height - 1u), BLACK);
    if (panel_x0 > 0u)
    {
        lcd_fill((uint16_t)(panel_x0 - 1u), 0u, (uint16_t)(panel_x0 - 1u), (uint16_t)(lcddev.height - 1u), WHITE);
    }

    plot_x0 = (uint16_t)(panel_x0 + APP_DISPLAY_SPECTRUM_AXIS_LABEL_W);
    plot_x0 = (uint16_t)(plot_x0 + APP_DISPLAY_SPECTRUM_MARGIN_L);
    plot_x1 = (panel_x1 > APP_DISPLAY_SPECTRUM_MARGIN_R)
            ? (uint16_t)(panel_x1 - APP_DISPLAY_SPECTRUM_MARGIN_R)
            : panel_x1;
    plot_y0 = APP_DISPLAY_SPECTRUM_MARGIN_T;
    plot_y1 = (lcddev.height > APP_DISPLAY_SPECTRUM_MARGIN_B)
            ? (uint16_t)(lcddev.height - APP_DISPLAY_SPECTRUM_MARGIN_B - 1u)
            : (uint16_t)(lcddev.height - 1u);
    if ((plot_x1 <= plot_x0) || (plot_y1 <= plot_y0))
    {
        g_back_color = prev_back_color;
        return;
    }

    plot_w = (uint16_t)(plot_x1 - plot_x0 + 1u);
    plot_h = (uint16_t)(plot_y1 - plot_y0 + 1u);

    if ((spectrum_frame != NULL) && (spectrum_frame->bin_count > 0u))
    {
        uint16_t i;

        bin_count = spectrum_frame->bin_count;
        if (bin_count > APP_SPECTRUM_BIN_COUNT)
        {
            bin_count = APP_SPECTRUM_BIN_COUNT;
        }

        min_bin = s_spectrum_display_min_bin(bin_count);
        active_band = s_spectrum_clamp_band(spectrum_frame->active_band, bin_count);
        preview_band = s_spectrum_clamp_band(spectrum_frame->preview_band, bin_count);

        for (i = 0u; i < bin_count; i++)
        {
            float mag = spectrum_frame->magnitude[i];
            float alpha;

            if ((!isfinite(mag)) || (mag < 0.0f))
            {
                mag = 0.0f;
            }

            if (s_spectrum_ema_valid == 0u)
            {
                s_spectrum_ema[i] = mag;
            }
            else
            {
                alpha = (mag >= s_spectrum_ema[i]) ? APP_SPECTRUM_BAR_ATTACK : APP_SPECTRUM_BAR_DECAY;
                s_spectrum_ema[i] += alpha * (mag - s_spectrum_ema[i]);
            }
        }

        s_spectrum_ema_valid = 1u;
    }
    else if (s_spectrum_ema_valid != 0u)
    {
        min_bin = s_spectrum_display_min_bin(bin_count);
    }

    min_hz = s_spectrum_axis_min_hz(bin_count);
    max_hz = s_spectrum_axis_max_hz(bin_count);

    if (s_spectrum_ema_valid != 0u)
    {
        uint16_t i;

        for (i = min_bin; i < bin_count; i++)
        {
            if (s_spectrum_ema[i] > current_peak_mag)
            {
                current_peak_mag = s_spectrum_ema[i];
                peak_bin = i;
            }
        }
    }

    if (current_peak_mag > s_spectrum_ref_mag)
    {
        s_spectrum_ref_mag += APP_SPECTRUM_REF_ATTACK * (current_peak_mag - s_spectrum_ref_mag);
    }
    else
    {
        s_spectrum_ref_mag += APP_SPECTRUM_REF_DECAY * (current_peak_mag - s_spectrum_ref_mag);
    }
    if (s_spectrum_ref_mag < APP_DISPLAY_SPECTRUM_MIN_MAG)
    {
        s_spectrum_ref_mag = APP_DISPLAY_SPECTRUM_MIN_MAG;
    }
    display_ref_mag = s_spectrum_ref_mag;

    lcd_draw_rectangle(plot_x0, plot_y0, plot_x1, plot_y1, WHITE);
    lcd_fill((uint16_t)(plot_x0 + 1u), (uint16_t)(plot_y0 + 1u), (uint16_t)(plot_x1 - 1u), (uint16_t)(plot_y1 - 1u), BLACK);

    {
        uint16_t band_start = (active_band.start_bin < min_bin) ? min_bin : active_band.start_bin;
        uint16_t band_end = (active_band.end_bin < min_bin) ? min_bin : active_band.end_bin;

        if ((band_start < bin_count) && (band_end < bin_count))
        {
            uint16_t y0 = s_spectrum_bin_to_y(band_start, plot_y0, plot_h, min_hz, max_hz);
            uint16_t y1 = s_spectrum_bin_to_y(band_end, plot_y0, plot_h, min_hz, max_hz);
            uint16_t band_y0 = (y0 < y1) ? y0 : y1;
            uint16_t band_y1 = (y0 > y1) ? y0 : y1;

            lcd_fill((uint16_t)(plot_x0 + 1u), band_y0, (uint16_t)(plot_x1 - 1u), band_y1, DARKBLUE);
        }

        if ((preview_band.start_bin != active_band.start_bin) || (preview_band.end_bin != active_band.end_bin))
        {
            uint16_t preview_start = (preview_band.start_bin < min_bin) ? min_bin : preview_band.start_bin;
            uint16_t preview_end = (preview_band.end_bin < min_bin) ? min_bin : preview_band.end_bin;

            if ((preview_start < bin_count) && (preview_end < bin_count))
            {
                uint16_t y0 = s_spectrum_bin_to_y(preview_start, plot_y0, plot_h, min_hz, max_hz);
                uint16_t y1 = s_spectrum_bin_to_y(preview_end, plot_y0, plot_h, min_hz, max_hz);
                uint16_t box_y0 = (y0 < y1) ? y0 : y1;
                uint16_t box_y1 = (y0 > y1) ? y0 : y1;

                lcd_draw_rectangle(plot_x0, box_y0, plot_x1, box_y1, LIGHTBLUE);
            }
        }
    }

    s_spectrum_draw_guides(panel_x0, plot_x0, plot_x1, plot_y0, plot_y1, min_hz, max_hz);

    if ((s_spectrum_ema_valid != 0u) && (display_ref_mag > APP_DISPLAY_SPECTRUM_MIN_MAG))
    {
        uint16_t i;

        for (i = min_bin; i < bin_count; i++)
        {
            float rel_db;
            uint16_t y_center = s_spectrum_bin_to_y(i, plot_y0, plot_h, min_hz, max_hz);
            uint16_t y_next = (i + 1u < bin_count)
                            ? s_spectrum_bin_to_y((uint16_t)(i + 1u), plot_y0, plot_h, min_hz, max_hz)
                            : plot_y0;
            uint16_t y_prev = (i > min_bin)
                            ? s_spectrum_bin_to_y((uint16_t)(i - 1u), plot_y0, plot_h, min_hz, max_hz)
                            : plot_y1;
            uint16_t bar_y0;
            uint16_t bar_y1;
            uint16_t bar_x1;
            uint32_t color = ((i >= active_band.start_bin) && (i <= active_band.end_bin)) ? YELLOW : CYAN;

            rel_db = 20.0f * log10f(fmaxf(s_spectrum_ema[i], APP_DISPLAY_SPECTRUM_MIN_MAG) / display_ref_mag);
            if (rel_db < APP_SPECTRUM_DB_FLOOR)
            {
                rel_db = APP_SPECTRUM_DB_FLOOR;
            }

            bar_x1 = s_spectrum_db_to_bar_x(rel_db, (uint16_t)(plot_x0 + 1u), (uint16_t)(plot_w - 2u));

            bar_y0 = (uint16_t)((y_center + y_next) / 2u);
            bar_y1 = (uint16_t)((y_center + y_prev) / 2u);
            if (bar_y1 < bar_y0)
            {
                uint16_t tmp = bar_y0;
                bar_y0 = bar_y1;
                bar_y1 = tmp;
            }
            if (bar_y1 < plot_y0)
            {
                bar_y1 = plot_y0;
            }
            if (bar_y0 > plot_y1)
            {
                bar_y0 = plot_y1;
            }

            lcd_fill((uint16_t)(plot_x0 + 1u), bar_y0, bar_x1, bar_y1, color);
        }

#if (APP_SPECTRUM_INFO_ENABLE != 0u)
        peak_hz = App_Spectrum_BinToHz(peak_bin);
#endif
        {
            uint16_t peak_y = s_spectrum_bin_to_y(peak_bin, plot_y0, plot_h, min_hz, max_hz);
            uint16_t marker_x1 = (uint16_t)(plot_x0 + ((plot_w > 14u) ? 12u : (plot_w - 1u)));

            lcd_draw_line(plot_x0, peak_y, marker_x1, peak_y, RED);
        }
    }
    else
    {
        static char idle_msg[] = "FFT idle";

        lcd_show_string((uint16_t)(plot_x0 + 12u),
                        (uint16_t)(plot_y0 + (plot_h / 2u) - 8u),
                        (uint16_t)(plot_w - 24u),
                        16u,
                        16u,
                        idle_msg,
                        GRAY);
    }

#if (APP_SPECTRUM_INFO_ENABLE != 0u)
    {
        char line[72];
        uint16_t info_x = (uint16_t)(plot_x0 + 4u);
        uint16_t info_y = (uint16_t)(plot_y0 + 4u);
        float band_lo_hz = App_Spectrum_BinToHz(active_band.start_bin);
        float band_hi_hz = App_Spectrum_BinToHz(active_band.end_bin);

        (void)snprintf(line, sizeof(line), "Band %0.1f~%0.0fHz", (double)band_lo_hz, (double)band_hi_hz);
        lcd_show_string(info_x, info_y, (uint16_t)(plot_w - 8u), 16u, 16u, line, LIGHTBLUE);

        (void)snprintf(line, sizeof(line), "Peak %0.1fkHz", (double)(peak_hz / 1000.0f));
        lcd_show_string(info_x, (uint16_t)(info_y + 16u), (uint16_t)(plot_w - 8u), 16u, 16u, line, YELLOW);

        (void)snprintf(line, sizeof(line), "Pos %+4.1f %+4.1f E %0.2f", (double)pos->x_angle, (double)pos->y_angle, isfinite(pos->energy) ? (double)pos->energy : 0.0);
        lcd_show_string(info_x, (uint16_t)(info_y + 32u), (uint16_t)(plot_w - 8u), 16u, 16u, line, CYAN);

        (void)snprintf(line,
                       sizeof(line),
                       "%s S:%s F:%0.1e",
                       App_Display_ModeName(s_mode),
                       (sai_dma_active != 0u) ? "ON" : "OFF",
                       (double)field_peak);
        lcd_show_string(info_x, (uint16_t)(info_y + 48u), (uint16_t)(plot_w - 8u), 16u, 16u, line, WHITE);
    }
#else
    (void)pos;
    (void)field_peak;
    (void)sai_dma_active;
    (void)s_last_noise_floor;
#endif

    g_back_color = prev_back_color;
}

/* 濞撳弶鐓嬫稉鈧弫鏉戞姎婢规澘顒熼幋鎰剼閻㈠娼伴妴? * 鏉╂瑦妲稿Ο鈥虫健鐎电懓顦婚張鈧弽绋跨妇閻ㄥ嫰鈧劕鎶氶崗銉ュ經閵?*/
void App_Display_Render(const Sound_Pos_t *pos,
                        const SRP_VisFrame_t *vis_frame,
                        const App_CameraFrame_t *camera_frame,
                        uint32_t frame_seq,
                        uint8_t sai_dma_active)
{
    float field_peak;
    uint32_t t_perf;
    App_SpectrumFrame_t spectrum_snapshot;
    const App_SpectrumFrame_t *spectrum_frame = NULL;
    uint8_t back_slot = s_backbuf_slot();
    uint32_t peak_idx = 0u;
    float peak_theta = 0.0f;
    float peak_phi = 0.0f;
    uint8_t camera_valid;
    if ((s_ready == 0u) || (pos == NULL) || (vis_frame == NULL))
    {
        return;
    }
    (void)back_slot;
    if (ltdc_wait_for_swap_complete(s_display_frame_budget_ms()) != 0u)
    {
        return;
    }
    t_perf = App_Perf_BeginCycles();
    field_peak = s_prepare_field(vis_frame);
    App_Perf_EndCycles(APP_PERF_SEC_DISP_PREPARE, t_perf);
    if (field_peak > s_peak_ema)
    {
        s_peak_ema += s_cfg.ema_attack * (field_peak - s_peak_ema);
    }
    else
    {
        s_peak_ema += s_cfg.ema_decay * (field_peak - s_peak_ema);
    }
    if (s_peak_ema < APP_DISPLAY_EMA_MIN_PEAK)
    {
        s_peak_ema = APP_DISPLAY_EMA_MIN_PEAK;
    }
    t_perf = App_Perf_BeginCycles();
    s_update_norm_field(field_peak, frame_seq);
    App_Perf_EndCycles(APP_PERF_SEC_DISP_NORM, t_perf);
    camera_valid = (uint8_t)((camera_frame != NULL) &&
                             (camera_frame->valid != 0u) &&
                             (camera_frame->pixels != NULL) &&
                             (camera_frame->width != 0u) &&
                             (camera_frame->height != 0u));
    t_perf = App_Perf_BeginCycles();
    s_clear_scene_gutters();
    if (((camera_valid != 0u) ||
         ((s_camera_view_mode == APP_DISPLAY_CAMERA_VIEW_CAMERA_FREEZE) && (s_camera_freeze_valid != 0u))) &&
        (s_camera_view_mode != APP_DISPLAY_CAMERA_VIEW_HEAT_ONLY))
    {
        s_dbg_camera_path_count++;
        s_dbg_camera_input_seq = (camera_valid != 0u) ? camera_frame->seq : s_camera_cache_seq;
        if (s_camera_view_mode == APP_DISPLAY_CAMERA_VIEW_CAMERA_ONLY)
        {
            s_render_camera_frame_rows(camera_frame);
        }
        else if (s_camera_view_mode == APP_DISPLAY_CAMERA_VIEW_CAMERA_FREEZE)
        {
            App_CameraFrame_t frozen_frame;

            if (s_camera_freeze_valid == 0u)
            {
                (void)s_capture_frozen_camera_frame(camera_frame);
            }

            memset(&frozen_frame, 0, sizeof(frozen_frame));
            if (s_camera_freeze_valid != 0u)
            {
                frozen_frame.pixels = s_camera_cache_pixels;
                frozen_frame.width = s_camera_freeze_w;
                frozen_frame.height = s_camera_freeze_h;
                frozen_frame.stride = s_camera_freeze_stride;
                frozen_frame.seq = s_camera_cache_seq;
                frozen_frame.valid = 1u;
                s_render_camera_frame_rows(&frozen_frame);
            }
        }
        else
        {
            s_render_field_alpha_rows(camera_frame, APP_CAMERA_OVERLAY_COLOR_565);
            s_dbg_camera_overlay_count++;
        }
    }
    if ((((camera_valid == 0u) &&
          !((s_camera_view_mode == APP_DISPLAY_CAMERA_VIEW_CAMERA_FREEZE) && (s_camera_freeze_valid != 0u))) ||
         (s_camera_view_mode == APP_DISPLAY_CAMERA_VIEW_HEAT_ONLY)))
    {
        s_camera_cache_valid = 0u;
        s_fill_rect_async(s_camera_x0, s_camera_y0, s_camera_x1, s_camera_y1, BLACK);
        s_render_field_rows();
    }
    s_draw_rect_async(s_map_x0, s_map_y0, s_map_x1, s_map_y1, WHITE);
    if (s_text_x > 0u)
    {
        s_draw_vline_async((uint16_t)(s_text_x - 1u), 0u, (uint16_t)(lcddev.height - 1u), WHITE);
    }
    if (vis_frame->peak_idx < SRP_GRID_TOTAL)
    {
        peak_idx = vis_frame->peak_idx;
        peak_theta = vis_frame->theta_deg[peak_idx];
        peak_phi = vis_frame->phi_deg[peak_idx];
    }
    s_apply_output_remap(&peak_theta, &peak_phi);
    {
        float ax = s_clamp_f32(pos->x_angle, (float)COARSE_ANGLE_MIN_DEG, (float)COARSE_ANGLE_MAX_DEG);
        float ay = s_clamp_f32(pos->y_angle, (float)COARSE_ANGLE_MIN_DEG, (float)COARSE_ANGLE_MAX_DEG);
        uint16_t cx = s_angle_to_x(ax);
        uint16_t cy = s_angle_to_y(ay);
        uint16_t half = APP_DISPLAY_CROSSHAIR_HALF_PX;
        uint16_t xl = (cx > (uint16_t)(s_map_x0 + half)) ? (uint16_t)(cx - half) : s_map_x0;
        uint16_t xr = ((uint32_t)cx + half < s_map_x1) ? (uint16_t)(cx + half) : s_map_x1;
        uint16_t yt = (cy > (uint16_t)(s_map_y0 + half)) ? (uint16_t)(cy - half) : s_map_y0;
        uint16_t yb = ((uint32_t)cy + half < s_map_y1) ? (uint16_t)(cy + half) : s_map_y1;
        s_draw_hline_async(xl, cy, xr, WHITE);
        s_draw_vline_async(cx, yt, yb, WHITE);
    }
    {
        uint16_t px = s_angle_to_x(peak_theta);
        uint16_t py = s_angle_to_y(peak_phi);
        uint16_t r = APP_DISPLAY_PEAK_MARKER_RADIUS_PX;
        uint16_t x0 = (px > (uint16_t)(s_map_x0 + r)) ? (uint16_t)(px - r) : s_map_x0;
        uint16_t y0 = (py > (uint16_t)(s_map_y0 + r)) ? (uint16_t)(py - r) : s_map_y0;
        uint16_t x1 = ((uint32_t)px + r < s_map_x1) ? (uint16_t)(px + r) : s_map_x1;
        uint16_t y1 = ((uint32_t)py + r < s_map_y1) ? (uint16_t)(py + r) : s_map_y1;
        s_draw_rect_async(x0, y0, x1, y1, WHITE);
    }
    App_Perf_EndCycles(APP_PERF_SEC_DISP_RENDER, t_perf);
    if (App_Spectrum_GetLatestFrame(&spectrum_snapshot) != 0u)
    {
        s_last_spectrum_frame = spectrum_snapshot;
        s_spectrum_frame_valid = 1u;
    }
    if (s_spectrum_frame_valid != 0u)
    {
        spectrum_frame = &s_last_spectrum_frame;
    }
    t_perf = App_Perf_BeginCycles();
    s_draw_overlay(pos, spectrum_frame, field_peak, sai_dma_active);
    App_Perf_EndCycles(APP_PERF_SEC_DISP_OVERLAY, t_perf);
    App_TouchTest_Render();
    t_perf = App_Perf_BeginCycles();
    s_commit_frame();
    App_Perf_EndCycles(APP_PERF_SEC_DISP_COMMIT, t_perf);
}

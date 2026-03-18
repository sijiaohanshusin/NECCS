/**
 * @file    ai_beamforming.c
 * @brief   澹板娉㈡潫鎴愬舰涓?SRP-PHAT 澹版簮瀹氫綅绠楁硶瀹炵幇
 * @details 瀹炵幇鍩轰簬鐩镐綅鍙樻崲鐨勫彲鎺у搷搴斿姛鐜?(SRP-PHAT) 澹版簮瀹氫綅绠楁硶
 *
 * 绠楁硶鍘熺悊锛? * 1. GCC-PHAT (骞夸箟浜掔浉鍏崇浉浣嶅彉鎹?
 *    - 璁＄畻楹﹀厠椋庡涔嬮棿鐨勪簰鐩稿叧
 *    - PHAT 鐧藉寲锛氶櫎浠ュ箙搴︼紝淇濈暀鐩镐綅淇℃伅
 *    - 浼樼偣锛氭姂鍒舵贩鍝嶏紝澧炲己鐩磋揪澹? *
 * 2. SRP (鍙帶鍝嶅簲鍔熺巼)
 *    - 鍦ㄧ┖闂寸綉鏍间笂鎼滅储鑳介噺鏈€澶х殑鏂瑰悜
 *    - 绱姞鎵€鏈夐害鍏嬮瀵圭殑 GCC-PHAT 鍝嶅簲
 *    - 鏈€澶у€煎搴斿０婧愭柟鍚? *
 * 3. 绮楁悳 + 绮炬悳绛栫暐
 *    - 绮楁悳锛?脳9 缃戞牸 (卤60掳, 姝ラ暱 15掳)锛屽揩閫熷畾浣? *    - 绮炬悳锛歍op-3 宄板€煎懆鍥?4脳4 缃戞牸 (卤10掳, 姝ラ暱 5掳)锛岀簿纭畾浣? *    - 鎬绘壂鎻忕偣锛?1 (绮楁悳) + 48 (绮炬悳) = 129 鐐? *
 * 鎬ц兘浼樺寲锛? * - 鐩镐綅鏃嬭浆浼樺寲锛氭瘡瀵归害鍏嬮浠?4 娆′笁瑙掑嚱鏁拌皟鐢? * - LUT 鍔犻€燂細绮楁悳浣跨敤棰勮绠?TDOA 琛? * - SIMD 鍔犻€燂細CMSIS-DSP 澶嶆暟杩愮畻
 */

#include "ai_beamforming.h"

#include "ai_config.h"
#include "ai_srp_lut.h"
#include "app_data_stream.h"

#include <math.h>
#include <string.h>

/* ============================================================================
 * 鍏ㄥ眬璇婃柇鍙橀噺 (Global Diagnostic Variables)
 * ============================================================================ */

/** @brief 鏃犳晥缁撴灉璁℃暟鍣?(NaN 鎴栫储寮曡秺鐣? */
volatile uint32_t g_srp_invalid_count = 0u;

/** @brief 浣庡姣斿害缁撴灉璁℃暟鍣?(璐ㄩ噺浣庝簬闂ㄩ檺) */
volatile uint32_t g_srp_low_contrast_count = 0u;

/** @brief 涓婃璁＄畻鐨勫姣斿害 (鐢ㄤ簬璋冭瘯) */
volatile float32_t g_srp_last_contrast = 0.0f;

/** @brief 涓婃璁＄畻鐨勮川閲忔寚鏍?(鐢ㄤ簬璋冭瘯) */
volatile float32_t g_srp_last_quality = 0.0f;
volatile uint32_t g_srp_vis_publish_count = 0u;
volatile uint32_t g_srp_vis_snapshot_retry_count = 0u;
static volatile uint32_t s_srp_last_peak_idx = 0u;
static volatile float32_t s_srp_last_peak_value = 0.0f;

/* ============================================================================
 * 闈欐€佸彉閲?(Static Variables)
 * ============================================================================ */

/** @brief 鏄惁鏈変笂娆℃湁鏁堢粨鏋?(0=鏃? 1=鏈? */
static uint8_t s_has_last_valid = 0u;

/** @brief 涓婃鏈夋晥鐨勫０婧愪綅缃?(鐢ㄤ簬浣庣疆淇″害鏃朵繚鎸? */
static Sound_Pos_t s_last_valid = {0.0f, 0.0f, 0.0f};

/** @brief 杩炵画浣庣疆淇″害甯ц鏁?(鐢ㄤ簬娣峰悎绛栫暐) */
static uint32_t s_low_conf_streak = 0u;

/** @brief 绮炬悳瑙掑害琛?(姘村钩瑙掞紝鍔ㄦ€佺敓鎴? */
static float32_t s_fine_theta_table[FINE_TOTAL];

/** @brief 绮炬悳瑙掑害琛?(鍨傜洿瑙掞紝鍔ㄦ€佺敓鎴? */
static float32_t s_fine_phi_table[FINE_TOTAL];
static SRP_VisFrame_t s_vis_publish_buffers[2];
static volatile uint8_t s_vis_publish_index = 0u;
static volatile uint32_t s_vis_publish_seq = 0u;

static void s_fill_visualization_frame(SRP_VisFrame_t *frame)
{
    uint32_t i;

    if (frame == NULL)
    {
        return;
    }

    memcpy(frame->power, SRP_Power, sizeof(float32_t) * SRP_GRID_TOTAL);

    for (i = 0u; i < COARSE_TOTAL; i++)
    {
        uint32_t ti = i / COARSE_GRID_SIZE;
        uint32_t pi = i % COARSE_GRID_SIZE;
        frame->theta_deg[i] = coarse_theta_deg[ti];
        frame->phi_deg[i] = coarse_phi_deg[pi];
    }

    for (i = 0u; i < FINE_TOTAL; i++)
    {
        frame->theta_deg[COARSE_TOTAL + i] = s_fine_theta_table[i];
        frame->phi_deg[COARSE_TOTAL + i] = s_fine_phi_table[i];
    }

    frame->peak_idx = (uint32_t)s_srp_last_peak_idx;
    frame->peak_value = (float32_t)s_srp_last_peak_value;
    if ((!isfinite(frame->peak_value)) || (frame->peak_idx >= SRP_GRID_TOTAL))
    {
        arm_max_f32(frame->power, SRP_GRID_TOTAL, &frame->peak_value, &frame->peak_idx);
    }
}

static void s_publish_visualization_frame(void)
{
    uint8_t next_index = (uint8_t)(s_vis_publish_index ^ 1u);

    __DMB();
    s_vis_publish_seq++;
    __DMB();
    s_fill_visualization_frame(&s_vis_publish_buffers[next_index]);
    __DMB();
    s_vis_publish_index = next_index;
    s_vis_publish_seq++;
    g_srp_vis_publish_count++;
}

/* ============================================================================
 * 缂栬瘧鏃舵鏌?(Compile-Time Checks)
 * ============================================================================ */

/** @brief 妫€鏌ヤ綆缃俊搴︾瓥鐣ラ厤缃槸鍚︽湁鏁?*/
#if (SRP_LOWCONF_POLICY != SRP_LOWCONF_REPORT_NEW) && \
    (SRP_LOWCONF_POLICY != SRP_LOWCONF_HOLD_LAST) && \
    (SRP_LOWCONF_POLICY != SRP_LOWCONF_MIXED)
#error "Invalid SRP_LOWCONF_POLICY"
#endif

/* ============================================================================
 * 杈呭姪鍑芥暟 (Helper Functions)
 * ============================================================================ */

/**
 * @brief   鍒ゆ柇涓や釜绮楁悳绱㈢偣鏄惁涓洪偦灞? * @details 鐢ㄤ簬 NMS (闈炴瀬澶у€兼姂鍒? 绠楁硶
 *
 * 閭诲眳瀹氫箟锛? * - 鍦?9脳9 缃戞牸涓紝鏇煎搱椤胯窛绂?<= SRP_TOPK_NMS_RADIUS
 * - 渚嬪锛?3,4) 鍜?(3,5) 鏄偦灞?(璺濈=1)
 * - 渚嬪锛?3,4) 鍜?(5,6) 涓嶆槸閭诲眳 (璺濈=3)
 *
 * @param   a  绮楁悳绱㈢偣绱㈠紩 A (0-80)
 * @param   b  绮楁悳绱㈢偣绱㈠紩 B (0-80)
 * @return  1: 鏄偦灞? 0: 涓嶆槸閭诲眳
 */
static uint8_t coarse_idx_is_neighbor(uint32_t a, uint32_t b)
{
    /* 灏嗕竴缁寸储寮曡浆鎹负浜岀淮鍧愭爣 (琛? 鍒? */
    uint32_t ai = a / COARSE_GRID_SIZE;  /* A 鐨勮绱㈠紩 (theta) */
    uint32_t ap = a % COARSE_GRID_SIZE;  /* A 鐨勫垪绱㈠紩 (phi) */
    uint32_t bi = b / COARSE_GRID_SIZE;  /* B 鐨勮绱㈠紩 (theta) */
    uint32_t bp = b % COARSE_GRID_SIZE;  /* B 鐨勫垪绱㈠紩 (phi) */

    /* 璁＄畻鏇煎搱椤胯窛绂?*/
    uint32_t dti = (ai > bi) ? (ai - bi) : (bi - ai);  /* 琛岃窛绂?*/
    uint32_t dpi = (ap > bp) ? (ap - bp) : (bp - ap);  /* 鍒楄窛绂?*/

    /* 鍒ゆ柇鏄惁鍦ㄩ偦鍩熷崐寰勫唴 */
    return (uint8_t)((dti <= SRP_TOPK_NMS_RADIUS) && (dpi <= SRP_TOPK_NMS_RADIUS));
}

/**
 * @brief   甯?NMS 鐨?Top-K 閫夋嫨绠楁硶
 * @details 浠庣矖鎼滅储缁撴灉涓€夋嫨 Top-K 涓嘲鍊硷紝鍚屾椂鎶戝埗鐩搁偦宄板€? *
 * NMS (Non-Maximum Suppression) 鍘熺悊锛? * - 閫夋嫨鏈€澶у€煎悗锛屾帓闄ゅ叾閭诲煙鍐呯殑鐐? * - 閬垮厤閫夋嫨鐩搁偦鐨勫嘲鍊?(鍙兘鏄悓涓€涓０婧愮殑鏃佺摚)
 * - 纭繚 Top-K 宄板€煎垎鏁ｅ湪涓嶅悓鍖哄煙
 *
 * 绠楁硶娴佺▼锛? * 1. 鎵惧埌鏈娇鐢ㄧ偣涓殑鏈€澶у€? * 2. 妫€鏌ヨ鐐规槸鍚︿笌宸查€夌偣鐩搁偦
 * 3. 濡傛灉涓嶇浉閭伙紝閫夋嫨璇ョ偣锛涘惁鍒欒烦杩? * 4. 閲嶅鐩村埌閫夊 K 涓偣
 * 5. 濡傛灉鏃犳硶閫夊 K 涓偣锛屾斁瀹介檺鍒剁户缁€夋嫨
 *
 * @param   arr      绮楁悳绱㈠姛鐜囨暟缁?(COARSE_TOTAL 涓偣)
 * @param   top_idx  杈撳嚭 Top-K 绱㈠紩鏁扮粍 (闀垮害 k)
 * @param   k        闇€瑕侀€夋嫨鐨勫嘲鍊兼暟閲?(閫氬父涓?3)
 */
static void find_top_k_indices_nms(const float32_t *arr, uint32_t *top_idx, uint32_t k)
{
    uint8_t used[COARSE_TOTAL] = {0};  /* 鏍囪宸蹭娇鐢ㄧ殑鐐?*/
    uint32_t chosen = 0u;              /* 宸查€夋嫨鐨勭偣鏁?*/

    /* 閫夋嫨 K 涓嘲鍊?*/
    for (uint32_t s = 0u; s < k; s++)
    {
        float32_t best_val = -1.0e30f;  /* 褰撳墠鏈€澶у€?*/
        uint32_t best_idx = 0u;         /* 褰撳墠鏈€澶у€肩储寮?*/
        uint8_t found = 0u;             /* 鏄惁鎵惧埌鏈夋晥鐐?*/

        /* 绗竴杞細鍦ㄩ潪閭诲煙鐐逛腑鎵炬渶澶у€?*/
        for (uint32_t n = 0u; n < COARSE_TOTAL; n++)
        {
            /* 璺宠繃宸蹭娇鐢ㄧ殑鐐?*/
            if (used[n] != 0u)
            {
                continue;
            }

            /* 妫€鏌ユ槸鍚︿笌宸查€夌偣鐩搁偦 */
            uint8_t allow = 1u;
            for (uint32_t c = 0u; c < chosen; c++)
            {
                if (coarse_idx_is_neighbor(n, top_idx[c]) != 0u)
                {
                    allow = 0u;  /* 鐩搁偦锛屼笉鍏佽閫夋嫨 */
                    break;
                }
            }
            if ((allow == 0u) || (arr[n] <= best_val))
            {
                continue;
            }

            /* 鏇存柊鏈€澶у€?*/
            best_val = arr[n];
            best_idx = n;
            found = 1u;
        }

        /* 绗簩杞細濡傛灉绗竴杞病鎵惧埌锛屾斁瀹介檺鍒?(鍏佽閭诲煙鐐? */
        if (found == 0u)
        {
            for (uint32_t n = 0u; n < COARSE_TOTAL; n++)
            {
                if ((used[n] == 0u) && (arr[n] > best_val))
                {
                    best_val = arr[n];
                    best_idx = n;
                    found = 1u;
                }
            }
        }

        /* 璁板綍閫夋嫨鐨勭偣 */
        top_idx[s] = best_idx;
        if (found != 0u)
        {
            used[best_idx] = 1u;
            chosen++;
        }
    }
}

/**
 * @brief   鑾峰彇缃戞牸鐐圭殑瑙掑害
 * @details 鏍规嵁绱㈠紩鏌ヨ绮楁悳鎴栫簿鎼滅殑瑙掑害
 *
 * 绱㈠紩鑼冨洿锛? * - [0, COARSE_TOTAL): 绮楁悳缃戞牸锛屾煡琛?coarse_theta_deg/coarse_phi_deg
 * - [COARSE_TOTAL, COARSE_TOTAL+FINE_TOTAL): 绮炬悳缃戞牸锛屾煡琛?s_fine_theta_table/s_fine_phi_table
 *
 * @param   idx        缃戞牸鐐圭储寮?(0-128)
 * @param   theta_deg  杈撳嚭姘村钩瑙?(搴?
 * @param   phi_deg    杈撳嚭鍨傜洿瑙?(搴?
 */
static void get_grid_angle(uint32_t idx, float32_t *theta_deg, float32_t *phi_deg)
{
    /* 绮楁悳缃戞牸 */
    if (idx < COARSE_TOTAL)
    {
        uint32_t ti = idx / COARSE_GRID_SIZE;  /* 琛岀储寮?(theta) */
        uint32_t pi = idx % COARSE_GRID_SIZE;  /* 鍒楃储寮?(phi) */
        *theta_deg = coarse_theta_deg[ti];
        *phi_deg = coarse_phi_deg[pi];
        return;
    }

    /* 绮炬悳缃戞牸 */
    idx -= COARSE_TOTAL;
    if (idx < FINE_TOTAL)
    {
        *theta_deg = s_fine_theta_table[idx];
        *phi_deg = s_fine_phi_table[idx];
        return;
    }

    /* 瓒婄晫淇濇姢 */
    *theta_deg = 0.0f;
    *phi_deg = 0.0f;
}

/**
 * @brief   璁＄畻鍏ㄥ眬娆″ぇ鍊?(涓嶆帓闄ら偦鍩?
 * @details 鐢ㄤ簬璁＄畻鍘熷瀵规瘮搴? *
 * @param   max_idx  鏈€澶у€肩储寮? * @return  娆″ぇ鍊? */
static float32_t compute_second_max_all(uint32_t max_idx)
{
    float32_t second_max = -1.0e30f;

    /* 閬嶅巻鎵€鏈夌偣锛屾壘娆″ぇ鍊?*/
    for (uint32_t i = 0u; i < SRP_GRID_TOTAL; i++)
    {
        if ((i != max_idx) && (SRP_Power[i] > second_max))
        {
            second_max = SRP_Power[i];
        }
    }

    return second_max;
}

/**
 * @brief   璁＄畻鎺掗櫎閭诲煙鐨勬澶у€? * @details 鐢ㄤ簬璁＄畻璐ㄩ噺鎸囨爣锛屾帓闄ゆ渶澶у€煎懆鍥寸殑鏃佺摚
 *
 * 閭诲煙瀹氫箟锛? * - 瑙掑害璺濈 <= SRP_CONTRAST_NEIGHBOR_EXCLUDE_DEG (榛樿 5掳)
 * - 鎺掗櫎鏃佺摚骞叉壈锛屾洿鍑嗙‘璇勪及宄板€艰川閲? *
 * @param   max_idx  鏈€澶у€肩储寮? * @return  鎺掗櫎閭诲煙鍚庣殑娆″ぇ鍊? */
static float32_t compute_second_max_excluding_neighbor(uint32_t max_idx)
{
    float32_t max_theta, max_phi;
    float32_t second_max = -1.0e30f;
    uint8_t found = 0u;

    /* 鑾峰彇鏈€澶у€肩殑瑙掑害 */
    get_grid_angle(max_idx, &max_theta, &max_phi);

    /* 閬嶅巻鎵€鏈夌偣锛屾壘鎺掗櫎閭诲煙鍚庣殑娆″ぇ鍊?*/
    for (uint32_t i = 0u; i < SRP_GRID_TOTAL; i++)
    {
        if (i == max_idx)
        {
            continue;
        }

        float32_t theta, phi;
        get_grid_angle(i, &theta, &phi);

        /* 鎺掗櫎閭诲煙鍐呯殑鐐?*/
        if ((fabsf(theta - max_theta) <= SRP_CONTRAST_NEIGHBOR_EXCLUDE_DEG) &&
            (fabsf(phi - max_phi) <= SRP_CONTRAST_NEIGHBOR_EXCLUDE_DEG))
        {
            continue;
        }

        /* 鏇存柊娆″ぇ鍊?*/
        if (SRP_Power[i] > second_max)
        {
            second_max = SRP_Power[i];
            found = 1u;
        }
    }

    /* 濡傛灉娌℃壘鍒?(鎵€鏈夌偣閮藉湪閭诲煙鍐?锛屽洖閫€鍒板叏灞€娆″ぇ鍊?*/
    if (found == 0u)
    {
        return compute_second_max_all(max_idx);
    }

    return second_max;
}

/**
 * @brief   搴旂敤浣庣疆淇″害鑳介噺杞笂闄? * @details 闄愬埗浣庣疆淇″害缁撴灉鐨勮兘閲忓€硷紝閬垮厤璇
 *
 * @param   energy  鍘熷鑳介噺
 * @return  澶勭悊鍚庣殑鑳介噺
 */
static float32_t apply_lowconf_energy(float32_t energy)
{
#if (SRP_ENABLE_ENERGY_SOFTCAP != 0u)
    /* 濡傛灉鑳介噺瓒呰繃涓婇檺锛屾埅鏂埌涓婇檺 */
    if (energy > SRP_AMBIGUOUS_ENERGY_MAX)
    {
        return SRP_AMBIGUOUS_ENERGY_MAX;
    }
#endif
    return energy;
}

/**
 * @brief   杈撳嚭瑙掑害閲嶆槧灏? * @details 鏍规嵁瀹夎鏂瑰悜璋冩暣杈撳嚭瑙掑害
 *
 * 鏀寔鐨勫彉鎹細
 * - 浜ゆ崲 X/Y 杞?(鏃嬭浆 90掳)
 * - 鍙嶈浆 X 杞?(闀滃儚缈昏浆)
 * - 鍙嶈浆 Y 杞?(闀滃儚缈昏浆)
 *
 * @param   x_angle  杈撳叆/杈撳嚭姘村钩瑙?(搴?
 * @param   y_angle  杈撳叆/杈撳嚭鍨傜洿瑙?(搴?
 */
static void remap_output_angles(float32_t *x_angle, float32_t *y_angle)
{
#if (SRP_OUTPUT_SWAP_XY != 0u)
    /* 浜ゆ崲 X/Y 杞?*/
    float32_t tmp = *x_angle;
    *x_angle = *y_angle;
    *y_angle = tmp;
#endif

#if (SRP_OUTPUT_INVERT_X != 0u)
    /* 鍙嶈浆 X 杞?*/
    *x_angle = -*x_angle;
#endif

#if (SRP_OUTPUT_INVERT_Y != 0u)
    /* 鍙嶈浆 Y 杞?*/
    *y_angle = -*y_angle;
#endif
}

/* ============================================================================
 * 鏍稿績绠楁硶鍑芥暟瀹炵幇 (Core Algorithm Implementation)
 * ============================================================================ */

/**
 * @brief   FFT 棰戝煙鍙樻崲澶勭悊
 * @details 瀵?16 璺害鍏嬮淇″彿杩涜棰戝煙鍙樻崲锛屼负 SRP-PHAT 鍑嗗鏁版嵁
 *
 * 澶勭悊娴佺▼锛? * 1. 鍘荤洿娴侊細璁＄畻鍧囧€煎苟鍑忓幓 (arm_mean_f32 + arm_offset_f32)
 *    - 鍘熷洜锛氱洿娴佸垎閲忎細骞叉壈浣庨 bin锛屽奖鍝嶅畾浣嶇簿搴? *    - 鏂规硶锛氳绠楀抚鍧囧€硷紝閫愮偣鍑忓幓
 *
 * 2. 鍔犵獥锛氫箻浠ユ眽瀹佺獥锛屽噺灏戦璋辨硠婕?(arm_mult_f32)
 *    - 鍘熷洜锛氱煩褰㈢獥浼氫骇鐢熶弗閲嶇殑棰戣氨娉勬紡
 *    - 鏂规硶锛氶€愮偣涔樹互棰勮绠楃殑姹夊畞绐楀嚱鏁? *    - 鏁堟灉锛氫富鐡ｅ彉瀹斤紝鏃佺摚闄嶄綆 40dB
 *
 * 3. RFFT锛氬疄鏁板揩閫熷倕閲屽彾鍙樻崲 (arm_rfft_fast_f32)
 *    - 绠楁硶锛欳ooley-Tukey FFT (鍩?2 鍒嗘不)
 *    - 澶嶆潅搴︼細O(N log N) = O(256 脳 8) = 2048 娆¤繍绠? *    - 浼樺寲锛歋IMD 鎸囦护骞惰澶勭悊 4 涓偣
 *
 * 杈撳叆鏁版嵁锛歁ic_Process_Buffer (16ch 脳 256 鐐?float32, DTCM)
 * 杈撳嚭鏁版嵁锛歁ic_Freq_Buffer (16ch 脳 256 鐐瑰鏁? DTCM)
 *
 * @note    鑰楁椂锛氱害 0.8ms @ 480MHz (16ch 脳 256 鐐?
 *          - 鍘荤洿娴侊細绾?0.1ms
 *          - 鍔犵獥锛氱害 0.1ms
 *          - RFFT锛氱害 0.6ms
 */
void AI_FFT_Process(void)
{
    float32_t mean_val;  /* 甯у潎鍊?(鐩存祦鍒嗛噺) */

    /* 瀵规瘡涓€氶亾杩涜 FFT 澶勭悊 */
    for (uint32_t ch = 0u; ch < MIC_CHANNELS; ch++)
    {
        /* 鎸囧悜褰撳墠閫氶亾鐨勬椂鍩熸暟鎹?*/
        float32_t *p_time = &Mic_Process_Buffer[ch * FRAME_LEN];

        /* 鎸囧悜褰撳墠閫氶亾鐨勯鍩熸暟鎹?*/
        float32_t *p_freq = &Mic_Freq_Buffer[ch * FRAME_LEN];

        /* 1. 鍘荤洿娴侊細璁＄畻鍧囧€?*/
        /* arm_mean_f32: 绱姞鎵€鏈夌偣锛岄櫎浠ョ偣鏁?*/
        arm_mean_f32(p_time, FRAME_LEN, &mean_val);

        /* 2. 鍘荤洿娴侊細鍑忓幓鍧囧€?*/
        /* arm_offset_f32: 閫愮偣鍑忓幓 mean_val */
        arm_offset_f32(p_time, -mean_val, p_time, FRAME_LEN);

        /* 3. 鍔犵獥锛氶€愮偣涔樹互姹夊畞绐?*/
        /* arm_mult_f32: p_time[i] *= Hanning_Window[i] */
        arm_mult_f32(p_time, Hanning_Window, p_time, FRAME_LEN);

        /* 4. RFFT锛氭椂鍩?鈫?棰戝煙 */
        /* arm_rfft_fast_f32: 256 鐐瑰疄鏁?FFT */
        /* ifftFlag=0: 姝ｅ悜 FFT (鏃跺煙 鈫?棰戝煙) */
        arm_rfft_fast_f32(&S_Rfft, p_time, p_freq, 0);
    }
}

/**
 * @brief   GCC-PHAT 璁＄畻
 * @details 璁＄畻鎵€鏈夐害鍏嬮瀵圭殑骞夸箟浜掔浉鍏崇浉浣嶅彉鎹? *
 * GCC-PHAT 鍘熺悊锛? * - GCC (Generalized Cross-Correlation): 骞夸箟浜掔浉鍏? *   R_ij(f) = X_i(f) * conj(X_j(f))
 *
 * - PHAT (Phase Transform): 鐩镐綅鍙樻崲 (鐧藉寲)
 *   R_ij_PHAT(f) = R_ij(f) / |R_ij(f)|
 *
 * - 浣滅敤锛? *   1. 淇濈暀鐩镐綅淇℃伅 (鏃跺欢宸?
 *   2. 鎶戝埗骞呭害淇℃伅 (娣峰搷銆佸櫔澹?
 *   3. 澧炲己鐩磋揪澹帮紝鎻愬崌瀹氫綅绮惧害
 *
 * 绠楁硶娴佺▼锛? * 1. 璁＄畻浜掔浉鍏筹細X_i(f) * conj(X_j(f))
 * 2. 璁＄畻骞呭害锛殀R_ij(f)|
 * 3. 鐧藉寲锛歊_ij(f) / (|R_ij(f)| + epsilon)
 *
 * 杈撳叆鏁版嵁锛歁ic_Freq_Buffer (16ch 脳 256 鐐瑰鏁? DTCM)
 * 杈撳嚭鏁版嵁锛欸CC_PHAT_Buffer (40 瀵?脳 40 bins 脳 2, AXI SRAM)
 *
 * @note    鑰楁椂锛氱害 1.5ms @ 480MHz (40 瀵?脳 40 bins)
 */
static void SRP_GCC_PHAT_Compute(void)
{
    /* 涓存椂缂撳啿鍖?(鏍堜笂鍒嗛厤锛屽揩閫熻闂? */
    float32_t conj_buf[SRP_FREQ_BINS * 2u];   /* 鍏辫江澶嶆暟 */
    float32_t cross_buf[SRP_FREQ_BINS * 2u];  /* 浜掔浉鍏?*/
    float32_t mag_buf[SRP_FREQ_BINS];         /* 骞呭害 */

    /* 瀵规瘡瀵归害鍏嬮璁＄畻 GCC-PHAT */
    for (uint32_t p = 0u; p < SRP_PAIR_COUNT; p++)
    {
        /* 鑾峰彇楹﹀厠椋庡鐨勭储寮?*/
        uint8_t mi = srp_pair_idx[p][0];  /* 楹﹀厠椋?i */
        uint8_t mj = srp_pair_idx[p][1];  /* 楹﹀厠椋?j */

        /* 鎸囧悜楹﹀厠椋?i 鐨勯鍩熸暟鎹?(璺宠繃 DC 鍜屼綆棰?bin) */
        const float32_t *xi = &Mic_Freq_Buffer[(uint32_t)mi * FRAME_LEN + (SRP_FREQ_BIN_START * 2u)];

        /* 鎸囧悜楹﹀厠椋?j 鐨勯鍩熸暟鎹?*/
        const float32_t *xj = &Mic_Freq_Buffer[(uint32_t)mj * FRAME_LEN + (SRP_FREQ_BIN_START * 2u)];

        /* 1. 璁＄畻鍏辫江锛歝onj(X_j) = [Re, -Im] */
        /* arm_cmplx_conj_f32: 瀹為儴涓嶅彉锛岃櫄閮ㄥ彇鍙?*/
        arm_cmplx_conj_f32((float32_t *)xj, conj_buf, SRP_FREQ_BINS);

        /* 2. 璁＄畻浜掔浉鍏筹細X_i * conj(X_j) */
        /* arm_cmplx_mult_cmplx_f32: 澶嶆暟涔樻硶 */
        /* (a+bi) * (c-di) = (ac+bd) + (bc-ad)i */
        arm_cmplx_mult_cmplx_f32((float32_t *)xi, conj_buf, cross_buf, SRP_FREQ_BINS);

        /* 3. 璁＄畻骞呭害锛殀R_ij| = sqrt(Re^2 + Im^2) */
        /* arm_cmplx_mag_f32: 澶嶆暟妯?*/
        arm_cmplx_mag_f32(cross_buf, mag_buf, SRP_FREQ_BINS);

        /* 4. PHAT 鐧藉寲锛歊_ij / (|R_ij| + epsilon) */
        float32_t *p_out = &GCC_PHAT_Buffer[p * SRP_FREQ_BINS * 2u];
        for (uint32_t k = 0u; k < SRP_FREQ_BINS; k++)
        {
            /* 璁＄畻鍊掓暟 (鍔?epsilon 闃叉闄ら浂) */
            float32_t inv_mag = 1.0f / (mag_buf[k] + PHAT_EPSILON);

            /* 鐧藉寲锛氬疄閮ㄥ拰铏氶儴閮介櫎浠ュ箙搴?*/
            p_out[2u * k] = cross_buf[2u * k] * inv_mag;       /* 瀹為儴 */
            p_out[2u * k + 1u] = cross_buf[2u * k + 1u] * inv_mag;  /* 铏氶儴 */
        }
    }
}

/**
 * @brief   SRP 绱姞璁＄畻
 * @details 瀵圭粰瀹氭柟鍚戠疮鍔犳墍鏈夐害鍏嬮瀵圭殑 GCC-PHAT 鍝嶅簲
 *
 * SRP 鍘熺悊锛? * - 鍋囪澹版簮鍦ㄦ柟鍚?(theta, phi)
 * - 璁＄畻姣忓楹﹀厠椋庣殑鐞嗚鏃跺欢宸?tau
 * - 灏?GCC-PHAT 鍝嶅簲鏃嬭浆 tau 瀵瑰簲鐨勭浉浣? * - 绱姞鎵€鏈夊鐨勫搷搴旓紝寰楀埌璇ユ柟鍚戠殑鍔熺巼
 *
 * 鐩镐綅鏃嬭浆鍏紡锛? * - 棰戠巼 f_k 鐨勭浉浣嶆棆杞細exp(j * 2蟺 * f_k * tau)
 * - 閫掓帹璁＄畻锛歟xp(j * 2蟺 * f_k * tau) = exp(j * 2蟺 * f_0 * tau) * [exp(j * 2蟺 * 螖f * tau)]^k
 * - 浼樺寲锛氭瘡瀵归害鍏嬮浠?4 娆′笁瑙掑嚱鏁拌皟鐢?(sin_d, cos_d, sin_phi, cos_phi)
 *
 * 绠楁硶娴佺▼锛? * 1. 璁＄畻鐩镐綅澧為噺锛歞_phi = 360掳 * 螖f * tau
 * 2. 璁＄畻鍒濆鐩镐綅锛歱hi_0 = d_phi * f_start
 * 3. 閫掓帹璁＄畻锛歱hi_k = phi_{k-1} + d_phi
 * 4. 绱姞鍝嶅簲锛歱ower += GCC[k] * exp(j * phi_k)
 *
 * @param   tau  鏃跺欢宸暟缁?(SRP_PAIR_COUNT 涓厓绱狅紝鍗曚綅锛氱)
 * @return  璇ユ柟鍚戠殑 SRP 鍔熺巼
 *
 * @note    鑰楁椂锛氱害 0.03ms @ 480MHz (40 瀵?脳 40 bins)
 */
static float32_t SRP_Accumulate_Point(const float32_t *tau)
{
    float32_t power = 0.0f;  /* 绱姞鍔熺巼 */

    /* 瀵规瘡瀵归害鍏嬮绱姞鍝嶅簲 */
    for (uint32_t p = 0u; p < SRP_PAIR_COUNT; p++)
    {
        /* 鎸囧悜褰撳墠楹﹀厠椋庡鐨?GCC-PHAT 鏁版嵁 */
        const float32_t *gcc = &GCC_PHAT_Buffer[p * SRP_FREQ_BINS * 2u];

        /* 璁＄畻鐩镐綅澧為噺 (搴? */
        /* d_phi = 360掳 * 螖f * tau */
        float32_t d_phi_deg = 360.0f * DELTA_F * tau[p];

        /* 璁＄畻鐩镐綅澧為噺鐨?sin/cos (鐢ㄤ簬閫掓帹) */
        float32_t sin_d, cos_d;
        arm_sin_cos_f32(d_phi_deg, &sin_d, &cos_d);

        /* 璁＄畻鍒濆鐩镐綅 (bin f_start 鐨勭浉浣? */
        /* phi_0 = d_phi * f_start */
        float32_t sin_phi, cos_phi;
        arm_sin_cos_f32(d_phi_deg * (float32_t)SRP_FREQ_BIN_START, &sin_phi, &cos_phi);

        /* 绱姞鎵€鏈夐鐜?bin 鐨勫搷搴?*/
        float32_t pair_sum = 0.0f;
        for (uint32_t k = 0u; k < SRP_FREQ_BINS; k++)
        {
            /* 璁＄畻鍐呯Н锛欸CC[k] * exp(j * phi_k) */
            /* 瀹為儴锛歊e(GCC) * cos(phi) + Im(GCC) * sin(phi) */
            pair_sum += gcc[2u * k] * cos_phi + gcc[2u * k + 1u] * sin_phi;

            /* 閫掓帹璁＄畻涓嬩竴涓?bin 鐨勭浉浣?*/
            /* exp(j * phi_{k+1}) = exp(j * phi_k) * exp(j * d_phi) */
            /* cos(phi_{k+1}) = cos(phi_k) * cos(d_phi) - sin(phi_k) * sin(d_phi) */
            /* sin(phi_{k+1}) = sin(phi_k) * cos(d_phi) + cos(phi_k) * sin(d_phi) */
            float32_t c_new = cos_phi * cos_d - sin_phi * sin_d;
            float32_t s_new = sin_phi * cos_d + cos_phi * sin_d;
            cos_phi = c_new;
            sin_phi = s_new;
        }

        /* 绱姞褰撳墠楹﹀厠椋庡鐨勫搷搴?*/
        power += pair_sum;
    }

    return power;
}

/**
 * @brief   SRP-PHAT 绠楁硶鍒濆鍖? * @details 鍒濆鍖栫粺璁¤鏁板櫒鍜屽巻鍙茬姸鎬? *
 * 鍒濆鍖栧唴瀹癸細
 * - 娓呴浂鏃犳晥缁撴灉璁℃暟鍣? * - 娓呴浂浣庡姣斿害璁℃暟鍣? * - 閲嶇疆涓婃鏈夋晥缁撴灉缂撳瓨
 * - 閲嶇疆浣庣疆淇″害杩炵画甯ц鏁? *
 * @note    鍦ㄧ郴缁熷惎鍔ㄦ椂璋冪敤涓€娆″嵆鍙?(App_Stream_Init 涓?
 */
void AI_SRP_PHAT_Init(void)
{
    /* 娓呴浂璇婃柇璁℃暟鍣?*/
    g_srp_invalid_count = 0u;
    g_srp_low_contrast_count = 0u;
    g_srp_last_contrast = 0.0f;
    g_srp_last_quality = 0.0f;
    s_srp_last_peak_idx = 0u;
    s_srp_last_peak_value = 0.0f;
    g_srp_vis_publish_count = 0u;
    g_srp_vis_snapshot_retry_count = 0u;
    s_vis_publish_index = 0u;
    s_vis_publish_seq = 0u;
    memset(s_vis_publish_buffers, 0, sizeof(s_vis_publish_buffers));

    /* 閲嶇疆鍘嗗彶鐘舵€?*/
    s_has_last_valid = 0u;
    s_last_valid.x_angle = 0.0f;
    s_last_valid.y_angle = 0.0f;
    s_last_valid.energy = 0.0f;
    s_low_conf_streak = 0u;
}

/**
 * @brief   SRP-PHAT 澹版簮瀹氫綅澶勭悊
 * @details 鍩轰簬棰戝煙鏁版嵁璁＄畻澹版簮鏂逛綅瑙掑拰鑳介噺
 *
 * 绠楁硶娴佺▼锛? * 1. GCC-PHAT 璁＄畻锛氬 40 瀵归害鍏嬮璁＄畻骞夸箟浜掔浉鍏? * 2. 绮楁悳锛氬湪 9脳9 缃戞牸 (卤60掳, 姝ラ暱 15掳) 涓婅绠?SRP 鍔熺巼
 * 3. Top-K 閫夋嫨锛氶€夋嫨鍓?3 涓嘲鍊?(甯?NMS 鎶戝埗鐩搁偦宄板€?
 * 4. 绮炬悳锛氬湪姣忎釜宄板€煎懆鍥?4脳4 缃戞牸 (卤10掳, 姝ラ暱 5掳) 缁嗗寲
 * 5. 璐ㄩ噺璇勪及锛氳绠楀姣斿害鍜岃川閲忔寚鏍? * 6. 缁撴灉杈撳嚭锛氭牴鎹疆淇″害绛栫暐杈撳嚭鏈€缁堢粨鏋? *
 * 绮楁悳绛栫暐锛? * - 浣跨敤棰勮绠?TDOA 鏌ユ壘琛?(tdoa_coarse_lut)
 * - 蹇€熸壂鎻?81 涓柟鍚戯紝瀹氫綅澶ц嚧鍖哄煙
 * - 鑰楁椂锛氱害 2.5ms (81 鐐?脳 0.03ms/鐐?
 *
 * 绮炬悳绛栫暐锛? * - 瀹炴椂璁＄畻 TDOA (鍥犱负瑙掑害涓嶅浐瀹?
 * - 鍦?Top-3 宄板€煎懆鍥寸粏鍖栵紝鎻愬崌绮惧害
 * - 鑰楁椂锛氱害 1.5ms (48 鐐?脳 0.03ms/鐐?
 *
 * 璐ㄩ噺璇勪及锛? * - 瀵规瘮搴?(contrast): (鏈€澶у€?- 娆″ぇ鍊? / 鏈€澶у€? * - 璐ㄩ噺 (quality): (鏈€澶у€?- 杩滃娆″ぇ鍊? / 鏈€澶у€? * - 浣庣疆淇″害锛歲uality < SRP_CONTRAST_MIN_RATIO
 *
 * 浣庣疆淇″害澶勭悊绛栫暐锛? * - REPORT_NEW: 鎶ュ憡鏂扮粨鏋?(瀹炴椂鍝嶅簲)
 * - HOLD_LAST: 淇濇寔涓婃鏈夋晥缁撴灉 (骞虫粦杈撳嚭)
 * - MIXED: 鐭湡淇濇寔锛岄暱鏈熸洿鏂?(鎶樹腑鏂规)
 *
 * @param   result  杈撳嚭澹版簮浣嶇疆缁撴瀯浣撴寚閽? *                  - x_angle: 姘村钩瑙掑害 (搴?
 *                  - y_angle: 鍨傜洿瑙掑害 (搴?
 *                  - energy: 褰掍竴鍖栬兘閲?[0, 1]
 *
 * @note    鑰楁椂锛氱害 4ms @ 480MHz (1.5ms GCC-PHAT + 2.5ms 绮楁悳 + 1.5ms 绮炬悳)
 * @note    蹇呴』鍦?AI_FFT_Process() 瀹屾垚鍚庤皟鐢? */
void AI_SRP_PHAT_Process(Sound_Pos_t *result)
{
    /* 鍙傛暟妫€鏌?*/
    if (result == NULL)
    {
        return;
    }

    /* 涓存椂鍙橀噺 */
    float32_t tau_buf[SRP_PAIR_COUNT];  /* TDOA 缂撳啿鍖?*/
    const float32_t fine_step = (2.0f * FINE_SPAN_DEG) / (float32_t)FINE_GRID_SIZE;
    const float32_t inv_c = 1.0f / SPEED_OF_SOUND;  /* 澹伴€熷€掓暟 (棰勮绠? */

    /* ========== 姝ラ 1: GCC-PHAT 璁＄畻 ========== */
    /* 璁＄畻鎵€鏈夐害鍏嬮瀵圭殑骞夸箟浜掔浉鍏崇浉浣嶅彉鎹?*/
    SRP_GCC_PHAT_Compute();

    /* ========== 姝ラ 2: 绮楁悳 ========== */
    /* 鍦?9脳9 缃戞牸涓婃壂鎻忥紝浣跨敤棰勮绠?TDOA 琛?*/
    for (uint32_t g = 0u; g < COARSE_TOTAL; g++)
    {
        /* 鏌ヨ〃鑾峰彇 TDOA锛岀疮鍔?SRP 鍔熺巼 */
        SRP_Power[g] = SRP_Accumulate_Point(tdoa_coarse_lut[g]);
    }

    /* ========== 姝ラ 3: Top-K 閫夋嫨 ========== */
    /* 閫夋嫨鍓?3 涓嘲鍊硷紝甯?NMS 鎶戝埗鐩搁偦宄板€?*/
    uint32_t top_idx[FINE_TOP_K];
    find_top_k_indices_nms(SRP_Power, top_idx, FINE_TOP_K);

    /* ========== 姝ラ 4: 绮炬悳 ========== */
    /* 鍦?Top-3 宄板€煎懆鍥磋繘琛?4脳4 绮剧粏鎵弿 */
    for (uint32_t t = 0u; t < FINE_TOP_K; t++)
    {
        /* 鑾峰彇绮楁悳宄板€肩殑绱㈠紩鍜岃搴?*/
        uint32_t coarse_g = top_idx[t];
        uint32_t ti = coarse_g / COARSE_GRID_SIZE;  /* 琛岀储寮?(theta) */
        uint32_t pi = coarse_g % COARSE_GRID_SIZE;  /* 鍒楃储寮?(phi) */

        float32_t center_theta = coarse_theta_deg[ti];  /* 涓績姘村钩瑙?*/
        float32_t center_phi = coarse_phi_deg[pi];      /* 涓績鍨傜洿瑙?*/

        /* 鍦ㄤ腑蹇冨懆鍥?4脳4 缃戞牸鎵弿 */
        for (uint32_t fi = 0u; fi < FINE_GRID_SIZE; fi++)
        {
            /* 璁＄畻绮炬悳姘村钩瑙?*/
            float32_t theta_h = center_theta + (-FINE_SPAN_DEG + ((float32_t)fi + 0.5f) * fine_step);
            float32_t sin_th, cos_th_unused;
            arm_sin_cos_f32(theta_h, &sin_th, &cos_th_unused);

            for (uint32_t fj = 0u; fj < FINE_GRID_SIZE; fj++)
            {
                /* 璁＄畻鍏ㄥ眬绱㈠紩 */
                uint32_t local_idx = fi * FINE_GRID_SIZE + fj;
                uint32_t global_idx = t * FINE_TOTAL_PER_TOP + local_idx;

                /* 璁＄畻绮炬悳鍨傜洿瑙?*/
                float32_t theta_v = center_phi + (-FINE_SPAN_DEG + ((float32_t)fj + 0.5f) * fine_step);
                float32_t sin_tv, cos_tv;
                arm_sin_cos_f32(theta_v, &sin_tv, &cos_tv);

                /* 璁板綍绮炬悳瑙掑害 (鐢ㄤ簬鍚庣画鏌ヨ) */
                s_fine_theta_table[global_idx] = theta_h;
                s_fine_phi_table[global_idx] = theta_v;

                /* 瀹炴椂璁＄畻 TDOA */
                /* tau = (dx * sin(theta) * cos(phi) + dy * sin(phi)) / c */
                float32_t sin_th_cos_tv = sin_th * cos_tv;
                for (uint32_t p = 0u; p < SRP_PAIR_COUNT; p++)
                {
                    tau_buf[p] = (srp_pair_dx[p] * sin_th_cos_tv + srp_pair_dy[p] * sin_tv) * inv_c;
                }

                /* 绱姞 SRP 鍔熺巼 */
                SRP_Power[COARSE_TOTAL + global_idx] = SRP_Accumulate_Point(tau_buf);
            }
        }
    }

    /* ========== 姝ラ 5: 鍏ㄥ眬鏈€澶у€兼悳绱?========== */
    /* 鍦ㄧ矖鎼?+ 绮炬悳缁撴灉涓壘鏈€澶у€?*/
    float32_t max_val;
    uint32_t max_idx;
    arm_max_f32(SRP_Power, SRP_GRID_TOTAL, &max_val, &max_idx);
    s_srp_last_peak_idx = max_idx;
    s_srp_last_peak_value = max_val;

    /* ========== 姝ラ 6: 寮傚父澶勭悊 ========== */
    /* 妫€鏌ョ粨鏋滄槸鍚︽湁鏁?(NaN 鎴栫储寮曡秺鐣? */
    if ((!isfinite(max_val)) || (max_idx >= SRP_GRID_TOTAL))
    {
        /* 璁板綍鏃犳晥缁撴灉 */
        g_srp_invalid_count++;
        g_srp_last_contrast = 0.0f;
        g_srp_last_quality = 0.0f;
        s_srp_last_peak_idx = 0u;
        s_srp_last_peak_value = 0.0f;

        /* 濡傛灉鏈変笂娆℃湁鏁堢粨鏋滐紝琛板噺杈撳嚭锛涘惁鍒欒緭鍑洪浂 */
        if (s_has_last_valid != 0u)
        {
            *result = s_last_valid;
            result->energy *= 0.90f;  /* 琛板噺 10% */
            remap_output_angles(&result->x_angle, &result->y_angle);
        }
        else
        {
            result->x_angle = 0.0f;
            result->y_angle = 0.0f;
            result->energy = 0.0f;
        }
        s_publish_visualization_frame();
        return;
    }

    /* ========== 姝ラ 7: 鎻愬彇鍊欓€夎搴?========== */
    float32_t cand_x;  /* 鍊欓€夋按骞宠 */
    float32_t cand_y;  /* 鍊欓€夊瀭鐩磋 */

    /* 鏍规嵁绱㈠紩鍒ゆ柇鏄矖鎼滆繕鏄簿鎼滅粨鏋?*/
    if (max_idx < COARSE_TOTAL)
    {
        /* 绮楁悳缁撴灉 */
        uint32_t ti = max_idx / COARSE_GRID_SIZE;
        uint32_t pi = max_idx % COARSE_GRID_SIZE;
        cand_x = coarse_theta_deg[ti];
        cand_y = coarse_phi_deg[pi];
    }
    else
    {
        /* 绮炬悳缁撴灉 */
        uint32_t fine_idx = max_idx - COARSE_TOTAL;
        cand_x = s_fine_theta_table[fine_idx];
        cand_y = s_fine_phi_table[fine_idx];
    }

    /* ========== 姝ラ 8: 鑳介噺褰掍竴鍖?========== */
    /* 褰掍竴鍖栧埌 [0, 1] 鑼冨洿 */
    float32_t norm = max_val / (float32_t)(SRP_PAIR_COUNT * SRP_FREQ_BINS);
    if (norm > 1.0f)
    {
        norm = 1.0f;  /* 涓婇檺鎴柇 */
    }
    if (norm < 0.0f)
    {
        norm = 0.0f;  /* 涓嬮檺鎴柇 */
    }

    /* ========== 姝ラ 9: 璐ㄩ噺璇勪及 ========== */
    /* 璁＄畻瀵规瘮搴﹀拰璐ㄩ噺鎸囨爣 */
    float32_t second_max_raw = compute_second_max_all(max_idx);  /* 鍏ㄥ眬娆″ぇ鍊?*/
    float32_t second_max_quality = compute_second_max_excluding_neighbor(max_idx);  /* 鎺掗櫎閭诲煙娆″ぇ鍊?*/

    /* 瀵规瘮搴︼細(鏈€澶у€?- 娆″ぇ鍊? / 鏈€澶у€?*/
    float32_t contrast_raw = (max_val - second_max_raw) / (fabsf(max_val) + 1.0e-6f);

    /* 璐ㄩ噺锛?鏈€澶у€?- 杩滃娆″ぇ鍊? / 鏈€澶у€?*/
    float32_t quality = (max_val - second_max_quality) / (fabsf(max_val) + 1.0e-6f);

    /* 璁板綍璇婃柇淇℃伅 */
    g_srp_last_contrast = contrast_raw;
    g_srp_last_quality = quality;

    /* ========== 姝ラ 10: 缃俊搴﹀垽鏂?========== */
    /* 鍒ゆ柇鏄惁涓轰綆缃俊搴︾粨鏋?*/
    uint8_t low_conf = (uint8_t)(quality < SRP_CONTRAST_MIN_RATIO);
    if (low_conf != 0u)
    {
        /* 浣庣疆淇″害锛氱疮鍔犺鏁板櫒 */
        g_srp_low_contrast_count++;
        s_low_conf_streak++;
    }
    else
    {
        /* 楂樼疆淇″害锛氶噸缃繛缁鏁?*/
        s_low_conf_streak = 0u;
    }

    /* ========== 姝ラ 11: 浣庣疆淇″害绛栫暐 ========== */
    /* 鏍规嵁閰嶇疆鐨勭瓥鐣ュ喅瀹氭槸鍚︿繚鎸佷笂娆＄粨鏋?*/
    uint8_t hold_last = 0u;
#if (SRP_LOWCONF_POLICY == SRP_LOWCONF_HOLD_LAST)
    /* 绛栫暐 1: 濮嬬粓淇濇寔涓婃鏈夋晥缁撴灉 */
    hold_last = (uint8_t)(s_has_last_valid != 0u);
#elif (SRP_LOWCONF_POLICY == SRP_LOWCONF_MIXED)
    /* 绛栫暐 2: 鐭湡淇濇寔锛岄暱鏈熸洿鏂?*/
    hold_last = (uint8_t)((s_has_last_valid != 0u) && (s_low_conf_streak <= SRP_LOWCONF_MIXED_HOLD_FRAMES));
#endif

    /* ========== 姝ラ 12: 杈撳嚭缁撴灉 ========== */
    /* 鏍规嵁缃俊搴﹀拰绛栫暐閫夋嫨杈撳嚭瑙掑害 */
    float32_t base_x = ((low_conf != 0u) && (hold_last != 0u)) ? s_last_valid.x_angle : cand_x;
    float32_t base_y = ((low_conf != 0u) && (hold_last != 0u)) ? s_last_valid.y_angle : cand_y;

    /* 搴旂敤杈撳嚭瑙掑害閲嶆槧灏?(閫傞厤瀹夎鏂瑰悜) */
    remap_output_angles(&base_x, &base_y);

    /* 濉厖杈撳嚭缁撴瀯浣?*/
    result->x_angle = base_x;
    result->y_angle = base_y;
    result->energy = (low_conf != 0u) ? apply_lowconf_energy(norm) : norm;

    /* ========== 姝ラ 13: 缂撳瓨鏈夋晥缁撴灉 ========== */
    /* 濡傛灉鏄珮缃俊搴︿笖婊¤冻闂ㄩ檺锛岀紦瀛樹负涓婃鏈夋晥缁撴灉 */
    if ((low_conf == 0u) &&
        (result->energy >= SRP_VALID_MIN_ENERGY) &&
        (quality >= SRP_VALID_MIN_QUALITY))
    {
        /* 缂撳瓨鍘熷瑙掑害 (鏈噸鏄犲皠) */
        s_last_valid.x_angle = cand_x;
        s_last_valid.y_angle = cand_y;
        s_last_valid.energy = result->energy;
        s_has_last_valid = 1u;
    }

    s_publish_visualization_frame();
}

void AI_SRP_CopyVisualizationFrame(SRP_VisFrame_t *frame)
{
    s_fill_visualization_frame(frame);
}

uint8_t AI_SRP_GetLatestVisualizationFrame(SRP_VisFrame_t *frame)
{
    uint32_t start_seq;
    uint32_t end_seq;
    uint8_t index;
    uint32_t attempt;

    if (frame == NULL)
    {
        return 0u;
    }

    for (attempt = 0u; attempt < 3u; attempt++)
    {
        start_seq = s_vis_publish_seq;
        __DMB();
        if ((start_seq & 1u) != 0u)
        {
            g_srp_vis_snapshot_retry_count++;
            continue;
        }

        index = s_vis_publish_index;
        memcpy(frame, &s_vis_publish_buffers[index], sizeof(*frame));
        __DMB();
        end_seq = s_vis_publish_seq;
        if ((start_seq == end_seq) && ((end_seq & 1u) == 0u))
        {
            return 1u;
        }

        g_srp_vis_snapshot_retry_count++;
    }

    return 0u;
}

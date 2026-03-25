/**
 * @file    app_audio_task.c
 * @brief   Audio pipeline task implementation
 */
#include "ai_beamforming.h"
#include "ai_preprocess.h"
#include "app_data_output.h"
#include "app_data_stream.h"
#include "app_main_task.h"
#include "app_perf.h"
#include "app_runtime.h"
#include "app_spectrum.h"
#include "app_task_cfg.h"

/* ============================================================================
 * 澶栭儴鍙橀噺 (External Variables)
 * ============================================================================ */

/** @brief 璋冭瘯璁℃暟锛氭瘡澶勭悊涓€甯ч煶棰戦€掑涓€娆?*/
extern int16_t found_val;

static uint32_t s_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo)           /* 浣庝簬涓嬮檺锛氱洿鎺ヨ繑鍥炰笅闄愬€?*/
    {
        return lo;
    }
    if (v > hi)           /* 楂樹簬涓婇檺锛氱洿鎺ヨ繑鍥炰笂闄愬€?*/
    {
        return hi;
    }
    return v;             /* 鍦ㄨ寖鍥村唴锛氬師鍊艰繑鍥?*/
}

/**
 * @brief   闊抽澶勭悊涓讳换鍔?
 * @details 澶勭悊娴佺▼锛欴MA 鍗婄紦鍐蹭簨浠?-> 瑙ｄ氦缁?-> FFT -> SRP-PHAT -> 缁撴灉鎶曢€掋€?
 *
 * 鍏抽敭鐐癸細
 * - 閫氳繃 `event.seq` 妫€娴嬩簨浠惰烦鍙樺苟绱寮傚父璁℃暟銆?
 * - 浣跨敤 `audio_algo_decim` 瀹炵幇绠楁硶闄嶉噰鏍凤紝闈炴墽琛屽抚澶嶇敤涓婃瀹氫綅缁撴灉銆?
 * - 澶嶇敤 `Mic_Freq_Buffer` 浣滀负涓存椂 q15 骞抽潰缂撳啿锛屽噺灏戦澶?RAM 鍗犵敤銆?
 *
 * 璋冭瘯杈撳嚭锛?
 * - `DEBUG_MODE=0`: 杈撳嚭 RMS銆?
 * - `DEBUG_MODE=1`: 杈撳嚭 FFT銆?
 * - `DEBUG_MODE=3`: 杈撳嚭 SRP 缁撴灉銆?
 *
 * @param   pvParameters  FreeRTOS 浠诲姟鍙傛暟锛堟湭浣跨敤锛?
 */
void Audio_Pipeline_Task(void *pvParameters)
{
    (void)pvParameters;  /* 浠诲姟鍙傛暟鏈娇鐢紝鏄惧紡杞崲閬垮厤缂栬瘧鍣?-Wunused-parameter 璀﹀憡 */

    /* ---- 浠诲姟灞€閮ㄧ姸鎬佸彉閲忥紙persistent across iterations锛?---- */

    /** @brief 宸叉墽琛岃В浜ょ粐鐨勬€诲抚鏁帮紙鐢ㄤ簬 DEBUG 鑺傛祦鍒ゆ柇锛?*/
    static uint32_t s_frame_cnt = 0u;

    /** @brief 涓婁竴涓帴鏀跺埌鐨?ISR 甯у簭鍙凤紝鐢ㄤ簬妫€娴嬭烦鍙橈紙涓㈠抚妫€娴嬶級 */
    uint32_t s_last_seq = 0u;

    /** @brief 鎶藉抚鐩镐綅璁℃暟锛? ~ decim-1 寰幆锛夛紝phase==0 鏃舵墽琛岀畻娉?*/
    uint32_t s_decim_phase = 0u;

    /** @brief 鏈疆锛堝惈鎶藉抚澶嶇敤锛夎閫佸線 UI 鐨勫０婧愪綅缃?*/
    Sound_Pos_t current_pos   = {0.0f, 0.0f, 0.0f};

    /** @brief 涓婃 SRP-PHAT 绠楁硶璁＄畻寰楀埌鐨勫０婧愪綅缃紙鎶藉抚鏃跺鐢級 */
    Sound_Pos_t last_algo_pos = {0.0f, 0.0f, 0.0f};

    /** @brief 鏄惁宸叉湁鑷冲皯涓€娆℃湁鏁堢畻娉曠粨鏋滐紙棣栧抚寮哄埗鎵ц绠楁硶锛屼笉鎶藉抚锛?*/
    uint8_t has_last_algo = 0u;

    /* ---- 甯у鐞嗗伐浣滃彉閲?---- */

    Audio_FrameEvent_t event;  /* 浠庨槦鍒楁帴鏀剁殑 DMA 鍗婄紦鍐蹭簨浠?*/

    /** @brief 鎸囧悜褰撳墠 DMA 鍗婄紦鍐茬殑璧峰鍦板潃锛圥ING 鎴?PONG 鍖猴級 */
    q15_t *p_current_dma_src;

    /** @brief 瑙ｄ氦缁囦复鏃剁紦鍐插尯锛屽鐢?Mic_Freq_Buffer锛堣妭鐪?RAM锛?
     *         Mic_Freq_Buffer 瀛樻斁 FFT 棰戝煙鏁版嵁锛屼絾鍦ㄨВ浜ょ粐闃舵棰戝煙璁＄畻灏氭湭寮€濮嬶紝
     *         鍥犳鍙互涓存椂鍊熺敤锛孎FT 闃舵浼氱敤 FFT 缁撴灉瑕嗙洊姝ゅ尯鍩?*/
    q15_t *p_temp_planar = (q15_t *)Mic_Freq_Buffer;

    /* ================================================================
     * 浠诲姟涓诲惊鐜紙姘镐笉閫€鍑猴級
     * ================================================================ */
    for (;;)
    {
        /* ---- 闃舵 1锛氱瓑寰?DMA 鍗婄紦鍐插畬鎴愪簨浠?---- */
        /* portMAX_DELAY = 姘镐箙闃诲鐩村埌鏈夋暟鎹紝涓嶅崰鐢?CPU */
        if (xQueueReceive(xAudioFrameQueue, &event, portMAX_DELAY) != pdTRUE)
        {
            continue;  /* 鐞嗚涓婁笉浼氬埌杈撅紙portMAX_DELAY 涓嬩笉浼氳秴鏃讹級锛屼繚鐣欎綔涓洪槻寰′唬鐮?*/
        }

        /* ---- 闃舵 2锛氫涪甯ф娴嬶紙閫氳繃搴忓彿璺冲彉鍒ゆ柇锛?---- */
        /* event.seq 鐢?ISR 渚у崟璋冮€掑鍐欏叆锛涜嫢鏈 seq > last_seq+1锛岃鏄庢湁甯ц瑕嗙洊 */
        if ((s_last_seq != 0u) && (event.seq > (s_last_seq + 1u)))
        {
            /* 绱璺冲彉閲忥紙鍙兘璺冲甯э紝濡傞槦鍒楀湪涓ゆ ISR 涔嬮棿鏈娑堣垂锛?*/
            g_audio_both_flags_count += (event.seq - s_last_seq - 1u);
        }
        s_last_seq = event.seq;  /* 鏇存柊涓婃搴忓彿鍩哄噯 */

        /* ---- 闃舵 3锛氭牴鎹?half_id 閫夋嫨姝ｇ‘鐨?DMA 缂撳啿鍖哄湴鍧€ ---- */
        if (event.half_id == AUDIO_DMA_HALF_PING)
        {
            /* PING 鍖猴細DMA 缂撳啿鍖哄墠鍗婃锛屽亸绉?0 */
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[0];
        }
        else if (event.half_id == AUDIO_DMA_HALF_PONG)
        {
            /* PONG 鍖猴細DMA 缂撳啿鍖哄悗鍗婃锛屽亸绉?MIC_CHANNELS * FRAME_LEN */
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[MIC_CHANNELS * FRAME_LEN];
        }
        else
        {
            /* half_id 涓洪潪娉曞€硷紙鐞嗚涓婁笉搴斿彂鐢燂級锛岃褰曞紓甯稿苟璺宠繃鏈抚 */
            g_audio_no_flag_count++;
            continue;
        }

        /* ---- 闃舵 4锛氱畻娉曟娊甯у喅绛?---- */
        {
            /* 璇诲彇褰撳墠鎶藉抚姣旓紙杩愯鏃跺彲閫氳繃 CLI 'cfg algodecim N' 淇敼锛?*/
            uint32_t decim = s_clamp_u32(App_RuntimeConfig_GetAudioAlgoDecim(),
                                         AUDIO_ALGO_DECIM_MIN,
                                         AUDIO_ALGO_DECIM_MAX);

            /* run_algo 鍐崇瓥锛?
             *   - 鑻ヤ粠鏈墽琛岃繃绠楁硶锛堥甯э級锛屽己鍒舵墽琛岋紙閬垮厤 UI 鏄剧ず鍏ㄩ浂浣嶇疆锛?
             *   - 鍚﹀垯锛屼粎鍦ㄧ浉浣嶄负 0 鏃舵墽琛岋紙姣?decim 甯ф墽琛屼竴娆★級 */
            uint8_t run_algo = (has_last_algo == 0u) ? 1u
                             : ((s_decim_phase == 0u) ? 1u : 0u);

            /* 鎺ㄨ繘鐩镐綅璁℃暟锛? -> 1 -> ... -> decim-1 -> 0 寰幆锛?*/
            s_decim_phase++;
            if (s_decim_phase >= decim)
            {
                s_decim_phase = 0u;  /* 鍥炵粫鍒?0锛屼笅娆″皢鍐嶆鎵ц绠楁硶 */
            }

            if (run_algo != 0u)
            {
                /* ============================================================
                 * 鎵ц瀹屾暣绠楁硶娴佹按绾匡細瑙ｄ氦缁?-> FFT -> SRP-PHAT
                 * ============================================================ */
                uint32_t t_audio = App_Perf_BeginCycles();  /* 寮€濮嬫暣浣撹鏃?*/
                uint32_t t_sec;                              /* 鍚勫瓙娈佃鏃惰捣鐐?*/

                found_val++;  /* 璋冭瘯璁℃暟锛氱粺璁″疄闄呮墽琛岀畻娉曠殑甯ф暟锛堝彲閫氳繃璋冭瘯鍣ㄨ瀵燂級 */

                /* ---- 瀛愰樁娈?A锛氳В浜ょ粐 + 绫诲瀷杞崲 ---- */
                /* 灏?DMA 浜ょ粐鏍煎紡锛坈h0_s0, ch1_s0, ..., ch0_s1, ch1_s1, ...锛?
                 * 杞崲涓哄钩闈㈡牸寮忥紙ch0_s0..ch0_sN, ch1_s0..ch1_sN, ...锛?
                 * 鍚屾椂瀹屾垚 q15 -> float 绫诲瀷杞崲骞跺瓨鍏?Mic_Process_Buffer */
                t_sec = App_Perf_BeginCycles();
                Deinterleave_Using_Matrix(p_current_dma_src,  /* 婧愶細浜ょ粐 DMA 缂撳啿 */
                                          p_temp_planar,       /* 涓存椂骞抽潰 q15 缂撳啿 */
                                          Mic_Process_Buffer,  /* 鐩爣锛歠loat 骞抽潰缂撳啿 */
                                          FRAME_LEN,           /* 姣忛€氶亾閲囨牱鐐规暟 */
                                          MIC_CHANNELS);       /* 楹﹀厠椋庨€氶亾鏁?*/
                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_DEINT, t_sec);

                s_frame_cnt++;  /* 閫掑瑙ｄ氦缁囧抚璁℃暟锛堢敤浜?DEBUG 鑺傛祦锛?*/

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 0)
                /* DEBUG 妯″紡 0锛氭瘡 DEBUG_THROTTLE_FRAMES 甯ц緭鍑轰竴娆?RMS 鏁版嵁鍒?VOFA+ */
                if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
                {
                    VOFA_Send_Channel_RMS();  /* 鍙戦€佸悇閫氶亾 RMS 鍒颁覆鍙ｆ尝褰㈠伐鍏?*/
                }
#endif
#endif

                /* ---- 瀛愰樁娈?B锛欶FT 棰戝煙鍙樻崲 ---- */
                /* 瀵?Mic_Process_Buffer 涓殑鍚勯€氶亾鏃跺煙淇″彿鎵ц FFT锛?
                 * 缁撴灉鍐欏叆 Mic_Freq_Buffer锛堣鐩栦簡瑙ｄ氦缁囬樁娈电殑涓存椂鏁版嵁锛?*/
                t_sec = App_Perf_BeginCycles();
                AI_FFT_Process();  /* 鍐呴儴浣跨敤 CMSIS-DSP arm_cfft_q15锛屽姞绐?鍙樻崲 */
                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_FFT, t_sec);
                App_Spectrum_PublishFromFft(event.seq);

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 1)
                /* DEBUG 妯″紡 1锛氳緭鍑烘寚瀹氶€氶亾鐨?FFT 棰戣氨骞呭害 */
                if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
                {
                    VOFA_Send_FFT_Magnitude(DEBUG_SPECTRUM_CHANNEL);
                }
#endif
#endif

                /* ---- 瀛愰樁娈?C锛歋RP-PHAT 澹版簮瀹氫綅 ---- */
                /* 鍩轰簬棰戝煙浜掑姛鐜囪氨鐩镐綅鍙樻崲璁＄畻澹版簮鏂逛綅瑙掞紝
                 * 缁撴灉锛坸_angle, y_angle, energy锛夊啓鍏?current_pos */
                t_sec = App_Perf_BeginCycles();
                AI_SRP_PHAT_Process(&current_pos);  /* 鏍稿績绠楁硶锛屾渶鑰楁椂鐨勯儴鍒?*/
                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_SRP, t_sec);

#ifdef DEBUG_ENABLE
#if (DEBUG_MODE == 3)
                /* DEBUG 妯″紡 3锛氳緭鍑?SRP 瀹氫綅缁撴灉锛堣搴?鑳介噺锛夊埌 VOFA+ */
                if ((s_frame_cnt % DEBUG_THROTTLE_FRAMES) == 0u)
                {
                    VOFA_Send_SRP_Result(&current_pos);
                }
#endif
#endif

                App_Perf_EndCycles(APP_PERF_SEC_AUDIO_TOTAL, t_audio); /* 缁撴潫鏁翠綋璁℃椂 */

                last_algo_pos = current_pos;  /* 缂撳瓨鏈缁撴灉锛屼緵鎶藉抚鏃跺鐢?*/
                has_last_algo = 1u;           /* 鏍囪宸叉湁鏈夋晥绠楁硶缁撴灉 */
                App_Perf_CountAudioProc();    /* 閫掑绠楁硶澶勭悊甯ц鏁帮紙鎬ц兘閫熺巼缁熻鐢級 */
            }
            else
            {
                /* 鎶藉抚闃舵锛氳烦杩囩畻娉曪紝鐩存帴澶嶇敤涓婃缁撴灉 */
                /* 杩欐牱 UI 浠诲姟浠嶈兘鏀跺埌鏁版嵁锛堜笉浼氶タ姝伙級锛屽彧鏄綅缃洿鏂伴鐜囬檷浣?*/
                current_pos = last_algo_pos;
            }
        }

        /* ---- 闃舵 5锛氬皢瀹氫綅缁撴灉鎶曢€掑埌 UI 浠诲姟 ---- */
        /* xQueueOverwrite锛氳嫢闃熷垪宸叉弧锛圲I 浠诲姟杩樻湭娑堣垂锛夛紝鐩存帴瑕嗙洊鏃ф暟鎹紝
         * 淇濊瘉 UI 濮嬬粓鎷垮埌鏈€鏂颁綅缃紝涓嶅洜闃熷垪婊¤€岄樆濉為煶棰戜换鍔?*/
        xQueueOverwrite(xPositionQueue, &current_pos);
        taskYIELD();

        /* 涓诲姩鍑鸿 CPU锛岃鍚屼紭鍏堢骇鐨?UI 浠诲姟鏈夋満浼氱珛鍗宠繍琛屽鐞嗗垰鎶曢€掔殑鏁版嵁 */
    }
}

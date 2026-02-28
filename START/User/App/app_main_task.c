#include "ai_preprocess.h"
#include "ai_beamforming.h"
#include "app_data_output.h"
#include "app_data_stream.h"
#include "app_main_task.h"


extern int16_t found_val;  // 保留外部变量声明（若在其他文件定义）

// 任务句柄和 IPC 对象保留定义（头文件中是 extern 声明，此处需实际定义）
TaskHandle_t xAudioPreTaskHandle = NULL;
TaskHandle_t xAlgoTaskHandle     = NULL;
TaskHandle_t xUITaskHandle       = NULL;
SemaphoreHandle_t xAudioDataReadySem = NULL;
QueueHandle_t xPositionQueue = NULL;

void App_Task_Init(void)
{
    // 1. 创建通信组件
    xAudioDataReadySem = xSemaphoreCreateBinary();
    xPositionQueue = xQueueCreate(1, sizeof(Sound_Pos_t)); // 深度为1的坐标队列,这里改成1，才能保证 xQueueOverwrite 的正确使用

    // 2. 创建预处理任务 (极高优先级: 4)
    xTaskCreate(Audio_Preprocess_Task, 
                "Audio_Pre", 
                512,  // 512 words = 2KB
                NULL, 
                4,    
                &xAudioPreTaskHandle);

    // 3. 创建算法任务 (高优先级: 3)
    xTaskCreate(AI_Algorithm_Task, 
                "AI_Algo", 
                2048, // 重点：2048 words = 8KB，算法极其吃栈空间！
                NULL, 
                3,    
                &xAlgoTaskHandle);

    // 4. 创建UI显示任务 (中优先级: 2)
    xTaskCreate(UI_Display_Task, 
                "UI_Disp", 
                1024, 
                NULL, 
                2,    
                &xUITaskHandle);
}



// ==========================================================
// 任务 1: 音频预处理 (被 SAI DMA 中断里的 vTaskNotifyGiveFromISR 唤醒)
// ==========================================================
void Audio_Preprocess_Task(void *pvParameters)
{
    uint32_t ulNotifiedValue;
    q15_t *p_current_dma_src = NULL;
    q15_t *p_temp_planar = (q15_t *)Mic_Freq_Buffer; // 临时平面缓冲区，复用频域缓冲区的内存空间，节省 DTCM
    for(;;)
    {
        // 1. 等待通知，醒来后清空所有接收到的 Bits (0xFFFFFFFF)
        
        xTaskNotifyWait(0x00, 0xFFFFFFFF, &ulNotifiedValue, portMAX_DELAY);
        // 2. 致命错误检测：如果 Ping 和 Pong 同时置位，说明 CPU 跑飞或算力不足，漏帧了！
        if((ulNotifiedValue & AUDIO_FLAG_PING) && (ulNotifiedValue & AUDIO_FLAG_PONG)) {
            // 这里应该点亮一个红灯，或者记录 Error Count
            // printf("Error: Audio Overrun!\n");
            continue; // 丢弃这一帧，赶紧去接下一帧
        }

        // 3. 精准判断源地址
        if(ulNotifiedValue & AUDIO_FLAG_PING) {
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[0];
            found_val++; // 仅用于调试，记录预处理任务被唤醒的次数
        } 
        else if(ulNotifiedValue & AUDIO_FLAG_PONG) {
            p_current_dma_src = (q15_t *)&Mic_Rx_Buffer[MIC_CHANNELS * FRAME_LEN];
        }

        // 4. 执行矩阵转置解交织...
        Deinterleave_Using_Matrix(p_current_dma_src, 
                                  p_temp_planar, 
                                  Mic_Process_Buffer, 
                                  FRAME_LEN, 
                                  MIC_CHANNELS);
        
        // 5. 通知算法开工...
        xSemaphoreGive(xAudioDataReadySem);
    }
}

// ==========================================================
// 任务 2: 核心声源定位算法
// ==========================================================

/* -----------------------------------------------------------------------
 * 调试节流控制
 *
 * 音频帧率 = Fs / FRAME_LEN = 48000 / 256 ≈ 187.5 帧/秒
 * DEBUG_THROTTLE_FRAMES = 20 → 每 20 帧发一次 ≈ 每 107ms 刷新一次 VOFA+
 *
 * 切换调试模式（改完重新编译即可）:
 *   DEBUG_MODE 0 → VOFA_Send_Channel_RMS()          16路能量，快速定位死通道
 *   DEBUG_MODE 1 → VOFA_Send_FFT_Magnitude(ch)      单路频谱，查看频率分布
 *
 * 调试完毕后注释掉 #define DEBUG_ENABLE 即可零开销关闭全部输出。
 * ----------------------------------------------------------------------- */
#define DEBUG_ENABLE
#define DEBUG_THROTTLE_FRAMES   20u
/* DEBUG_MODE 说明:
 *   0 = AC-RMS 能量检测  (FFT前, 16路标准差)
 *   1 = 单路频谱查看     (FFT后, 128-bin幅度)
 *   2 = 原始TDM槽位诊断  (完全绕过解交织，判断问题来自软件还是PCMD3180)
 *   3 = SRP-PHAT 定位结果 (方位角+能量+粗搜功率图) */
#define DEBUG_MODE              3
#define DEBUG_SPECTRUM_CHANNEL  0u      /* 模式1时查看的通道号 [0..15] */

void AI_Algorithm_Task(void *pvParameters)
{
    static uint32_t s_frame_cnt = 0;
    Sound_Pos_t current_pos;
    for(;;)
    {
        // 1. 阻塞等待预处理任务发来的信号
        if(xSemaphoreTake(xAudioDataReadySem, portMAX_DELAY) == pdTRUE)
        {
            s_frame_cnt++;

#ifdef DEBUG_ENABLE
            /* 模式0: FFT 前发 AC-RMS */
#if (DEBUG_MODE == 0)
            if (s_frame_cnt % DEBUG_THROTTLE_FRAMES == 0)
            {
                VOFA_Send_Channel_RMS();
            }
#endif
            /* 模式2: 原始TDM槽位诊断，完全绕过解交织
             * 直接读 Non-Cacheable DMA 缓冲区，FFT 前后均可调用 */
#if (DEBUG_MODE == 2)
            if (s_frame_cnt % DEBUG_THROTTLE_FRAMES == 0)
            {
                VOFA_Send_Raw_TDM_Slot_RMS();
            }
#endif
#endif /* DEBUG_ENABLE */

            // 2. 16路 FFT: 去直流 -> 汉宁加窗 -> RFFT
            // 结果写入 Mic_Freq_Buffer (复数交织, 256 float32_t / ch)
            AI_FFT_Process();

#ifdef DEBUG_ENABLE
            /* 模式1: FFT 后发频谱 */
#if (DEBUG_MODE == 1)
            if (s_frame_cnt % DEBUG_THROTTLE_FRAMES == 0)
            {
                VOFA_Send_FFT_Magnitude(DEBUG_SPECTRUM_CHANNEL);
            }
#endif
#endif /* DEBUG_ENABLE */

            // 3. SRP-PHAT 声源定位
            // 输入: Mic_Freq_Buffer (16路复数频谱)
            // 输出: current_pos {x_angle, y_angle, energy}
            AI_SRP_PHAT_Process(&current_pos);

#ifdef DEBUG_ENABLE
            /* 模式3: SRP-PHAT 定位结果 + 粗搜功率图 */
#if (DEBUG_MODE == 3)
            if (s_frame_cnt % DEBUG_THROTTLE_FRAMES == 0)
            {
                VOFA_Send_SRP_Result(&current_pos);
            }
#endif
#endif /* DEBUG_ENABLE */

            // 4. 把结果扔进队列，甩手掌柜交给 UI 去画图
            xQueueOverwrite(xPositionQueue, &current_pos);
        }
    }
}

// ==========================================================
// 任务 3: UI 图像融合与显示
// ==========================================================
void UI_Display_Task(void *pvParameters)
{
    Sound_Pos_t draw_pos;
    for(;;)
    {
        // 1. 每 33ms 刷新一帧 (约 30 FPS)
        vTaskDelay(pdMS_TO_TICKS(33));

        // 2. 去队列里掏最新的坐标 (非阻塞，拿不到就用上一帧的旧坐标)
        if(xQueueReceive(xPositionQueue, &draw_pos, 0) == pdPASS)
        {
            // 3. 根据新坐标，更新热力图生成函数
        }

        // 4. 调用 DMA2D，把热力图与 OV5640 的摄像头画面混合，推流到 LCD
    }
}
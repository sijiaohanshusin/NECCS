#include "app_data_stream.h"
#include "mpu.h" // 必须包含定义了内存段宏的头文件
#include <math.h>

// ============================================================
// Area 1: DMA 缓冲区 -> SRAM1 (0x30000000)
// ============================================================
// 关键点: 
// 1. 配置为 Non-Cacheable (通过 MPU)。
// 2. 32字节对齐 (配合 AXI 总线突发传输效率)。
// 3. 这里的 section 名字必须与 Scatter File (.sct) 对应。

__SECTION_DMA_BUFFER __attribute__((aligned(32)))
int16_t Mic_Rx_Buffer[DMA_BUFFER_SIZE] = {0}; // 双缓冲区，初始值为0

// ============================================================
// Area 2: DSP 计算核心区 -> DTCM (0x20000000)
// ============================================================
// 关键点:
// 1. DTCM 与 CPU 同频 (400MHz+)，无等待，无 Cache 冲突。
// 2. 这里的变量访问速度是 SRAM1/SDRAM 的 2-3 倍。

//用于存放从 DMA 搬运并转为浮点后的数据
__SECTION_DTCM __attribute__((aligned(32)))
float32_t Mic_Process_Buffer[MIC_CHANNELS * FRAME_LEN] = {0.0f}; // 解交织后的单帧数据，初始值为0

// 用于存放 FFT 变换后的复数数据
__SECTION_DTCM __attribute__((aligned(32)))
float32_t Mic_Freq_Buffer[MIC_CHANNELS * FRAME_LEN] = {0.0f};
// FFT 实例结构体 (存放旋转因子表等)
// 放入 DTCM 可微弱提升索引加载速度
__SECTION_DTCM
arm_rfft_fast_instance_f32 S_Rfft;

// 预计算汉宁窗 (Hanning Window)，在 App_Stream_Init 中填充，之后只读
__SECTION_DTCM __attribute__((aligned(32)))
float32_t Hanning_Window[FRAME_LEN] = {0.0f};

// ============================================================
// Area 3: 初始化函数 (供 main 调用)
// ============================================================
void App_Stream_Init(void)
{
    // 1. 初始化 FFT 结构体 (256点)
    // 这一步非常重要，否则 FFT 计算会进入 HardFault
    arm_rfft_fast_init_f32(&S_Rfft, FRAME_LEN);

    // 2. 预计算汉宁窗系数 (周期型，适合频谱分析)
    // w[n] = 0.5 * (1 - cos(2*pi*n/N))，N = FRAME_LEN
    // 只在启动时计算一次，后续 FFT 任务直接乘用
    for (int n = 0; n < FRAME_LEN; n++)
    {
        Hanning_Window[n] = 0.5f * (1.0f - cosf(2.0f * 3.14159265358979f * (float32_t)n / (float32_t)FRAME_LEN));
    }
}
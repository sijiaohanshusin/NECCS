#include "app_data_stream.h"
#include "mpu.h" // 必须包含定义了内存段宏的头文件

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

// ============================================================
// Area 3: 初始化函数 (供 main 调用)
// ============================================================
void App_Stream_Init(void)
{
    // 初始化 FFT 结构体 (256点)
    // 这一步非常重要，否则 FFT 计算会进入 HardFault
    arm_rfft_fast_init_f32(&S_Rfft, FRAME_LEN);
    
}
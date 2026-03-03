# NECCS 声学相机项目

## 项目概述

基于 STM32H743 的实时声源定位系统（声学相机），采用 16 路 PDM 麦克风阵列和 SRP-PHAT 算法实现声源方位角检测。

### 核心功能
- **实时声源定位**：30 FPS 声源方位角输出
- **高精度算法**：SRP-PHAT 波束成形，±5° 精度
- **可视化显示**：800×480 LCD 热力图实时显示
- **低延迟处理**：端到端延迟 < 50ms

### 硬件平台
- **MCU**：STM32H743IIT6 @ 480MHz
- **音频前端**：2×PCMD3180 (16路 PDM → TDM16)
- **显示**：800×480 RGB LCD (LTDC)
- **内存**：32MB SDRAM + 128KB DTCM + 512KB AXI SRAM
- **接口**：SAI (TDM16, 48kHz), I2C, UART

---

## 系统架构

### 数据流水线

```
PDM 麦克风 (16路)
    ↓
PCMD3180 × 2 (PDM → TDM16)
    ↓
SAI DMA (48kHz, 16-bit)
    ↓
解交织 + 类型转换 (0.3ms)
    ↓
FFT 频域变换 (0.8ms)
    ↓
SRP-PHAT 声源定位 (4ms)
    ↓
UI 显示 (30 FPS)
```

### 任务调度 (FreeRTOS)

| 任务名称 | 优先级 | 堆栈 | 周期 | 功能 |
|---------|--------|------|------|------|
| Audio_Pipeline_Task | 4 | 2304 | 5.33ms | 音频处理流水线 |
| UI_Display_Task | 4 | 2048 | 33ms | UI 渲染和显示 |
| PCMD3180InitTask | 6 | 512 | 一次性 | 硬件初始化 |
| StartDefaultTask | 3 | 1024 | 1s | 空闲任务 |

### 内存布局

| 区域 | 地址 | 大小 | 特性 | 用途 |
|------|------|------|------|------|
| DTCM | 0x20000000 | 128KB | 零等待 | FFT 缓冲、算法变量 |
| SRAM1 | 0x30000000 | 256KB | Non-Cacheable | DMA 缓冲区 |
| AXI SRAM | 0x24000000 | 512KB | Cacheable | GCC-PHAT、SRP 功率 |
| SDRAM | 0xC0000000 | 32MB | 混合 | 帧缓冲、通用数据 |

---

## 算法原理

### SRP-PHAT 声源定位

**算法流程**：
1. **GCC-PHAT**：计算 40 对麦克风的广义互相关
   - 互相关：`R_ij(f) = X_i(f) * conj(X_j(f))`
   - PHAT 白化：`R_ij_PHAT(f) = R_ij(f) / |R_ij(f)|`
   - 作用：抑制混响，增强直达声

2. **粗搜**：9×9 网格 (±60°, 步长 15°)
   - 使用预计算 TDOA 查找表
   - 快速定位声源大致方向
   - 耗时：约 2.5ms

3. **精搜**：Top-3 峰值周围 4×4 网格 (±10°, 步长 5°)
   - 实时计算 TDOA
   - 精确定位声源方向
   - 耗时：约 1.5ms

4. **质量评估**：
   - 对比度：`(最大值 - 次大值) / 最大值`
   - 质量：`(最大值 - 远处次大值) / 最大值`
   - 低置信度处理策略

**性能指标**：
- 总扫描点：81 (粗搜) + 48 (精搜) = 129 点
- 总耗时：约 4ms @ 480MHz
- 角度精度：±5°
- 覆盖范围：±60° (120° 视场角)

---

## 关键文件说明

### 算法模块 (`START/User/Algorithm/`)

| 文件 | 功能 | 关键函数 |
|------|------|----------|
| `ai_config.h` | 全局配置参数 | 宏定义 |
| `ai_preprocess.c/h` | 音频预处理 | `Deinterleave_Using_Matrix()` |
| `ai_beamforming.c/h` | SRP-PHAT 算法 | `AI_FFT_Process()`, `AI_SRP_PHAT_Process()` |
| `ai_srp_lut.c/h` | 预计算查找表 | 数据表 |

### 应用模块 (`START/User/App/`)

| 文件 | 功能 | 关键函数 |
|------|------|----------|
| `app_main_task.c/h` | FreeRTOS 任务 | `Audio_Pipeline_Task()`, `UI_Display_Task()` |
| `app_data_stream.c/h` | 缓冲区定义 | `App_Stream_Init()` |
| `app_display.c/h` | UI 显示 | `App_Display_Render()` |
| `app_data_output.c/h` | 调试输出 | `VOFA_Send_*()` |

### 硬件驱动 (`START/User/Hardware/`, `START/User/BSP/`)

| 文件 | 功能 | 关键函数 |
|------|------|----------|
| `pcmd3180.c/h` | PCMD3180 驱动 | `PCMD3180_Init_Device()` |
| `soft_i2c.c/h` | 软件 I2C | `PCMD_WriteReg()`, `PCMD_ReadReg()` |
| `sdram.c/h` | SDRAM 驱动 | `sdram_init()` |
| `lcd.c/h` | LCD 驱动 | `LCD_Init()` |
| `ltdc.c/h` | LTDC 驱动 | `LTDC_Init()` |

### 核心文件 (`START/Core/`)

| 文件 | 功能 | 关键函数 |
|------|------|----------|
| `main.c` | 主程序入口 | `main()` |
| `freertos.c` | FreeRTOS 初始化 | `MX_FREERTOS_Init()` |
| `mpu.c/h` | MPU 配置 | `App_MPU_Config()` |
| `sai.c` | SAI 配置 | `MX_SAI1_Init()` |

---

## 编译和烧录

### 开发环境
- **IDE**：Keil MDK-ARM V5
- **编译器**：ARM Compiler 5 (AC5)
- **调试器**：ST-Link V2/V3
- **串口工具**：VOFA+ (调试输出)

### 编译步骤
1. 打开 Keil 工程文件 `START/MDK-ARM/START.uvprojx`
2. 选择目标配置 (Debug/Release)
3. 编译项目 (F7)
4. 烧录到目标板 (F8)

### 配置选项

**调试模式** (`app_main_task.c`):
```c
#define DEBUG_ENABLE        // 启用调试输出
#define DEBUG_MODE 3        // 0=RMS, 1=FFT, 3=SRP
```

**SRP-PHAT 参数** (`ai_config.h`):
```c
#define SRP_FREQ_BIN_START  3u      // 起始频率 bin
#define SRP_FREQ_BIN_END    42u     // 结束频率 bin
#define SRP_CONTRAST_MIN_RATIO  0.03f  // 质量门限
```

---

## 性能分析

### 时间预算 (单帧 5.33ms)

| 阶段 | 耗时 | 占比 |
|------|------|------|
| 解交织 + 类型转换 | 0.3ms | 5.6% |
| FFT (16ch × 256点) | 0.8ms | 15.0% |
| GCC-PHAT (40对) | 1.5ms | 28.1% |
| SRP 粗搜 (81点) | 2.5ms | 46.9% |
| SRP 精搜 (48点) | 1.5ms | 28.1% |
| **总计** | **~5.1ms** | **95.7%** |

### 内存使用

| 类型 | 大小 | 位置 |
|------|------|------|
| DMA 缓冲区 | 16KB | SRAM1 |
| 时域缓冲区 | 16KB | DTCM |
| 频域缓冲区 | 16KB | DTCM |
| GCC-PHAT 缓冲 | 12.8KB | AXI SRAM |
| SRP 功率网格 | 0.5KB | AXI SRAM |
| 帧缓冲区 | 768KB | SDRAM |
| **总计** | **~62KB + 768KB** | - |

---

## 调试和测试

### 串口调试 (VOFA+)

**配置**：
- 波特率：115200
- 数据位：8
- 停止位：1
- 校验位：无

**输出格式** (DEBUG_MODE=3):
```
[θh, θv, energy, 粗搜功率图(49点)]
```

### 常见问题

**1. SDRAM 测试失败**
- 检查 FMC 引脚连接
- 检查 SDRAM 时钟频率 (120MHz)
- 检查电源电压 (3.3V)

**2. 音频无输出**
- 检查 SAI 时钟配置 (48kHz)
- 检查 PCMD3180 I2C 地址 (0x4C, 0x4D)
- 检查 PDM 麦克风连接

**3. LCD 无显示**
- 检查 LTDC 时钟配置
- 检查 RGB 引脚连接
- 检查背光使能

**4. 定位精度低**
- 调整 SRP_CONTRAST_MIN_RATIO
- 检查麦克风阵列几何参数
- 检查声速配置 (343 m/s)

---

## 扩展和优化

### 可能的改进方向

1. **算法优化**
   - 使用 MUSIC 或 ESPRIT 算法提升精度
   - 实现多声源定位
   - 添加卡尔曼滤波平滑输出

2. **性能优化**
   - 使用 NEON 指令加速 FFT
   - 优化 SRP 扫描策略
   - 使用 DMA2D 加速 UI 渲染

3. **功能扩展**
   - 添加声源跟踪
   - 实现声纹识别
   - 支持录音和回放

4. **硬件升级**
   - 增加麦克风数量 (32/64路)
   - 使用更高采样率 (96kHz)
   - 添加以太网接口

---

## 参考资料

### 相关文档
- STM32H743 数据手册
- PCMD3180 数据手册
- W9825G6KH SDRAM 数据手册
- FreeRTOS 官方文档
- CMSIS-DSP 库文档

### 算法论文
- DiBiase, J. H. (2000). "A High-Accuracy, Low-Latency Technique for Talker Localization in Reverberant Environments Using Microphone Arrays"
- Knapp, C., & Carter, G. (1976). "The generalized correlation method for estimation of time delay"

### 开源项目
- ODAS (Open embeddeD Audition System)
- BeamformIt
- pyroomacoustics

---

## 许可证

本项目仅供学习和研究使用。

## 联系方式

- 作者：四角函数sin
- 项目：NECCS 声学相机
- 日期：2026-03-03

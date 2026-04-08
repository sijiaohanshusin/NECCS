# NECCS 声学相机 — 开发者指南

> **文档版本**: 1.0 | **最后更新**: 2026-04-08 | **适用固件**: START 工程

---

## 1. 项目概述

NECCS（Noise & Echo Capture Camera System）是一套基于 **STM32H743IIT6** Cortex-M7 微控制器的 **16 阵元实时声学相机**系统。系统通过 16 路 PDM MEMS 麦克风阵列采集声场数据，使用 **SRP-PHAT**（可控响应功率-相位变换）算法实时定位声源方向，并将定位结果以热力图形式叠加到摄像头画面上，实现"看见声音"。

### 关键技术指标

| 指标 | 数值 |
|------|------|
| 麦克风通道数 | 16（`MIC_CHANNELS = 16`） |
| 采样率 | 48 kHz（`SAMPLING_RATE = 48000`） |
| 单帧采样点数 | 256 点（`FRAME_LEN = 256`） |
| 帧周期 | 5.33 ms（256 / 48000） |
| FFT 频率分辨率 | 187.5 Hz（48000 / 256） |
| SRP 频率范围 | bin 3 ~ bin 42（约 562 ~ 7875 Hz） |
| 麦克风对数量 | 40（`SRP_PAIR_COUNT = 40`） |
| 粗搜索网格 | 9 × 9 = 81 点，角度范围 ±60° |
| 细搜索 | Top-3 峰值各 4 × 4，共 48 点 |
| SRP 扫描总点数 | 129（81 + 48） |
| UI 默认帧率 | 20 FPS（范围 5 ~ 30 FPS） |
| 核心算法 | SRP-PHAT + GCC-PHAT 白化互相关 |

---

## 2. 开发环境

### 2.1 工具链

| 工具 | 版本/说明 |
|------|-----------|
| **IDE** | Keil MDK-ARM V5 |
| **编译器** | ARM Compiler 5（AC5），ToolsetNumber = 0x4 |
| **优化等级** | `-O2`（全局 Optim=4），个别模块 `-O0` 以方便调试 |
| **目标 CPU** | Cortex-M7，带 DP-FPU（`FPU3(DFPU)`），Little-Endian |
| **链接器** | ARM Scatter-loading，脚本 `MDK/START.sct` |
| **CMSIS** | CMSIS-CORE v5.5.0 + CMSIS-DSP（arm_math.h） |
| **RTOS** | FreeRTOS v10.3.1，CMSIS-RTOS V2 封装，Heap4 |

### 2.2 调试器

- **ST-Link V2/V3** 或 **J-Link**（工程默认配置为 J-Link，参见 `MDK-ARM/JLinkSettings.ini`）
- SVD 文件：`STM32H743.svd`（外设寄存器直接察看）

### 2.3 辅助工具

| 工具 | 用途 |
|------|------|
| **Python 3.x** | LUT 生成（`tools/generate_srp_lut.py`）、验证（`tools/srp_doa_sanity_check.py`） |
| **Node.js** | 阵列可视化（`tools/plot_array_32ch.js`） |
| **STM32CubeMX** | 外设初始化代码生成（`START.ioc`） |

### 2.4 工程文件

- Keil 工程入口：`START/MDK-ARM/START.uvprojx`
- CubeMX 配置：`START/START.ioc`
- 链接器脚本：`START/MDK/START.sct`

---

## 3. 硬件概要

### 3.1 MCU

| 参数 | 值 |
|------|-----|
| 型号 | STM32H743IIT6 |
| 内核 | ARM Cortex-M7 @ 480 MHz |
| Flash | 2 MB（0x08000000，2048 KB） |
| DTCM | 128 KB（0x20000000 ~ 0x2001FFFF） |
| AXI SRAM | 512 KB（0x24000000 ~ 0x2407FFFF） |
| D2 SRAM | 256 KB（0x30000000 ~ 0x3003FFFF） |
| 外部 HSE | 25 MHz 晶振 |

### 3.2 音频前端

```
16x PDM MEMS Mic
      │
      ▼
PCMD3180 × 2 (TI 8ch PDM→TDM 转换器)
      │
      ▼ TDM16 (16 slot × 16-bit)
SAI1 Block A (主接收模式, 48kHz)
      │
      ▼ DMA1 Stream0 (循环模式, Very High 优先级)
D2 SRAM 双缓冲 (PING/PONG)
```

**SAI GPIO 引脚**：
- `PE4` — SAI1_FS_A（帧同步）
- `PE5` — SAI1_SCK_A（位时钟；与 LCD_G0 共用，需降低翻转速率）
- `PC1` — SAI1_SD_A（数据线）

**SAI 时钟链路**：HSE 25 MHz → PLL2 (M=5, N=108, P=44) → ≈12.27 MHz → SAI 内部分频 → BCLK ≈ 768 kHz

### 3.3 显示

| 参数 | 值 |
|------|-----|
| 类型 | 800 × 480 LCD（RGB 并口） |
| 接口 | LTDC + DMA2D 硬件加速 |
| 触摸 | 电容触摸（I2C），`APP_TOUCH_ENABLE = 1` |
| 帧缓冲 | SDRAM 双缓冲（MPU Region 2, 0xC0000000, 4 MB） |

### 3.4 摄像头

| 参数 | 值 |
|------|-----|
| 型号 | OV2640 |
| 接口 | DCMI（DMA） |
| 预览分辨率 | 320 × 240（`APP_CAMERA_PREVIEW_W/H`） |
| 帧缓冲 | SDRAM（MPU Region 3, 0xC0400000, 1 MB） |

### 3.5 存储

| 模块 | 地址 / 接口 | 大小 |
|------|-------------|------|
| SDRAM | FMC, 0xC0000000 | 32 MB |
| NOR Flash | QSPI | — |

---

## 4. 软件架构

### 4.1 目录结构

```
START/
├── Core/                          # CubeMX 生成的底层初始化
│   ├── Inc/                       #   main.h, FreeRTOSConfig.h, sai.h, dma.h ...
│   └── Src/                       #   main.c, sai.c, dma.c, freertos.c, mpu.c ...
├── Drivers/
│   ├── CMSIS/                     # ARM CMSIS-CORE + CMSIS-DSP
│   └── STM32H7xx_HAL_Driver/     # ST HAL 库
├── MDK/
│   └── START.sct                  # 链接器散列文件
├── MDK-ARM/
│   └── START.uvprojx             # Keil 工程文件
├── Middlewares/
│   ├── LVGL/                      # LVGL 图形库
│   └── Third_Party/FreeRTOS/     # FreeRTOS 内核
└── User/                          # ★ 应用层代码
    ├── Algorithm/                 # 声源定位算法
    │   ├── ai_beamforming.c/h     #   FFT + SRP-PHAT 核心算法
    │   ├── ai_preprocess.c/h      #   解交织 + 类型转换
    │   ├── ai_srp_lut.c/h         #   SRP 延迟查找表（离线生成）
    │   └── ai_config.h            #   算法派生常量
    ├── App/                       # 应用框架
    │   ├── app_user_config.h      #   ★ 统一可配置项入口
    │   ├── app_main_task.c/h      #   FreeRTOS 任务创建与入口
    │   ├── app_audio_task.c       #   音频流水线任务实现
    │   ├── app_ui_task.c          #   UI 显示任务实现
    │   ├── app_ui_cli.c/h         #   UART CLI 命令解释器
    │   ├── app_display.c/h        #   热力图渲染引擎
    │   ├── app_camera.c/h         #   OV2640 摄像头管理
    │   ├── app_runtime.c/h        #   运行时配置管理（线程安全）
    │   ├── app_perf.c/h           #   CPU 周期性能剖析
    │   ├── app_lvgl_ui.c/h        #   LVGL UI 后端
    │   └── app_types.h            #   共享数据类型定义
    ├── BSP/                       # 板级支持包
    │   ├── LCD/                   #   LTDC + DMA2D 驱动
    │   ├── TOUCH/                 #   电容触摸驱动
    │   └── sdram.c/h              #   SDRAM (FMC) 初始化
    └── Hardware/                  # 外部芯片驱动
        ├── pcmd3180.c/h           #   PCMD3180 PDM→TDM 配置
        ├── camera_ov2640.c/h      #   OV2640 寄存器配置
        └── soft_i2c.c/h           #   软件 I2C 驱动
```

### 4.2 模块依赖关系

```
                    ┌───────────────┐
                    │  app_main_task│ (任务创建/调度)
                    └──────┬────────┘
               ┌───────────┼───────────┐
               ▼                       ▼
    ┌──────────────────┐    ┌──────────────────┐
    │ Audio_Pipeline    │    │ UI_Display        │
    │  (app_audio_task) │    │  (app_ui_task)    │
    └────────┬─────────┘    └────────┬─────────┘
             │                       │
    ┌────────▼─────────┐    ┌────────▼─────────┐
    │ Algorithm/        │    │ app_display       │
    │  ai_preprocess    │    │ app_lvgl_ui       │
    │  ai_beamforming   │    │ app_camera        │
    │  ai_srp_lut       │    │ app_ui_cli        │
    └────────┬─────────┘    └────────┬─────────┘
             │                       │
    ┌────────▼─────────┐    ┌────────▼─────────┐
    │ CMSIS-DSP         │    │ BSP/LCD           │
    │ (arm_math.h)      │    │ BSP/TOUCH         │
    └──────────────────┘    │ BSP/sdram          │
                            └──────────────────┘
```

### 4.3 FreeRTOS 任务架构

| 任务名称 | 入口函数 | 优先级 | 栈大小 (words) | 栈大小 (bytes) | 职责 |
|----------|----------|--------|---------------|----------------|------|
| Audio Pipeline | `Audio_Pipeline_Task()` | 4 | 2304 | 9216 | SAI DMA 事件等待 → 解交织 → FFT → SRP-PHAT → 发送定位结果 |
| UI Display | `UI_Display_Task()` | 4 | 2048 | 8192 | 接收定位结果 → 热力图渲染 → LCD 刷新 → CLI 轮询 |
| Timer Service | — | 2 | 256 | 1024 | FreeRTOS 软件定时器服务（系统内置） |
| Idle | — | 0 | 128 | 512 | 空闲任务（系统内置） |

**FreeRTOS 配置要点**（`FreeRTOSConfig.h`）：

| 参数 | 值 | 说明 |
|------|-----|------|
| `configTICK_RATE_HZ` | 1000 | 1 ms Tick |
| `configMAX_PRIORITIES` | 56 | 优先级级数 |
| `configTOTAL_HEAP_SIZE` | 65536 (64 KB) | Heap4 堆空间 |
| `configUSE_PREEMPTION` | 1 | 抢占式调度 |
| `configUSE_TIMERS` | 1 | 软件定时器使能 |
| Heap 实现 | Heap4 | 支持合并的 best-fit |

**队列通信**：

| 队列 | 长度 | 写入方式 | 生产者 | 消费者 |
|------|------|----------|--------|--------|
| `xAudioFrameQueue` | 1 | `xQueueOverwrite`（ISR） | SAI DMA 半完成/完成中断 | Audio Pipeline Task |
| `xPositionQueue` | 1 | `xQueueOverwrite` | Audio Pipeline Task | UI Display Task |

> 队列长度均为 1，采用覆盖写策略——只保留最新数据，丢弃旧帧，保证实时性。

---

## 5. 内存布局

### 5.1 MPU 区域配置

| Region | 地址 | 大小 | 用途 | 缓存策略 | 共享 |
|--------|------|------|------|----------|------|
| 0 | `0x30000000` | 256 KB | D2 SRAM（SAI DMA 缓冲） | Non-Cacheable | Shareable |
| 1 | `0xC0000000` | 32 MB | SDRAM 全局（算法/纹理/字库） | WB, No Write-Allocate | Not Shareable |
| 2 | `0xC0000000` | 4 MB | LTDC 帧缓冲（覆盖 Region 1） | Non-Cacheable | Shareable |
| 3 | `0xC0400000` | 1 MB | DCMI 摄像头采集窗口（覆盖 Region 1） | Non-Cacheable | Shareable |

> Region 编号越大优先级越高。Region 2/3 的 Non-Cacheable 属性覆盖了 Region 1 对应子区间，确保 DMA 外设与 CPU 看到一致的数据。

### 5.2 SRAM 区域用途

| 区域 | 地址范围 | 大小 | 链接器段 | 主要用途 |
|------|----------|------|----------|----------|
| **DTCM** | `0x20000000` ~ `0x2001FFFF` | 128 KB | `.dtcm_data` + STACK/HEAP | FFT 缓冲、SRP 中间结果、堆栈——零等待周期 CPU 访问 |
| **AXI SRAM** | `0x24000000` ~ `0x2407FFFF` | 512 KB | `.axi_sram_data` + 默认 RW/ZI | GCC-PHAT 缓冲、全局变量、FreeRTOS 堆 |
| **D2 SRAM** | `0x30000000` ~ `0x3003FFFF` | 256 KB | `.dma_buffer`, `.d2_sram_data` | SAI DMA 双缓冲区（Non-Cacheable，DMA 直接访问） |
| **SDRAM** | `0xC0000000` ~ `0xC1FFFFFF` | 32 MB | 固定窗口（非链接器管理） | 见下表 |

**SDRAM 固定分配**（`START.sct` 注释）：

| 地址范围 | 大小 | 用途 |
|----------|------|------|
| `0xC0000000` ~ `0xC06FFFFF` | 7 MB | LTDC / 摄像头 / 显示固定缓冲 |
| `0xC0700000` ~ `0xC07BFFFF` | 768 KB | LVGL 全屏面板帧缓冲 |
| `0xC07C0000` ~ `0xC07CBFFF` | 48 KB | LVGL 堆池 |

### 5.3 链接器散列文件 (`START.sct`)

```
LR_IROM1 0x08000000 0x00200000 {       ; 2 MB Flash
  ER_IROM1  0x08000000 0x00200000 {    ;   代码 + 只读数据
    *.o (RESET, +First)
    *(InRoot$$Sections)
    .ANY (+RO +XO)
  }
  RW_IRAM_DTCM 0x20000000 0x00020000 { ;   128 KB DTCM
    startup_stm32h743xx.o (STACK, HEAP)
    *.o (.dtcm_data)
  }
  RW_IRAM_AXI 0x24000000 0x00080000 {  ;   512 KB AXI SRAM
    *.o (.axi_sram_data)
    .ANY (+RW +ZI)
  }
  RW_SRAM_D2 0x30000000 0x00040000 {   ;   256 KB D2 SRAM
    *.o (.dma_buffer)
    *.o (.d2_sram_data)
  }
}
```

> **如何将变量放到指定区域**：在 C 源文件中使用 `__attribute__((section(".dtcm_data")))` 或 `__attribute__((section(".dma_buffer")))` 修饰。

---

## 6. 音频处理管线

### 6.1 数据流

```
SAI1 DMA (D2 SRAM 循环双缓冲)
  │  HalfCplt / Cplt 中断
  ▼
xAudioFrameQueue (覆盖写, 长度 1)
  │  xQueueReceive (阻塞)
  ▼
Audio_Pipeline_Task
  ├─ 1. 解交织 (Deinterleave_Using_Matrix)
  │     int16 交织 → int16 平面 → float32 平面
  │     使用 arm_mat_trans_q15 + arm_q15_to_float
  │     耗时 ≈ 0.3 ms @ 480 MHz
  │
  ├─ 2. FFT (AI_FFT_Process)
  │     去直流 → 汉宁窗 → arm_rfft_fast_f32
  │     16 通道 × 256 点
  │     耗时 ≈ 0.8 ms @ 480 MHz
  │
  ├─ 3. SRP-PHAT (AI_SRP_PHAT_Process)
  │     GCC-PHAT 白化互相关 (40 麦克风对)
  │     粗搜索 9×9 + 细搜索 3×(4×4)
  │     Top-K NMS + 置信度判定
  │
  └─ 4. 发送结果
        xQueueOverwrite → xPositionQueue
                            │
                            ▼
                     UI_Display_Task
```

### 6.2 关键数据结构

| 结构体 / 类型 | 定义位置 | 描述 |
|---------------|----------|------|
| `Audio_FrameEvent_t` | `app_types.h` | DMA 半缓冲事件（`half_id`: PING/PONG） |
| `Sound_Pos_t` | `app_types.h` | 定位结果（方位角 X/Y、能量、质量） |
| `SRP_VisFrame_t` | `ai_beamforming.h` | SRP 功率场可视化数据（粗/细网格能量值） |

### 6.3 DMA 双缓冲参数

| 参数 | 值 |
|------|-----|
| 总缓冲大小 | `DMA_BUFFER_SIZE = MIC_CHANNELS × FRAME_LEN × 2 = 16 × 256 × 2 = 8192` 样本 (int16) |
| 半缓冲大小 | 4096 样本 = 8192 字节 |
| 交织格式 | ch0_s0, ch1_s0, ..., ch15_s0, ch0_s1, ... |

### 6.4 SRP-PHAT 算法参数

| 参数 | 值 | 宏定义 |
|------|-----|--------|
| 声速 | 343.0 m/s | `SPEED_OF_SOUND` |
| PHAT 白化 epsilon | 1.0e-10 | `PHAT_EPSILON` |
| SRP 频率 bin 范围 | 3 ~ 42（共 40 bin） | `SRP_FREQ_BIN_START/END` |
| 低置信度策略 | 直接上报新结果 | `SRP_LOWCONF_POLICY = 0` |
| 对比度门限 | 0.03 | `SRP_CONTRAST_MIN_RATIO` |
| 有效最小能量 | 0.05 | `SRP_VALID_MIN_ENERGY` |

---

## 7. 显示渲染流程

### 7.1 渲染后端

系统支持两种渲染后端，在运行时可通过 CLI 切换：

| 后端 | 枚举值 | 说明 |
|------|--------|------|
| **Legacy** | `APP_UI_RENDER_BACKEND_LEGACY` | 直接操作帧缓冲的传统渲染路径 |
| **LVGL** | `APP_UI_RENDER_BACKEND_LVGL` | 基于 LVGL 图形库的 UI 后端 |

默认启动后端由 `APP_LVGL_BOOT_AS_DEFAULT`（当前 = 1，即默认 LVGL）决定。

### 7.2 显示模式

三种显示模式控制热力图画质与速度的权衡（`App_Display_Mode_t`）：

| 模式 | 字符串 | 特点 |
|------|--------|------|
| **FAST** | `fast` | 优先帧率，最少平滑/融合 |
| **BALANCED** | `balanced` | 默认，帧率与画质折中 |
| **CLEAN** | `clean` | 优先画质，更平滑，计算量更高 |

### 7.3 显示参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 摄像头显示区 | 640 × 480 px | `APP_DISPLAY_CAMERA_VIEW_W/H` |
| 热力图叠加区 | 480 × 480 px | `APP_DISPLAY_HEAT_VIEW_W/H` |
| 右侧 UI 面板 | 160 px 宽 | `APP_DISPLAY_UI_PANEL_W` |
| 热力场分辨率 | 96 × 96（默认） | `APP_DISPLAY_FIELD_W/H`（可选 72×72 或 56×56） |
| 热力图 LUT | 256 色 | `APP_DISPLAY_HEAT_LUT_SIZE` |
| 高斯平滑 | 半径 2, σ=1.05, 1 pass | `APP_DISPLAY_SMOOTH_*` |
| 插值 | 最近邻 / 双线性 | 运行时可切换 |
| DMA2D 超时 | 0x1FFFFF 次轮询 | `APP_DISPLAY_DMA2D_TIMEOUT` |

### 7.4 DMA2D 加速

- LTDC 帧缓冲位于 SDRAM（MPU Non-Cacheable），LTDC 硬件直接读取
- DMA2D 用于大面积色块填充和像素格式转换，减少 CPU 负担
- 每次 blit 最大行数由 `APP_DISPLAY_BLIT_ROWS_MAX`（默认 8）控制

### 7.5 帧缓冲管理

- LTDC 使用 SDRAM 起始 4 MB 区域（MPU Region 2, Non-Cacheable）
- 摄像头 DMA 目标位于 `0xC0400000`（MPU Region 3, Non-Cacheable，1 MB）
- LVGL 使用 `0xC0700000` 起始的 768 KB 作为面板帧缓冲，`0xC07C0000` 起始 48 KB 作为堆池

---

## 8. UART CLI 命令参考

CLI 通过 UART1 提供，上电自动打印波特率和使用提示。所有命令**不区分大小写**。

### 基本命令

| 命令 | 说明 |
|------|------|
| `help` | 打印帮助信息 |
| `cfg help` | 同上 |
| `cfg status` | 打印当前所有运行时配置状态 |

### 渲染配置命令（`cfg <key> [value]`）

| Key | 参数 | 说明 |
|-----|------|------|
| `backend` | `legacy` \| `lvgl` | 切换 UI 渲染后端（`old` = `legacy`） |
| `mode` | `fast` \| `balanced` \| `clean` | 切换显示模式（`bal` = `balanced`） |
| `interp` | `nearest` \| `bilinear` | 热力图插值方式（`near`/`bil` 缩写可用） |
| `norm` | `fast` \| `full` | 归一化策略 |
| `uifps` | `<5..30>` | 设置 UI 目标帧率 |
| `algodecim` | `<1..8>` | 算法抽帧比（1 = 每帧算, 8 = 8 帧算一次） |
| `contrast` | `<-6..-80>` | 动态范围底限 dB（正数自动取负） |
| `gamma` | `<0.5..2.5>` | Gamma 校正（<1 压缩高亮, >1 提升对比度） |
| `noise` | `<0..0.6>` | 噪声门限比（低于此比例能量不显示） |
| `adapt` | `<0..6>` | 自适应噪声估计增益 |
| `smooth` | `<0..3>` | 空间平滑迭代次数（0=最尖锐） |
| `fine` | `<0..3>` | 精细网格叠加增益（0=不叠加） |
| `bilinear` | `<0\|1>` | 快捷开关双线性插值 |
| `textdiv` | `<1..20>` | 文字 OSD 刷新分频（N 帧刷一次） |
| `blit` | `<1..8>` | DMA2D 每次传输最大行数 |

### 性能与诊断命令

| Key | 参数 | 说明 |
|-----|------|------|
| `perf` | `on` \| `off` | 开启/关闭 CPU 周期性能统计 |
| `perf` | `dump` | 打印所有 perf 区间统计结果 |
| `perf` | `reset` | 清零所有统计数据 |

### 外设控制命令

| Key | 参数 | 说明 |
|-----|------|------|
| `uart` | `recover` | UART 异常恢复 |
| `cam` | `retry` | 重试摄像头初始化 |
| `camview` | `overlay` \| `camera` \| `heat` \| `freeze` | 切换摄像头显示模式 |
| `camfreeze` | `on` \| `off` | 冻结/解冻摄像头画面 |

### 使用示例

```
cfg mode fast          # 切换到快速模式
cfg uifps 15           # 设置目标帧率 15 FPS
cfg perf on            # 开启性能统计
cfg perf dump          # 查看各处理阶段耗时
cfg backend lvgl       # 切换到 LVGL 渲染后端
cfg contrast 30        # 设置动态范围底限 -30 dB
cfg smooth 2           # 两次高斯平滑
cfg camview overlay    # 热力图叠加到摄像头画面
```

---

## 9. 构建与烧录

### 9.1 打开工程

1. 安装 **Keil MDK-ARM V5**（需有效 License，支持 STM32H7 系列）
2. 双击 `START/MDK-ARM/START.uvprojx` 打开工程
3. 确认 Target Device 为 `STM32H743IITx`

### 9.2 编译

1. 菜单 **Project → Build Target**（或 `F7`）
2. 预期无 Error；若有部分 Warning 属于 HAL 库自身，可忽略
3. 编译产物位于 `START/MDK-ARM/START/` 目录

### 9.3 烧录

1. 连接 **J-Link** 或 **ST-Link** 到开发板 SWD 接口
2. 菜单 **Flash → Download**（或 `F8`）
3. 等待 "Programming done. Verify OK." 提示

### 9.4 调试

1. 菜单 **Debug → Start/Stop Debug Session**（`Ctrl+F5`）
2. 调试时可利用 SVD 文件直接查看 SAI、LTDC、DMA 等外设寄存器
3. 建议在 `DebugConfig/` 下的 `.dbgconf` 文件中配置初始断点

### 9.5 注意事项

- 首次烧录新板时，确认 BOOT0 拉低（从 Flash 启动）
- 若 SDRAM 初始化失败，检查 FMC 时钟和引脚配置
- PE5 与 LCD 共用引脚，SAI 时钟翻转速率过高可能导致 LCD 花屏

---

## 10. 调试技巧

### 10.1 Perf 性能统计

系统内置基于 DWT CYCCNT 的 CPU 周期性能剖析，涵盖以下区间：

| 枚举 | 含义 |
|------|------|
| `APP_PERF_SEC_AUDIO_TOTAL` | 音频处理总耗时 |
| `APP_PERF_SEC_AUDIO_DEINT` | 解交织 |
| `APP_PERF_SEC_AUDIO_FFT` | FFT 变换 |
| `APP_PERF_SEC_AUDIO_SRP` | SRP-PHAT 定位 |
| `APP_PERF_SEC_UI_LOOP` | UI 主循环总耗时 |
| `APP_PERF_SEC_UI_SNAPSHOT` | UI 数据快照 |
| `APP_PERF_SEC_UI_RENDER` | UI 渲染 |
| `APP_PERF_SEC_DISP_PREPARE` | 显示数据准备 |
| `APP_PERF_SEC_DISP_NORM` | 归一化 |
| `APP_PERF_SEC_DISP_RENDER` | 热力图渲染 |
| `APP_PERF_SEC_DISP_OVERLAY` | 叠加层绘制 |
| `APP_PERF_SEC_DISP_COMMIT` | 帧提交（刷屏） |

**使用方法**：
```
cfg perf on       # 开启（使能 DWT CYCCNT）
# ... 等待若干帧 ...
cfg perf dump     # 打印各区间平均/最大/最小周期数
cfg perf reset    # 清零重新统计
cfg perf off      # 关闭
```

统计参数：环形样本窗 `PERF_RING_SAMPLES = 64`，吞吐率打印周期 `PERF_RATE_PERIOD_MS = 1000 ms`。

### 10.2 关键断点位置

| 文件 | 函数 / 行 | 用途 |
|------|-----------|------|
| `app_audio_task.c` | `Audio_Pipeline_Task` 开头的 `xQueueReceive` | 观察 DMA 事件到达时机 |
| `ai_beamforming.c` | `AI_SRP_PHAT_Process` 返回前 | 检查定位结果（角度、能量、质量） |
| `ai_preprocess.c` | `Deinterleave_Using_Matrix` 返回 | 验证解交织后的数据正确性 |
| `app_display.c` | `App_Display_Render` | 渲染入口，检查输入数据 |
| `app_ui_cli.c` | `ui_cli_apply_line` | 调试 CLI 命令解析 |
| `mpu.c` | `App_MPU_Config` | 验证 MPU 区域配置 |
| `sai.c` | `MX_SAI1_Init` | 检查 SAI 寄存器配置 |

### 10.3 VOFA 调试输出

通过 `app_user_config.h` 中的宏启用 VOFA 可视化调试：

```c
#define DEBUG_ENABLE                  // 取消注释以开启
#define DEBUG_MODE           3        // 0=RMS, 1=FFT, 3=SRP 定位结果
#define DEBUG_THROTTLE_FRAMES 20     // 每 20 帧输出一次
#define DEBUG_SPECTRUM_CHANNEL 0     // FFT 调试通道
#define VOFA_UART_TX_TIMEOUT  5      // 串口发送超时 ms
```

### 10.4 运行时诊断计数器

以下全局变量可在调试器 Watch 窗口中实时查看：

| 变量 | 含义 |
|------|------|
| `g_audio_both_flags_count` | ISR 帧序号跳变累计（估算丢帧） |
| `g_audio_no_flag_count` | 音频任务未收到标志的次数（异常） |
| `g_audio_frame_seq_isr` | ISR 侧帧序号（判断 DMA 活跃） |
| `g_ui_render_count` | UI 渲染帧计数 |
| `g_ui_queue_rx_count` | UI 队列成功接收次数 |
| `g_ui_queue_timeout_count` | UI 队列超时次数 |
| `g_ltdc_fifo_underrun_count` | LTDC FIFO 下溢次数 |
| `g_ltdc_transfer_error_count` | LTDC 传输错误次数 |

### 10.5 常见问题排查（FAQ）

**Q: 上电后 LCD 白屏或花屏**
- 检查 SDRAM 初始化是否成功（`sdram.c`）
- 确认 MPU Region 2 配置正确（LTDC 帧缓冲 Non-Cacheable）
- 查看 `g_ltdc_fifo_underrun_count` 是否持续增长

**Q: 没有声音数据 / 音频任务不运行**
- 检查 `g_audio_frame_seq_isr` 是否递增 → SAI DMA 是否工作
- 确认 PCMD3180 I2C 初始化成功（`pcmd3180.c`）
- 确认 D2 SRAM（`0x30000000`）MPU 配置为 Non-Cacheable

**Q: SRP 定位结果飘忽不定**
- 使用 `cfg perf dump` 检查 SRP 算法是否在帧周期内完成
- 调大 `cfg algodecim` 降低算法负载
- 调整 `cfg noise` 和 `cfg contrast` 抑制背景噪声
- 检查 `SRP_LOWCONF_POLICY` 策略设置

**Q: UI 帧率过低**
- `cfg mode fast` 切换到快速模式
- `cfg smooth 0` 关闭高斯平滑
- `cfg bilinear 0` 切换到最近邻插值
- `cfg textdiv 5` 降低文字刷新频率
- `cfg algodecim 2` 释放 CPU 给渲染

**Q: CLI 无响应**
- 确认串口波特率匹配（上电会打印 `UI CLI ready @xxx baud`）
- 尝试 `cfg uart recover` 恢复 UART 状态
- 检查 `g_ui_cli_rx_alive` 是否为 1（2 秒内有数据 = 活跃）

**Q: 摄像头无画面**
- 使用 `cfg cam retry` 重试初始化
- 确认 OV2640 供电和 I2C 通信正常
- 检查 MPU Region 3（DCMI DMA 目标区域）配置

---

## 附录 A：统一配置入口速查

所有编译期可配置项集中在 `User/App/app_user_config.h`，按功能分组：

| 分组 | 典型宏 | 说明 |
|------|--------|------|
| 摄像头 | `APP_CAMERA_ENABLE`, `APP_CAMERA_PREVIEW_W/H` | 摄像头开关与预览尺寸 |
| 触摸 | `APP_TOUCH_ENABLE` | 触摸屏开关 |
| LVGL | `APP_LVGL_ENABLE`, `APP_LVGL_BOOT_AS_DEFAULT` | LVGL 使能与默认后端 |
| 调试 | `DEBUG_ENABLE`, `DEBUG_MODE` | VOFA 输出开关与模式 |
| 音频/算法 | `MIC_CHANNELS`, `FRAME_LEN`, `SAMPLING_RATE` | 基础音频参数 |
| SRP 搜索 | `COARSE_GRID_SIZE`, `FINE_TOP_K`, `FINE_GRID_SIZE` | 搜索网格配置 |
| 低置信度 | `SRP_LOWCONF_POLICY`, `SRP_CONTRAST_MIN_RATIO` | 结果质量门限 |
| 任务 | `APP_AUDIO_TASK_PRIO`, `APP_UI_TASK_PRIO`, `*_STACK_WORDS` | FreeRTOS 任务配置 |
| 显示 | `APP_DISPLAY_FIELD_W/H`, `APP_DISPLAY_RAM_SAVE_LEVEL` | 热力场分辨率与 RAM 策略 |
| CLI | `UI_CLI_ENABLE`, `UI_CLI_LINE_MAX`, `UI_CLI_RX_RING_SIZE` | UART CLI 配置 |

---

## 附录 B：工具脚本

| 脚本 | 路径 | 用途 |
|------|------|------|
| `generate_srp_lut.py` | `tools/` | 根据阵列几何生成 SRP 延迟查找表 (`ai_srp_lut.c`) |
| `srp_doa_sanity_check.py` | `tools/` | SRP-PHAT 定位算法 Python 验证 |
| `array_32ch_design.py` | `tools/` | 阵列几何设计与优化 |
| `plot_array_32ch.js` | `tools/` | 阵列布局可视化（Node.js） |
| `pull-clean.ps1` | `tools/` | 拉取并清理代码库（PowerShell） |

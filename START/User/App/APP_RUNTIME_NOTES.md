# APP Runtime Notes

Date: 2026-03-04

## 目的

本文件用于集中记录运行期说明，包括但不限于：
- 串口在线命令
- 运行状态输出解释
- 调参建议
- 现场排障要点

## 维护约定

从现在开始，凡是类似“串口在线命令（USART1）”这类运行说明，统一追加到本文件，不再分散写在临时消息中。

## 串口在线命令（USART1）

入口串口：`USART1`  
波特率：`921600`  
命令由 UI 任务轮询接收，输入一行后回车生效。

### 帮助与状态

- `help`
- `cfg help`
- `cfg status`

### 模式切换

- `cfg mode fast`
- `cfg mode balanced`
- `cfg mode bal`（`balanced` 的简写）
- `cfg mode clean`

### 对比度 / 降噪 / 清晰度参数

- `cfg contrast <db_floor>`
  - 示例：`cfg contrast -45`
  - 说明：若输入正数会自动转为负值，实际范围由代码夹紧到 `[-80, -6]`
- `cfg gamma <0.5..2.5>`
- `cfg noise <0..0.6>`
  - 含义：噪声门限比例（相对峰值）
- `cfg adapt <0..6>`
  - 含义：背景噪声自适应增益
- `cfg smooth <0..3>`
  - 含义：平滑次数
- `cfg fine <0..3>`
  - 含义：细网格融合增益

### 刷新速度相关参数

- `cfg bilinear <0|1>`
  - `0`：最近邻，速度更快
  - `1`：双线性，画面更平滑
- `cfg textdiv <1..20>`
  - 含义：文本覆盖层刷新分频，值越大文本刷新越慢、整体帧率压力越小
- `cfg blit <1..8>`
  - 含义：每次块拷贝行数，通常值越大开销越小（受内存和总线情况影响）

## 状态输出解释（`cfg status`）

`cfg status` 会输出当前模式、参数和 DMA2D 计数，重点关注：
- `dma2d transfer`
  - DMA2D 传输累计次数，持续增长表示 DMA2D 在参与工作
- `dma2d timeout`
  - DMA2D 超时累计次数，持续增长表示总线或配置异常
- `dma2d fallback`
  - 软件回退累计次数，持续增长表示 DMA2D 传输失败后走了 CPU 填充路径

## 快速建议

- 追求最高刷新速度：`cfg mode fast`
- 背景噪声偏多：
  - 先提高门限：`cfg noise 0.10` ~ `0.18`
  - 再提高自适应：`cfg adapt 2.0` ~ `3.5`
  - 需要更“干净”可加：`cfg smooth 1` 或 `cfg smooth 2`
- 对比度不够：
  - `cfg contrast -40` ~ `-55`
  - 配合 `cfg gamma 1.1` ~ `1.4`

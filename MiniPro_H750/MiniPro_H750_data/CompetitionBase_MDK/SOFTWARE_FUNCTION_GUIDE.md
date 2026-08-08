# MiniPro H750 综测软件函数使用说明书

这份说明只保留本题和类似现场综测会用到的软件函数。目标是：找到模板、修改参数、直接照抄。

## 1. 程序的固定结构

所有程序都分为“只执行一次的初始化”和“永远重复的循环”两部分：

```c
int main(void)
{
    /* 系统和硬件初始化：只执行一次。 */
    /* 直接保留 competition_main.c 中现有顺序。 */

    exam2025_init(nvm_ok);       /* 上电初始内容，只执行一次。 */

    while (1)
    {
        exam2025_process();      /* 按键、串口、计时等，必须反复执行。 */
        delay_ms(1);             /* 1ms短延时，原样保留。 */
    }
}
```

规则：

- `xxx_init()` 通常只在进入 `while(1)` 前调用一次。
- 按键扫描、串口接收和非阻塞计时必须放在循环服务函数中。
- 不要在循环里反复执行 `lcd_init()`、`comp_keys_init()` 等初始化。

## 2. 屏幕显示

### 2.1 清屏

```c
lcd_clear(WHITE);
```

参数是背景颜色。常用颜色：`WHITE`、`BLACK`、`RED`、`GREEN`、`BLUE`、`YELLOW`、`CYAN`。

### 2.2 显示英文、数字和符号

```c
lcd_show_string(20, 80, 400, 32, 32, "aa = 1.5", BLUE);
```

参数依次为：

| 参数 | 含义 |
|---|---|
| `20` | 左上角 X 坐标，单位像素 |
| `80` | 左上角 Y 坐标，单位像素 |
| `400` | 允许文字使用的区域宽度 |
| `32` | 允许文字使用的区域高度 |
| `32` | 字号；常用 `16`、`24`、`32` |
| `"aa = 1.5"` | 要显示的 ASCII 字符串 |
| `BLUE` | 文字颜色 |

改变显示内容时，直接替换双引号里的文字：

```c
lcd_show_string(20, 80, 400, 32, 32, "TEST OK", GREEN);
```

更新动态数值前先覆盖旧区域，避免残留：

```c
char text[32];
sprintf(text, "value = %d", value);
lcd_fill(20, 120, 300, 151, WHITE);              /* 擦除旧文字区域 */
lcd_show_string(20, 120, 280, 32, 32, text, BLUE);
```

`lcd_fill(x1, y1, x2, y2, color)` 的四个坐标是矩形左上角和右下角。

### 2.3 显示已收录的汉字

```c
comp_hanzi_show_utf8(20, 20, COMP_TEXT_ST_EXAM, DARKBLUE, WHITE);
```

参数依次为：X坐标、Y坐标、UTF-8文字、文字颜色、背景颜色。

可直接使用的短语包括：

```c
COMP_TEXT_ST_EXAM       /* st测评题 */
COMP_TEXT_SEND_OK       /* 发送成功 */
COMP_TEXT_COMPETITION   /* 比赛 */
COMP_TEXT_TRACK         /* 赛道 */
COMP_TEXT_EMBEDDED      /* 嵌入式 */
COMP_TEXT_CHIP          /* 芯片 */
COMP_TEXT_SYSTEM        /* 系统 */
COMP_TEXT_DESIGN        /* 设计 */
COMP_TEXT_LIGHT_UP      /* 点亮 */
COMP_TEXT_ENABLE        /* 赋能 */
```

未收入字库的汉字会显示方框，需要比赛前补字模，不能直接随便输入任意汉字。

## 3. 四个 LED

### 3.1 控制单个灯

```c
comp_led_set(COMP_LED1, 1);     /* LED1亮 */
comp_led_set(COMP_LED1, 0);     /* LED1灭 */
comp_led_toggle(COMP_LED2);     /* LED2当前状态翻转 */
```

第一个参数可用 `COMP_LED1`、`COMP_LED2`、`COMP_LED3`、`COMP_LED4`；第二个参数 `1=亮，0=灭`。

### 3.2 一次控制四个灯

```c
comp_led_set_mask(0x05);        /* 二进制0101：LED1、LED3亮 */
comp_led_set_mask(0x0F);        /* 二进制1111：四灯全亮 */
comp_led_set_mask(0x00);        /* 四灯全灭 */
comp_led_set_mask(0x0A);        /* 二进制1010：LED2、LED4亮 */
```

从最低位开始依次对应 LED1、LED2、LED3、LED4。

### 3.3 直接启动现有流水灯

```c
exam2025_start_led_sequence(500);
```

参数是每增加点亮一个灯的间隔，单位毫秒。`500` 表示每 0.5 秒增加一个灯。

## 4. 按键检测

### 4.1 检测一次按下事件

```c
comp_key_t key;

key = comp_keys_poll_event();
if (key != COMP_KEY_NONE)
{
    switch (key)
    {
        case COMP_KEY_BOARD_0:          /* 板载按键1 */
            comp_led_set(COMP_LED1, 1);
            break;

        case COMP_KEY_BOARD_1:          /* 板载按键2 */
            exam2025_uart_send_line("key2");
            break;

        case COMP_KEY_EXT_0:            /* 外接按键3 */
            exam2025_set_aa_tenths(
                exam2025_get_aa_tenths() + 5, 1, 1);
            break;

        case COMP_KEY_EXT_1:            /* 外接按键4 */
            exam2025_set_aa_tenths(
                exam2025_get_aa_tenths() - 5, 1, 1);
            break;

        default:
            break;
    }
}
```

`comp_keys_poll_event()` 已包含 20ms 消抖。按住不放只返回一次按下事件，不会在循环中连续触发。

### 4.2 判断按键是否仍按住或已经松开

```c
uint8_t mask = comp_keys_read_mask();

if ((mask & 0x01) == 0)
{
    /* 板载按键1已经松开。 */
}
```

掩码位：bit0=按键1、bit1=按键2、bit2=按键3、bit3=按键4。必须持续调用 `comp_keys_poll_event()`，稳定状态才会更新。

## 5. 延时和计时

### 5.1 简单短延时

```c
delay_ms(100);       /* 程序暂停100毫秒 */
```

参数单位是毫秒。短暂蜂鸣或上电等待可以使用。不要写 `delay_ms(30000)`，因为这30秒内按键、串口和显示都不能处理。

### 5.2 不阻塞其他功能的计时模板

```c
static uint32_t last_time = 0;
uint32_t now = HAL_GetTick();

if ((uint32_t)(now - last_time) >= 1000)
{
    last_time = now;
    comp_led_toggle(COMP_LED1);     /* 每1秒执行一次 */
}
```

`HAL_GetTick()` 返回开机后的毫秒数。`1000` 是间隔毫秒数，直接修改即可。

## 6. 蜂鸣器

```c
comp_buzzer_set(1);      /* 持续响 */
comp_buzzer_set(0);      /* 停止 */
comp_buzzer_beep(100);   /* 响100ms后自动停止；期间程序会阻塞100ms */
```

条件控制模板：

```c
if (aa_tenths == 0)
{
    comp_buzzer_set(1);
}
else
{
    comp_buzzer_set(0);
}
```

## 7. 四位数码管

```c
comp_max7219_show_number(1234, 0);     /* 显示1234；第二参数0=不补前导0 */
comp_max7219_show_number(25, 1);       /* 显示0025 */
comp_max7219_show_fixed1(15);          /* 显示1.5 */
comp_max7219_show_fixed1(-5);          /* 显示-0.5 */
comp_max7219_clear();                   /* 清空 */
comp_max7219_set_intensity(3);          /* 亮度0~15 */
```

`show_fixed1()` 的参数是实际数值的10倍，因此 1.5 传15。该驱动只用于 MAX7219 加四位共阴数码管，不适用于8×8点阵。

## 8. PWM和呼吸灯

初始化真正的硬件PWM：

```c
comp_pwm_init(1000, 500);
```

参数：`1000` 是频率1000Hz；`500` 是千分之500，即50%占空比。既定输出引脚是 PA11/P5-14。

运行中修改占空比：

```c
comp_pwm_set_duty(0);       /* 0% */
comp_pwm_set_duty(250);     /* 25% */
comp_pwm_set_duty(500);     /* 50% */
comp_pwm_set_duty(1000);    /* 100% */
comp_pwm_stop();            /* 停止PWM，PA11输出低电平 */
```

当前呼吸过程已经写在 `exam2025_process_breathing_led()` 中：约2秒完成暗→亮→暗，并同步更新 PA11 硬件PWM和PB14上的LED1。

## 9. 串口发送与接收

### 9.1 发送字符串

```c
exam2025_uart_send_line("key2");
exam2025_uart_send_line("st is ok");
```

参数是要发送的 ASCII 字符串，函数自动在结尾添加回车换行 `\r\n`。

发送变量：

```c
char text[32];
sprintf(text, "value=%d", value);
exam2025_uart_send_line(text);
```

### 9.2 修改收到命令后的动作

在 `exam_2025.c` 的 `exam2025_process_uart_line()` 中修改：

```c
if ((length == 2) && (data[0] == 's') && (data[1] == 't'))
{
    exam2025_uart_send_line("st is ok");
}
```

例如新题要求收到 `on` 后点亮四灯：

```c
if ((length == 2) && (data[0] == 'o') && (data[1] == 'n'))
{
    comp_led_set_mask(0x0F);
    exam2025_uart_send_line("OK");
}
```

上位机设置：COM12、115200、8位数据位、无校验、1位停止位。

## 10. 变量aa与掉电保存

```c
exam2025_set_aa_tenths(15, 1, 1);
```

三个参数：

| 参数 | 含义 |
|---|---|
| `15` | `aa×10`，所以表示1.5 |
| 第一个 `1` | 保存到板载QSPI；写0则不保存 |
| 第二个 `1` | 通过串口发送；写0则不发送 |

读取当前值：

```c
int16_t aa_tenths = exam2025_get_aa_tenths();
```

加减0.5：

```c
exam2025_set_aa_tenths(exam2025_get_aa_tenths() + 5, 1, 1);
exam2025_set_aa_tenths(exam2025_get_aa_tenths() - 5, 1, 1);
```

这个封装已经同时完成：刷新TFTLCD、刷新数码管、判断蜂鸣器条件、按要求保存、按要求发送。现场优先调用这个函数，不需要自己直接操作QSPI。

## 11. 修改去年题目时只找这四处

1. `exam_2025.h`：修改30秒、流水灯间隔、aa步长。
2. `exam2025_init()`：修改上电文字、初始灯状态、初始变量。
3. `exam2025_process_key()`：修改四个按键分别执行的动作。
4. `exam2025_process_uart_line()`：修改收到上位机命令后的判断和回复。

`competition_main.c` 的基础初始化顺序和 `while(1)` 一般原样保留。

#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"
#include "./BSP/MPU/mpu.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/COMPETITION/comp_led.h"
#include "./BSP/COMPETITION/comp_keys.h"
#include "./BSP/COMPETITION/comp_buzzer.h"
#include "./BSP/COMPETITION/comp_pwm.h"
#include "./BSP/COMPETITION/comp_nvm.h"
#include "./BSP/COMPETITION/comp_max7219.h"
#include "exam_2025.h"

/*
 * ======================== 比赛现场先看这里 ========================
 *
 * 这个 main.c 只负责两件事：
 * 1. 上电时把需要的硬件驱动初始化一次；
 * 2. 在 while(1) 中反复调用 exam2025_process() 处理按键、灯、串口等任务。
 *
 * 换成今年的新题时，通常不要重写下面的时钟和硬件初始化。主要修改：
 * - 时间、步长：User/exam_2025.h 顶部的 EXAM2025_... 宏；
 * - 按键做什么：User/exam_2025.c 的 exam2025_process_key()；
 * - 收到串口命令做什么：exam2025_process_uart_line()；
 * - 上电显示和初始状态：exam2025_init()；
 * - aa 改变后做什么：exam2025_set_aa_tenths()。
 *
 * SOFTWARE_FUNCTION_GUIDE.md 中有可以直接照抄的函数和代码模板。
 * =================================================================
 */

int main(void)
{
    uint8_t nvm_ok;

    /*
     * 【基础系统初始化】以下顺序已经验证，换题时整段照抄，不要随意删改。
     * usart_init(115200)：参数是波特率；上位机也必须选择 115200、8N1。
     * delay_init(480)：480 对应当前 CPU 480MHz，不是延时时间。
     */
    sys_cache_enable();
    HAL_Init();
    sys_stm32_clock_init(240, 2, 2, 4);  /* Cortex-M7 at 480 MHz. */
    delay_init(480);
    usart_init(115200);
    led_init();
    mpu_memory_protection();
    lcd_init();             /* 只初始化一次；显示内容在 exam2025_init() 中修改。 */

    /*
     * 【竞赛外设初始化】使用哪个外设，就保留对应的 init()。
     * init() 只能在上电时调用一次，不能放到 while(1) 里反复调用。
     */
    comp_led_init();        /* 之后用 comp_led_set()/set_mask()/toggle() 控制四灯。 */
    comp_keys_init();       /* 之后在循环中用 comp_keys_poll_event() 检测按键。 */
    comp_buzzer_init();     /* 之后用 comp_buzzer_set(1/0) 控制响/停。 */
    comp_max7219_init();    /* 之后用 show_fixed1() 显示带一位小数的数值。 */

    /*
     * comp_pwm_init(频率Hz, 初始占空比千分数)
     * 500U = 500Hz；0U = 0/1000 = 0%。
     * 例如 1kHz、50% 写成 comp_pwm_init(1000U, 500U)。
     * 硬件 PWM 固定从既定 PA11/P5-14 输出。
     */
    (void)comp_pwm_init(500U, 0U);

    /* comp_nvm_init() 返回 0 表示板载 QSPI 存储正常，非0表示失败。 */
    nvm_ok = (comp_nvm_init() == 0U) ? 1U : 0U;

    /*
     * 【现场改题入口1：上电执行一次】
     * exam2025_init() 内设置初始灯状态、屏幕文字、恢复 aa 和初始显示。
     * 如果今年题目要求“上电显示XXX/初始变量为XXX”，主要修改这个函数。
     */
    exam2025_init(nvm_ok);

    while (1)
    {
        /*
         * 【现场改题入口2：持续循环执行】
         * 这个函数内部处理：按键、30秒计时、流水灯、呼吸灯、串口接收。
         * 必须一直调用，不能删；也不要在这里写 delay_ms(30000) 这种长延时。
         */
        exam2025_process();

        /* 1ms 短延时降低空转占用；不是题目的计时时间，可以原样保留。 */
        delay_ms(1);
    }
}

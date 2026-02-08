#include "soft_i2c.h"
#include <stdio.h>
#include "cmsis_os.h"

// FreeRTOS 互斥量，防止多任务冲突
static osMutexId_t i2cMutexHandle;
static const osMutexAttr_t i2cMutex_attributes = {
  .name = "I2CMutex"
};

// ================= DWT 微秒延时 (STM32H7 专用) =================
// 软件I2C速率控制: delay_us(2) 约等于 250kHz, delay_us(5) 约等于 100kHz
static void I2C_Delay_us(volatile uint32_t microseconds)
{
    uint32_t clk_cycle_start = DWT->CYCCNT;
    // H7通常运行在400MHz+, 1us = 400-480 cycles
    // SystemCoreClock 是系统核心时钟频率
    //microseconds *= (SystemCoreClock / 1000000); 
    microseconds *= (SystemCoreClock / 100000); 
    while ((DWT->CYCCNT - clk_cycle_start) < microseconds);
}

// ================= GPIO 底层操作 (Open-Drain 模式) =================
// SDA High (释放总线, 由上拉电阻拉高)
#define SDA_HIGH()      HAL_GPIO_WritePin(I2C_PORT, I2C_SDA_PIN, GPIO_PIN_SET)
// SDA Low (拉低总线)
#define SDA_LOW()       HAL_GPIO_WritePin(I2C_PORT, I2C_SDA_PIN, GPIO_PIN_RESET)
// SCL High
#define SCL_HIGH()      HAL_GPIO_WritePin(I2C_PORT, I2C_SCL_PIN, GPIO_PIN_SET)
// SCL Low
#define SCL_LOW()       HAL_GPIO_WritePin(I2C_PORT, I2C_SCL_PIN, GPIO_PIN_RESET)
// 读取 SDA 状态
#define SDA_READ()      HAL_GPIO_ReadPin(I2C_PORT, I2C_SDA_PIN)

// ================= I2C 协议层 (Bit-Banging) =================

static void I2C_Start(void)
{
    SDA_HIGH();
    SCL_HIGH();
    I2C_Delay_us(2);
    SDA_LOW();      // SCL高电平时，SDA由高变低 -> Start
    I2C_Delay_us(2);
    SCL_LOW();      // 钳住I2C总线，准备发送或接收数据
}

static void I2C_Stop(void)
{
    SCL_LOW();
    SDA_LOW();
    I2C_Delay_us(2);
    SCL_HIGH();
    I2C_Delay_us(2);
    SDA_HIGH();     // SCL高电平时，SDA由低变高 -> Stop
    I2C_Delay_us(2);
}

static uint8_t I2C_WaitAck(void)
{
    uint8_t ack = 0;
    
    SDA_HIGH();     // 释放SDA，让从机驱动
    I2C_Delay_us(2);
    SCL_HIGH();     // 拉高SCL读取ACK
    I2C_Delay_us(2);
    
    if (SDA_READ()) // 如果读到高电平，说明从机没有ACK (NACK)
        ack = 1;    // 失败
    else
        ack = 0;    // 成功 (SDA被从机拉低)

    SCL_LOW();      // 结束ACK周期
    return ack;
}

static void I2C_SendByte(uint8_t byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        if (byte & 0x80)
            SDA_HIGH();
        else
            SDA_LOW();
        
        byte <<= 1;
        I2C_Delay_us(2); // 数据建立时间
        SCL_HIGH();
        I2C_Delay_us(2); // 数据保持时间
        SCL_LOW();
        I2C_Delay_us(1);
    }
}

static uint8_t I2C_ReadByte(uint8_t ack)
{
    uint8_t i, receive = 0;
    
    SDA_HIGH(); // 切换为输入模式(Open-Drain下写1即释放)
    
    for (i = 0; i < 8; i++)
    {
        receive <<= 1;
        SCL_HIGH();
        I2C_Delay_us(2);
        if (SDA_READ()) receive++;
        SCL_LOW();
        I2C_Delay_us(2);
    }
    
    // 发送 ACK (Low) 或 NACK (High)
    if (!ack)
        SDA_LOW();
    else
        SDA_HIGH();
        
    I2C_Delay_us(2);
    SCL_HIGH();
    I2C_Delay_us(2);
    SCL_LOW();
    SDA_HIGH(); // 释放总线
    
    return receive;
}

// ================= 初始化与应用层 API =================

/**
 * @brief 系统启动时调用一次，初始化DWT和Mutex
 */
void Soft_I2C_Init(void)
{
    // 1. 开启 DWT 计数器用于精确延时
    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }

    // 2. 初始化互斥量
    i2cMutexHandle = osMutexNew(&i2cMutex_attributes);

    // 3. 初始状态：拉高总线
    SCL_HIGH();
    SDA_HIGH();
}

/**
 * @brief 写寄存器 (对应 Figure 80: Single-Byte Write Transfer)
 * @param devAddr 7位设备地址 (0x4C 或 0x4D)
 * @param regAddr 寄存器地址
 * @param data    要写入的数据
 * @return 0:成功, 1:失败
 */
uint8_t PCMD_WriteReg(uint8_t devAddr, uint8_t regAddr, uint8_t data)
{
    // 获取锁 (等待 100ms 超时)
    if (osMutexAcquire(i2cMutexHandle, 100) != osOK) return 1;

    I2C_Start();

    // 发送设备地址 + 写位(0)
    // 根据手册 Table 43, I2C Slave Address 是 7-bit
    I2C_SendByte(devAddr << 1); 
    if (I2C_WaitAck()) { I2C_Stop(); osMutexRelease(i2cMutexHandle); return 1; }

    // 发送寄存器地址
    I2C_SendByte(regAddr);
    if (I2C_WaitAck()) { I2C_Stop(); osMutexRelease(i2cMutexHandle); return 1; }

    // 发送数据
    I2C_SendByte(data);
    if (I2C_WaitAck()) { I2C_Stop(); osMutexRelease(i2cMutexHandle); return 1; }

    I2C_Stop();
    
    // 释放锁
    osMutexRelease(i2cMutexHandle);
    return 0;
}

/**
 * @brief 读寄存器 (对应 Figure 82: Single-Byte Read Transfer)
 * @param devAddr 7位设备地址
 * @param regAddr 寄存器地址
 * @param pData   读取数据的指针
 * @return 0:成功, 1:失败
 */
uint8_t PCMD_ReadReg(uint8_t devAddr, uint8_t regAddr, uint8_t *pData)
{
    if (osMutexAcquire(i2cMutexHandle, 100) != osOK) return 1;

    // 1. Dummy Write: 写入要读取的寄存器地址
    I2C_Start();
    I2C_SendByte(devAddr << 1); // Write Address
    if (I2C_WaitAck()) { I2C_Stop(); osMutexRelease(i2cMutexHandle); return 1; }
    
    I2C_SendByte(regAddr);
    if (I2C_WaitAck()) { I2C_Stop(); osMutexRelease(i2cMutexHandle); return 1; }

    // 2. Restart & Read: 发送重复起始信号 (Repeat Start)
    I2C_Start();
    I2C_SendByte((devAddr << 1) | 0x01); // Read Address (R/W bit = 1)
    if (I2C_WaitAck()) { I2C_Stop(); osMutexRelease(i2cMutexHandle); return 1; }

    // 3. 读取数据并发送 NACK (根据 Figure 82，最后一个字节Master发NACK)
    *pData = I2C_ReadByte(1); // 1 = NACK

    I2C_Stop();
    osMutexRelease(i2cMutexHandle);
    return 0;
}

void I2C_Scan(void)
{
    printf("Scanning I2C bus...\r\n");
    for(uint16_t i = 0; i < 128; i++)
    {
        // 尝试向地址 i 发送一个空写命令
        // I2C_Start -> Send Addr(Write) -> Wait Ack
        I2C_Start();
        I2C_SendByte(i << 1); // 转换为8位写地址
        
        if(I2C_WaitAck() == 0) // 收到 ACK (0表示成功)
        {
            printf("Device found at 0x%02X\r\n", i);
            I2C_Stop();
        }
        else
        {
            I2C_Stop(); // 没收到ACK，发送Stop
        }
        
        // 稍微延时，防止发太快
        osDelay(10);
    }
    printf("Scan done.\r\n");
}
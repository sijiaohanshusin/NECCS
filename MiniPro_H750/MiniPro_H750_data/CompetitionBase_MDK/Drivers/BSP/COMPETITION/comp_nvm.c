#include "./BSP/COMPETITION/comp_nvm.h"
#include "./BSP/NORFLASH/norflash.h"
#include "./BSP/NORFLASH/norflash_ex.h"

#define COMP_NVM_FLASH_BYTES        (16UL * 1024UL * 1024UL)
#define COMP_NVM_RESERVED_SECTOR    (COMP_NVM_FLASH_BYTES - 4096UL)

static uint16_t s_flash_id;

static uint8_t comp_nvm_id_is_valid(uint16_t id)
{
    return ((id == W25Q128) || (id == BY25Q128) || (id == NM25Q128)) ? 1U : 0U;
}

uint8_t comp_nvm_init(void)
{
    s_flash_id = norflash_ex_read_id();
    return comp_nvm_id_is_valid(s_flash_id) ? 0U : 1U;
}

uint16_t comp_nvm_flash_id(void)
{
    return s_flash_id;
}

uint8_t comp_nvm_read(uint16_t offset, uint8_t *buffer, uint16_t length)
{
    if ((buffer == NULL) || ((uint32_t)offset + length > COMP_NVM_CAPACITY_BYTES))
    {
        return 1U;
    }

    norflash_ex_read(buffer, COMP_NVM_RESERVED_SECTOR + offset, length);
    return 0U;
}

uint8_t comp_nvm_write(uint16_t offset, const uint8_t *buffer, uint16_t length)
{
    if ((buffer == NULL) || ((uint32_t)offset + length > COMP_NVM_CAPACITY_BYTES))
    {
        return 1U;
    }

    return norflash_ex_write((uint8_t *)buffer,
                             COMP_NVM_RESERVED_SECTOR + offset,
                             length);
}

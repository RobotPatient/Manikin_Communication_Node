/**
 * @file crc16_koopman_hw.c
 * @brief Hardware-accelerated CRC-16 implementation for STM32
 */
#include "crc16_koopman_hw.h"
#include "crc16_koopman.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>

/* Include STM32 HAL headers directly. This approach avoids needing specific
 * Kconfig settings while still providing access to the hardware CRC features */
#if defined(CONFIG_SOC_SERIES_STM32H7X)
#include <stm32h7xx.h>
#include <stm32h7xx_hal.h>
#include <stm32h7xx_ll_crc.h>
#elif defined(CONFIG_SOC_SERIES_STM32F4X)
#include <stm32f4xx.h>
#include <stm32f4xx_hal.h>
#include <stm32f4xx_ll_crc.h>
#else
/* Define a flag to indicate this is not an STM32 with hardware CRC */
#define NO_STM32_HW_CRC
#endif

LOG_MODULE_REGISTER(crc16_koopman_hw, LOG_LEVEL_INF);

/* Koopman polynomial: 0x8D95 */
#define CRC16_KOOPMAN_POLY 0x8D95

/* Flag to track if hardware CRC is available */
static bool hw_crc_available = false;

/**
 * @brief Initialize the hardware CRC peripheral for Koopman polynomial
 * 
 * This function initializes the STM32 hardware CRC peripheral with
 * the Koopman polynomial (0x8D95) configuration.
 *
 * @return true if hardware CRC was successfully initialized, false otherwise
 */
bool crc16_koopman_hw_init(void) {
#ifdef NO_STM32_HW_CRC
    /* Not an STM32 with hardware CRC */
    hw_crc_available = false;
    LOG_WRN("STM32 hardware CRC not available on this platform");
    return false;
#else
    /* Enable CRC clock using HAL macro */
    #if defined(CONFIG_SOC_SERIES_STM32H7X) || defined(CONFIG_SOC_SERIES_STM32F4X)
    __HAL_RCC_CRC_CLK_ENABLE();
    #else
    /* Unknown STM32 variant */
    hw_crc_available = false;
    LOG_WRN("Unknown STM32 variant, cannot initialize hardware CRC");
    return false;
    #endif
    
    /* Reset CRC peripheral */
    LL_CRC_ResetCRCCalculationUnit(CRC);
    
    /* The STM32H7 allows configuring custom polynomials, but not all STM32 variants do */
    #if defined(CONFIG_SOC_SERIES_STM32H7X)
    /* Configure CRC for 16-bit with Koopman polynomial */
    LL_CRC_SetPolynomialSize(CRC, LL_CRC_POLYLENGTH_16B);
    LL_CRC_SetPolynomialCoef(CRC, CRC16_KOOPMAN_POLY);
    LL_CRC_SetInitialData(CRC, 0);
    
    /* Don't reverse input or output data */
    LL_CRC_SetInputDataReverseMode(CRC, LL_CRC_INDATA_REVERSE_NONE);
    LL_CRC_SetOutputDataReverseMode(CRC, LL_CRC_OUTDATA_REVERSE_NONE);
    
    hw_crc_available = true;
    LOG_INF("STM32H7 hardware CRC initialized with Koopman polynomial 0x%04X", CRC16_KOOPMAN_POLY);
    #else
    /* For other STM32 variants, we'll use our optimized bit-by-bit implementation */
    hw_crc_available = false;
    LOG_INF("This STM32 variant doesn't support custom polynomials, using optimized software implementation");
    #endif
    
    return hw_crc_available;
#endif /* NO_STM32_HW_CRC */
}

/**
 * @brief Check if hardware CRC is available
 *
 * @return true if hardware CRC is available and initialized, false otherwise
 */
bool crc16_koopman_hw_available(void) {
    return hw_crc_available;
}

/**
 * @brief Calculate CRC-16 using hardware acceleration if available
 * 
 * This function calculates a 16-bit CRC using the Koopman polynomial 0x8D95.
 * 
 * @param data Pointer to the data buffer
 * @param length Length of the data buffer in bytes
 * @return Calculated CRC-16 value
 */
uint16_t crc16_koopman_hw(const uint8_t *data, size_t length) {
    return crc16_koopman_hw_update(0, data, length);
}

/**
 * @brief Calculate CRC-16 using optimized bit-by-bit implementation
 *
 * This is a high-performance bit-by-bit implementation optimized for embedded systems.
 *
 * @param crc Initial CRC value
 * @param data Pointer to the data buffer
 * @param length Length of the data in bytes
 * @return Updated CRC-16 value
 */
static uint16_t calculate_crc16_koopman_optimized(uint16_t crc, const uint8_t *data, size_t length) {
    size_t i;
    uint8_t byte;
    
    for (i = 0; i < length; i++) {
        byte = data[i];
        crc ^= ((uint16_t)byte << 8);
        
        /* Unrolled loop for better performance */
        if (crc & 0x8000) crc = (crc << 1) ^ CRC16_KOOPMAN_POLY; else crc = crc << 1;
        if (crc & 0x8000) crc = (crc << 1) ^ CRC16_KOOPMAN_POLY; else crc = crc << 1;
        if (crc & 0x8000) crc = (crc << 1) ^ CRC16_KOOPMAN_POLY; else crc = crc << 1;
        if (crc & 0x8000) crc = (crc << 1) ^ CRC16_KOOPMAN_POLY; else crc = crc << 1;
        if (crc & 0x8000) crc = (crc << 1) ^ CRC16_KOOPMAN_POLY; else crc = crc << 1;
        if (crc & 0x8000) crc = (crc << 1) ^ CRC16_KOOPMAN_POLY; else crc = crc << 1;
        if (crc & 0x8000) crc = (crc << 1) ^ CRC16_KOOPMAN_POLY; else crc = crc << 1;
        if (crc & 0x8000) crc = (crc << 1) ^ CRC16_KOOPMAN_POLY; else crc = crc << 1;
    }
    
    return crc;
}

/**
 * @brief Update CRC-16 using STM32 hardware CRC peripheral
 *
 * @param crc Initial CRC value
 * @param data Pointer to the data buffer
 * @param length Length of the data in bytes
 * @return Updated CRC-16 value
 */
#if !defined(NO_STM32_HW_CRC) && defined(CONFIG_SOC_SERIES_STM32H7X)
static uint16_t calculate_crc16_hw_stm32h7(uint16_t crc, const uint8_t *data, size_t length) {
    /* Reset CRC peripheral if initial value is 0, otherwise we need to
     * pre-compute the CRC state for the provided initial value, which
     * is complex for non-zero values */
    if (crc == 0) {
        LL_CRC_ResetCRCCalculationUnit(CRC);
    } else {
        /* For non-zero initial CRC, we'll use the software implementation,
         * as properly initializing the hardware CRC state is complex */
        return calculate_crc16_koopman_optimized(crc, data, length);
    }
    
    /* Process data byte-by-byte */
    for (size_t i = 0; i < length; i++) {
        LL_CRC_FeedData8(CRC, data[i]);
    }
    
    /* Read the 16-bit CRC result */
    return (uint16_t)LL_CRC_ReadData16(CRC);
}
#endif

/**
 * @brief Update an existing CRC-16 with new data
 * 
 * @param crc Initial CRC value (use 0 for first call)
 * @param data Pointer to the data buffer
 * @param length Length of the data buffer in bytes
 * @return Updated CRC-16 value
 */
uint16_t crc16_koopman_hw_update(uint16_t crc, const uint8_t *data, size_t length) {
    /* Validate input parameters */
    if (data == NULL && length > 0) {
        LOG_ERR("Invalid input: NULL data pointer with non-zero length");
        return crc;
    }
    
#if !defined(NO_STM32_HW_CRC) && defined(CONFIG_SOC_SERIES_STM32H7X)
    /* For STM32H7, we can use the hardware CRC if available */
    if (hw_crc_available) {
        return calculate_crc16_hw_stm32h7(crc, data, length);
    }
#endif
    
    /* Use optimized software implementation */
    return calculate_crc16_koopman_optimized(crc, data, length);
}
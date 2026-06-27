/**
 * @file twl_compat.c
 * @brief Prepares TWL-mode audio, touch and MCU state for DS child boot.
 *
 * The codec scripts, touch/sound register order and volume exception list are
 * derived from Pico-Loader `DSMode.cpp`, then reduced to the compatibility path
 * needed before launching a saved DS Download Play payload.
 */
#include <nds.h>
#include <stdbool.h>

#define REG8(addr)   (*(volatile u8*)(addr))
#define REG16(addr)  (*(volatile u16*)(addr))
#ifndef ARRAY_COUNT
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define TWL_REG_SPICNT   0x040001c0u
#define TWL_REG_SPIDATA  0x040001c2u
#define TWL_SPICNT_SPEED_4_MHZ   (0u << 0)
#define TWL_SPICNT_SPEED_1_MHZ   (2u << 0)
#define TWL_SPICNT_BUSY          (1u << 7)
#define TWL_SPICNT_DEVICE_PMIC   (0u << 8)
#define TWL_SPICNT_DEVICE_TOUCH  (2u << 8)
#define TWL_SPICNT_HOLD_CS       (1u << 11)
#define TWL_SPICNT_ENABLE        (1u << 15)

#define TWL_REG_I2C_DATA 0x04004500u
#define TWL_REG_I2C_CNT  0x04004501u
#define TWL_I2C_CNT_LAST       (1u << 0)
#define TWL_I2C_CNT_FIRST      (1u << 1)
#define TWL_I2C_CNT_ERROR      (1u << 2)
#define TWL_I2C_CNT_ACK        (1u << 4)
#define TWL_I2C_CNT_DIR_WRITE  (0u << 5)
#define TWL_I2C_CNT_ENABLE     (1u << 7)
#define I2C_DEVICE_MCU     0x4au
#define MCU_REG_MODE       0x12u

#define TWL_REG_POWCNT 0x04000304u
#define POWCNT_SOUND_ENABLE (1u << 0)

#define REG_I2SCNT 0x04004700u
#define I2SCNT_MIX_RATIO_DSP_0_NITRO_8 (8u << 0)
#define I2SCNT_FREQUENCY_32728_HZ      (0u << 13)
#define I2SCNT_ENABLE                  (1u << 15)

#define TWL_REG_SOUNDCNT 0x04000500u
#define SOUNDCNT_MASTER_VOLUME(x) ((x) << 0)
#define SOUNDCNT_MASTER_ENABLE    (1u << 15)

#define CODEC_PAGE_0   0u
#define CODEC_PAGE_1   1u
#define CODEC_PAGE_3   3u
#define CODEC_PAGE_4   4u
#define CODEC_PAGE_255 255u

#define CODEC_REG_PAGE_CONTROL 0u
#define CODEC_REG_PAGE0_RESET 1u
#define CODEC_REG_PAGE0_CLOCK_GEN_MUXING 4u
#define CODEC_REG_PAGE0_PLL_P_R 5u
#define CODEC_REG_PAGE0_PLL_J 6u
#define CODEC_REG_PAGE0_DAC_NDAC_VAL 11u
#define CODEC_REG_PAGE0_DAC_MDAC_VAL 12u
#define CODEC_REG_PAGE0_DAC_DOSR_VAL_MSB 13u
#define CODEC_REG_PAGE0_DAC_DOSR_VAL_LSB 14u
#define CODEC_REG_PAGE0_DAC_IDAC_VAL 15u
#define CODEC_REG_PAGE0_DAC_MINIDSP_INTERPOLATION 16u
#define CODEC_REG_PAGE0_ADC_NADC_VAL 18u
#define CODEC_REG_PAGE0_ADC_MADC_VAL 19u
#define CODEC_REG_PAGE0_ADC_AOSR_VAL 20u
#define CODEC_REG_PAGE0_ADC_IDAC_VAL 21u
#define CODEC_REG_PAGE0_ADC_MINIDSP_DECIMATION 22u
#define CODEC_REG_PAGE0_CLKOUT_M_VAL 26u
#define CODEC_REG_PAGE0_BCLK_N_VAL 30u
#define CODEC_REG_PAGE0_ADC_FLAG_REGISTER 36u
#define CODEC_REG_PAGE0_GPIO1_IN_OUT_PIN_CONTROL 51u
#define CODEC_REG_PAGE0_GPIO2_IN_OUT_PIN_CONTROL 52u
#define CODEC_REG_PAGE0_SDOUT_OUT_PIN_CONTROL 53u
#define CODEC_REG_PAGE0_SDIN_IN_PIN_CONTROL 54u
#define CODEC_REG_PAGE0_MISO_OUT_PIN_CONTROL 55u
#define CODEC_REG_PAGE0_SCLK_IN_PIN_CONTROL 56u
#define CODEC_REG_PAGE0_GPI1_GPI2_PIN_CONTROL 57u
#define CODEC_REG_PAGE0_GPI3_PIN_CONTROL 58u
#define CODEC_REG_PAGE0_DAC_INSTRUCTION_SET 60u
#define CODEC_REG_PAGE0_ADC_INSTRUCTION_SET 61u
#define CODEC_REG_PAGE0_DAC_DATA_PATH_SETUP 63u
#define CODEC_REG_PAGE0_DAC_VOLUME_CONTROL 64u
#define CODEC_REG_PAGE0_DAC_LEFT_VOLUME_CONTROL 65u
#define CODEC_REG_PAGE0_DAC_RIGHT_VOLUME_CONTROL 66u
#define CODEC_REG_PAGE0_DRC_CONTROL_1 68u
#define CODEC_REG_PAGE0_DRC_CONTROL_2 69u
#define CODEC_REG_PAGE0_BEEP_LENGTH_MSB 73u
#define CODEC_REG_PAGE0_BEEP_LENGTH_MID 74u
#define CODEC_REG_PAGE0_BEEP_LENGTH_LSB 75u
#define CODEC_REG_PAGE0_BEEP_SIN_MSB 76u
#define CODEC_REG_PAGE0_BEEP_SIN_LSB 77u
#define CODEC_REG_PAGE0_BEEP_COS_MSB 78u
#define CODEC_REG_PAGE0_BEEP_COS_LSB 79u
#define CODEC_REG_PAGE0_ADC_DIGITAL_MIC 81u
#define CODEC_REG_PAGE0_ADC_DIGITAL_VOLUME_CONTROL_FINE_ADJUST 82u
#define CODEC_REG_PAGE0_AGC_MAXIMUM_GAIN 88u
#define CODEC_REG_PAGE0_VOL_MICDET_PIN_SAR_ADC_VOLUME_CONTROL 116u
#define CODEC_REG_PAGE0_VOL_MICDET_PIN_GAIN 117u

#define CODEC_REG_PAGE1_HEADPHONE_DRIVERS 31u
#define CODEC_REG_PAGE1_CLASS_D_SPEAKER_AMPLIFIER 32u
#define CODEC_REG_PAGE1_HP_OUTPUT_DRIVERS_POP_REMOVAL_SETTINGS 33u
#define CODEC_REG_PAGE1_OUTPUT_DRIVER_PGA_RAMP_DOWN_PERIOD_CONTROL 34u
#define CODEC_REG_PAGE1_DAC_L_DAC_R_OUTPUT_MIXER_ROUTING 35u
#define CODEC_REG_PAGE1_LEFT_ANALOG_VOL_TO_HPL 36u
#define CODEC_REG_PAGE1_RIGHT_ANALOG_VOL_TO_HPR 37u
#define CODEC_REG_PAGE1_LEFT_ANALOG_VOL_TO_SPL 38u
#define CODEC_REG_PAGE1_RIGHT_ANALOG_VOL_TO_SPR 39u
#define CODEC_REG_PAGE1_HPL_DRIVER 40u
#define CODEC_REG_PAGE1_HPR_DRIVER 41u
#define CODEC_REG_PAGE1_SPL_DRIVER 42u
#define CODEC_REG_PAGE1_SPR_DRIVER 43u
#define CODEC_REG_PAGE1_HP_DRIVER_CONTROL 44u
#define CODEC_REG_PAGE1_MICBIAS 46u
#define CODEC_REG_PAGE1_MIC_PGA 47u
#define CODEC_REG_PAGE1_DELTA_SIGMA_MONO_ADC_CHANNEL_FINE_GAIN_INPUT_SELECTION_FOR_P_TERMINAL 48u
#define CODEC_REG_PAGE1_ADC_INPUT_SELECTION_FOR_M_TERMINAL 49u
#define CODEC_REG_PAGE1_INPUT_CM_SETTINGS 50u

#define CODEC_REG_PAGE3_SAR_ADC_CONTROL_1 2u
#define CODEC_REG_PAGE3_SAR_ADC_CONTROL_2 3u
#define CODEC_REG_PAGE3_SCAN_MODE_TIMER_CLOCK 16u

#define CODEC_REG_PAGE255_BACKWARDS_COMPATIBILITY_MODE 5u
#define CODEC_PAGE255_BACKWARDS_COMPATIBILITY_MODE_ON 0u

#define PMIC_REG_CONTROL 0u
#define PMIC_CONTROL_AMP_ENABLE (1u << 0)
#define PMIC_CONTROL_AMP_MUTE   (1u << 1)

#define GAMECODE3(a, b, c) ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16))
#define SPI_WAIT_TIMEOUT_LOOPS 100000u
#define I2C_WAIT_TIMEOUT_LOOPS 100000u

/**
 * @brief Waits until the ARM7 SPI controller is idle.
 *
 * @return true when the busy bit cleared before the local timeout.
 */
static bool twl_spi_wait_busy(void) {
    for (unsigned i = 0; i < SPI_WAIT_TIMEOUT_LOOPS; i++) {
        if ((REG16(TWL_REG_SPICNT) & TWL_SPICNT_BUSY) == 0) return true;
    }
    return false;
}

/**
 * @brief Transfers one SPI byte while keeping chip select asserted.
 *
 * The helper is used for codec and PMIC register address phases. It returns
 * `0xff` on timeout because the caller cannot recover once the TWL-to-NTR
 * handover path has started.
 */
static u8 twl_spi_transfer_byte(u32 control, u8 data) {
    if (!twl_spi_wait_busy()) return 0xffu;
    REG16(TWL_REG_SPICNT) = (u16)(control | TWL_SPICNT_ENABLE | TWL_SPICNT_HOLD_CS);
    REG16(TWL_REG_SPIDATA) = data;
    if (!twl_spi_wait_busy()) return 0xffu;
    return (u8)REG16(TWL_REG_SPIDATA);
}

/**
 * @brief Transfers the final SPI byte and releases chip select afterwards.
 *
 * @return The received SPI byte, or `0xff` if either busy wait timed out.
 */
static u8 twl_spi_transfer_last_byte(u32 control, u8 data) {
    if (!twl_spi_wait_busy()) return 0xffu;
    REG16(TWL_REG_SPICNT) = (u16)(control | TWL_SPICNT_ENABLE);
    REG16(TWL_REG_SPIDATA) = data;
    if (!twl_spi_wait_busy()) return 0xffu;
    return (u8)REG16(TWL_REG_SPIDATA);
}

/**
 * @brief Reads a register from the TWL touchscreen/audio codec.
 *
 * The register page is controlled separately by `codec_set_page()`.
 */
static u8 codec_read(u8 reg) {
    twl_spi_transfer_byte(TWL_SPICNT_DEVICE_TOUCH | TWL_SPICNT_SPEED_4_MHZ, (u8)((reg << 1) | 1u));
    return twl_spi_transfer_last_byte(TWL_SPICNT_DEVICE_TOUCH | TWL_SPICNT_SPEED_4_MHZ, 0);
}

/**
 * @brief Writes a register on the active TWL codec page.
 */
static void codec_write(u8 reg, u8 data) {
    twl_spi_transfer_byte(TWL_SPICNT_DEVICE_TOUCH | TWL_SPICNT_SPEED_4_MHZ, (u8)(reg << 1));
    twl_spi_transfer_last_byte(TWL_SPICNT_DEVICE_TOUCH | TWL_SPICNT_SPEED_4_MHZ, data);
}

/**
 * @brief Selects the active TWL codec register page.
 */
static void codec_set_page(u8 page) {
    codec_write(CODEC_REG_PAGE_CONTROL, page);
}

/**
 * @brief Reads a PMIC control register through the ARM7 SPI bus.
 */
static u8 pmic_read(u8 reg) {
    twl_spi_transfer_byte(TWL_SPICNT_DEVICE_PMIC | TWL_SPICNT_SPEED_1_MHZ, (u8)(reg | (1u << 7)));
    return twl_spi_transfer_last_byte(TWL_SPICNT_DEVICE_PMIC | TWL_SPICNT_SPEED_1_MHZ, 0);
}

/**
 * @brief Writes a PMIC control register through the ARM7 SPI bus.
 */
static void pmic_write(u8 reg, u8 data) {
    twl_spi_transfer_byte(TWL_SPICNT_DEVICE_PMIC | TWL_SPICNT_SPEED_1_MHZ, reg);
    twl_spi_transfer_last_byte(TWL_SPICNT_DEVICE_PMIC | TWL_SPICNT_SPEED_1_MHZ, data);
}

/**
 * @brief Enables or mutes the external speaker amplifier in PMIC control state.
 */
static void pmic_set_amplifier_enable(bool enabled) {
    u8 control = pmic_read(PMIC_REG_CONTROL);
    if (enabled) {
        control |= PMIC_CONTROL_AMP_ENABLE;
        control &= (u8)~PMIC_CONTROL_AMP_MUTE;
    } else {
        control &= (u8)~PMIC_CONTROL_AMP_ENABLE;
        control |= PMIC_CONTROL_AMP_MUTE;
    }
    pmic_write(PMIC_REG_CONTROL, control);
}

/**
 * @brief Adds the small bus-settle delay used between MCU I2C writes.
 */
static void twl_i2c_delay(void) {
    for (volatile u32 i = 0; i < 0x180u; i++) {
        __asm__ volatile("");
    }
}

/**
 * @brief Waits until the TWL I2C controller has finished the current byte.
 */
static bool twl_i2c_wait_busy(void) {
    for (unsigned i = 0; i < I2C_WAIT_TIMEOUT_LOOPS; i++) {
        if ((REG8(TWL_REG_I2C_CNT) & TWL_I2C_CNT_ENABLE) == 0) return true;
    }
    return false;
}

/**
 * @brief Starts an I2C transaction and checks that the addressed device ACKed.
 */
static bool twl_i2c_start(u8 device, bool read) {
    if (!twl_i2c_wait_busy()) return false;
    REG8(TWL_REG_I2C_DATA) = (u8)(device | (read ? 1u : 0u));
    REG8(TWL_REG_I2C_CNT) = TWL_I2C_CNT_FIRST | TWL_I2C_CNT_DIR_WRITE | TWL_I2C_CNT_ENABLE;
    if (!twl_i2c_wait_busy()) return false;
    return (REG8(TWL_REG_I2C_CNT) & TWL_I2C_CNT_ACK) != 0;
}

/**
 * @brief Writes one I2C data byte and returns whether the receiver ACKed it.
 */
static bool twl_i2c_write(u8 data, bool last) {
    REG8(TWL_REG_I2C_DATA) = data;
    REG8(TWL_REG_I2C_CNT) = (u8)((last ? TWL_I2C_CNT_LAST : 0u) | TWL_I2C_CNT_DIR_WRITE | TWL_I2C_CNT_ENABLE);
    if (!twl_i2c_wait_busy()) return false;
    return (REG8(TWL_REG_I2C_CNT) & TWL_I2C_CNT_ACK) != 0;
}

/**
 * @brief Attempts one MCU register write over the TWL I2C controller.
 *
 * The transaction ends with the controller error/last sequence used by
 * Pico-Loader's TWL-to-NTR mode switch.
 */
static bool mcu_try_write_reg(u8 reg, u8 value) {
    if (!twl_i2c_start(I2C_DEVICE_MCU, false)) return false;
    twl_i2c_delay();
    if (!twl_i2c_write(reg, false)) return false;
    twl_i2c_delay();
    if (!twl_i2c_write(value, false)) return false;
    twl_i2c_delay();
    REG8(TWL_REG_I2C_CNT) = TWL_I2C_CNT_LAST | TWL_I2C_CNT_DIR_WRITE | TWL_I2C_CNT_ERROR | TWL_I2C_CNT_ENABLE;
    return twl_i2c_wait_busy();
}

/**
 * @brief Writes an MCU register, retrying short I2C failures before handover.
 */
static bool mcu_write_reg(u8 reg, u8 value) {
    for (unsigned i = 0; i < 8; i++) {
        if (mcu_try_write_reg(reg, value)) return true;
        REG8(TWL_REG_I2C_CNT) = TWL_I2C_CNT_LAST | TWL_I2C_CNT_DIR_WRITE | TWL_I2C_CNT_ERROR | TWL_I2C_CNT_ENABLE;
    }
    return false;
}

/**
 * @brief Checks Pico-Loader's DS-mode codec volume exception list.
 *
 * The game-code set is derived from Pico-Loader
 * `DSMode::ShouldUseVolumeFix()`. Only the first three game-code bytes are
 * compared so regional variants of the same title use the same speaker gain.
 */
static bool should_use_volume_fix(u32 game_code) {
    switch (game_code & 0x00ffffffu) {
        case GAMECODE3('A', '3', 'T'):
        case GAMECODE3('A', '4', 'U'):
        case GAMECODE3('A', '5', 'H'):
        case GAMECODE3('A', '5', 'I'):
        case GAMECODE3('A', '8', 'N'):
        case GAMECODE3('A', 'B', 'J'):
        case GAMECODE3('A', 'B', 'N'):
        case GAMECODE3('A', 'B', 'X'):
        case GAMECODE3('A', 'C', 'C'):
        case GAMECODE3('A', 'C', 'L'):
        case GAMECODE3('A', 'C', 'Z'):
        case GAMECODE3('A', 'D', 'A'):
        case GAMECODE3('A', 'H', 'D'):
        case GAMECODE3('A', 'J', 'U'):
        case GAMECODE3('A', 'K', 'A'):
        case GAMECODE3('A', 'K', 'E'):
        case GAMECODE3('A', 'L', 'H'):
        case GAMECODE3('A', 'M', 'H'):
        case GAMECODE3('A', 'N', '9'):
        case GAMECODE3('A', 'N', 'R'):
        case GAMECODE3('A', 'P', 'A'):
        case GAMECODE3('A', 'P', 'Y'):
        case GAMECODE3('A', 'R', 'T'):
        case GAMECODE3('A', 'V', '2'):
        case GAMECODE3('A', 'V', '3'):
        case GAMECODE3('A', 'V', '4'):
        case GAMECODE3('A', 'V', '5'):
        case GAMECODE3('A', 'V', '6'):
        case GAMECODE3('A', 'V', 'I'):
        case GAMECODE3('A', 'V', 'T'):
        case GAMECODE3('A', 'W', 'H'):
        case GAMECODE3('A', 'W', 'Y'):
        case GAMECODE3('A', 'X', 'B'):
        case GAMECODE3('A', 'X', 'J'):
        case GAMECODE3('A', 'Y', '7'):
        case GAMECODE3('A', 'Y', 'K'):
        case GAMECODE3('A', 'Z', 'W'):
        case GAMECODE3('C', 'P', 'U'):
        case GAMECODE3('Y', 'B', '2'):
        case GAMECODE3('Y', 'B', '3'):
        case GAMECODE3('Y', 'B', 'O'):
        case GAMECODE3('Y', 'C', 'H'):
        case GAMECODE3('Y', 'C', 'Q'):
        case GAMECODE3('Y', 'F', 'E'):
        case GAMECODE3('Y', 'F', 'S'):
        case GAMECODE3('Y', 'G', '8'):
        case GAMECODE3('Y', 'G', 'D'):
        case GAMECODE3('Y', 'K', 'R'):
        case GAMECODE3('Y', 'N', 'Z'):
        case GAMECODE3('Y', 'O', '9'):
        case GAMECODE3('Y', 'O', 'N'):
        case GAMECODE3('Y', 'R', 'M'):
        case GAMECODE3('Y', 'T', '3'):
        case GAMECODE3('Y', 'W', '2'):
        case GAMECODE3('Y', 'Y', 'K'):
            return true;
        default:
            return false;
    }
}

typedef enum {
    CODEC_OP_PAGE,
    CODEC_OP_READ,
    CODEC_OP_WRITE,
    CODEC_OP_WRITE_VOLUME,
} CodecOpType;

/**
 * @brief One compact step in the TWL codec reinitialization script.
 *
 * `CODEC_OP_WRITE_VOLUME` defers the value until runtime because
 * Pico-Loader applies a title-specific speaker-gain exception for a small set
 * of DS-mode games.
 */
typedef struct {
    CodecOpType type;
    u8 reg;
    u8 value;
} CodecOp;

/**
 * @brief Replays a bounded codec register script on the ARM7 SPI bus.
 *
 * The script is derived from Pico-Loader `DSMode::SwitchCodecToDSMode()`;
 * table encoding keeps the register sequence auditable while preserving the
 * exact write order needed by TWL audio hardware.
 */
static void codec_run_script(const CodecOp *script, unsigned count, u8 volume_fix_value) {
    for (unsigned i = 0; i < count; i++) {
        const CodecOp *op = &script[i];
        switch (op->type) {
            case CODEC_OP_PAGE:
                codec_set_page(op->reg);
                break;
            case CODEC_OP_READ:
                (void)codec_read(op->reg);
                break;
            case CODEC_OP_WRITE:
                codec_write(op->reg, op->value);
                break;
            case CODEC_OP_WRITE_VOLUME:
                codec_write(op->reg, volume_fix_value);
                break;
        }
    }
}

/*
 * DS-mode codec bring-up script for TWL hardware before jumping to NTR code.
 * Register values are derived from Pico-Loader `DSMode::SwitchCodecToDSMode()`.
 */
static const CodecOp s_codec_ds_mode_script[] = {
    { CODEC_OP_PAGE, CODEC_PAGE_0, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_GPI3_PIN_CONTROL, 0x00 },
    { CODEC_OP_READ, CODEC_REG_PAGE0_ADC_DIGITAL_MIC, 0 },
    { CODEC_OP_PAGE, CODEC_PAGE_3, 0 },
    { CODEC_OP_READ, CODEC_REG_PAGE3_SAR_ADC_CONTROL_1, 0 },
    { CODEC_OP_PAGE, CODEC_PAGE_0, 0 },
    { CODEC_OP_READ, CODEC_REG_PAGE0_DAC_DATA_PATH_SETUP, 0 },
    { CODEC_OP_PAGE, CODEC_PAGE_1, 0 },
    { CODEC_OP_READ, CODEC_REG_PAGE1_HPL_DRIVER, 0 },
    { CODEC_OP_READ, CODEC_REG_PAGE1_SPL_DRIVER, 0 },
    { CODEC_OP_READ, CODEC_REG_PAGE1_MICBIAS, 0 },
    { CODEC_OP_PAGE, CODEC_PAGE_0, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_DIGITAL_VOLUME_CONTROL_FINE_ADJUST, 0x80 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_VOLUME_CONTROL, 0x0C },
    { CODEC_OP_PAGE, CODEC_PAGE_1, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_LEFT_ANALOG_VOL_TO_HPL, 0xFF },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_RIGHT_ANALOG_VOL_TO_HPR, 0xFF },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_LEFT_ANALOG_VOL_TO_SPL, 0x7F },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_RIGHT_ANALOG_VOL_TO_SPR, 0x7F },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_HPL_DRIVER, 0x4A },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_HPR_DRIVER, 0x4A },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_SPL_DRIVER, 0x10 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_SPR_DRIVER, 0x10 },
    { CODEC_OP_PAGE, CODEC_PAGE_0, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_DIGITAL_MIC, 0x00 },
    { CODEC_OP_PAGE, CODEC_PAGE_3, 0 },
    { CODEC_OP_READ, CODEC_REG_PAGE3_SAR_ADC_CONTROL_1, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE3_SAR_ADC_CONTROL_1, 0x98 },
    { CODEC_OP_PAGE, CODEC_PAGE_1, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_DAC_L_DAC_R_OUTPUT_MIXER_ROUTING, 0x00 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_HEADPHONE_DRIVERS, 0x14 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_CLASS_D_SPEAKER_AMPLIFIER, 0x14 },
    { CODEC_OP_PAGE, CODEC_PAGE_0, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_DATA_PATH_SETUP, 0x00 },
    { CODEC_OP_READ, CODEC_REG_PAGE0_DAC_NDAC_VAL, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_PLL_P_R, 0x00 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_NDAC_VAL, 0x01 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_MDAC_VAL, 0x02 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_NADC_VAL, 0x01 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_MADC_VAL, 0x02 },
    { CODEC_OP_PAGE, CODEC_PAGE_1, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_MICBIAS, 0x00 },
    { CODEC_OP_PAGE, CODEC_PAGE_0, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_GPI3_PIN_CONTROL, 0x60 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_RESET, 0x01 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_GPI1_GPI2_PIN_CONTROL, 0x66 },
    { CODEC_OP_PAGE, CODEC_PAGE_1, 0 },
    { CODEC_OP_READ, CODEC_REG_PAGE1_CLASS_D_SPEAKER_AMPLIFIER, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_CLASS_D_SPEAKER_AMPLIFIER, 0x10 },
    { CODEC_OP_PAGE, CODEC_PAGE_0, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_CLOCK_GEN_MUXING, 0x00 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_NADC_VAL, 0x81 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_MADC_VAL, 0x82 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_DIGITAL_MIC, 0x82 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_DIGITAL_MIC, 0x00 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_CLOCK_GEN_MUXING, 0x03 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_PLL_P_R, 0xA1 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_PLL_J, 0x15 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_NDAC_VAL, 0x87 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_MDAC_VAL, 0x83 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_NADC_VAL, 0x87 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_MADC_VAL, 0x83 },
    { CODEC_OP_PAGE, CODEC_PAGE_3, 0 },
    { CODEC_OP_READ, CODEC_REG_PAGE3_SCAN_MODE_TIMER_CLOCK, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE3_SCAN_MODE_TIMER_CLOCK, 0x08 },
    { CODEC_OP_PAGE, CODEC_PAGE_4, 0 },
    { CODEC_OP_WRITE, 0x08, 0x7F },
    { CODEC_OP_WRITE, 0x09, 0xE1 },
    { CODEC_OP_WRITE, 0x0A, 0x80 },
    { CODEC_OP_WRITE, 0x0B, 0x1F },
    { CODEC_OP_WRITE, 0x0C, 0x7F },
    { CODEC_OP_WRITE, 0x0D, 0xC1 },
    { CODEC_OP_PAGE, CODEC_PAGE_0, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_LEFT_VOLUME_CONTROL, 0x08 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_RIGHT_VOLUME_CONTROL, 0x08 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_GPI3_PIN_CONTROL, 0x00 },
    { CODEC_OP_PAGE, CODEC_PAGE_4, 0 },
    { CODEC_OP_WRITE, 0x08, 0x7F },
    { CODEC_OP_WRITE, 0x09, 0xE1 },
    { CODEC_OP_WRITE, 0x0A, 0x80 },
    { CODEC_OP_WRITE, 0x0B, 0x1F },
    { CODEC_OP_WRITE, 0x0C, 0x7F },
    { CODEC_OP_WRITE, 0x0D, 0xC1 },
    { CODEC_OP_PAGE, CODEC_PAGE_1, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_MIC_PGA, 0x2B },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_DELTA_SIGMA_MONO_ADC_CHANNEL_FINE_GAIN_INPUT_SELECTION_FOR_P_TERMINAL, 0x40 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_ADC_INPUT_SELECTION_FOR_M_TERMINAL, 0x40 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_INPUT_CM_SETTINGS, 0x60 },
    { CODEC_OP_PAGE, CODEC_PAGE_0, 0 },
    { CODEC_OP_READ, CODEC_REG_PAGE0_VOL_MICDET_PIN_SAR_ADC_VOLUME_CONTROL, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_VOL_MICDET_PIN_SAR_ADC_VOLUME_CONTROL, 0x02 },
    { CODEC_OP_READ, CODEC_REG_PAGE0_VOL_MICDET_PIN_SAR_ADC_VOLUME_CONTROL, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_VOL_MICDET_PIN_SAR_ADC_VOLUME_CONTROL, 0x10 },
    { CODEC_OP_READ, CODEC_REG_PAGE0_VOL_MICDET_PIN_SAR_ADC_VOLUME_CONTROL, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_VOL_MICDET_PIN_SAR_ADC_VOLUME_CONTROL, 0x40 },
    { CODEC_OP_PAGE, CODEC_PAGE_1, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_HP_OUTPUT_DRIVERS_POP_REMOVAL_SETTINGS, 0x20 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_OUTPUT_DRIVER_PGA_RAMP_DOWN_PERIOD_CONTROL, 0xF0 },
    { CODEC_OP_PAGE, CODEC_PAGE_0, 0 },
    { CODEC_OP_READ, CODEC_REG_PAGE0_ADC_DIGITAL_MIC, 0 },
    { CODEC_OP_READ, CODEC_REG_PAGE0_DAC_DATA_PATH_SETUP, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_DATA_PATH_SETUP, 0xD4 },
    { CODEC_OP_PAGE, CODEC_PAGE_1, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_DAC_L_DAC_R_OUTPUT_MIXER_ROUTING, 0x44 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_HEADPHONE_DRIVERS, 0xD4 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_HPL_DRIVER, 0x4E },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_HPR_DRIVER, 0x4E },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_LEFT_ANALOG_VOL_TO_HPL, 0x9E },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_RIGHT_ANALOG_VOL_TO_HPR, 0x9E },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_CLASS_D_SPEAKER_AMPLIFIER, 0xD4 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_SPL_DRIVER, 0x14 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_SPR_DRIVER, 0x14 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_LEFT_ANALOG_VOL_TO_SPL, 0xA7 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_RIGHT_ANALOG_VOL_TO_SPR, 0xA7 },
    { CODEC_OP_PAGE, CODEC_PAGE_0, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_VOLUME_CONTROL, 0x00 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_GPI3_PIN_CONTROL, 0x60 },
    { CODEC_OP_PAGE, CODEC_PAGE_1, 0 },
    { CODEC_OP_WRITE_VOLUME, CODEC_REG_PAGE1_LEFT_ANALOG_VOL_TO_SPL, 0 },
    { CODEC_OP_WRITE_VOLUME, CODEC_REG_PAGE1_RIGHT_ANALOG_VOL_TO_SPR, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_MICBIAS, 0x03 },
    { CODEC_OP_PAGE, CODEC_PAGE_3, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE3_SAR_ADC_CONTROL_2, 0x00 },
    { CODEC_OP_PAGE, CODEC_PAGE_1, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_HP_OUTPUT_DRIVERS_POP_REMOVAL_SETTINGS, 0x20 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_OUTPUT_DRIVER_PGA_RAMP_DOWN_PERIOD_CONTROL, 0xF0 },
    { CODEC_OP_READ, CODEC_REG_PAGE1_OUTPUT_DRIVER_PGA_RAMP_DOWN_PERIOD_CONTROL, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_OUTPUT_DRIVER_PGA_RAMP_DOWN_PERIOD_CONTROL, 0x00 },
    { CODEC_OP_PAGE, CODEC_PAGE_0, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_DIGITAL_VOLUME_CONTROL_FINE_ADJUST, 0x80 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_DIGITAL_MIC, 0x00 },
    { CODEC_OP_WRITE, 0x03, 0x44 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_DOSR_VAL_MSB, 0x00 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_DOSR_VAL_LSB, 0x80 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_IDAC_VAL, 0x80 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_MINIDSP_INTERPOLATION, 0x08 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_AOSR_VAL, 0x80 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_IDAC_VAL, 0x80 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_MINIDSP_DECIMATION, 0x04 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_CLKOUT_M_VAL, 0x01 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_BCLK_N_VAL, 0x01 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_FLAG_REGISTER, 0x80 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_GPIO1_IN_OUT_PIN_CONTROL, 0x34 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_GPIO2_IN_OUT_PIN_CONTROL, 0x32 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_SDOUT_OUT_PIN_CONTROL, 0x12 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_SDIN_IN_PIN_CONTROL, 0x03 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_MISO_OUT_PIN_CONTROL, 0x02 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_SCLK_IN_PIN_CONTROL, 0x03 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DAC_INSTRUCTION_SET, 0x19 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_ADC_INSTRUCTION_SET, 0x05 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DRC_CONTROL_1, 0x0F },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_DRC_CONTROL_2, 0x38 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_BEEP_LENGTH_MSB, 0x00 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_BEEP_LENGTH_MID, 0x00 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_BEEP_LENGTH_LSB, 0xEE },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_BEEP_SIN_MSB, 0x10 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_BEEP_SIN_LSB, 0xD8 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_BEEP_COS_MSB, 0x7E },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_BEEP_COS_LSB, 0xE3 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_AGC_MAXIMUM_GAIN, 0x7F },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_VOL_MICDET_PIN_SAR_ADC_VOLUME_CONTROL, 0xD2 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE0_VOL_MICDET_PIN_GAIN, 0x2C },
    { CODEC_OP_PAGE, CODEC_PAGE_1, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_OUTPUT_DRIVER_PGA_RAMP_DOWN_PERIOD_CONTROL, 0x70 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE1_HP_DRIVER_CONTROL, 0x20 },
    { CODEC_OP_PAGE, CODEC_PAGE_3, 0 },
    { CODEC_OP_READ, CODEC_REG_PAGE3_SAR_ADC_CONTROL_1, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE3_SAR_ADC_CONTROL_1, 0x98 },
    { CODEC_OP_PAGE, CODEC_PAGE_255, 0 },
    { CODEC_OP_WRITE, CODEC_REG_PAGE255_BACKWARDS_COMPATIBILITY_MODE, CODEC_PAGE255_BACKWARDS_COMPATIBILITY_MODE_ON },
};

/**
 * @brief Programs TWL codec and PMIC state so an NTR child can use DS audio.
 *
 * The sequence is the local C-table form of Pico-Loader's DS-mode codec switch;
 * it only varies the speaker volume register through `should_use_volume_fix()`.
 */
static void switch_codec_to_ds_mode(u32 game_code) {
    u8 vol_level = should_use_volume_fix(game_code) ? 0xACu : 0xA7u;
    codec_run_script(s_codec_ds_mode_script, ARRAY_COUNT(s_codec_ds_mode_script), vol_level);
    pmic_set_amplifier_enable(true);
}

/**
 * @brief Enables the TWL I2S bridge and codec settings expected by NTR code.
 *
 * The register order follows Pico-Loader `DSMode::SwitchToDSTouchAndSoundMode()`
 * so touch/audio hardware is ready before ARM9 releases the downloaded child.
 */
static void switch_to_ds_touch_and_sound_mode(u32 game_code) {
    REG16(REG_I2SCNT) = I2SCNT_MIX_RATIO_DSP_0_NITRO_8 | I2SCNT_FREQUENCY_32728_HZ;
    codec_set_page(CODEC_PAGE_0);
    codec_write(CODEC_REG_PAGE0_DAC_NDAC_VAL, 0x87);
    codec_write(CODEC_REG_PAGE0_ADC_NADC_VAL, 0x87);
    codec_write(CODEC_REG_PAGE0_PLL_J, 21);
    REG16(REG_I2SCNT) |= I2SCNT_ENABLE;
    switch_codec_to_ds_mode(game_code);
    REG16(TWL_REG_SOUNDCNT) = SOUNDCNT_MASTER_ENABLE | SOUNDCNT_MASTER_VOLUME(0x7F);
}

/**
 * @brief Prepares TWL ARM7 sound, touch and MCU mode before launching NTR code.
 *
 * This is the ARM7 half of the Pico-Loader-derived TWL-to-NTR compatibility
 * path. It powers audio, reinitializes codec state for DS mode, and switches
 * the MCU mode register back to the NTR setting; MCU write failure is ignored
 * because boot handover cannot surface a recoverable error at this point.
 */
void arm7_prepare_ds_mode_for_boot(u32 game_code) {
    REG16(TWL_REG_POWCNT) |= POWCNT_SOUND_ENABLE;
    switch_to_ds_touch_and_sound_mode(game_code);
    (void)mcu_write_reg(MCU_REG_MODE, 0);
}

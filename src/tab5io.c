/*
 * tab5io.c -- the Tab5's internal I2C bus and its two IO expanders. See
 * tab5io.h.
 *
 * Carved out of defeatist-music-player-for-m5tab5's player.c, where it
 * was never a module of its own; the import commit records which lines.
 * The comments are as they were there.
 *
 * SPDX-License-Identifier: MIT
 */

#include "tab5io.h"

#include <stdbool.h>
#include <stddef.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "tab5io";

/* ---- I2C bus: same pins the display example uses ---- */
#define BSP_I2C_NUM             (0)
#define BSP_I2C_SDA             (GPIO_NUM_31)
#define BSP_I2C_SCL             (GPIO_NUM_32)
#define I2C_TIMEOUT_MS          (1000)

/* ---- PI4IOE5V6416 expander 1: P1 = SPK_EN ---- */
/* PI4IOE_ADDR_1 and the PI4IOE_REG_* registers: tab5io.h */
#define PI4IOE1_IO_DIR          (0x7F)
#define PI4IOE1_OUT_SET         (0x76)  /* P1 SPK_EN, P2 EXT5V, P4 LCD_RST, P5 TP_RST, P6 CAM_RST */

/* PI4IOE_ADDR_2: tab5io.h */
#define PI4IOE2_IO_DIR          (0xB9)
#define CHG_EN_BIT              (1u << 7)   /* 6052: P7, the charger's enable */

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_exp1, s_exp2;

/* ------------------------------------------------------------------ */
/* I2C helpers                                                         */
/* ------------------------------------------------------------------ */

static esp_err_t reg_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = BSP_I2C_NUM,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c_bus);
}

static esp_err_t add_dev(uint8_t addr, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(s_i2c_bus, &cfg, out);
}

/*
 * Expander 1 carries P1 SPK_EN, P2 EXT5V_EN, P4 LCD_RST, P5 TP_RST and
 * P6 CAM_RST. PI4IOE1_OUT_SET (0x76) drives all of them high in one
 * write, so the amplifier and the panel come out of reset together.
 *
 * Both OUT_SET (0x05) and OUT_H_IM (0x07) matter: the expander parks
 * pins high-impedance after reset, so writing the value alone leaves
 * them floating -- amp off, panel dead, no error from either.
 */
/*
 * The first write of the boot, retried.
 *
 * A panic anywhere in the program resets the P4 without releasing the
 * bus, and a slave interrupted mid-byte can sit on SDA into the next
 * boot. The first transmit then NACKs -- which the v5 I2C master driver
 * reports as ESP_ERR_INVALID_STATE -- and since app_main() checks this
 * function, the recovery boot aborts before the panel is up. One crash
 * becomes an unrecoverable loop with nothing on screen to say why.
 *
 * Three attempts 20 ms apart. A device that is genuinely absent still
 * fails, 60 ms later; a bus that just needs a moment gets it.
 */
static esp_err_t reg_write_retry(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 3; i++) {
        err = reg_write(dev, reg, val);
        if (err == ESP_OK) {
            if (i) ESP_LOGW(TAG, "expander answered on attempt %d", i + 1);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return err;
}

static esp_err_t io_expanders_init(void)
{

    ESP_RETURN_ON_ERROR(add_dev(PI4IOE_ADDR_1, &s_exp1), TAG, "expander 0x43 absent");
    ESP_RETURN_ON_ERROR(reg_write_retry(s_exp1, PI4IOE_REG_CHIP_RESET, 0xFF), TAG, "reset 1");
    ESP_RETURN_ON_ERROR(reg_write(s_exp1, PI4IOE_REG_IO_DIR, PI4IOE1_IO_DIR), TAG, "dir 1");
    ESP_RETURN_ON_ERROR(reg_write(s_exp1, PI4IOE_REG_OUT_HIGH_Z, 0x00), TAG, "high-z 1");
    ESP_RETURN_ON_ERROR(reg_write(s_exp1, PI4IOE_REG_PULL_SEL, 0x7F), TAG, "pull sel 1");
    ESP_RETURN_ON_ERROR(reg_write(s_exp1, PI4IOE_REG_PULL_EN, 0x7F), TAG, "pull en 1");
    ESP_RETURN_ON_ERROR(reg_write(s_exp1, PI4IOE_REG_OUT_SET, PI4IOE1_OUT_SET), TAG, "out 1");
    ESP_LOGI(TAG, "SPK_EN and LCD_RST released (expander 0x%02X)", PI4IOE_ADDR_1);

    ESP_RETURN_ON_ERROR(add_dev(PI4IOE_ADDR_2, &s_exp2), TAG, "expander 0x44 absent");
    ESP_RETURN_ON_ERROR(reg_write(s_exp2, PI4IOE_REG_CHIP_RESET, 0xFF), TAG, "reset 2");
    ESP_RETURN_ON_ERROR(reg_write(s_exp2, PI4IOE_REG_IO_DIR, PI4IOE2_IO_DIR), TAG, "dir 2");

    /*
     * 6052: CHG_EN is P7 here, and the chip reset above leaves it in
     * high-Z, where the charger stays off: on USB-C the board ran but
     * the pack never charged. M5Unified's Tab5 init drives it high and
     * setBatteryCharge() is digitalWrite(7, ...) on this expander.
     * Direction, out of high-Z, drive -- the order wifi.c and usbhost.c
     * use. Not fatal: a board that cannot charge can still play.
     */
    {
        static const struct { uint8_t reg; bool set; } chg[] = {
            { PI4IOE_REG_IO_DIR,     true  },   /* output        */
            { PI4IOE_REG_OUT_HIGH_Z, false },   /* out of high-Z */
            { PI4IOE_REG_OUT_SET,    true  },   /* drive high    */
        };
        esp_err_t err = ESP_OK;
        for (size_t i = 0; i < sizeof(chg) / sizeof(chg[0]) && err == ESP_OK; i++) {
            uint8_t reg = chg[i].reg, val = 0;
            err = i2c_master_transmit_receive(s_exp2, &reg, 1, &val, 1, I2C_TIMEOUT_MS);
            if (err != ESP_OK) break;
            val = chg[i].set ? (uint8_t)(val | CHG_EN_BIT) : (uint8_t)(val & (uint8_t)~CHG_EN_BIT);
            err = reg_write(s_exp2, reg, val);
        }
        if (err == ESP_OK) ESP_LOGI(TAG, "CHG_EN driven high (expander 0x%02X P7)", PI4IOE_ADDR_2);
        else ESP_LOGW(TAG, "CHG_EN not set: %s -- the pack will not charge", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(100));     /* panel out of reset before first command */
    return ESP_OK;
}

esp_err_t tab5io_init(void)
{
    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "i2c bus");
    return io_expanders_init();
}

i2c_master_bus_handle_t tab5io_bus(void)  { return s_i2c_bus; }
i2c_master_dev_handle_t tab5io_exp1(void) { return s_exp1; }
i2c_master_dev_handle_t tab5io_exp2(void) { return s_exp2; }

/*
 * tab5io.h -- the Tab5's internal I2C bus and its two IO expanders.
 *
 * The bus is I2C0 on GPIO31 (SDA) and GPIO32 (SCL). Most of the board
 * hangs off it -- the codecs, touch, the battery monitor, the clock --
 * and so do two PI4IOE5V6416 expanders that carry lines the P4 has no
 * GPIO for:
 *
 *   0x43  P1 SPK_EN, P2 EXT5V_EN, P4 LCD_RST, P5 TP_RST, P6 CAM_RST
 *   0x44  P0 WLAN_PWR_EN, P3 USB5V_EN, P4 PWROFF_PULSE, P7 CHG_EN
 *
 * tab5io_init() brings up the bus, chip-resets both expanders, drives
 * every 0x43 line above high in one write (so the amplifier, the panel
 * and the touch controller all come out of reset together), sets 0x44's
 * directions, drives CHG_EN so the pack charges, and waits 100 ms for
 * the panel. Everything that needs the board starts after it.
 *
 * The 0x44 lines other than CHG_EN are each driven by the module that
 * owns them (usbhost.c, feckless-network's wifi.c, the application's
 * power-off), read-modify-write so they leave each other alone. The
 * register numbers are here for them.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PI4IOE_ADDR_1           (0x43)
#define PI4IOE_ADDR_2           (0x44)

#define PI4IOE_REG_CHIP_RESET   (0x01)
#define PI4IOE_REG_IO_DIR       (0x03)
#define PI4IOE_REG_OUT_SET      (0x05)
#define PI4IOE_REG_OUT_HIGH_Z   (0x07)
#define PI4IOE_REG_PULL_EN      (0x0B)
#define PI4IOE_REG_PULL_SEL     (0x0D)

/* Once, first thing in app_main(). The first write is retried, because a
 * panic can leave a slave holding SDA into the next boot. */
esp_err_t tab5io_init(void);

/* The bus, for adding devices to. NULL before tab5io_init(). */
i2c_master_bus_handle_t tab5io_bus(void);

/* The expanders at 0x43 and 0x44. NULL before tab5io_init(). */
i2c_master_dev_handle_t tab5io_exp1(void);
i2c_master_dev_handle_t tab5io_exp2(void);

#ifdef __cplusplus
}
#endif

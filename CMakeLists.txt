# feckless-drivers-for-m5tab5
#
# usb_host_uac is REQUIRED BY NAME and not listed in idf_component.yml:
# the player vendors it with a descriptor-parser fix the registry does not
# have (its 5026), and a project's own components/ wins over anything a
# manifest would pull. An application without its own copy adds
# espressif/usb_host_uac to its manifest. `usb` is IDF's on 5.x and
# espressif/usb on 6.x, brought in by usb_host_uac's manifest.
#
# esp_lcd_touch is PUBLIC because touch.h hands out its types.
#
# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/tab5io.c"
         "src/usbhost.c" "src/uac.c" "src/hid.c" "src/battery.c"
         "src/rtc8130.c" "src/touch.c"
         "src/audio_out.c" "src/polyrsp.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES esp_driver_i2c esp_driver_gpio esp_lcd_touch usb
    PRIV_REQUIRES esp_timer esp_driver_i2s usb_host_uac
             esp_lcd_touch_gt911 esp_lcd_touch_st7123
)

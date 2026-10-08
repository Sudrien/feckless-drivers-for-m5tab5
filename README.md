# Feckless Drivers for Tab5

Board drivers for the M5Stack Tab5, as an ESP-IDF component. Pulled out
of
[Defeatist Music Player for M5Tab5](https://github.com/Sudrien/defeatist-music-player-for-m5tab5),
where they were written and where they run; the long comments in the
source are that project's record of why each thing is the way it is,
and they came along unchanged.

| Header | What it is |
|---|---|
| `tab5io.h` | The internal I2C bus and the two PI4IOE5V6416 expanders: reset, directions, SPK_EN, LCD_RST, TP_RST, CHG_EN. First thing at boot |
| `audio_out.h` | I2S, the ES8388 (playback) and the ES7210 (the two array microphones, and a headset's on the jack), speaker or headphones or USB audio, volume, and capture |
| `polyrsp.h` | A polyphase resampler, for USB audio devices that will not take the stream's rate |
| `usbhost.h` | The USB-A port: USB5V_EN on the IO expander, the host stack, its task, and the class drivers registered on it |
| `uac.h` | USB audio output (and a headset's microphone) through usb_host_uac |
| `hid.h` | A USB HID remote: consumer-control keys from a headset or keyboard |
| `battery.h` | The pack's voltage and current from the INA226, and a trace armed around events that load the supply |
| `rtc8130.h` | The RX8130CE real-time clock |
| `touch.h` | The touch controller, GT911 (rev 1) or ST7123 (rev 2), probed |
| `micpcm.h` | Microphone samples into int32 frames. Header-only and host-tested |

## Using it

```yaml
# main/idf_component.yml
dependencies:
  feckless_drivers:
    git: https://github.com/Sudrien/feckless-drivers-for-m5tab5.git
    version: "v0.3.0"
```

and `feckless_drivers` in `main`'s `REQUIRES`.

The USB host is the one shared piece: everything else on the port --
mass storage, Ethernet, audio, HID -- registers a class with it before
it starts.

```c
tab5io_init();                               /* bus, expanders, LCD_RST */
audio_out_init(tab5io_bus(), tab5io_exp1(), 48000);
usbhost_init(tab5io_exp2());                 /* the PI4IOE at 0x44 */
usbhost_set_config_select(ethcfg_select);    /* optional; see below */
storage_init(&storage_usb);                  /* registers "msc" */
uac_init();
hid_init(on_button);
ethernet_init();                             /* registers "eth" */
usbhost_start();
```

`usbhost_set_config_select()` lets the application pick a device's USB
configuration before it is set. feckless-network-handler's
`ethcfg_select()` is the function to pass if you use its Ethernet: it
moves Realtek adapters to their CDC-ECM configuration.

## What the application must do

**Supply `usb_host_uac`.** Required by name rather than pulled from the
registry, because the player builds against a vendored 1.5.0 with a
descriptor-parser fix. Anything else adds `espressif/usb_host_uac` to
its own manifest.

**Set the USB host's Kconfig in its own `sdkconfig.defaults`.** The
player's sets, among others:

```
CONFIG_USB_HOST_HUBS_SUPPORTED=y
CONFIG_USB_HOST_HUB_MULTI_LEVEL=y
CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=y   # needed for usbhost_set_config_select()
CONFIG_USB_HOST_DWC_DMA_CAP_MEMORY_IN_PSRAM=y
```

That file explains each of them; copy from it rather than from here, and
`rm sdkconfig` before the next build.

**Call `tab5io_init()` first.** Everything here, and the display in
feckless-graphics-handler, needs the bus and the expanders' reset lines
released. The lines on 0x44 other than CHG_EN are each driven by their
own module (`usbhost.c`'s USB5V_EN, feckless-network's WLAN_PWR_EN); the
power-off pulse is still the application's.

**Capture borrows playback's clocks.** The microphones and the ES8388
share one I2S port, and `audio_out_capture_begin()` re-clocks the
playback channel rather than opening a second port. A program that only
records still calls `audio_out_init()`; see `audio_out.h`.

## Tests

```
make -C test
```

## Licence

MIT. See `LICENSE`.

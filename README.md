# Feckless Drivers for Tab5

Board drivers for the M5Stack Tab5, as an ESP-IDF component. Pulled out
of
[Defeatist Music Player for M5Tab5](https://github.com/Sudrien/defeatist-music-player-for-m5tab5),
where they were written and where they run; the long comments in the
source are that project's record of why each thing is the way it is,
and they came along unchanged.

| Header | What it is |
|---|---|
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
    git: https://github.com/Sudrien/feckless-drivers-for-tab5.git
    version: "v0.1.0"
```

and `feckless_drivers` in `main`'s `REQUIRES`.

The USB host is the one shared piece: everything else on the port --
mass storage, Ethernet, audio, HID -- registers a class with it before
it starts.

```c
usbhost_init(exp2);                          /* the PI4IOE at 0x44 */
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

**Own the IO expanders.** `usbhost.c` drives USB5V_EN on the expander
at 0x44 itself, but resetting the expanders and setting their directions
is still the application's (`io_expanders_init()` in the player).

## Tests

```
make -C test
```

## Licence

MIT. See `LICENSE`.

# rsPager Dual-Boot

The dual-boot layout lets one flash carry three apps on the T-Pager's 16MB
flash, with a boot-time chooser:

| Partition  | Slot  | Offset   | Size    | Contents |
|------------|-------|----------|---------|----------|
| launcher   | ota_0 | 0x10000  | 1MB     | Boot chooser UI |
| standalone | ota_1 | 0x110000 | 4MB     | Full Ratpager messenger |
| rnode      | ota_2 | 0x510000 | 3MB     | RNode (KISS over USB CDC + BLE) |
| littlefs   | —     | 0x810000 | ~7.9MB  | User data (unchanged from single-app layout) |

The littlefs and coredump regions are byte-identical to the legacy
single-app layout (`partitions_16MB.csv`), so migrating to the dual image
preserves identity, messages, contacts, and settings.

## Boot flow

- The ESP32 ROM bootloader reads `otadata` and starts the selected slot.
- The launcher (ota_0) shows Standalone/RNode, remembers the last choice in
  NVS (`rslaunch/last`), auto-boots it after 7 seconds, and writes the choice
  via `esp_ota_set_boot_partition()`.
- Both Standalone and RNode re-arm the launcher at every boot
  (`rs_pager::returnToLauncherNextBoot()`), so any reset returns to the chooser.
  On the legacy single-app layout that call self-selects ota_0 and is a no-op.

## Launcher controls

Scroll wheel up/down + click, or keys: `W`/`S` select, `Enter` boot,
`Q` = Standalone now, `A` = RNode now. Any input cancels auto-boot.
The T-Pager has no touch panel.

## RNode mode

The vendored RNode firmware (`vendor/rnode_firmware`, `make firmware-tpager`)
self-provisions EEPROM on first boot (915 MHz defaults) and enables BLE
pairing on first run when no bonds exist. The display shows the standard
RNode UI (node address, status) and the BLE pairing PIN during pairing.
Reset to return to the launcher.

## Build

```bash
make prep-tpager    # once: arduino-cli esp32 core + libs
make package        # dist/: rspager-full.zip, rspager-standalone.zip,
                    #        rspager-rnode.zip, rspager-*-app.bin
make flash port=/dev/cu.usbmodemXXXX   # flash full image
```

`rspager-full.bin` flashes at offset 0x0 and contains all three apps.
`rspager-standalone.zip` repackages the standalone factory image with the old
single-app partition table and no launcher. The `*-app.bin` files are bare app
images for external launchers or manual OTA-slot flashing.

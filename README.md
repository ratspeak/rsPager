<div align="center">

# [rsPager](https://ratspeak.org/)

**Dual-mode Ratspeak firmware for the LilyGo T-Pager.**

[![Status](https://img.shields.io/badge/status-beta-yellow.svg)](#install)
[![Model](https://img.shields.io/badge/model-LilyGo%20T--Pager-success.svg)](https://wiki.lilygo.cc/products/t-lora-series/t-lora-pager/)
[![Version](https://img.shields.io/badge/version-1.0.3-success.svg)](https://github.com/ratspeak/rsPager/releases)
[![License](https://img.shields.io/badge/license-AGPL--3.0--or--later-blue.svg)](LICENSE)

[Ratspeak](https://github.com/ratspeak/Ratspeak) |
[Docs](https://ratspeak.org/docs.html) |
[Downloads](https://ratspeak.org/download.html) |
[rsReticulum](https://github.com/ratspeak/rsReticulum)

</div>

---

rsPager beta v1.0.3 turns a
[LilyGo T-Pager / T-LoRa-Pager](https://wiki.lilygo.cc/products/t-lora-series/t-lora-pager/)
into a two-mode Reticulum handheld. Standalone mode is an on-device
Ratspeak/LXMF messenger. RNode mode makes the T-Pager a host-controlled radio
for Ratspeak, Sideband, or another Reticulum client over BLE or USB serial.

## Install

Use the Ratspeak web flasher:
[ratspeak.org/download.html](https://ratspeak.org/download.html).

Put the T-Pager in download mode with the BOOT button, connect USB-C, then
flash `rspager-full`. The standalone-only and RNode-only images are release
artifacts for launcher users or focused testing.

## Modes

On boot, the launcher lets you choose:

- **Standalone**: a local Reticulum/LXMF messenger with identity management,
  contacts, peer discovery, messages, LoRa, and WiFi TCP access.
- **RNode**: a host-controlled RNode-style radio target for Ratspeak or other
  Reticulum clients over BLE or USB serial.

RNode mode self-provisions the T-Pager RNode product/model/default config on
first boot, so users should not need a separate `rnodeconf` setup step for the
bundled release images.

## Basic Use

On first boot, Standalone mode generates a Reticulum identity and asks for a
display name. Your LXMF address is the 32-character hex string you share with
contacts.

- Tabs: Home, Chats, Contacts, Peers, Settings.
- Navigation: keyboard plus scroll encoder and click/Enter.
- Announce: press Enter on Home, or select Announce from the Home controls.
- Add contacts: select a discovered peer, then open or save the chat.
- Send messages: open a chat, type, and press Enter.
- Delivery color: yellow while sending, green after delivery confirmation.
- Sleep screen: tap BOOT. Wake with any key or the encoder click.
- Power off: hold BOOT, then confirm. Power back on by holding PWR for about
  one second or plugging in USB. Holding BOOT for six seconds forces off without
  the confirm; holding PWR for about 12 seconds hard power-cycles in hardware.

## Radio Presets

`Long Fast` is the compiled-in default. Host clients can change RNode radio
parameters through normal RNode commands, and Standalone mode exposes radio
settings on-device. Changes apply immediately.

| Preset | SF | BW | CR | TXP | Bitrate | Link budget |
|---|---:|---:|---:|---:|---:|---:|
| Short Turbo | 7 | 500 kHz | 4/5 | 14 dBm | 21.99 kbps | 140 dB |
| Short Fast | 7 | 250 kHz | 4/5 | 14 dBm | 10.84 kbps | 143 dB |
| Short Slow | 8 | 250 kHz | 4/5 | 14 dBm | 6.25 kbps | 145.5 dB |
| Medium Fast | 9 | 250 kHz | 4/5 | 17 dBm | 3.52 kbps | 148 dB |
| Medium Slow | 10 | 250 kHz | 4/5 | 17 dBm | 1.95 kbps | 150.5 dB |
| Long Turbo | 11 | 500 kHz | 4/8 | 22 dBm | 1.34 kbps | 150 dB |
| **Long Fast** *(default)* | **11** | **250 kHz** | **4/5** | **22 dBm** | **1.07 kbps** | **153 dB** |
| Long Moderate | 11 | 125 kHz | 4/8 | 22 dBm | 0.34 kbps | 156 dB |

The supported SX1262 radio is an 850-950 MHz target. 868 MHz and 915 MHz are
valid software profiles for that hardware range. 433 MHz requires 433 MHz radio
hardware. You are responsible for operating within local laws and radio
regulations.

## WiFi Bridging

WiFi bridging is experimental. STA mode can connect to existing WiFi and reach
remote Reticulum nodes such as `rns.ratspeak.org:4242`.

AP mode exposes a local TCP endpoint for a nearby Reticulum host:

```ini
[[rspager]]
  type = TCPClientInterface
  target_host = 192.168.4.1
  target_port = 4242
```

The bridging UI and interface behavior may change as Ratspeak's client release
stabilizes.

## Build From Source

```bash
git clone https://github.com/ratspeak/rsPager
cd rsPager
python3 -m pip install platformio esptool
make prep-tpager
make package
make flash port=/dev/cu.usbmodem3101
```

Useful build targets:

```bash
make build-launcher      # launcher only
make build-standalone    # standalone messenger app
make build-rnode         # host-controlled RNode target
make full-image          # launcher + Standalone + RNode
make standalone-image    # standalone merged image
make rnode-only-image    # standalone RNode merged image
make package             # release zips and app images
```

Release artifacts are written to `dist/`:

```text
dist/rspager-full.zip
dist/rspager-standalone.zip
dist/rspager-rnode.zip
dist/rspager-standalone-app.bin
dist/rspager-rnode-app.bin
```

Use the `.zip` files with the Ratspeak web flasher. The `*-app.bin` files are
bare app images for external launchers or manual OTA-slot flashing.

## License

rsPager standalone firmware, launcher, partition tables, and packaging tools
are licensed under the GNU Affero General Public License v3.0 or later. See
[LICENSE](LICENSE).

Vendored third-party code keeps its own license notices, including
`vendor/rnode_firmware/` and `lib/Crypto`.

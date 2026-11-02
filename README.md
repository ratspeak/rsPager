<div align="center">

# rsPager <br> ⚠️ DEPRECATED - use [ratspeak-handheld](https://github.com/ratspeak/ratspeak-handheld) ⚠️

**Dual-mode Ratspeak firmware for the LilyGo T-Pager.**

[![Status](https://img.shields.io/badge/status-beta-yellow.svg)](#install)
[![Model](https://img.shields.io/badge/model-LilyGo%20T--Pager-success.svg)](https://wiki.lilygo.cc/products/t-lora-series/t-lora-pager/)
[![Legacy version](https://img.shields.io/badge/legacy_version-1.0.4-success.svg)](https://github.com/ratspeak/rsPager/releases)
[![License](https://img.shields.io/badge/license-AGPL--3.0--or--later-blue.svg)](LICENSE)

[Ratspeak](https://github.com/ratspeak/Ratspeak) |
[Current docs](https://docs.ratspeak.org/docs/hardware/handheld-guide) |
[Legacy releases](https://github.com/ratspeak/rsPager/releases) |
[rsReticulum](https://github.com/ratspeak/rsReticulum)

</div>

---

**Legacy repository.** Current firmware and development are in
[Ratspeak Handheld](https://github.com/ratspeak/ratspeak-handheld). The modes,
controls, radio defaults and build artifacts below describe this retained
repository; they are not the current handheld release instructions.

rsPager beta v1.0.4 turns a
[LilyGo T-Pager / T-LoRa-Pager](https://wiki.lilygo.cc/products/t-lora-series/t-lora-pager/)
into a two-mode Reticulum handheld. Standalone mode is an on-device
Ratspeak/LXMF messenger. RNode mode makes the T-Pager a host-controlled radio
for Ratspeak, Sideband, or another Reticulum client over BLE or USB serial.

## Install

For current firmware, follow the
[handheld installation guide](https://docs.ratspeak.org/docs/hardware/flashing-firmware)
and choose **T-Pager** for the SX1262 model in the
[Ratspeak web flasher](https://ratspeak.org/download.html). The current full
image is `pager-full.bin`.

Back up your identity and messages before changing firmware or layouts. Legacy
packages from this repository use different names and storage layouts; matching
a device name does not establish that an update will preserve its data.

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
  one second or plugging in USB. On USB power, shutdown enters deep sleep until
  USB is unplugged; the encoder click wakes it. Holding BOOT for six seconds
  forces off without saving pending state. Holding PWR for about 12 seconds
  hard power-cycles in hardware.

## Radio Presets

`Long Fast` is the compiled-in default. Host clients can change RNode radio
parameters through normal RNode commands, and Standalone mode exposes radio
settings on-device. Changes apply immediately.

| Preset | SF | BW | CR | TXP |
|---|---:|---:|---:|---:|
| Short Turbo | 7 | 500 kHz | 4/5 | 14 dBm |
| Short Fast | 7 | 250 kHz | 4/5 | 14 dBm |
| Short Slow | 8 | 250 kHz | 4/5 | 14 dBm |
| Medium Fast | 9 | 250 kHz | 4/5 | 17 dBm |
| Medium Slow | 10 | 250 kHz | 4/5 | 17 dBm |
| Long Turbo | 11 | 500 kHz | 4/8 | 22 dBm |
| **Long Fast** *(default)* | **11** | **250 kHz** | **4/5** | **22 dBm** |
| Long Moderate | 11 | 125 kHz | 4/8 | 22 dBm |

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

This configuration describes the legacy firmware's access point. Join its WiFi
network first; use the address shown by the device if it differs from the default.

## Build From Source

These commands build the retained legacy source. Install Git, Make, Python 3
with virtual-environment support, and Arduino CLI first. Use a fresh checkout:
packaging replaces files under `dist/`.

```bash
git clone https://github.com/ratspeak/rsPager
cd rsPager
git checkout 5d5842f47e93a9a9aa25643018608772da54308b
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install platformio esptool
make prep-tpager
make package
```

After backing up the device, `make flash PORT=/dev/cu.usbmodemXXXX` builds and
writes the full legacy image at offset `0x0`. Replace the port with your device
and close any app using it. A factory write can replace settings; do not assume
that data from another firmware layout will survive.

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

Legacy artifacts are written to `dist/`:

```text
dist/rspager-full.zip
dist/rspager-standalone.zip
dist/rspager-rnode.zip
dist/rspager-standalone-app.bin
dist/rspager-rnode-app.bin
```

Each `.zip` contains a merged factory image and its legacy flasher manifest.
The `*-app.bin` files are bare applications for a compatible external launcher
or a correctly sized OTA slot. Do not write a bare application at offset `0x0`.

## License

rsPager standalone firmware, launcher, partition tables, and packaging tools
are licensed under the GNU Affero General Public License v3.0 or later. See
[LICENSE](LICENSE).

Vendored third-party code keeps its own license notices, including
`vendor/rnode_firmware/` and `lib/Crypto`.

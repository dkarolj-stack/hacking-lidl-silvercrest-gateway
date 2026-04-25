# `ot-ctl` Cheat-sheet — Operating the Thread Network

A focused reference for the `ot-ctl` commands you actually need to drive
the gateway's OpenThread Border Router. Curated, not exhaustive — the
[upstream OpenThread CLI reference](https://github.com/openthread/openthread/blob/main/src/cli/README.md)
covers the long tail.

For Thread/Matter concepts (fabric, commissioning, mesh-local prefix,
OTBR vs RCP), read [`THREAD-MATTER-PRIMER.md`](./THREAD-MATTER-PRIMER.md)
first.

---

## 1. How to invoke `ot-ctl`

`ot-ctl` is a thin client that talks to a running `otbr-agent` over a
Unix socket. Where you run it depends on which use case you set up
(see [`README.md` §2](./README.md#2-use-cases)):

| Use case | How to invoke |
|----------|---------------|
| **2 — OTBR on host** | `docker exec -it otbr ot-ctl <cmd>` |
| **3 — OTBR on gateway** | `ot-ctl <cmd>` (binary in `/userdata/usr/bin/`) |
| **1 — ZoH** | not applicable — ZoH is Zigbee, no `ot-ctl` |

Each command below is a single line you type after `ot-ctl`.

---

## 2. Bring-up

`S70otbr` brings the network up automatically on boot when
`/userdata/etc/radio.conf` has `MODE=otbr`. The manual sequence below is
only useful inside a fresh Docker container, or to recover a
half-configured node:

```sh
ifconfig up
thread start
state                  # poll until: detached → child → router → leader
```

`leader` means your gateway is the network leader (first/only Thread
node). `router` or `child` means it joined an existing Thread network.

---

## 3. State & inspection

The first commands to run when something looks wrong:

```sh
state                  # current role: disabled / detached / child / router / leader
dataset active         # full network bundle: channel, panid, network key, etc.
extaddr                # this node's EUI-64 (64-bit MAC)
rloc16                 # 16-bit short address inside the mesh
router table           # list of active routers in the network
child table            # end-devices attached to this router
neighbor table         # radio neighbours (with RSSI, link quality)
counters mac           # PHY/MAC counters (TX/RX, errors, retries) — useful for debugging packet loss
```

---

## 4. Channel

Thread uses 802.15.4 channels **11–26** (2.405–2.480 GHz, 5 MHz spacing).

```sh
channel                # read current channel
```

Changing channel requires going through a **pending dataset**:

```sh
dataset channel 25
dataset commit active
```

> **Default behaviour.** This project's `26-OT-RCP/patches/` does **not**
> override the channel — `dataset init new` randomises it within 11–26.
> So the default channel is "whatever's in the active dataset"; always
> read `ot-ctl channel` rather than assuming a value.

---

## 5. TX power

Set in dBm. Read first, write second:

```sh
txpower                # read current TX power
txpower 8              # set to +8 dBm
```

> **Default behaviour.** This project does not force a TX power, so we
> inherit Silicon Labs RAIL defaults. The EFR32MG1B chip can do up to
> **+19 dBm**, but the actual ceiling is regulatory: FCC ~+19 dBm,
> ETSI/CE ~+10 dBm. The RAIL platform layer applies the right
> per-channel cap — read `ot-ctl txpower` to see what you're actually
> getting.

---

## 6. Dataset / network identity

The active dataset bundles all the network parameters. Create a fresh
one (random channel, PAN ID, network key, etc.):

```sh
dataset init new
dataset networkname "MyThread"
dataset panid 0x1234
dataset commit active
```

Export the dataset as TLV hex (for sharing with another device or hub):

```sh
dataset active -x
```

Use `dataset` (no args) to see the *pending* dataset under construction
before you commit.

---

## 7. Commissioning a joiner

To let a new Thread device join the network, run a commissioner on the
border router and a joiner on the device:

```sh
# On the border router:
commissioner start
commissioner joiner add * J01NME 120     # any joiner, passphrase, 120 s window

# On the joiner device:
joiner start J01NME
```

`*` accepts any joiner; for stricter pairing, use the joiner's EUI-64
instead of `*`.

---

## 8. Border Router state (use cases 2 & 3 only)

```sh
br state               # running / disabled
br omrprefix           # the IPv6 /64 prefix this BR advertises on the LAN
br nat64state          # whether NAT64 is active (Thread → IPv4 internet)
```

Useful when Matter devices are attached but not reachable from the LAN —
usually an `omrprefix` or NAT64 issue.

---

## 9. Diagnostics

```sh
ping fdde:ad00:beef:0:...   # IPv6 ping inside the mesh
scan                        # 802.15.4 energy scan (busy channels)
networks                    # passive scan: nearby Thread networks
```

---

## 10. Persistence on this gateway *(use case 3 only)*

Use case 3 has a project-specific quirk that upstream documentation
cannot tell you:

- `otbr-agent` runs from `/tmp/thread` (tmpfs, **RAM**). This is
  deliberate — frame counters update ~480 times/day and would burn
  through JFFS2 wear-levelling on flash.
- A background daemon in
  [`S70otbr`](../../3-Main-SoC-Realtek-RTL8196E/34-Userdata/skeleton/etc/init.d/S70otbr)
  polls the active dataset every **30 s** (5 s while detached) and
  copies it to `/userdata/thread` (flash) **only when it changes**.
- On clean shutdown (`S70otbr stop` or reboot via `init`), a final sync
  runs.
- On power loss, only the frame counters are lost — OpenThread recovers
  by jumping the counter ahead at next boot.

**User-visible consequence.** A `dataset commit active` is in effect
*immediately* (RAM), but only **reflash-survivable after the next sync
tick** (≤30 s). If you reboot or pull power within that window, the
dataset reverts to the last flash-synced version. To force a sync,
`/userdata/etc/init.d/S70otbr stop` (it runs the final-sync trap), then
`start` again.

`/userdata/thread/` is part of `SAVE_FILES` in `flash_install_rtl8196e.sh`
and `3-Main-SoC-Realtek-RTL8196E/flash_remote.sh`, so the dataset
survives userdata reflashes.

In use case 2 (OTBR on host) the dataset lives in the `otbr_data`
Docker volume — same idea, different storage; `docker volume rm` will
wipe it.

---

## 11. See also

- [Upstream OpenThread CLI reference](https://github.com/openthread/openthread/blob/main/src/cli/README.md) — every command, every flag.
- [`THREAD-MATTER-PRIMER.md`](./THREAD-MATTER-PRIMER.md) — Thread/Matter concepts.
- [`README.md`](./README.md) — firmware and use-case overview.
- [`docker/README.md`](./docker/README.md) — full Docker setup for the three use cases.

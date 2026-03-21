# rcp-stack - Rootless Zigbee Stack Manager

Systemd --user manager for the complete RCP chain:
```
RCP (EFR32) ←serialgateway→ socat ←PTY→ cpcd ←CPC→ zigbeed ←PTY→ socat ←PTY→ Z2M
```

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           HOST (PC / Raspberry Pi)                       │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐               │
│  │ socat-cpc-rcp│───▶│    cpcd      │───▶│   zigbeed    │               │
│  │   (TCP→PTY)  │    │  (CPC daemon)│    │ (EmberZNet)  │               │
│  └──────────────┘    └──────────────┘    └──────────────┘               │
│         │                   │                   │                        │
│         ▼                   ▼                   ▼                        │
│  /tmp/ttyCpcRcp      CPC sockets         /tmp/ttyZigbeed                │
│                    /dev/shm/cpcd/              │                        │
│                                                ▼                        │
│                                    ┌──────────────────┐                 │
│                                    │ socat-zigbeed-pty│                 │
│                                    │   (PTY bridge)   │                 │
│                                    └──────────────────┘                 │
│                                                │                        │
│                                                ▼                        │
│                                         /tmp/ttyZ2M                     │
│                                                │                        │
│                                                ▼                        │
│                                    ┌──────────────────┐                 │
│                                    │   Zigbee2MQTT    │                 │
│                                    └──────────────────┘                 │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
         ▲
         │ TCP (serialgateway)
         ▼
┌─────────────────┐
│ Lidl Gateway    │
│ (RCP firmware)  │
└─────────────────┘
```

## Prerequisites

1. **cpcd** installed (`/usr/local/bin/cpcd`) - see `../cpcd/`
2. **zigbeed** installed (`/usr/local/bin/zigbeed`) - see `../zigbeed-8.2.2/`
3. **socat** installed (`apt install socat`)
4. **serialgateway** on the gateway (exposes the RCP via TCP)
5. **Direct Ethernet cable** between host and gateway (strongly recommended)

> **Network Quality:** The CPC protocol is sensitive to latency and packet loss.
> For reliable operation, connect the gateway directly to the host with an Ethernet
> cable. Avoid WiFi, congested switches, or multiple network hops.

## Installation

```bash
# 1. Copy the main script
sudo cp bin/rcp-stack /usr/local/bin/
sudo chmod +x /usr/local/bin/rcp-stack

# 2. First run (creates config)
rcp-stack up
# -> Creates ~/.config/rcp-stack/rcp-stack.env
# -> Expected error: "Edit it with your paths, then rerun"

# 3. Edit configuration
nano ~/.config/rcp-stack/rcp-stack.env
```

## Configuration

Edit `~/.config/rcp-stack/rcp-stack.env`:

```bash
# TCP endpoint of the RCP (serialgateway on the gateway)
RCP_ENDPOINT=tcp://192.168.1.100:8888

# Commands for each service
CPCD_COMMAND='cpcd -c "$HOME/.config/rcp-stack/cpcd.conf"'
ZIGBEED_COMMAND='zigbeed -r "spinel+cpc://$CPC_INSTANCE_NAME?iid=1&iid-list=0" -p "$ZIGBEED_PTY"'
Z2M_COMMAND='zigbee2mqtt'

# Optional (default values)
# CPC_INSTANCE_NAME=cpcd_bringup
# CPC_SOCKET_DIR=/dev/shm/cpcd/cpcd_bringup
# CPC_RCP_PTY=/tmp/ttyCpcRcp
# ZIGBEED_PTY=/tmp/ttyZigbeed
# Z2M_PTY=/tmp/ttyZ2M
# RCP_ENDPOINT_TIMEOUT=5
```

Also copy the cpcd.conf file:
```bash
cp examples/cpcd.conf.example ~/.config/rcp-stack/cpcd.conf
# Edit if needed (binding_key_file, etc.)
```

## Usage

```bash
# Start the complete chain (checks TCP connectivity first)
rcp-stack up

# Stop cleanly
rcp-stack down

# Show status
rcp-stack status

# Full diagnostics
rcp-stack doctor
```

The `up` command verifies the RCP endpoint is reachable before starting services:
```
Checking RCP endpoint: 192.168.1.126:8888 ...
RCP endpoint 192.168.1.126:8888 is reachable
```

## Systemd Services

The `rcp-stack up` command installs and starts these services in order:

| Service | Description | Dependencies |
|---------|-------------|--------------|
| `socat-cpc-rcp.service` | TCP→PTY for cpcd | - |
| `cpcd-bringup.service` | CPC daemon | socat-cpc-rcp |
| `socat-zigbeed-pty.service` | PTY bridge zigbeed↔Z2M | - |
| `zigbeed.service` | Zigbee daemon | cpcd, socat-zigbeed-pty |
| `zigbee2mqtt.service` | Zigbee2MQTT | zigbeed |

### Manual Service Management

```bash
# View logs
journalctl --user -u zigbeed.service -f

# Restart a service
systemctl --user restart zigbeed.service

# Enable at boot (optional)
systemctl --user enable socat-cpc-rcp.service cpcd-bringup.service \
  socat-zigbeed-pty.service zigbeed.service zigbee2mqtt.service
loginctl enable-linger $USER
```

## File Structure

```
~/.config/rcp-stack/
├── rcp-stack.env          # Main configuration
├── cpcd.conf              # cpcd config
└── bin/                   # Helper scripts (auto-installed)
    ├── rcp-check-cpcd-conf
    ├── rcp-check-endpoint
    ├── rcp-check-zigbeed-conf
    ├── rcp-cleanup
    ├── rcp-ensure-dirs
    ├── rcp-run-command
    ├── rcp-socat-rcp
    ├── rcp-wait-active
    ├── rcp-wait-cpcd
    └── rcp-wait-pty

~/.local/state/rcp-stack/
└── zigbeed/
    └── host_token.nvm     # zigbeed token (persistent)

/dev/shm/cpcd/cpcd_bringup/
├── cpcd.sock              # Main CPC socket
└── ctrl.cpcd.sock         # Control socket

/tmp/
├── ttyCpcRcp              # PTY: socat → cpcd
├── ttyZigbeed             # PTY: zigbeed output
└── ttyZ2M                 # PTY: Z2M input
```

## Zigbee2MQTT Configuration

In `zigbee2mqtt/data/configuration.yaml`:

```yaml
serial:
  port: /tmp/ttyZ2M
  adapter: ember
  baudrate: 115200
```

## Troubleshooting

### "Cannot connect to RCP endpoint"
The gateway is not reachable. Check:
- Gateway is powered on
- serialgateway is running on the gateway
- Network connectivity (ping the gateway IP)
- Correct port number in `RCP_ENDPOINT`

### "RCP_ENDPOINT is not set"
Edit `~/.config/rcp-stack/rcp-stack.env` and set `RCP_ENDPOINT`.

### "cpcd.conf not found"
Copy the example file:
```bash
cp examples/cpcd.conf.example ~/.config/rcp-stack/cpcd.conf
```

### CPC sockets not created
```bash
rcp-stack down
rm -rf /dev/shm/cpcd/cpcd_bringup
rcp-stack up
```

### Incompatible zigbeed token (v1 vs v2)
EmberZNet 8.2+ requires a v2 token:
```bash
rm ~/.local/state/rcp-stack/zigbeed/host_token.nvm
rcp-stack up
```

### Root-owned files in config directories
```bash
sudo chown -R $USER:$USER ~/.config/rcp-stack ~/.local/state/rcp-stack ~/.cpcd
```

## Why socat for PTYs?

1. **Stability**: PTYs created by zigbeed disappear if the process crashes
2. **Decoupling**: Z2M can restart without losing the PTY
3. **Stable symlinks**: `/tmp/ttyZ2M` always exists as long as socat runs

# OpenThread Border Router (POSIX) for RTL8196E

Cross-compilation of [ot-br-posix](https://github.com/openthread/ot-br-posix) for Realtek RTL8196E (Lexra MIPS) with musl libc.

## Disclaimer

**This project is experimental.**

- This build has **not been tested on actual hardware** by the author (who does not own Matter/Thread devices)
- The RTL8196E has **limited resources** (32 MB RAM, 180 MHz CPU) which may prove insufficient for a full Thread Border Router workload
- The `otbr-agent` binary alone is 3.2 MB and the OpenThread stack is memory-intensive
- This is provided as a proof-of-concept for cross-compilation; real-world performance is unknown

## Prerequisites

Before deploying `ot-br-posix`, the following system components must be updated:

### 1. Kernel with IPv6 Support

The stock kernel does not include IPv6. You must rebuild the kernel with the IPv6 configuration.

See [`../32-Kernel/README-IPV6.md`](../32-Kernel/README-IPV6.md) for:
- Required kernel options (IPv6, TUN, Netfilter, IEEE 802.15.4)
- Recommended options for full functionality (ip6tables, ipset)
- Size impact analysis

Use the provided config: `../32-Kernel/config-5.10.246-realtek-ipv6.txt`

### 2. Busybox with IPv6 Support

The busybox build in `33-Rootfs` must be recompiled with IPv6 enabled:

```
CONFIG_FEATURE_IPV6=y           # Core IPv6 support
CONFIG_FEATURE_IFUPDOWN_IPV6=y  # ifup/ifdown IPv6 support
CONFIG_PING6=y                  # ping6 command
CONFIG_TRACEROUTE6=y            # traceroute6 command (optional)
```

Without these options, basic IPv6 tools (`ping6`, `ip -6`, etc.) will not work.

## Architecture

The gateway has a single external interface (Ethernet) and communicates with Thread devices via the Silabs RCP radio chip.

```
┌─────────────────────────────────────────────────────────────┐
│                    Local Network (WiFi/Ethernet)            │
│         Matter Controllers (Google Home, Apple Home...)     │
└─────────────────────────────┬───────────────────────────────┘
                              │ IPv4/IPv6
                              │
┌─────────────────────────────┴───────────────────────────────┐
│                      RTL8196E Gateway                       │
│  ┌──────────┐                              ┌──────────────┐ │
│  │   eth0   │◄─────── IPv6 routing ───────►│    wpan0     │ │
│  │ Ethernet │                              │  (TUN/TAP)   │ │
│  └──────────┘                              └──────┬───────┘ │
│       │                                          │          │
│       │            ┌──────────────┐              │          │
│       └───────────►│  otbr-agent  │◄─────────────┘          │
│                    │  - Border Agent                        │
│                    │  - mDNS/DNS-SD                         │
│                    │  - IPv6 Router                         │
│                    └───────┬──────┘                         │
│                            │ Spinel/HDLC (UART)             │
│                    ┌───────┴──────┐                         │
│                    │  Silabs RCP  │                         │
│                    │  (EFR32)     │                         │
│                    └───────┬──────┘                         │
└────────────────────────────┼────────────────────────────────┘
                             │ 802.15.4 radio
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                   Thread Network (mesh)                     │
│    ┌─────────┐    ┌─────────┐    ┌─────────┐               │
│    │ Matter  │    │ Matter  │    │ Matter  │               │
│    │ Device  │    │ Device  │    │ Device  │               │
│    └─────────┘    └─────────┘    └─────────┘               │
└─────────────────────────────────────────────────────────────┘
```

### Network Interfaces

| Interface | Type | Role |
|-----------|------|------|
| `eth0` | Physical | Connection to local network (backbone) |
| `wpan0` | Virtual (TUN) | Represents the Thread network |

### Communication Flow

1. **Thread devices → Local network**: IPv6 packets from Thread devices arrive via 802.15.4 radio → Silabs RCP → otbr-agent → wpan0 → eth0
2. **Matter commissioning**: Controllers discover the Border Router via mDNS (`_meshcop._udp`) on eth0, then communicate with Thread devices through the Border Agent
3. **Services on eth0**: mDNS (port 5353), Border Agent, DNS-SD Discovery Proxy

## Build Status

Successfully cross-compiled with Border Agent and mDNS support. Produces statically linked binaries:
- `otbr-agent` (~3.2 MB stripped)
- `ot-ctl` (~57 KB stripped)

## Features Enabled

| Feature | Description |
|---------|-------------|
| **Border Agent** | Thread commissioning (Matter/HomeKit compatible) |
| **mDNS/DNS-SD** | OpenThread built-in implementation (no external deps) |
| **SRP Advertising Proxy** | Service Registration Protocol proxy |
| **DNS-SD Discovery Proxy** | DNS-based service discovery |
| **Border Routing** | IPv6 routing between Thread and infrastructure |

## Features Disabled

| Feature | CMake Option | Reason |
|---------|--------------|--------|
| D-Bus | `OTBR_DBUS=OFF` | No D-Bus on embedded target |
| Web UI | `OTBR_WEB=OFF` | Reduces complexity |
| REST API | `OTBR_REST=OFF` | Reduces complexity |
| Backbone Router | `OTBR_BACKBONE_ROUTER=OFF` | Advanced feature |
| TREL | `OTBR_TREL=OFF` | Thread Radio Encapsulation Link |
| NAT64 | `OTBR_NAT64=OFF` | Requires TAYGA |
| DNS Upstream | `OTBR_DNS_UPSTREAM_QUERY=OFF` | Advanced feature |

## Building

```bash
./build_otbr.sh
```

The build script:
1. Clones ot-br-posix repository with submodules
2. Generates a CMake toolchain file with link command override for circular dependencies
3. Configures CMake with cross-compilation toolchain
4. Compiles and links all components
5. Strips binaries

## Installing

Copy the binaries directly to the gateway via SSH:

```bash
# Replace GATEWAY_IP with your gateway's IP address
cat build/src/agent/otbr-agent | ssh root@GATEWAY_IP:8888 'cat > /userdata/usr/local/bin/otbr-agent && chmod +x /userdata/usr/local/bin/otbr-agent'
cat build/third_party/openthread/repo/src/posix/ot-ctl | ssh root@GATEWAY_IP:8888 'cat > /userdata/usr/local/bin/ot-ctl && chmod +x /userdata/usr/local/bin/ot-ctl'
```

## Build Issues & Solutions

### 1. Submodule Cloning

**Problem:** Using `--depth 1` with recursive submodules fails because submodules reference specific commits that may not be in a shallow clone.

**Solution:** Clone without depth limit for submodules:
```bash
git submodule update --init --recursive  # No --depth 1
```

### 2. Deprecated CMake Option

**Problem:** `OT_POSIX_CONFIG_RCP_BUS=UART` is deprecated.

**Solution:** Use the new option:
```cmake
-DOT_POSIX_RCP_HDLC_BUS=ON
```

### 3. mDNS Implementation

**Problem:** External mDNS (Avahi/mDNSResponder) adds dependencies.

**Solution:** Use OpenThread's built-in mDNS implementation:
```cmake
-DOTBR_MDNS=openthread
```

This enables mDNS/DNS-SD without any external dependencies. The following features are automatically enabled:
- `OT_MDNS=ON`
- `OT_DNSSD_SERVER=ON`
- `OT_DNSSD_DISCOVERY_PROXY=ON`
- `OT_SRP_ADV_PROXY=ON`

### 4. Circular Library Dependencies

**Problem:** Static linking fails with undefined references to `otPlatAlarmMilli*` due to circular dependencies between static libraries (e.g., `openthread-ftd` ↔ `openthread-posix`).

**Solution:** Override CMake's link command template in the toolchain file to automatically use `--start-group`/`--end-group`:
```cmake
set(CMAKE_CXX_LINK_EXECUTABLE
    "<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group")
```

This tells CMake to wrap all libraries in `--start-group`/`--end-group` when linking executables, allowing the linker to resolve circular references automatically.

## Kernel Requirements

The Linux kernel must have the following options enabled:

```
CONFIG_IPV6=y                    # IPv6 networking stack
CONFIG_IPV6_ROUTER_PREF=y        # Router preference
CONFIG_IPV6_MULTIPLE_TABLES=y    # Multiple routing tables
CONFIG_TUN=y                     # TUN/TAP device (for wpan0)
CONFIG_NETFILTER=y               # Netfilter framework
CONFIG_NF_CONNTRACK=y            # Connection tracking
CONFIG_NF_CONNTRACK_IPV6=y       # IPv6 connection tracking
CONFIG_IEEE802154=y              # IEEE 802.15.4 support
```

A pre-configured kernel config with IPv6 support is available at:
`../32-Kernel/config-5.10.246-realtek-ipv6.txt`

See `../32-Kernel/README-IPV6.md` for details on kernel options.

## Usage

### Running otbr-agent

```bash
# With UART-connected RCP (e.g., Silicon Labs EFR32)
otbr-agent -I wpan0 -B eth0 spinel+hdlc+uart:///dev/ttyUSB0?uart-baudrate=460800

# Options:
#   -I wpan0     Thread network interface name
#   -B eth0      Backbone/infrastructure interface
#   spinel+hdlc+uart://...  RCP connection URL
```

### Using ot-ctl

```bash
# Connect to running otbr-agent
ot-ctl

# Example commands:
> state
> dataset active
> ipaddr
> srp server
> mdns state
```

## Directory Structure

After running `build_otbr.sh`:

```
ot-br-posix/
├── build_otbr.sh          # Build script
├── README.md              # This file
├── ot-br-posix/           # Cloned source repository (created by script)
└── build/                 # CMake build directory (created by script)
    ├── toolchain-mips-lexra.cmake
    ├── src/agent/otbr-agent
    └── third_party/openthread/repo/src/posix/ot-ctl
```

## References

- [OpenThread Border Router](https://openthread.io/guides/border-router)
- [ot-br-posix GitHub](https://github.com/openthread/ot-br-posix)
- [Thread Specification](https://www.threadgroup.org/)
- [Matter Protocol](https://csa-iot.org/all-solutions/matter/)

## License

ot-br-posix is licensed under BSD-3-Clause.

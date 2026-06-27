# PlatformIO nRF52 Build Entrypoint

Authoritative nRF52 build entrypoint.

Build authority:

```text
nRF52 -> PlatformIO
```

Current transitional path may be:

```text
removed root esp_pio
root platformio.ini
target-specific PlatformIO files
```

Final wrapper direction:

```text
builds/pio_nrf52
  -> invokes apps/nrf52_node app shell
```

This directory is the active nRF52 wrapper used for `t-echo-lite`, and it also
keeps a wrapper build for `gat562_mesh_evb_pro` while the root PlatformIO
environment remains available.

Current release-build commands:

```bash
platformio run -d builds/pio_nrf52 -e t-echo-lite
platformio run -d builds/pio_nrf52 -e gat562_mesh_evb_pro
```

Release artifacts for `t-echo-lite` prefer the generated `firmware.uf2` for
manual flashing. CI verifies that this UF2 uses the nRF52840 family ID
`0xADA52840`. The `.hex` and `.zip` outputs remain useful for diagnostics and
toolchain-specific fallback paths.

Rules:

- thin wrapper only
- Build Entrypoint invokes
- App Shell composes
- do not assemble Chat/Map/GPS runtime here
- do not choose UX pack here
- do not define board facts here
- keep current PlatformIO flow stable until wrapper parity is proven

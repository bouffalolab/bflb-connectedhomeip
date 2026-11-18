# Bouffalo Lab BL618DG Matter Controller (chip-tool port)

Status: work in progress — staged plan, see the approved plan document.

## 阶段 0 verified build flow (lighting-app as the commissionee vehicle)

The Matter platform layer is being brought up on BL618DG through the
lighting-app first. Build requirements discovered so far (they apply to the
controller app too):

1. **Build inside the activated Matter environment** — the GN step needs `gn`,
   `zap-cli` and the pigweed environment variables:
   ```
   source scripts/activate.sh
   ```
2. **`BL_SDK_BASE` must be a make command-line variable** (a plain `export`
   is overridden by the example Makefile, which defaults to the in-tree
   v2.3.27 SDK that has no BL618DG support):
   ```
   cd examples/lighting-app/bouffalolab
   make CHIP=bl618dg BOARD=bl618dgdk CPU_ID=ap CONFIG_WIFI=y CONFIG_MFD=y \
        BUILD_DIR=build-bl618dg \
        BL_SDK_BASE=/home/weiyin/workspace/gerrit/legacy/bouffalo_sdk
   ```
3. **Never pass `cmake_definition+=...` on the command line** — a command-line
   variable silences every `cmake_definition +=` inside the Makefile and the
   configure loses `-DCHIP_ROOT` etc.
4. The defconfig selects `CONFIG_BTBLECONTROLLER_LIB=ble1m0s1bredr0` for
   bl618dg (the previous non-bl616 fallback `m2s1` is bl616cl-only).

## Test infrastructure

- `scripts/test/bouffalolab/bflb_board.py` — board handle: find by USB iSerial
  (CKLink-Lite CDC-ACM, VID:PID 42bf:b210), DTR/RTS in-band reset
  (`BOUFFALOLAB5555DTR0`/`RTS0`/`RTS1`), console capture.
- `scripts/test/bouffalolab/bflbctl.py` — CLI: `list|reset|wait-boot|send|console|flash`.
- `scripts/test/bouffalolab/boards.yaml.example` — role → iSerial map
  (`lighting` / `controller`).

## BLE stack CLI

The controller image enables the Bouffalo SDK BLE stack shell commands. At the
`bouffalolab />` prompt, start a passive scan with duplicate filtering disabled
and print each device:

```
ble_start_scan 0 0 0x0060 0x0060 0
```

Stop it with:

```
ble_stop_scan
```

Do not run this scan concurrently with `matter ble-scan`. The chip-tool already
initializes the BLE stack; use the scan commands directly rather than
reinitializing it with `ble_enable`.

BL618DG also exposes the SDK RF coexistence test commands. To force the shared
RF path to Bluetooth before scanning, run `ble_coex_rf_set bt`; restore Wi-Fi
priority with `ble_coex_rf_set wifi`.

For low-level PHY RF-path diagnostics, the BL618DG image also exports:

```
btble_path_combo
btble_path_standalone
```

These call the SDK `cmd_set_btble_combo()` and
`cmd_set_btble_standalone()` functions respectively and print before/after
markers. They directly switch the PHY RF path; use only for diagnostics and
expect active Wi-Fi/BLE operation to be disrupted. They do not replace the
startup RF initialization.

## Known issue: non-fatal mbedTLS diagnostic

During BL618DG BLE commissioning, the controller logs
`mbedTLS error: -0xFFFFFFFF` while processing the PAA certificate (after its
AKID is logged and before Certification Declaration verification). In the
observed successful run, attestation verification and commissioning continued
successfully, so this message did not fail commissioning. Its originating
mbedTLS call and root cause have not yet been identified; investigate this
separately if it recurs or affects verification.

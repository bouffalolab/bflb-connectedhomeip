# Bouffalo Lab BL618DG Matter Controller

1. **Build inside the activated Matter environment** — the GN step needs `gn`,
   `zap-cli` and the pigweed environment variables:
   ```
   source scripts/activate.sh
   ```
2. **`BL_SDK_BASE` must be a make command-line variable** :
   ```
   make -C examples/lighting-app/bouffalolab CHIP=bl618dg BOARD=bl618dgdk \
        CPU_ID=ap CONFIG_WIFI=y CONFIG_MFD=y \
        BL_SDK_BASE=/home/weiyin/workspace/gerrit/legacy/bouffalo_sdk
   ```

## Wi-Fi commissioning

### Connect the BL618DG controller to an AP

This connects the controller itself to the Wi-Fi AP. Run at the
`bouffalolab />` prompt, replacing the SSID and password with your AP
credentials:

```
matter wifi connect <ssid> <password>
```

### Commission another Matter device onto the AP

This commissions a separate Matter device over BLE and provisions it to join
the specified Wi-Fi network. Replace the node ID, SSID, and password with the
values for your setup:

```
matter chip-tool pairing ble-wifi <node-id> <ssid> <password> 20202021 3840
```

The PIN code `20202021` and discriminator `3840` are the defaults used by the
BL616 lighting-app example described above.

## Lighting cluster commands

The controller registers the generated OnOff and LevelControl command sets for
the BL616 lighting app. At the `bouffalolab />` prompt, run them through the
`matter` shell command (replace node and endpoint IDs with the commissioned
lighting device's values):

```
matter chip-tool onoff on <node-id> <endpoint-id>
matter chip-tool onoff off <node-id> <endpoint-id>
matter chip-tool onoff toggle <node-id> <endpoint-id>
matter chip-tool levelcontrol move-to-level <level-0-254> <transition-time> <options-mask> <options-override> <node-id> <endpoint-id>
```

The full generated OnOff and LevelControl command sets are registered. The
controller intentionally registers only these cluster commands—not the
generated attribute read/write/event commands—to keep the embedded image within
its flash partition.

## Known issue: non-fatal mbedTLS diagnostic

During BL618DG BLE commissioning, the controller logs
`mbedTLS error: -0xFFFFFFFF` while processing the PAA certificate (after its
AKID is logged and before Certification Declaration verification). In the
observed successful run, attestation verification and commissioning continued
successfully, so this message did not fail commissioning. Its originating
mbedTLS call and root cause have not yet been identified; investigate this
separately if it recurs or affects verification.

# Matter `Bouffalo Lab` Minimal App Example

This example demonstrates a minimal Matter device implementation for Bouffalo Lab platforms, including only the essential clusters on endpoint 0.

## Features

- Only includes endpoint 0 with Basic and Descriptor clusters (required by Matter specification)
- Minimal memory footprint
- Simple build configuration
- Ready for commissioning and basic Matter functionality testing

## Supported Boards

- `bl616dk` (BL616 Development Kit)

## Build approach

Since the target `bouffalolab-bl616dk-minimal-wifi-littlefs` doesn't exist in the current build system, you have two options:

### Option 1: Modify an existing example
Use an existing Bouffalo Lab example and modify it to use your minimal ZAP file:
```bash
# First, build an existing example to verify your environment works
./scripts/build/build_examples.py --target bouffalolab-bl616dk-light-wifi-littlefs build
```

Then modify that example to use your minimal ZAP file instead of the full one.

### Option 2: Create a custom build approach
Create a simple build script or modify the existing BUILD.gn files to reference your minimal ZAP file.

## How to create the minimal ZAP file

The minimal ZAP file I created earlier only includes:
- Basic Information Cluster (mandatory)
- Descriptor Cluster (mandatory)

This is the minimal required set for a Matter device to function properly.

## Testing

Once built, this example can be commissioned using the chip-tool:
```shell
# For BLE commissioning
./out/linux-x64-chip-tool/chip-tool pairing ble-wifi <device_node_id> <wifi_ssid> <wifi_passwd> 20202021 3840

# For on-network commissioning
./out/linux-x64-chip-tool/chip-tool pairing onnetwork <device_node_id> 20202021
```

## Integration Notes

To properly integrate this into the existing build system, you would need to:

1. **Add your ZAP file to the appropriate directory structure**
2. **Modify the existing BUILD.gn to reference your minimal ZAP file**
3. **Update the build system to recognize the new target**
4. **Follow the existing naming conventions**

## Files Created

The files I've created for this minimal example include:
- `mini-app.zap` - Minimal ZAP data model with only endpoint 0 clusters
- `BUILD.gn` - Build configuration for the example
- `main.cpp` - Basic application entry point
- `CHIPProjectConfig.h` - Platform configuration

This minimal example provides the most basic Matter device functionality with only the required clusters, making it ideal for testing the core Matter stack on Bouffalo Lab platforms.
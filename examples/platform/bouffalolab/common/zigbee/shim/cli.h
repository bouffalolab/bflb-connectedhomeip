/*
 * Shim header for zbapp_otaServerHandler.c when the SDK shell is OFF
 * (CONFIG_SHELL != y).
 *
 * zbapp_otaServerHandler.c does `#if defined(CONFIG_SHELL) ... #include "shell.h"
 * ... #else #include "cli.h" #include "utils_string.h" #endif`. The SDK does not
 * ship the legacy cli.h, so this empty shim lets the include resolve. Every symbol
 * the file actually uses from cli.h/utils_string.h (ZB_CLI, ZB_CLI_CMD,
 * get_*_from_string) sits under `#if defined(CFG_ZIGBEE_CLI)`, which is off when the
 * shell is off (CONFIG_ZIGBEE_CLI=0), so the shim body can stay empty.
 */
#ifndef SHIM_CLI_H
#define SHIM_CLI_H
#endif /* SHIM_CLI_H */

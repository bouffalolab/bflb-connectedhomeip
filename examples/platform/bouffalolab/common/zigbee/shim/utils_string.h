/*
 * Shim header for zbapp_otaServerHandler.c when the SDK shell is OFF
 * (CONFIG_SHELL != y). See shim/cli.h for the rationale: the SDK lacks the legacy
 * utils_string.h, and every symbol the file uses from it (get_*_from_string) is
 * under `#if defined(CFG_ZIGBEE_CLI)` (off when the shell is off), so this can be
 * empty.
 */
#ifndef SHIM_UTILS_STRING_H
#define SHIM_UTILS_STRING_H
#endif /* SHIM_UTILS_STRING_H */

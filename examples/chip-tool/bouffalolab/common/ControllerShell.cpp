/*
 * Copyright (c) 2026 Project CHIP Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 */

#include <lib/core/CHIPError.h>
#include <lib/shell/Engine.h>

#include <cstdio>

extern "C" {
#include <shell.h>
}

void cmd_matter(size_t argc, char ** argv)
{
    if (argc > 1)
    {
        TEMPORARY_RETURN_IGNORED chip::Shell::Engine::Root().ExecCommand(static_cast<int>(argc - 1), argv + 1);
    }
}

SHELL_CMD_EXPORT_ALIAS(cmd_matter, matter, matter command);

#if defined(BL618DG)
extern "C" void cmd_set_btble_standalone(int argc, char ** argv);
extern "C" void cmd_set_btble_combo(int argc, char ** argv);

void cmd_btble_path_standalone(size_t argc, char ** argv)
{
    (void) argv;
    if (argc != 1)
    {
        printf("Usage: btble_path_standalone\r\n");
        return;
    }

    printf("[RF_PATH] Calling cmd_set_btble_standalone\r\n");
    cmd_set_btble_standalone(0, nullptr);
    printf("[RF_PATH] cmd_set_btble_standalone returned\r\n");
}

void cmd_btble_path_combo(size_t argc, char ** argv)
{
    (void) argv;
    if (argc != 1)
    {
        printf("Usage: btble_path_combo\r\n");
        return;
    }

    printf("[RF_PATH] Calling cmd_set_btble_combo\r\n");
    cmd_set_btble_combo(0, nullptr);
    printf("[RF_PATH] cmd_set_btble_combo returned\r\n");
}

SHELL_CMD_EXPORT_ALIAS(cmd_btble_path_standalone, btble_path_standalone, Select BT standalone RF path);
SHELL_CMD_EXPORT_ALIAS(cmd_btble_path_combo, btble_path_combo, Select BT/Wi-Fi combo RF path);
#endif

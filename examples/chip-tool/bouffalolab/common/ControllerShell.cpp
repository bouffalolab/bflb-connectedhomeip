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

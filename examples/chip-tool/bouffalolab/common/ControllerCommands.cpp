/*
 * Copyright (c) 2026 Project CHIP Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "ControllerCommands.h"

#include <commands/common/Commands.h>
#include <commands/example/ExampleCredentialIssuerCommands.h>
#include <commands/pairing/Commands.h>
#include <commands/session-management/Commands.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMem.h>
#include <lib/shell/Engine.h>

#include <cstdlib>

namespace {

Commands sChipToolCommands;
ExampleCredentialIssuerCommands sCredentialIssuerCommands;

CHIP_ERROR RunChipTool(int argc, char * argv[])
{
    constexpr int kMaxArguments = 32;
    VerifyOrReturnError(argc < kMaxArguments, CHIP_ERROR_INVALID_ARGUMENT);
    char * commandArgv[kMaxArguments + 1] = { const_cast<char *>("chip-tool") };
    for (int index = 0; index < argc; ++index)
    {
        commandArgv[index + 1] = argv[index];
    }
    return sChipToolCommands.Run(argc + 1, commandArgv) == EXIT_SUCCESS ? CHIP_NO_ERROR : CHIP_ERROR_INTERNAL;
}

chip::Shell::shell_command_t sControllerShellCommands[] = {
    { RunChipTool, "chip-tool", "Run a Matter controller command" },
};

} // namespace

void ControllerCommands::Init()
{
    chip::Shell::Engine::Root().RegisterCommands(sControllerShellCommands, 1);
    registerCommandsPairing(sChipToolCommands, &sCredentialIssuerCommands);
    registerCommandsSessionManagement(sChipToolCommands, &sCredentialIssuerCommands);
}

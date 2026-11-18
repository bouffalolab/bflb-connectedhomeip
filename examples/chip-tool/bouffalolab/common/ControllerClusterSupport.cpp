/*
 * Copyright (c) 2026 Project CHIP Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include <commands/common/BDXDiagnosticLogsServerDelegate.h>

// The embedded controller doesn't serve BDX diagnostic logs. ClusterCommand
// references this delegate only to clear per-command file state on completion.
BDXDiagnosticLogsServerDelegate BDXDiagnosticLogsServerDelegate::sInstance;

void BDXDiagnosticLogsServerDelegate::AddFileDesignator(chip::app::CommandSender * sender,
                                                        const chip::CharSpan & fileDesignator)
{
    (void) sender;
    (void) fileDesignator;
}

void BDXDiagnosticLogsServerDelegate::RemoveFileDesignator(chip::app::CommandSender * sender)
{
    (void) sender;
}

CHIP_ERROR BDXDiagnosticLogsServerDelegate::OnTransferBegin(chip::bdx::BDXTransferProxy * transfer)
{
    (void) transfer;
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR BDXDiagnosticLogsServerDelegate::OnTransferEnd(chip::bdx::BDXTransferProxy * transfer, CHIP_ERROR error)
{
    (void) transfer;
    (void) error;
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR BDXDiagnosticLogsServerDelegate::OnTransferData(chip::bdx::BDXTransferProxy * transfer,
                                                            const chip::ByteSpan & data)
{
    (void) transfer;
    (void) data;
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

/*
 * Copyright (c) 2026 Project CHIP Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#include "ControllerDataModelProvider.h"

#include <protocols/interaction_model/StatusCode.h>

namespace chip {
namespace Controller {
namespace {
using Status = Protocols::InteractionModel::Status;
}

CHIP_ERROR ControllerDataModelProvider::Endpoints(ReadOnlyBufferBuilder<app::DataModel::EndpointEntry> &)
{
    return CHIP_NO_ERROR;
}

CHIP_ERROR ControllerDataModelProvider::DeviceTypes(EndpointId, ReadOnlyBufferBuilder<app::DataModel::DeviceTypeEntry> &)
{
    return CHIP_IM_GLOBAL_STATUS(UnsupportedEndpoint);
}

CHIP_ERROR ControllerDataModelProvider::ClientClusters(EndpointId, ReadOnlyBufferBuilder<ClusterId> &)
{
    return CHIP_IM_GLOBAL_STATUS(UnsupportedEndpoint);
}

CHIP_ERROR ControllerDataModelProvider::ServerClusters(EndpointId, ReadOnlyBufferBuilder<app::DataModel::ServerClusterEntry> &)
{
    return CHIP_IM_GLOBAL_STATUS(UnsupportedEndpoint);
}

#if CHIP_CONFIG_USE_ENDPOINT_UNIQUE_ID
CHIP_ERROR ControllerDataModelProvider::EndpointUniqueID(EndpointId, MutableCharSpan &)
{
    return CHIP_IM_GLOBAL_STATUS(UnsupportedEndpoint);
}
#endif

CHIP_ERROR ControllerDataModelProvider::EventInfo(const app::ConcreteEventPath &, app::DataModel::EventEntry &)
{
    return CHIP_IM_GLOBAL_STATUS(UnsupportedEndpoint);
}

CHIP_ERROR ControllerDataModelProvider::Attributes(const app::ConcreteClusterPath &,
                                                   ReadOnlyBufferBuilder<app::DataModel::AttributeEntry> &)
{
    return CHIP_IM_GLOBAL_STATUS(UnsupportedEndpoint);
}

CHIP_ERROR ControllerDataModelProvider::GeneratedCommands(const app::ConcreteClusterPath &, ReadOnlyBufferBuilder<CommandId> &)
{
    return CHIP_IM_GLOBAL_STATUS(UnsupportedEndpoint);
}

CHIP_ERROR ControllerDataModelProvider::AcceptedCommands(
    const app::ConcreteClusterPath &, ReadOnlyBufferBuilder<app::DataModel::AcceptedCommandEntry> &)
{
    return CHIP_IM_GLOBAL_STATUS(UnsupportedEndpoint);
}

app::DataModel::ActionReturnStatus ControllerDataModelProvider::ReadAttribute(const app::DataModel::ReadAttributeRequest &,
                                                                               app::AttributeValueEncoder &)
{
    return Status::UnsupportedEndpoint;
}

app::DataModel::ActionReturnStatus ControllerDataModelProvider::WriteAttribute(const app::DataModel::WriteAttributeRequest &,
                                                                                app::AttributeValueDecoder &)
{
    return Status::UnsupportedEndpoint;
}

std::optional<app::DataModel::ActionReturnStatus> ControllerDataModelProvider::InvokeCommand(
    const app::DataModel::InvokeRequest &, TLV::TLVReader &, app::CommandHandler *)
{
    return Status::UnsupportedEndpoint;
}

} // namespace Controller
} // namespace chip

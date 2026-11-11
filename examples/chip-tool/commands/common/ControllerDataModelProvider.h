/*
 * Copyright (c) 2026 Project CHIP Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#pragma once

#include <app/data-model-provider/Provider.h>

namespace chip {
namespace Controller {

/// Data model provider for controller-only builds, which expose no local endpoints.
class ControllerDataModelProvider final : public app::DataModel::Provider
{
public:
    CHIP_ERROR Endpoints(ReadOnlyBufferBuilder<app::DataModel::EndpointEntry> & builder) override;
    CHIP_ERROR DeviceTypes(EndpointId endpointId, ReadOnlyBufferBuilder<app::DataModel::DeviceTypeEntry> & builder) override;
    CHIP_ERROR ClientClusters(EndpointId endpointId, ReadOnlyBufferBuilder<ClusterId> & builder) override;
    CHIP_ERROR ServerClusters(EndpointId endpointId, ReadOnlyBufferBuilder<app::DataModel::ServerClusterEntry> & builder) override;
#if CHIP_CONFIG_USE_ENDPOINT_UNIQUE_ID
    CHIP_ERROR EndpointUniqueID(EndpointId endpointId, MutableCharSpan & endpointUniqueId) override;
#endif
    CHIP_ERROR EventInfo(const app::ConcreteEventPath & path, app::DataModel::EventEntry & eventInfo) override;
    CHIP_ERROR Attributes(const app::ConcreteClusterPath & path,
                          ReadOnlyBufferBuilder<app::DataModel::AttributeEntry> & builder) override;
    CHIP_ERROR GeneratedCommands(const app::ConcreteClusterPath & path, ReadOnlyBufferBuilder<CommandId> & builder) override;
    CHIP_ERROR AcceptedCommands(const app::ConcreteClusterPath & path,
                                ReadOnlyBufferBuilder<app::DataModel::AcceptedCommandEntry> & builder) override;
    void ListAttributeWriteNotification(const app::ConcreteAttributePath &, app::DataModel::ListWriteOperation, FabricIndex) override {}

    app::DataModel::ActionReturnStatus ReadAttribute(const app::DataModel::ReadAttributeRequest & request,
                                                     app::AttributeValueEncoder & encoder) override;
    app::DataModel::ActionReturnStatus WriteAttribute(const app::DataModel::WriteAttributeRequest & request,
                                                      app::AttributeValueDecoder & decoder) override;
    std::optional<app::DataModel::ActionReturnStatus> InvokeCommand(const app::DataModel::InvokeRequest & request,
                                                                    TLV::TLVReader & inputArguments,
                                                                    app::CommandHandler * handler) override;
};

} // namespace Controller
} // namespace chip

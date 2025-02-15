/* Copyright 2020 The OpenXLA Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "xla/pjrt/interpreter_device.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "xla/client/client_library.h"
#include "xla/client/local_client.h"
#include "xla/pjrt/local_device_state.h"
#include "xla/pjrt/pjrt_client.h"
#include "xla/pjrt/pjrt_stream_executor_client.h"
#include "xla/service/platform_util.h"
#include "xla/stream_executor/platform.h"
#include "xla/stream_executor/stream_executor.h"
#include "xla/tsl/platform/statusor.h"
#include "xla/util.h"
#include "tsl/platform/fingerprint.h"

namespace xla {

static const char kInterpreterPlatformName[] = "interpreter";

const int InterpreterMemorySpace::kKindId = []() {
  uint32_t kind_id = tsl::Fingerprint32(InterpreterMemorySpace::kKind);
  return static_cast<int>(kind_id);
}();

InterpreterDevice::InterpreterDevice(
    int id, std::unique_ptr<LocalDeviceState> local_device_state)
    : PjRtStreamExecutorDevice(id, std::move(local_device_state),
                               /*device_kind=*/kInterpreterPlatformName) {}

absl::StatusOr<std::unique_ptr<PjRtClient>> GetInterpreterClient() {
  TF_ASSIGN_OR_RETURN(se::Platform * platform,
                      PlatformUtil::GetPlatform("Interpreter"));
  if (platform->VisibleDeviceCount() != 1) {
    return FailedPrecondition(
        "Interpreter platform should have exactly one device.");
  }
  LocalClientOptions options;
  options.set_platform(platform);
  TF_ASSIGN_OR_RETURN(LocalClient * client,
                      ClientLibrary::GetOrCreateLocalClient(options));

  se::StreamExecutor* executor = client->backend().stream_executor(0).value();
  auto device_state = std::make_unique<LocalDeviceState>(
      executor, client, LocalDeviceState::kSynchronous,
      /*max_inflight_computations=*/1,
      /*allow_event_reuse=*/false, /*use_callback_stream=*/false);
  auto device = std::make_unique<InterpreterDevice>(0, std::move(device_state));
  auto memory_space = std::make_unique<InterpreterMemorySpace>(0, device.get());
  device->AttachMemorySpace(memory_space.get(), /*is_default=*/true);
  std::vector<std::unique_ptr<PjRtMemorySpace>> memory_spaces;
  memory_spaces.push_back(std::move(memory_space));
  std::vector<std::unique_ptr<PjRtStreamExecutorDevice>> devices;
  devices.push_back(std::move(device));

  return std::unique_ptr<PjRtClient>(std::make_unique<PjRtStreamExecutorClient>(
      "interpreter", client, std::move(devices),
      /*process_index=*/0, std::move(memory_spaces), /*allocator=*/nullptr,
      /*host_memory_allocator=*/nullptr,
      /*should_stage_host_to_device_transfers=*/false,
      /*gpu_run_options=*/nullptr));
}

}  // namespace xla

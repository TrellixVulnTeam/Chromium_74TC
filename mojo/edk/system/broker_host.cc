// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/broker_host.h"

#include <utility>

#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "base/threading/thread_task_runner_handle.h"
#include "mojo/edk/embedder/platform_handle_vector.h"
#include "mojo/edk/embedder/platform_shared_buffer.h"
#include "mojo/edk/system/broker_messages.h"

namespace mojo {
namespace edk {

namespace {

// To prevent abuse, limit the maximum size of shared memory buffers.
// TODO(rockot): Re-consider this limit, or do something smarter.
const uint32_t kMaxSharedBufferSize = 16 * 1024 * 1024;

}  // namespace

BrokerHost::BrokerHost(base::ProcessHandle client_process,
                       ScopedPlatformHandle platform_handle)
#if defined(OS_WIN)
    : client_process_(client_process)
#endif
{
  CHECK(platform_handle.is_valid());

  base::MessageLoop::current()->AddDestructionObserver(this);

  channel_ = Channel::Create(
      this, std::move(platform_handle), base::ThreadTaskRunnerHandle::Get());
  channel_->Start();
}

BrokerHost::~BrokerHost() {
  // We're always destroyed on the creation thread, which is the IO thread.
  base::MessageLoop::current()->RemoveDestructionObserver(this);

  if (channel_)
    channel_->ShutDown();
}

void BrokerHost::PrepareHandlesForClient(PlatformHandleVector* handles) {
#if defined(OS_WIN)
  if (!Channel::Message::RewriteHandles(
      base::GetCurrentProcessHandle(), client_process_, handles)) {
    // NOTE: We only log an error here. We do not signal a logical error or
    // prevent any message from being sent. The client should handle unexpected
    // invalid handles appropriately.
    DLOG(ERROR) << "Failed to rewrite one or more handles to broker client.";
  }
#endif
}

void BrokerHost::SendChannel(ScopedPlatformHandle handle) {
  CHECK(handle.is_valid());
  CHECK(channel_);

  Channel::MessagePtr message =
      CreateBrokerMessage(BrokerMessageType::INIT, 1, nullptr);
  ScopedPlatformHandleVectorPtr handles;
  handles.reset(new PlatformHandleVector(1));
  handles->at(0) = handle.release();
  PrepareHandlesForClient(handles.get());
  message->SetHandles(std::move(handles));
  channel_->Write(std::move(message));
}

void BrokerHost::OnBufferRequest(uint32_t num_bytes) {
  scoped_refptr<PlatformSharedBuffer> buffer;
  scoped_refptr<PlatformSharedBuffer> read_only_buffer;
  if (num_bytes <= kMaxSharedBufferSize) {
    buffer = PlatformSharedBuffer::Create(num_bytes);
    if (buffer)
      read_only_buffer = buffer->CreateReadOnlyDuplicate();
    if (!read_only_buffer)
      buffer = nullptr;
  } else {
    LOG(ERROR) << "Shared buffer request too large: " << num_bytes;
  }

  Channel::MessagePtr message = CreateBrokerMessage(
      BrokerMessageType::BUFFER_RESPONSE, buffer ? 2 : 0, nullptr);
  if (buffer) {
    ScopedPlatformHandleVectorPtr handles;
    handles.reset(new PlatformHandleVector(2));
    handles->at(0) = buffer->PassPlatformHandle().release();
    handles->at(1) = read_only_buffer->PassPlatformHandle().release();
    PrepareHandlesForClient(handles.get());
    message->SetHandles(std::move(handles));
  }

  channel_->Write(std::move(message));
}

void BrokerHost::OnChannelMessage(const void* payload,
                                  size_t payload_size,
                                  ScopedPlatformHandleVectorPtr handles) {
  if (payload_size < sizeof(BrokerMessageHeader))
    return;

  const BrokerMessageHeader* header =
      static_cast<const BrokerMessageHeader*>(payload);
  switch (header->type) {
    case BrokerMessageType::BUFFER_REQUEST:
      if (payload_size ==
            sizeof(BrokerMessageHeader) + sizeof(BufferRequestData)) {
        const BufferRequestData* request =
            reinterpret_cast<const BufferRequestData*>(header + 1);
        OnBufferRequest(request->size);
      }
      break;

    default:
      LOG(ERROR) << "Unexpected broker message type: " << header->type;
      break;
  }
}

void BrokerHost::OnChannelError() { delete this; }

void BrokerHost::WillDestroyCurrentMessageLoop() { delete this; }

}  // namespace edk
}  // namespace mojo

// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/media/midi_host.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/metrics/histogram_macros.h"
#include "base/process/process.h"
#include "base/trace_event/trace_event.h"
#include "content/browser/bad_message.h"
#include "content/browser/browser_main_loop.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/common/media/midi_messages.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/user_metrics.h"
#include "media/midi/midi_manager.h"
#include "media/midi/midi_message_queue.h"
#include "media/midi/midi_message_util.h"

namespace content {
namespace {

// The total number of bytes which we're allowed to send to the OS
// before knowing that they have been successfully sent.
const size_t kMaxInFlightBytes = 10 * 1024 * 1024;  // 10 MB.

// We keep track of the number of bytes successfully sent to
// the hardware.  Every once in a while we report back to the renderer
// the number of bytes sent since the last report. This threshold determines
// how many bytes will be sent before reporting back to the renderer.
const size_t kAcknowledgementThresholdBytes = 1024 * 1024;  // 1 MB.

bool IsDataByte(uint8_t data) {
  return (data & 0x80) == 0;
}

bool IsSystemRealTimeMessage(uint8_t data) {
  return 0xf8 <= data && data <= 0xff;
}

}  // namespace

using midi::kSysExByte;
using midi::kEndOfSysExByte;
using midi::mojom::Result;

MidiHost::MidiHost(int renderer_process_id,
                   midi::MidiManager* midi_manager)
    : BrowserMessageFilter(MidiMsgStart),
      renderer_process_id_(renderer_process_id),
      has_sys_ex_permission_(false),
      is_session_requested_(false),
      midi_manager_(midi_manager),
      sent_bytes_in_flight_(0),
      bytes_sent_since_last_acknowledgement_(0),
      output_port_count_(0) {
  DCHECK(midi_manager_);
}

MidiHost::~MidiHost() = default;

void MidiHost::OnChannelClosing() {
  // If we get here the MidiHost is going to be destroyed soon. Prevent any
  // subsequent calls from MidiManager by closing our session.
  // If Send() is called from a different thread (e.g. a separate thread owned
  // by the MidiManager implementation), it will get posted to the IO thread.
  // There is a race condition here if our refcount is 0 and we're about to or
  // have already entered OnDestruct().
  if (is_session_requested_ && midi_manager_) {
    midi_manager_->EndSession(this);
    is_session_requested_ = false;
  }
}

void MidiHost::OnDestruct() const {
  BrowserThread::DeleteOnIOThread::Destruct(this);
}

// IPC Messages handler
bool MidiHost::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(MidiHost, message)
    IPC_MESSAGE_HANDLER(MidiHostMsg_StartSession, OnStartSession)
    IPC_MESSAGE_HANDLER(MidiHostMsg_SendData, OnSendData)
    IPC_MESSAGE_HANDLER(MidiHostMsg_EndSession, OnEndSession)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  return handled;
}

void MidiHost::OnStartSession() {
  is_session_requested_ = true;
  if (midi_manager_)
    midi_manager_->StartSession(this);
}

void MidiHost::OnSendData(uint32_t port,
                          const std::vector<uint8_t>& data,
                          double timestamp) {
  {
    base::AutoLock auto_lock(output_port_count_lock_);
    if (output_port_count_ <= port) {
      bad_message::ReceivedBadMessage(this, bad_message::MH_INVALID_MIDI_PORT);
      return;
    }
  }

  if (data.empty())
    return;

  // Blink running in a renderer checks permission to raise a SecurityError
  // in JavaScript. The actual permission check for security purposes
  // happens here in the browser process.
  if (!has_sys_ex_permission_ &&
      std::find(data.begin(), data.end(), kSysExByte) != data.end()) {
    bad_message::ReceivedBadMessage(this, bad_message::MH_SYS_EX_PERMISSION);
    return;
  }

  if (!IsValidWebMIDIData(data))
    return;

  {
    base::AutoLock auto_lock(in_flight_lock_);
    // Sanity check that we won't send too much data.
    // TODO(yukawa): Consider to send an error event back to the renderer
    // after some future discussion in W3C.
    if (data.size() + sent_bytes_in_flight_ > kMaxInFlightBytes)
      return;
    sent_bytes_in_flight_ += data.size();
  }
  if (midi_manager_)
    midi_manager_->DispatchSendMidiData(this, port, data, timestamp);
}

void MidiHost::OnEndSession() {
  is_session_requested_ = false;
  if (midi_manager_)
    midi_manager_->EndSession(this);
}

void MidiHost::CompleteStartSession(Result result) {
  DCHECK(is_session_requested_);
  if (result == Result::OK) {
    // ChildSecurityPolicy is set just before OnStartSession by
    // MidiDispatcherHost. So we can safely cache the policy.
    has_sys_ex_permission_ = ChildProcessSecurityPolicyImpl::GetInstance()->
        CanSendMidiSysExMessage(renderer_process_id_);
  }
  Send(new MidiMsg_SessionStarted(result));
}

void MidiHost::AddInputPort(const midi::MidiPortInfo& info) {
  base::AutoLock auto_lock(messages_queues_lock_);
  // MidiMessageQueue is created later in ReceiveMidiData().
  received_messages_queues_.push_back(nullptr);
  Send(new MidiMsg_AddInputPort(info));
}

void MidiHost::AddOutputPort(const midi::MidiPortInfo& info) {
  base::AutoLock auto_lock(output_port_count_lock_);
  output_port_count_++;
  Send(new MidiMsg_AddOutputPort(info));
}

void MidiHost::SetInputPortState(uint32_t port,
                                 midi::MidiPortState state) {
  Send(new MidiMsg_SetInputPortState(port, state));
}

void MidiHost::SetOutputPortState(uint32_t port,
                                  midi::MidiPortState state) {
  Send(new MidiMsg_SetOutputPortState(port, state));
}

void MidiHost::ReceiveMidiData(uint32_t port,
                               const uint8_t* data,
                               size_t length,
                               double timestamp) {
  TRACE_EVENT0("midi", "MidiHost::ReceiveMidiData");

  base::AutoLock auto_lock(messages_queues_lock_);
  if (received_messages_queues_.size() <= port)
    return;

  // Lazy initialization
  if (received_messages_queues_[port] == nullptr)
    received_messages_queues_[port] = new midi::MidiMessageQueue(true);

  received_messages_queues_[port]->Add(data, length);
  std::vector<uint8_t> message;
  while (true) {
    received_messages_queues_[port]->Get(&message);
    if (message.empty())
      break;

    // MIDI devices may send a system exclusive messages even if the renderer
    // doesn't have a permission to receive it. Don't kill the renderer as
    // OnSendData() does.
    if (message[0] == kSysExByte && !has_sys_ex_permission_)
      continue;

    // Send to the renderer.
    Send(new MidiMsg_DataReceived(port, message, timestamp));
  }
}

void MidiHost::AccumulateMidiBytesSent(size_t n) {
  {
    base::AutoLock auto_lock(in_flight_lock_);
    if (n <= sent_bytes_in_flight_)
      sent_bytes_in_flight_ -= n;
  }

  if (bytes_sent_since_last_acknowledgement_ + n >=
      bytes_sent_since_last_acknowledgement_)
    bytes_sent_since_last_acknowledgement_ += n;

  if (bytes_sent_since_last_acknowledgement_ >=
      kAcknowledgementThresholdBytes) {
    Send(new MidiMsg_AcknowledgeSentData(
        bytes_sent_since_last_acknowledgement_));
    bytes_sent_since_last_acknowledgement_ = 0;
  }
}

void MidiHost::Detach() {
  midi_manager_ = nullptr;
}

// static
bool MidiHost::IsValidWebMIDIData(const std::vector<uint8_t>& data) {
  bool in_sysex = false;
  size_t sysex_start_offset = 0;
  size_t waiting_data_length = 0;
  for (size_t i = 0; i < data.size(); ++i) {
    const uint8_t current = data[i];
    if (IsSystemRealTimeMessage(current))
      continue;  // Real time message can be placed at any point.
    if (waiting_data_length > 0) {
      if (!IsDataByte(current))
        return false;  // Error: |current| should have been data byte.
      --waiting_data_length;
      continue;  // Found data byte as expected.
    }
    if (in_sysex) {
      if (data[i] == kEndOfSysExByte) {
        in_sysex = false;
        UMA_HISTOGRAM_COUNTS("Media.Midi.SysExMessageSizeUpTo1MB",
                             i - sysex_start_offset + 1);
      } else if (!IsDataByte(current)) {
        return false;  // Error: |current| should have been data byte.
      }
      continue;  // Found data byte as expected.
    }
    if (current == kSysExByte) {
      in_sysex = true;
      sysex_start_offset = i;
      continue;  // Found SysEX
    }
    waiting_data_length = midi::GetMidiMessageLength(current);
    if (waiting_data_length == 0)
      return false;  // Error: |current| should have been a valid status byte.
    --waiting_data_length;  // Found status byte
  }
  return waiting_data_length == 0 && !in_sysex;
}

}  // namespace content

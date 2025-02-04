// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/video_capture/video_capture_device_proxy_impl.h"

#include "base/logging.h"
#include "media/capture/video/video_capture_buffer_pool_impl.h"
#include "media/capture/video/video_capture_jpeg_decoder.h"
#include "services/video_capture/buffer_tracker_factory_impl.h"
#include "services/video_capture/receiver_mojo_to_media_adapter.h"

namespace video_capture {

VideoCaptureDeviceProxyImpl::VideoCaptureDeviceProxyImpl(
    std::unique_ptr<media::VideoCaptureDevice> device,
    const media::VideoCaptureJpegDecoderFactoryCB&
        jpeg_decoder_factory_callback)
    : device_(std::move(device)),
      jpeg_decoder_factory_callback_(jpeg_decoder_factory_callback),
      device_running_(false) {}

VideoCaptureDeviceProxyImpl::~VideoCaptureDeviceProxyImpl() {
  if (device_running_)
    device_->StopAndDeAllocate();
}

void VideoCaptureDeviceProxyImpl::Start(
   const media::VideoCaptureFormat& requested_format,
    media::ResolutionChangePolicy resolution_change_policy,
    media::PowerLineFrequency power_line_frequency,
    mojom::VideoFrameReceiverPtr receiver) {
  media::VideoCaptureParams params;
  params.requested_format = requested_format;
  params.resolution_change_policy = resolution_change_policy;
  params.power_line_frequency = power_line_frequency;
  receiver.set_connection_error_handler(
      base::Bind(&VideoCaptureDeviceProxyImpl::OnClientConnectionErrorOrClose,
                 base::Unretained(this)));

  auto media_receiver =
      base::MakeUnique<ReceiverMojoToMediaAdapter>(std::move(receiver));

  // Create a dedicated buffer pool for the device usage session.
  const int kMaxBufferCount = 2;
  auto buffer_tracker_factory = base::MakeUnique<BufferTrackerFactoryImpl>();
  scoped_refptr<media::VideoCaptureBufferPool> buffer_pool(
      new media::VideoCaptureBufferPoolImpl(std::move(buffer_tracker_factory),
                                            kMaxBufferCount));

  auto device_client = base::MakeUnique<media::VideoCaptureDeviceClient>(
      std::move(media_receiver), buffer_pool, jpeg_decoder_factory_callback_);

  device_->AllocateAndStart(params, std::move(device_client));
  device_running_ = true;
}

void VideoCaptureDeviceProxyImpl::Stop() {
  device_->StopAndDeAllocate();
  device_running_ = false;
}

void VideoCaptureDeviceProxyImpl::OnClientConnectionErrorOrClose() {
  if (device_running_) {
    device_->StopAndDeAllocate();
    device_running_ = false;
  }
}

}  // namespace video_capture

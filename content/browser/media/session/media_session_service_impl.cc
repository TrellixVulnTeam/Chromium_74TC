// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/media/session/media_session_service_impl.h"

#include "content/browser/media/session/media_metadata_sanitizer.h"
#include "content/browser/media/session/media_session.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"

namespace content {

MediaSessionServiceImpl::MediaSessionServiceImpl(
    RenderFrameHost* render_frame_host)
    : render_frame_host_(render_frame_host) {}

MediaSessionServiceImpl::~MediaSessionServiceImpl() = default;

// static
void MediaSessionServiceImpl::Create(
    RenderFrameHost* render_frame_host,
    blink::mojom::MediaSessionServiceRequest request) {
  MediaSessionServiceImpl* impl =
      new MediaSessionServiceImpl(render_frame_host);
  impl->Bind(std::move(request));
}

void MediaSessionServiceImpl::SetMetadata(
    const base::Optional<content::MediaMetadata>& metadata) {
  // When receiving a MediaMetadata, the browser process can't trust that it is
  // coming from a known and secure source. It must be processed accordingly.
  if (metadata.has_value() &&
      !MediaMetadataSanitizer::CheckSanity(metadata.value())) {
    render_frame_host_->GetProcess()->ShutdownForBadMessage(
        RenderProcessHost::CrashReportMode::GENERATE_CRASH_DUMP);
    return;
  }

  WebContentsImpl* contents = static_cast<WebContentsImpl*>(
      WebContentsImpl::FromRenderFrameHost(render_frame_host_));
  if (contents)
    MediaSession::Get(contents)->SetMetadata(metadata);
}

void MediaSessionServiceImpl::Bind(
    blink::mojom::MediaSessionServiceRequest request) {
  binding_.reset(new mojo::Binding<blink::mojom::MediaSessionService>(
      this, std::move(request)));
}

}  // namespace content

// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_WS_SERVER_WINDOW_SURFACE_H_
#define SERVICES_UI_WS_SERVER_WINDOW_SURFACE_H_

#include <set>

#include "base/macros.h"
#include "cc/ipc/compositor_frame.mojom.h"
#include "cc/surfaces/frame_sink_id.h"
#include "cc/surfaces/surface_factory.h"
#include "cc/surfaces/surface_factory_client.h"
#include "cc/surfaces/surface_id.h"
#include "cc/surfaces/surface_id_allocator.h"
#include "cc/surfaces/surface_sequence_generator.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "services/ui/public/interfaces/surface.mojom.h"
#include "services/ui/public/interfaces/window_tree.mojom.h"
#include "services/ui/ws/ids.h"

namespace ui {

class DisplayCompositor;

namespace ws {

class ServerWindow;
class ServerWindowSurfaceManager;

// Server side representation of a WindowSurface.
class ServerWindowSurface : public mojom::Surface,
                            public cc::SurfaceFactoryClient {
 public:
  ServerWindowSurface(ServerWindowSurfaceManager* manager,
                      const cc::FrameSinkId& frame_sink_id,
                      mojo::InterfaceRequest<mojom::Surface> request,
                      mojom::SurfaceClientPtr client);

  ~ServerWindowSurface() override;

  const gfx::Size& last_submitted_frame_size() const {
    return last_submitted_frame_size_;
  }

  bool may_contain_video() const { return may_contain_video_; }

  // mojom::Surface:
  void SubmitCompositorFrame(
      cc::CompositorFrame frame,
      const SubmitCompositorFrameCallback& callback) override;

  // There is a 1-1 correspondence between FrameSinks and frame sources.
  // The FrameSinkId uniquely identifies the FrameSink, and since there is
  // one FrameSink per ServerWindowSurface, it allows the window server
  // to uniquely identify the window, and the thus the client that generated the
  // frame.
  const cc::FrameSinkId& frame_sink_id() const { return frame_sink_id_; }

  // The LocalFrameId can be thought of as an identifier to a bucket of
  // sequentially submitted CompositorFrames in the same FrameSink all sharing
  // the same size and device scale factor.
  const cc::LocalFrameId& local_frame_id() const { return local_frame_id_; }

  bool has_frame() const { return !local_frame_id_.is_null(); }

  cc::SurfaceId GetSurfaceId() const;

  // Creates a surface dependency token that expires when this
  // ServerWindowSurface goes away.
  cc::SurfaceSequence CreateSurfaceSequence();

  ServerWindow* window();

 private:

  // SurfaceFactoryClient implementation.
  void ReturnResources(const cc::ReturnedResourceArray& resources) override;
  void SetBeginFrameSource(cc::BeginFrameSource* begin_frame_source) override;

  const cc::FrameSinkId frame_sink_id_;
  cc::SurfaceSequenceGenerator surface_sequence_generator_;

  ServerWindowSurfaceManager* manager_;  // Owns this.

  gfx::Size last_submitted_frame_size_;

  cc::LocalFrameId local_frame_id_;
  cc::SurfaceIdAllocator surface_id_allocator_;
  cc::SurfaceFactory surface_factory_;

  mojom::SurfaceClientPtr client_;
  mojo::Binding<Surface> binding_;

  bool may_contain_video_ = false;

  DISALLOW_COPY_AND_ASSIGN(ServerWindowSurface);
};

}  // namespace ws

}  // namespace ui

#endif  // SERVICES_UI_WS_SERVER_WINDOW_SURFACE_H_

// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/surfaces/surface_factory.h"

#include <utility>

#include "base/trace_event/trace_event.h"
#include "cc/output/compositor_frame.h"
#include "cc/output/copy_output_request.h"
#include "cc/surfaces/surface.h"
#include "cc/surfaces/surface_factory_client.h"
#include "cc/surfaces/surface_manager.h"
#include "ui/gfx/geometry/size.h"

namespace cc {
SurfaceFactory::SurfaceFactory(const FrameSinkId& frame_sink_id,
                               SurfaceManager* manager,
                               SurfaceFactoryClient* client)
    : frame_sink_id_(frame_sink_id),
      manager_(manager),
      client_(client),
      holder_(client),
      needs_sync_points_(true) {}

SurfaceFactory::~SurfaceFactory() {
  if (!surface_map_.empty()) {
    LOG(ERROR) << "SurfaceFactory has " << surface_map_.size()
               << " entries in map on destruction.";
  }
  DestroyAll();
}

void SurfaceFactory::DestroyAll() {
  if (manager_) {
    for (auto& pair : surface_map_)
      manager_->Destroy(std::move(pair.second));
  }
  surface_map_.clear();
}

void SurfaceFactory::Create(const LocalFrameId& local_frame_id) {
  std::unique_ptr<Surface> surface(base::MakeUnique<Surface>(
      SurfaceId(frame_sink_id_, local_frame_id), this));
  manager_->RegisterSurface(surface.get());
  DCHECK(!surface_map_.count(local_frame_id));
  surface_map_[local_frame_id] = std::move(surface);
}

void SurfaceFactory::Destroy(const LocalFrameId& local_frame_id) {
  OwningSurfaceMap::iterator it = surface_map_.find(local_frame_id);
  DCHECK(it != surface_map_.end());
  DCHECK(it->second->factory().get() == this);
  std::unique_ptr<Surface> surface(std::move(it->second));
  surface_map_.erase(it);
  if (manager_)
    manager_->Destroy(std::move(surface));
}

void SurfaceFactory::SetPreviousFrameSurface(const LocalFrameId& new_id,
                                             const LocalFrameId& old_id) {
  OwningSurfaceMap::iterator it = surface_map_.find(new_id);
  DCHECK(it != surface_map_.end());
  Surface* old_surface =
      manager_->GetSurfaceForId(SurfaceId(frame_sink_id_, old_id));
  if (old_surface) {
    it->second->SetPreviousFrameSurface(old_surface);
  }
}

void SurfaceFactory::SubmitCompositorFrame(const LocalFrameId& local_frame_id,
                                           CompositorFrame frame,
                                           const DrawCallback& callback) {
  TRACE_EVENT0("cc", "SurfaceFactory::SubmitCompositorFrame");
  OwningSurfaceMap::iterator it = surface_map_.find(local_frame_id);
  DCHECK(it != surface_map_.end());
  DCHECK(it->second->factory().get() == this);
  const CompositorFrame& previous_frame = it->second->GetEligibleFrame();
  // Tell the SurfaceManager if this is the first frame submitted with this
  // LocalFrameId.
  if (!previous_frame.delegated_frame_data) {
    float device_scale_factor = frame.metadata.device_scale_factor;
    gfx::Size frame_size;
    // CompositorFrames may not be populated with a RenderPass in unit tests.
    if (frame.delegated_frame_data &&
        !frame.delegated_frame_data->render_pass_list.empty()) {
      frame_size =
          frame.delegated_frame_data->render_pass_list[0]->output_rect.size();
    }
    manager_->SurfaceCreated(it->second->surface_id(), frame_size,
                             device_scale_factor);
  }
  it->second->QueueFrame(std::move(frame), callback);
  if (!manager_->SurfaceModified(SurfaceId(frame_sink_id_, local_frame_id))) {
    TRACE_EVENT_INSTANT0("cc", "Damage not visible.", TRACE_EVENT_SCOPE_THREAD);
    it->second->RunDrawCallbacks();
  }
}

void SurfaceFactory::RequestCopyOfSurface(
    const LocalFrameId& local_frame_id,
    std::unique_ptr<CopyOutputRequest> copy_request) {
  OwningSurfaceMap::iterator it = surface_map_.find(local_frame_id);
  if (it == surface_map_.end()) {
    copy_request->SendEmptyResult();
    return;
  }
  DCHECK(it->second->factory().get() == this);
  it->second->RequestCopyOfOutput(std::move(copy_request));
  manager_->SurfaceModified(SurfaceId(frame_sink_id_, local_frame_id));
}

void SurfaceFactory::WillDrawSurface(const LocalFrameId& id,
                                     const gfx::Rect& damage_rect) {
  client_->WillDrawSurface(id, damage_rect);
}

void SurfaceFactory::ReceiveFromChild(
    const TransferableResourceArray& resources) {
  holder_.ReceiveFromChild(resources);
}

void SurfaceFactory::RefResources(const TransferableResourceArray& resources) {
  holder_.RefResources(resources);
}

void SurfaceFactory::UnrefResources(const ReturnedResourceArray& resources) {
  holder_.UnrefResources(resources);
}

}  // namespace cc

// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_FAKE_OUTPUT_SURFACE_CLIENT_H_
#define CC_TEST_FAKE_OUTPUT_SURFACE_CLIENT_H_

#include "cc/output/output_surface_client.h"

namespace cc {

class OutputSurface;

class FakeOutputSurfaceClient : public OutputSurfaceClient {
 public:
  FakeOutputSurfaceClient() = default;

  void SetNeedsRedrawRect(const gfx::Rect& damage_rect) override {}
  void DidReceiveSwapBuffersAck() override;
  void DidReceiveTextureInUseResponses(
      const gpu::TextureInUseResponses& responses) override {}
  void DidLoseOutputSurface() override;

  int swap_count() { return swap_count_; }

  bool did_lose_output_surface_called() {
    return did_lose_output_surface_called_;
  }

 private:
  int swap_count_ = 0;
  bool did_lose_output_surface_called_ = false;
};

}  // namespace cc

#endif  // CC_TEST_FAKE_OUTPUT_SURFACE_CLIENT_H_

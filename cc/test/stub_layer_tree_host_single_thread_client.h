// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_STUB_LAYER_TREE_HOST_SINGLE_THREAD_CLIENT_H_
#define CC_TEST_STUB_LAYER_TREE_HOST_SINGLE_THREAD_CLIENT_H_

#include "cc/trees/layer_tree_host_single_thread_client.h"

namespace cc {

class StubLayerTreeHostSingleThreadClient
    : public LayerTreeHostSingleThreadClient {
 public:
  ~StubLayerTreeHostSingleThreadClient() override;

  // LayerTreeHostSingleThreadClient implementation.
  void RequestScheduleComposite() override {}
  void RequestScheduleAnimation() override {}
  void DidPostSwapBuffers() override {}
  void DidReceiveCompositorFrameAck() override {}
  void DidAbortSwapBuffers() override {}
};

}  // namespace cc

#endif  // CC_TEST_STUB_LAYER_TREE_HOST_SINGLE_THREAD_CLIENT_H_

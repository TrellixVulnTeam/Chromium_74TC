// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/model/model_type_service.h"

#include <utility>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "components/sync/model/data_type_error_handler_mock.h"
#include "components/sync/model/fake_model_type_change_processor.h"
#include "components/sync/model/metadata_batch.h"
#include "components/sync/model/stub_model_type_service.h"
#include "components/sync/protocol/model_type_state.pb.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

// A mock MTCP that lets verify DisableSync and OnMetadataLoaded were called in
// the ways that we expect.
class MockModelTypeChangeProcessor : public FakeModelTypeChangeProcessor {
 public:
  explicit MockModelTypeChangeProcessor(const base::Closure& disabled_callback)
      : disabled_callback_(disabled_callback) {}
  ~MockModelTypeChangeProcessor() override {}

  void DisableSync() override { disabled_callback_.Run(); }

  void OnMetadataLoaded(SyncError error,
                        std::unique_ptr<MetadataBatch> batch) override {
    on_metadata_loaded_error_ = error;
    on_metadata_loaded_batch_ = std::move(batch);
  }

  const SyncError& on_metadata_loaded_error() const {
    return on_metadata_loaded_error_;
  }
  MetadataBatch* on_metadata_loaded_batch() {
    return on_metadata_loaded_batch_.get();
  }

 private:
  // This callback is invoked when DisableSync() is called, instead of
  // remembering that this event happened in our own state. The reason for this
  // is that after DisableSync() is called on us, the service is going to
  // destroy this processor instance, and any state would be lost. The callback
  // allows this information to reach somewhere safe instead.
  base::Closure disabled_callback_;

  SyncError on_metadata_loaded_error_;
  std::unique_ptr<MetadataBatch> on_metadata_loaded_batch_;
};

class MockModelTypeService : public StubModelTypeService {
 public:
  MockModelTypeService()
      : StubModelTypeService(base::Bind(&MockModelTypeService::CreateProcessor,
                                        base::Unretained(this))) {}
  ~MockModelTypeService() override {}

  MockModelTypeChangeProcessor* change_processor() const {
    return static_cast<MockModelTypeChangeProcessor*>(
        ModelTypeService::change_processor());
  }

  bool processor_disable_sync_called() const {
    return processor_disable_sync_called_;
  }

 private:
  std::unique_ptr<ModelTypeChangeProcessor> CreateProcessor(
      ModelType type,
      ModelTypeService* service) {
    return base::MakeUnique<MockModelTypeChangeProcessor>(base::Bind(
        &MockModelTypeService::OnProcessorDisableSync, base::Unretained(this)));
  }

  void OnProcessorDisableSync() { processor_disable_sync_called_ = true; }

  bool processor_disable_sync_called_ = false;
};

class ModelTypeServiceTest : public ::testing::Test {
 public:
  ModelTypeServiceTest() {}
  ~ModelTypeServiceTest() override {}

  void OnSyncStarting() {
    service_.OnSyncStarting(
        base::MakeUnique<DataTypeErrorHandlerMock>(),
        base::Bind(&ModelTypeServiceTest::OnProcessorStarted,
                   base::Unretained(this)));
  }

  bool start_callback_called() const { return start_callback_called_; }
  MockModelTypeService* service() { return &service_; }

 private:
  void OnProcessorStarted(
      SyncError error,
      std::unique_ptr<ActivationContext> activation_context) {
    start_callback_called_ = true;
  }

  bool start_callback_called_ = false;
  MockModelTypeService service_;
};

// OnSyncStarting should create a processor and call OnSyncStarting on it.
TEST_F(ModelTypeServiceTest, OnSyncStarting) {
  EXPECT_FALSE(start_callback_called());
  OnSyncStarting();

  // FakeModelTypeProcessor is the one that calls the callback, so if it was
  // called then we know the call on the processor was made.
  EXPECT_TRUE(start_callback_called());
}

// DisableSync should call DisableSync on the processor and then delete it.
TEST_F(ModelTypeServiceTest, DisableSync) {
  EXPECT_FALSE(service()->processor_disable_sync_called());
  service()->DisableSync();

  // Disabling also wipes out metadata, and the service should have told the new
  // processor about this.
  EXPECT_TRUE(service()->processor_disable_sync_called());

  EXPECT_FALSE(
      service()->change_processor()->on_metadata_loaded_error().IsSet());
  MetadataBatch* batch =
      service()->change_processor()->on_metadata_loaded_batch();
  EXPECT_NE(nullptr, batch);
  EXPECT_EQ(sync_pb::ModelTypeState().SerializeAsString(),
            batch->GetModelTypeState().SerializeAsString());
  EXPECT_EQ(0U, batch->TakeAllMetadata().size());
}

// ResolveConflicts should return USE_REMOTE unless the remote data is deleted.
TEST_F(ModelTypeServiceTest, DefaultConflictResolution) {
  EntityData local_data;
  EntityData remote_data;

  // There is no deleted/deleted case because that's not a conflict.

  local_data.specifics.mutable_preference()->set_value("value");
  EXPECT_FALSE(local_data.is_deleted());
  EXPECT_TRUE(remote_data.is_deleted());
  EXPECT_EQ(ConflictResolution::USE_LOCAL,
            service()->ResolveConflict(local_data, remote_data).type());

  remote_data.specifics.mutable_preference()->set_value("value");
  EXPECT_FALSE(local_data.is_deleted());
  EXPECT_FALSE(remote_data.is_deleted());
  EXPECT_EQ(ConflictResolution::USE_REMOTE,
            service()->ResolveConflict(local_data, remote_data).type());

  local_data.specifics.clear_preference();
  EXPECT_TRUE(local_data.is_deleted());
  EXPECT_FALSE(remote_data.is_deleted());
  EXPECT_EQ(ConflictResolution::USE_REMOTE,
            service()->ResolveConflict(local_data, remote_data).type());
}

}  // namespace syncer

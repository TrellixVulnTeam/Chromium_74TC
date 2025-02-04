// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/scheduler_worker.h"

#include <stddef.h>

#include <memory>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/waitable_event.h"
#include "base/task_scheduler/scheduler_lock.h"
#include "base/task_scheduler/sequence.h"
#include "base/task_scheduler/task.h"
#include "base/task_scheduler/task_tracker.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Mock;
using testing::Ne;
using testing::StrictMock;

namespace base {
namespace internal {
namespace {

const size_t kNumSequencesPerTest = 150;

class SchedulerWorkerDefaultDelegate : public SchedulerWorker::Delegate {
 public:
  SchedulerWorkerDefaultDelegate() = default;

  // SchedulerWorker::Delegate:
  void OnMainEntry(SchedulerWorker* worker,
                   const TimeDelta& detach_duration) override {}
  scoped_refptr<Sequence> GetWork(SchedulerWorker* worker) override {
    return nullptr;
  }
  void DidRunTaskWithPriority(TaskPriority task_priority,
                              const TimeDelta& task_latency) override {
    ADD_FAILURE() << "Unexpected call to DidRunTaskWithPriority()";
  }
  void ReEnqueueSequence(scoped_refptr<Sequence> sequence) override {
    ADD_FAILURE() << "Unexpected call to ReEnqueueSequence()";
  }
  TimeDelta GetSleepTimeout() override { return TimeDelta::Max(); }
  bool CanDetach(SchedulerWorker* worker) override { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(SchedulerWorkerDefaultDelegate);
};

// The test parameter is the number of Tasks per Sequence returned by GetWork().
class TaskSchedulerWorkerTest : public testing::TestWithParam<size_t> {
 protected:
  TaskSchedulerWorkerTest()
      : main_entry_called_(WaitableEvent::ResetPolicy::MANUAL,
                           WaitableEvent::InitialState::NOT_SIGNALED),
        num_get_work_cv_(lock_.CreateConditionVariable()),
        worker_set_(WaitableEvent::ResetPolicy::MANUAL,
                    WaitableEvent::InitialState::NOT_SIGNALED) {}

  void SetUp() override {
    worker_ = SchedulerWorker::Create(
        ThreadPriority::NORMAL, MakeUnique<TestSchedulerWorkerDelegate>(this),
        &task_tracker_, SchedulerWorker::InitialState::ALIVE);
    ASSERT_TRUE(worker_);
    worker_set_.Signal();
    main_entry_called_.Wait();
  }

  void TearDown() override {
    worker_->JoinForTesting();
  }

  size_t TasksPerSequence() const { return GetParam(); }

  // Wait until GetWork() has been called |num_get_work| times.
  void WaitForNumGetWork(size_t num_get_work) {
    AutoSchedulerLock auto_lock(lock_);
    while (num_get_work_ < num_get_work)
      num_get_work_cv_->Wait();
  }

  void SetMaxGetWork(size_t max_get_work) {
    AutoSchedulerLock auto_lock(lock_);
    max_get_work_ = max_get_work;
  }

  void SetNumSequencesToCreate(size_t num_sequences_to_create) {
    AutoSchedulerLock auto_lock(lock_);
    EXPECT_EQ(0U, num_sequences_to_create_);
    num_sequences_to_create_ = num_sequences_to_create;
  }

  size_t NumRunTasks() {
    AutoSchedulerLock auto_lock(lock_);
    return num_run_tasks_;
  }

  std::vector<scoped_refptr<Sequence>> CreatedSequences() {
    AutoSchedulerLock auto_lock(lock_);
    return created_sequences_;
  }

  std::vector<scoped_refptr<Sequence>> EnqueuedSequences() {
    AutoSchedulerLock auto_lock(lock_);
    return re_enqueued_sequences_;
  }

  std::unique_ptr<SchedulerWorker> worker_;

 private:
  class TestSchedulerWorkerDelegate : public SchedulerWorkerDefaultDelegate {
   public:
    TestSchedulerWorkerDelegate(TaskSchedulerWorkerTest* outer)
        : outer_(outer) {}

    ~TestSchedulerWorkerDelegate() override {
      EXPECT_FALSE(IsCallToDidRunTaskWithPriorityExpected());
    }

    // SchedulerWorker::Delegate:
    void OnMainEntry(SchedulerWorker* worker,
                     const TimeDelta& detach_duration) override {
      outer_->worker_set_.Wait();
      EXPECT_EQ(outer_->worker_.get(), worker);
      EXPECT_FALSE(IsCallToDidRunTaskWithPriorityExpected());

      // Without synchronization, OnMainEntry() could be called twice without
      // generating an error.
      AutoSchedulerLock auto_lock(outer_->lock_);
      EXPECT_FALSE(outer_->main_entry_called_.IsSignaled());
      outer_->main_entry_called_.Signal();
    }

    scoped_refptr<Sequence> GetWork(SchedulerWorker* worker) override {
      EXPECT_FALSE(IsCallToDidRunTaskWithPriorityExpected());
      EXPECT_EQ(outer_->worker_.get(), worker);

      {
        AutoSchedulerLock auto_lock(outer_->lock_);

        // Increment the number of times that this method has been called.
        ++outer_->num_get_work_;
        outer_->num_get_work_cv_->Signal();

        // Verify that this method isn't called more times than expected.
        EXPECT_LE(outer_->num_get_work_, outer_->max_get_work_);

        // Check if a Sequence should be returned.
        if (outer_->num_sequences_to_create_ == 0)
          return nullptr;
        --outer_->num_sequences_to_create_;
      }

      // Create a Sequence with TasksPerSequence() Tasks.
      scoped_refptr<Sequence> sequence(new Sequence);
      for (size_t i = 0; i < outer_->TasksPerSequence(); ++i) {
        std::unique_ptr<Task> task(new Task(
            FROM_HERE, Bind(&TaskSchedulerWorkerTest::RunTaskCallback,
                            Unretained(outer_)),
            TaskTraits(), TimeDelta()));
        EXPECT_TRUE(outer_->task_tracker_.WillPostTask(task.get()));
        sequence->PushTask(std::move(task));
      }

      ExpectCallToDidRunTaskWithPriority(sequence->PeekTaskTraits().priority());

      {
        // Add the Sequence to the vector of created Sequences.
        AutoSchedulerLock auto_lock(outer_->lock_);
        outer_->created_sequences_.push_back(sequence);
      }

      return sequence;
    }

    void DidRunTaskWithPriority(TaskPriority task_priority,
                                const TimeDelta& task_latency) override {
      AutoSchedulerLock auto_lock(expect_did_run_task_with_priority_lock_);
      EXPECT_TRUE(expect_did_run_task_with_priority_);
      EXPECT_EQ(expected_task_priority_, task_priority);
      EXPECT_FALSE(task_latency.is_max());
      expect_did_run_task_with_priority_ = false;
    }

    // This override verifies that |sequence| contains the expected number of
    // Tasks and adds it to |enqueued_sequences_|. Unlike a normal
    // EnqueueSequence implementation, it doesn't reinsert |sequence| into a
    // queue for further execution.
    void ReEnqueueSequence(scoped_refptr<Sequence> sequence) override {
      EXPECT_FALSE(IsCallToDidRunTaskWithPriorityExpected());
      EXPECT_GT(outer_->TasksPerSequence(), 1U);

      // Verify that |sequence| contains TasksPerSequence() - 1 Tasks.
      for (size_t i = 0; i < outer_->TasksPerSequence() - 1; ++i) {
        EXPECT_TRUE(sequence->TakeTask());
        EXPECT_EQ(i == outer_->TasksPerSequence() - 2, sequence->Pop());
      }

      // Add |sequence| to |re_enqueued_sequences_|.
      AutoSchedulerLock auto_lock(outer_->lock_);
      outer_->re_enqueued_sequences_.push_back(std::move(sequence));
      EXPECT_LE(outer_->re_enqueued_sequences_.size(),
                outer_->created_sequences_.size());
    }

   private:
    // Expect a call to DidRunTaskWithPriority() with |task_priority| as
    // argument before the next call to any other method of this delegate.
    void ExpectCallToDidRunTaskWithPriority(TaskPriority task_priority) {
      AutoSchedulerLock auto_lock(expect_did_run_task_with_priority_lock_);
      expect_did_run_task_with_priority_ = true;
      expected_task_priority_ = task_priority;
    }

    bool IsCallToDidRunTaskWithPriorityExpected() const {
      AutoSchedulerLock auto_lock(expect_did_run_task_with_priority_lock_);
      return expect_did_run_task_with_priority_;
    }

    TaskSchedulerWorkerTest* outer_;

    // Synchronizes access to |expect_did_run_task_with_priority_| and
    // |expected_task_priority_|.
    mutable SchedulerLock expect_did_run_task_with_priority_lock_;

    // Whether the next method called on this delegate should be
    // DidRunTaskWithPriority().
    bool expect_did_run_task_with_priority_ = false;

    // Expected priority for the next call to DidRunTaskWithPriority().
    TaskPriority expected_task_priority_ = TaskPriority::BACKGROUND;
  };

  void RunTaskCallback() {
    AutoSchedulerLock auto_lock(lock_);
    ++num_run_tasks_;
    EXPECT_LE(num_run_tasks_, created_sequences_.size());
  }

  TaskTracker task_tracker_;

  // Synchronizes access to all members below.
  mutable SchedulerLock lock_;

  // Signaled once OnMainEntry() has been called.
  WaitableEvent main_entry_called_;

  // Number of Sequences that should be created by GetWork(). When this
  // is 0, GetWork() returns nullptr.
  size_t num_sequences_to_create_ = 0;

  // Number of times that GetWork() has been called.
  size_t num_get_work_ = 0;

  // Maximum number of times that GetWork() can be called.
  size_t max_get_work_ = 0;

  // Condition variable signaled when |num_get_work_| is incremented.
  std::unique_ptr<ConditionVariable> num_get_work_cv_;

  // Sequences created by GetWork().
  std::vector<scoped_refptr<Sequence>> created_sequences_;

  // Sequences passed to EnqueueSequence().
  std::vector<scoped_refptr<Sequence>> re_enqueued_sequences_;

  // Number of times that RunTaskCallback() has been called.
  size_t num_run_tasks_ = 0;

  // Signaled after |worker_| is set.
  WaitableEvent worker_set_;

  DISALLOW_COPY_AND_ASSIGN(TaskSchedulerWorkerTest);
};

// Verify that when GetWork() continuously returns Sequences, all Tasks in these
// Sequences run successfully. The test wakes up the SchedulerWorker once.
TEST_P(TaskSchedulerWorkerTest, ContinuousWork) {
  // Set GetWork() to return |kNumSequencesPerTest| Sequences before starting to
  // return nullptr.
  SetNumSequencesToCreate(kNumSequencesPerTest);

  // Expect |kNumSequencesPerTest| calls to GetWork() in which it returns a
  // Sequence and one call in which its returns nullptr.
  const size_t kExpectedNumGetWork = kNumSequencesPerTest + 1;
  SetMaxGetWork(kExpectedNumGetWork);

  // Wake up |worker_| and wait until GetWork() has been invoked the
  // expected amount of times.
  worker_->WakeUp();
  WaitForNumGetWork(kExpectedNumGetWork);

  // All tasks should have run.
  EXPECT_EQ(kNumSequencesPerTest, NumRunTasks());

  // If Sequences returned by GetWork() contain more than one Task, they aren't
  // empty after the worker pops Tasks from them and thus should be returned to
  // EnqueueSequence().
  if (TasksPerSequence() > 1)
    EXPECT_EQ(CreatedSequences(), EnqueuedSequences());
  else
    EXPECT_TRUE(EnqueuedSequences().empty());
}

// Verify that when GetWork() alternates between returning a Sequence and
// returning nullptr, all Tasks in the returned Sequences run successfully. The
// test wakes up the SchedulerWorker once for each Sequence.
TEST_P(TaskSchedulerWorkerTest, IntermittentWork) {
  for (size_t i = 0; i < kNumSequencesPerTest; ++i) {
    // Set GetWork() to return 1 Sequence before starting to return
    // nullptr.
    SetNumSequencesToCreate(1);

    // Expect |i + 1| calls to GetWork() in which it returns a Sequence and
    // |i + 1| calls in which it returns nullptr.
    const size_t expected_num_get_work = 2 * (i + 1);
    SetMaxGetWork(expected_num_get_work);

    // Wake up |worker_| and wait until GetWork() has been invoked
    // the expected amount of times.
    worker_->WakeUp();
    WaitForNumGetWork(expected_num_get_work);

    // The Task should have run
    EXPECT_EQ(i + 1, NumRunTasks());

    // If Sequences returned by GetWork() contain more than one Task, they
    // aren't empty after the worker pops Tasks from them and thus should be
    // returned to EnqueueSequence().
    if (TasksPerSequence() > 1)
      EXPECT_EQ(CreatedSequences(), EnqueuedSequences());
    else
      EXPECT_TRUE(EnqueuedSequences().empty());
  }
}

INSTANTIATE_TEST_CASE_P(OneTaskPerSequence,
                        TaskSchedulerWorkerTest,
                        ::testing::Values(1));
INSTANTIATE_TEST_CASE_P(TwoTasksPerSequence,
                        TaskSchedulerWorkerTest,
                        ::testing::Values(2));

namespace {

class ControllableDetachDelegate : public SchedulerWorkerDefaultDelegate {
 public:
  ControllableDetachDelegate(TaskTracker* task_tracker)
      : task_tracker_(task_tracker),
        work_processed_(WaitableEvent::ResetPolicy::MANUAL,
                        WaitableEvent::InitialState::NOT_SIGNALED),
        detach_requested_(WaitableEvent::ResetPolicy::MANUAL,
                          WaitableEvent::InitialState::NOT_SIGNALED) {
    EXPECT_TRUE(task_tracker_);
  }

  ~ControllableDetachDelegate() override = default;

  // SchedulerWorker::Delegate:
  MOCK_METHOD2(OnMainEntry,
               void(SchedulerWorker* worker, const TimeDelta& detach_duration));

  scoped_refptr<Sequence> GetWork(SchedulerWorker* worker)
      override {
    // Sends one item of work to signal |work_processed_|. On subsequent calls,
    // sends nullptr to indicate there's no more work to be done.
    if (work_requested_)
      return nullptr;

    work_requested_ = true;
    scoped_refptr<Sequence> sequence(new Sequence);
    std::unique_ptr<Task> task(new Task(
        FROM_HERE, Bind(&WaitableEvent::Signal, Unretained(&work_processed_)),
        TaskTraits(), TimeDelta()));
    EXPECT_TRUE(task_tracker_->WillPostTask(task.get()));
    sequence->PushTask(std::move(task));
    return sequence;
  }

  void DidRunTaskWithPriority(TaskPriority task,
                              const TimeDelta& task_latency) override {}

  bool CanDetach(SchedulerWorker* worker) override {
    detach_requested_.Signal();
    return can_detach_;
  }

  void WaitForWorkToRun() {
    work_processed_.Wait();
  }

  void WaitForDetachRequest() {
    detach_requested_.Wait();
  }

  void ResetState() {
    work_requested_ = false;
    work_processed_.Reset();
    detach_requested_.Reset();
  }

  void set_can_detach(bool can_detach) { can_detach_ = can_detach; }

 private:
  TaskTracker* const task_tracker_;
  bool work_requested_ = false;
  bool can_detach_ = false;
  WaitableEvent work_processed_;
  WaitableEvent detach_requested_;

  DISALLOW_COPY_AND_ASSIGN(ControllableDetachDelegate);
};

}  // namespace

TEST(TaskSchedulerWorkerTest, WorkerDetaches) {
  TaskTracker task_tracker;
  // Will be owned by SchedulerWorker.
  ControllableDetachDelegate* delegate =
      new StrictMock<ControllableDetachDelegate>(&task_tracker);
  delegate->set_can_detach(true);
  EXPECT_CALL(*delegate, OnMainEntry(_, TimeDelta::Max()));
  std::unique_ptr<SchedulerWorker> worker =
      SchedulerWorker::Create(
          ThreadPriority::NORMAL, WrapUnique(delegate), &task_tracker,
          SchedulerWorker::InitialState::ALIVE);
  worker->WakeUp();
  delegate->WaitForWorkToRun();
  Mock::VerifyAndClear(delegate);
  delegate->WaitForDetachRequest();
  // Sleep to give a chance for the detach to happen. A yield is too short.
  PlatformThread::Sleep(TimeDelta::FromMilliseconds(50));
  ASSERT_FALSE(worker->ThreadAliveForTesting());
}

TEST(TaskSchedulerWorkerTest, WorkerDetachesAndWakes) {
  TaskTracker task_tracker;
  // Will be owned by SchedulerWorker.
  ControllableDetachDelegate* delegate =
      new StrictMock<ControllableDetachDelegate>(&task_tracker);
  delegate->set_can_detach(true);
  EXPECT_CALL(*delegate, OnMainEntry(_, TimeDelta::Max()));
  std::unique_ptr<SchedulerWorker> worker =
      SchedulerWorker::Create(
          ThreadPriority::NORMAL, WrapUnique(delegate), &task_tracker,
          SchedulerWorker::InitialState::ALIVE);
  worker->WakeUp();
  delegate->WaitForWorkToRun();
  Mock::VerifyAndClear(delegate);
  delegate->WaitForDetachRequest();
  // Sleep to give a chance for the detach to happen. A yield is too short.
  PlatformThread::Sleep(TimeDelta::FromMilliseconds(50));
  ASSERT_FALSE(worker->ThreadAliveForTesting());

  delegate->ResetState();
  delegate->set_can_detach(false);
  // When SchedulerWorker recreates its thread, expect OnMainEntry() to be
  // called with a detach duration which is not TimeDelta::Max().
  EXPECT_CALL(*delegate, OnMainEntry(worker.get(), Ne(TimeDelta::Max())));
  worker->WakeUp();
  delegate->WaitForWorkToRun();
  Mock::VerifyAndClear(delegate);
  delegate->WaitForDetachRequest();
  PlatformThread::Sleep(TimeDelta::FromMilliseconds(50));
  ASSERT_TRUE(worker->ThreadAliveForTesting());
  worker->JoinForTesting();
}

TEST(TaskSchedulerWorkerTest, CreateDetached) {
  TaskTracker task_tracker;
  // Will be owned by SchedulerWorker.
  ControllableDetachDelegate* delegate =
      new StrictMock<ControllableDetachDelegate>(&task_tracker);
  std::unique_ptr<SchedulerWorker> worker =
      SchedulerWorker::Create(
          ThreadPriority::NORMAL, WrapUnique(delegate), &task_tracker,
          SchedulerWorker::InitialState::DETACHED);
  ASSERT_FALSE(worker->ThreadAliveForTesting());
  EXPECT_CALL(*delegate, OnMainEntry(worker.get(), TimeDelta::Max()));
  worker->WakeUp();
  delegate->WaitForWorkToRun();
  Mock::VerifyAndClear(delegate);
  delegate->WaitForDetachRequest();
  ASSERT_TRUE(worker->ThreadAliveForTesting());
  worker->JoinForTesting();
}

namespace {

class ExpectThreadPriorityDelegate : public SchedulerWorkerDefaultDelegate {
 public:
  ExpectThreadPriorityDelegate()
      : priority_verified_in_get_work_event_(
            WaitableEvent::ResetPolicy::AUTOMATIC,
            WaitableEvent::InitialState::NOT_SIGNALED),
        expected_thread_priority_(ThreadPriority::BACKGROUND) {}

  void SetExpectedThreadPriority(ThreadPriority expected_thread_priority) {
    expected_thread_priority_ = expected_thread_priority;
  }

  void WaitForPriorityVerifiedInGetWork() {
    priority_verified_in_get_work_event_.Wait();
  }

  // SchedulerWorker::Delegate:
  void OnMainEntry(SchedulerWorker* worker,
                   const TimeDelta& detach_duration) override {
    VerifyThreadPriority();
  }
  scoped_refptr<Sequence> GetWork(SchedulerWorker* worker) override {
    VerifyThreadPriority();
    priority_verified_in_get_work_event_.Signal();
    return nullptr;
  }

 private:
  void VerifyThreadPriority() {
    AutoSchedulerLock auto_lock(expected_thread_priority_lock_);
    EXPECT_EQ(expected_thread_priority_,
              PlatformThread::GetCurrentThreadPriority());
  }

  // Signaled after GetWork() has verified the priority of the worker thread.
  WaitableEvent priority_verified_in_get_work_event_;

  // Synchronizes access to |expected_thread_priority_|.
  SchedulerLock expected_thread_priority_lock_;

  // Expected thread priority for the next call to OnMainEntry() or GetWork().
  ThreadPriority expected_thread_priority_;

  DISALLOW_COPY_AND_ASSIGN(ExpectThreadPriorityDelegate);
};

}  // namespace

TEST(TaskSchedulerWorkerTest, BumpPriorityOfAliveThreadDuringShutdown) {
  TaskTracker task_tracker;

  std::unique_ptr<ExpectThreadPriorityDelegate> delegate(
      new ExpectThreadPriorityDelegate);
  ExpectThreadPriorityDelegate* delegate_raw = delegate.get();
  delegate_raw->SetExpectedThreadPriority(
      PlatformThread::CanIncreaseCurrentThreadPriority()
          ? ThreadPriority::BACKGROUND
          : ThreadPriority::NORMAL);

  std::unique_ptr<SchedulerWorker> worker = SchedulerWorker::Create(
      ThreadPriority::BACKGROUND, std::move(delegate), &task_tracker,
      SchedulerWorker::InitialState::ALIVE);

  // Verify that the initial thread priority is BACKGROUND (or NORMAL if thread
  // priority can't be increased).
  worker->WakeUp();
  delegate_raw->WaitForPriorityVerifiedInGetWork();

  // Verify that the thread priority is bumped to NORMAL during shutdown.
  delegate_raw->SetExpectedThreadPriority(ThreadPriority::NORMAL);
  task_tracker.SetHasShutdownStartedForTesting();
  worker->WakeUp();
  delegate_raw->WaitForPriorityVerifiedInGetWork();

  worker->JoinForTesting();
}

TEST(TaskSchedulerWorkerTest, BumpPriorityOfDetachedThreadDuringShutdown) {
  TaskTracker task_tracker;

  std::unique_ptr<ExpectThreadPriorityDelegate> delegate(
      new ExpectThreadPriorityDelegate);
  ExpectThreadPriorityDelegate* delegate_raw = delegate.get();
  delegate_raw->SetExpectedThreadPriority(ThreadPriority::NORMAL);

  // Create a DETACHED thread.
  std::unique_ptr<SchedulerWorker> worker = SchedulerWorker::Create(
      ThreadPriority::BACKGROUND, std::move(delegate), &task_tracker,
      SchedulerWorker::InitialState::DETACHED);

  // Pretend that shutdown has started.
  task_tracker.SetHasShutdownStartedForTesting();

  // Wake up the thread and verify that its priority is NORMAL when
  // OnMainEntry() and GetWork() are called.
  worker->WakeUp();
  delegate_raw->WaitForPriorityVerifiedInGetWork();

  worker->JoinForTesting();
}

}  // namespace
}  // namespace internal
}  // namespace base

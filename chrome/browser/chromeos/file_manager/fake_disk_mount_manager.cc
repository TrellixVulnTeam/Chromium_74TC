// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/file_manager/fake_disk_mount_manager.h"

namespace file_manager {

FakeDiskMountManager::MountRequest::MountRequest(
    const std::string& source_path,
    const std::string& source_format,
    const std::string& mount_label,
    chromeos::MountType type,
    chromeos::MountAccessMode access_mode)
    : source_path(source_path),
      source_format(source_format),
      mount_label(mount_label),
      type(type),
      access_mode(access_mode) {}

FakeDiskMountManager::MountRequest::MountRequest(const MountRequest& other) =
    default;

FakeDiskMountManager::UnmountRequest::UnmountRequest(
    const std::string& mount_path,
    chromeos::UnmountOptions options)
    : mount_path(mount_path),
      options(options) {
}

FakeDiskMountManager::FakeDiskMountManager() {
}

FakeDiskMountManager::~FakeDiskMountManager() {
}

void FakeDiskMountManager::AddObserver(Observer* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void FakeDiskMountManager::RemoveObserver(Observer* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

const chromeos::disks::DiskMountManager::DiskMap&
FakeDiskMountManager::disks() const {
  return disks_;
}

const chromeos::disks::DiskMountManager::Disk*
FakeDiskMountManager::FindDiskBySourcePath(
    const std::string& source_path) const {
  DiskMap::const_iterator iter = disks_.find(source_path);
  return iter != disks_.end() ? iter->second.get() : nullptr;
}

const chromeos::disks::DiskMountManager::MountPointMap&
FakeDiskMountManager::mount_points() const {
  return mount_points_;
}

void FakeDiskMountManager::EnsureMountInfoRefreshed(
    const EnsureMountInfoRefreshedCallback& callback,
    bool force) {
  callback.Run(true);
}

void FakeDiskMountManager::MountPath(const std::string& source_path,
                                     const std::string& source_format,
                                     const std::string& mount_label,
                                     chromeos::MountType type,
                                     chromeos::MountAccessMode access_mode) {
  mount_requests_.push_back(
      MountRequest(source_path, source_format, mount_label, type, access_mode));

  const MountPointInfo mount_point(
      source_path,
      source_path,
      type,
      chromeos::disks::MOUNT_CONDITION_NONE);
  mount_points_.insert(make_pair(source_path, mount_point));
  for (auto& observer : observers_) {
    observer.OnMountEvent(DiskMountManager::MOUNTING,
                          chromeos::MOUNT_ERROR_NONE, mount_point);
  }
}

void FakeDiskMountManager::UnmountPath(const std::string& mount_path,
                                       chromeos::UnmountOptions options,
                                       const UnmountPathCallback& callback) {
  unmount_requests_.push_back(UnmountRequest(mount_path, options));

  MountPointMap::iterator iter = mount_points_.find(mount_path);
  if (iter == mount_points_.end())
    return;

  const MountPointInfo mount_point = iter->second;
  mount_points_.erase(iter);
  for (auto& observer : observers_) {
    observer.OnMountEvent(DiskMountManager::UNMOUNTING,
                          chromeos::MOUNT_ERROR_NONE, mount_point);
  }

  // Enqueue callback so that |FakeDiskMountManager::FinishAllUnmountRequest()|
  // can call them.
  pending_unmount_callbacks_.push(callback);
}

bool FakeDiskMountManager::FinishAllUnmountPathRequests() {
  if (pending_unmount_callbacks_.empty())
    return false;

  while (!pending_unmount_callbacks_.empty()) {
    pending_unmount_callbacks_.front().Run(chromeos::MOUNT_ERROR_NONE);
    pending_unmount_callbacks_.pop();
  }
  return true;
}

void FakeDiskMountManager::FormatMountedDevice(const std::string& mount_path) {
}

void FakeDiskMountManager::UnmountDeviceRecursively(
    const std::string& device_path,
    const UnmountDeviceRecursivelyCallbackType& callback) {
}

bool FakeDiskMountManager::AddDiskForTest(std::unique_ptr<Disk> disk) {
  DCHECK(disk);
  return disks_.insert(make_pair(disk->device_path(), std::move(disk))).second;
}

bool FakeDiskMountManager::AddMountPointForTest(
    const MountPointInfo& mount_point) {
  return false;
}

void FakeDiskMountManager::InvokeDiskEventForTest(
    chromeos::disks::DiskMountManager::DiskEvent event,
    const chromeos::disks::DiskMountManager::Disk* disk) {
  for (auto& observer : observers_)
    observer.OnDiskEvent(event, disk);
}

}  // namespace file_manager

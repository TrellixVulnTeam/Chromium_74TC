// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/common/system/tray/system_tray_notifier.h"

#include "ash/common/system/accessibility_observer.h"
#include "ash/common/system/audio/audio_observer.h"
#include "ash/common/system/date/clock_observer.h"
#include "ash/common/system/ime/ime_observer.h"
#include "ash/common/system/update/update_observer.h"
#include "ash/common/system/user/user_observer.h"

#if defined(OS_CHROMEOS)
#include "ash/common/system/chromeos/bluetooth/bluetooth_observer.h"
#include "ash/common/system/chromeos/enterprise/enterprise_domain_observer.h"
#include "ash/common/system/chromeos/media_security/media_capture_observer.h"
#include "ash/common/system/chromeos/network/network_observer.h"
#include "ash/common/system/chromeos/network/network_portal_detector_observer.h"
#include "ash/common/system/chromeos/screen_security/screen_capture_observer.h"
#include "ash/common/system/chromeos/screen_security/screen_share_observer.h"
#include "ash/common/system/chromeos/session/last_window_closed_observer.h"
#include "ash/common/system/chromeos/session/logout_button_observer.h"
#include "ash/common/system/chromeos/session/session_length_limit_observer.h"
#include "ash/common/system/chromeos/tray_tracing.h"
#include "ash/common/system/chromeos/virtual_keyboard/virtual_keyboard_observer.h"
#endif

namespace ash {

SystemTrayNotifier::SystemTrayNotifier() {}

SystemTrayNotifier::~SystemTrayNotifier() {}

void SystemTrayNotifier::AddAccessibilityObserver(
    AccessibilityObserver* observer) {
  accessibility_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveAccessibilityObserver(
    AccessibilityObserver* observer) {
  accessibility_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyAccessibilityModeChanged(
    AccessibilityNotificationVisibility notify) {
  for (auto& observer : accessibility_observers_)
    observer.OnAccessibilityModeChanged(notify);
}

void SystemTrayNotifier::AddAudioObserver(AudioObserver* observer) {
  audio_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveAudioObserver(AudioObserver* observer) {
  audio_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyAudioOutputVolumeChanged(uint64_t node_id,
                                                        double volume) {
  for (auto& observer : audio_observers_)
    observer.OnOutputNodeVolumeChanged(node_id, volume);
}

void SystemTrayNotifier::NotifyAudioOutputMuteChanged(bool mute_on,
                                                      bool system_adjust) {
  for (auto& observer : audio_observers_)
    observer.OnOutputMuteChanged(mute_on, system_adjust);
}

void SystemTrayNotifier::NotifyAudioNodesChanged() {
  for (auto& observer : audio_observers_)
    observer.OnAudioNodesChanged();
}

void SystemTrayNotifier::NotifyAudioActiveOutputNodeChanged() {
  for (auto& observer : audio_observers_)
    observer.OnActiveOutputNodeChanged();
}

void SystemTrayNotifier::NotifyAudioActiveInputNodeChanged() {
  for (auto& observer : audio_observers_)
    observer.OnActiveInputNodeChanged();
}

void SystemTrayNotifier::AddClockObserver(ClockObserver* observer) {
  clock_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveClockObserver(ClockObserver* observer) {
  clock_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyRefreshClock() {
  for (auto& observer : clock_observers_)
    observer.Refresh();
}

void SystemTrayNotifier::NotifyDateFormatChanged() {
  for (auto& observer : clock_observers_)
    observer.OnDateFormatChanged();
}

void SystemTrayNotifier::NotifySystemClockTimeUpdated() {
  for (auto& observer : clock_observers_)
    observer.OnSystemClockTimeUpdated();
}

void SystemTrayNotifier::NotifySystemClockCanSetTimeChanged(bool can_set_time) {
  for (auto& observer : clock_observers_)
    observer.OnSystemClockCanSetTimeChanged(can_set_time);
}

void SystemTrayNotifier::AddIMEObserver(IMEObserver* observer) {
  ime_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveIMEObserver(IMEObserver* observer) {
  ime_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyRefreshIME() {
  for (auto& observer : ime_observers_)
    observer.OnIMERefresh();
}

void SystemTrayNotifier::NotifyRefreshIMEMenu(bool is_active) {
  for (auto& observer : ime_observers_)
    observer.OnIMEMenuActivationChanged(is_active);
}

void SystemTrayNotifier::AddLocaleObserver(LocaleObserver* observer) {
  locale_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveLocaleObserver(LocaleObserver* observer) {
  locale_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyLocaleChanged(LocaleObserver::Delegate* delegate,
                                             const std::string& cur_locale,
                                             const std::string& from_locale,
                                             const std::string& to_locale) {
  for (auto& observer : locale_observers_)
    observer.OnLocaleChanged(delegate, cur_locale, from_locale, to_locale);
}

void SystemTrayNotifier::AddUpdateObserver(UpdateObserver* observer) {
  update_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveUpdateObserver(UpdateObserver* observer) {
  update_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyUpdateRecommended(const UpdateInfo& info) {
  for (auto& observer : update_observers_)
    observer.OnUpdateRecommended(info);
}

void SystemTrayNotifier::AddUserObserver(UserObserver* observer) {
  user_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveUserObserver(UserObserver* observer) {
  user_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyUserUpdate() {
  for (auto& observer : user_observers_)
    observer.OnUserUpdate();
}

void SystemTrayNotifier::NotifyUserAddedToSession() {
  for (auto& observer : user_observers_)
    observer.OnUserAddedToSession();
}

#if defined(OS_CHROMEOS)

void SystemTrayNotifier::AddBluetoothObserver(BluetoothObserver* observer) {
  bluetooth_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveBluetoothObserver(BluetoothObserver* observer) {
  bluetooth_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyRefreshBluetooth() {
  for (auto& observer : bluetooth_observers_)
    observer.OnBluetoothRefresh();
}

void SystemTrayNotifier::NotifyBluetoothDiscoveringChanged() {
  for (auto& observer : bluetooth_observers_)
    observer.OnBluetoothDiscoveringChanged();
}

void SystemTrayNotifier::AddEnterpriseDomainObserver(
    EnterpriseDomainObserver* observer) {
  enterprise_domain_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveEnterpriseDomainObserver(
    EnterpriseDomainObserver* observer) {
  enterprise_domain_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyEnterpriseDomainChanged() {
  for (auto& observer : enterprise_domain_observers_)
    observer.OnEnterpriseDomainChanged();
}

void SystemTrayNotifier::AddLastWindowClosedObserver(
    LastWindowClosedObserver* observer) {
  last_window_closed_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveLastWindowClosedObserver(
    LastWindowClosedObserver* observer) {
  last_window_closed_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyLastWindowClosed() {
  for (auto& observer : last_window_closed_observers_)
    observer.OnLastWindowClosed();
}

void SystemTrayNotifier::AddLogoutButtonObserver(
    LogoutButtonObserver* observer) {
  logout_button_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveLogoutButtonObserver(
    LogoutButtonObserver* observer) {
  logout_button_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyShowLoginButtonChanged(bool show_login_button) {
  for (auto& observer : logout_button_observers_)
    observer.OnShowLogoutButtonInTrayChanged(show_login_button);
}

void SystemTrayNotifier::NotifyLogoutDialogDurationChanged(
    base::TimeDelta duration) {
  for (auto& observer : logout_button_observers_)
    observer.OnLogoutDialogDurationChanged(duration);
}

void SystemTrayNotifier::AddMediaCaptureObserver(
    MediaCaptureObserver* observer) {
  media_capture_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveMediaCaptureObserver(
    MediaCaptureObserver* observer) {
  media_capture_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyMediaCaptureChanged() {
  for (auto& observer : media_capture_observers_)
    observer.OnMediaCaptureChanged();
}

void SystemTrayNotifier::AddNetworkObserver(NetworkObserver* observer) {
  network_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveNetworkObserver(NetworkObserver* observer) {
  network_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyRequestToggleWifi() {
  for (auto& observer : network_observers_)
    observer.RequestToggleWifi();
}

void SystemTrayNotifier::AddNetworkPortalDetectorObserver(
    NetworkPortalDetectorObserver* observer) {
  network_portal_detector_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveNetworkPortalDetectorObserver(
    NetworkPortalDetectorObserver* observer) {
  network_portal_detector_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyOnCaptivePortalDetected(
    const std::string& service_path) {
  for (auto& observer : network_portal_detector_observers_)
    observer.OnCaptivePortalDetected(service_path);
}

void SystemTrayNotifier::AddScreenCaptureObserver(
    ScreenCaptureObserver* observer) {
  screen_capture_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveScreenCaptureObserver(
    ScreenCaptureObserver* observer) {
  screen_capture_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyScreenCaptureStart(
    const base::Closure& stop_callback,
    const base::string16& sharing_app_name) {
  for (auto& observer : screen_capture_observers_)
    observer.OnScreenCaptureStart(stop_callback, sharing_app_name);
}

void SystemTrayNotifier::NotifyScreenCaptureStop() {
  for (auto& observer : screen_capture_observers_)
    observer.OnScreenCaptureStop();
}

void SystemTrayNotifier::AddScreenShareObserver(ScreenShareObserver* observer) {
  screen_share_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveScreenShareObserver(
    ScreenShareObserver* observer) {
  screen_share_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyScreenShareStart(
    const base::Closure& stop_callback,
    const base::string16& helper_name) {
  for (auto& observer : screen_share_observers_)
    observer.OnScreenShareStart(stop_callback, helper_name);
}

void SystemTrayNotifier::NotifyScreenShareStop() {
  for (auto& observer : screen_share_observers_)
    observer.OnScreenShareStop();
}

void SystemTrayNotifier::AddSessionLengthLimitObserver(
    SessionLengthLimitObserver* observer) {
  session_length_limit_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveSessionLengthLimitObserver(
    SessionLengthLimitObserver* observer) {
  session_length_limit_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifySessionStartTimeChanged() {
  for (auto& observer : session_length_limit_observers_)
    observer.OnSessionStartTimeChanged();
}

void SystemTrayNotifier::NotifySessionLengthLimitChanged() {
  for (auto& observer : session_length_limit_observers_)
    observer.OnSessionLengthLimitChanged();
}

void SystemTrayNotifier::AddTracingObserver(TracingObserver* observer) {
  tracing_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveTracingObserver(TracingObserver* observer) {
  tracing_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyTracingModeChanged(bool value) {
  for (auto& observer : tracing_observers_)
    observer.OnTracingModeChanged(value);
}

void SystemTrayNotifier::AddVirtualKeyboardObserver(
    VirtualKeyboardObserver* observer) {
  virtual_keyboard_observers_.AddObserver(observer);
}

void SystemTrayNotifier::RemoveVirtualKeyboardObserver(
    VirtualKeyboardObserver* observer) {
  virtual_keyboard_observers_.RemoveObserver(observer);
}

void SystemTrayNotifier::NotifyVirtualKeyboardSuppressionChanged(
    bool suppressed) {
  for (auto& observer : virtual_keyboard_observers_)
    observer.OnKeyboardSuppressionChanged(suppressed);
}

#endif  // defined(OS_CHROMEOS)

}  // namespace ash

// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/common/wm_shell.h"

#include <utility>

#include "ash/common/accelerators/accelerator_controller.h"
#include "ash/common/accelerators/ash_focus_manager_factory.h"
#include "ash/common/accessibility_delegate.h"
#include "ash/common/focus_cycler.h"
#include "ash/common/keyboard/keyboard_ui.h"
#include "ash/common/new_window_delegate.h"
#include "ash/common/palette_delegate.h"
#include "ash/common/session/session_state_delegate.h"
#include "ash/common/shelf/app_list_shelf_item_delegate.h"
#include "ash/common/shelf/shelf_controller.h"
#include "ash/common/shelf/shelf_delegate.h"
#include "ash/common/shelf/shelf_model.h"
#include "ash/common/shelf/shelf_window_watcher.h"
#include "ash/common/shell_delegate.h"
#include "ash/common/shell_window_ids.h"
#include "ash/common/system/brightness_control_delegate.h"
#include "ash/common/system/keyboard_brightness_control_delegate.h"
#include "ash/common/system/toast/toast_manager.h"
#include "ash/common/system/tray/system_tray_controller.h"
#include "ash/common/system/tray/system_tray_delegate.h"
#include "ash/common/system/tray/system_tray_notifier.h"
#include "ash/common/wallpaper/wallpaper_controller.h"
#include "ash/common/wallpaper/wallpaper_delegate.h"
#include "ash/common/wm/immersive_context_ash.h"
#include "ash/common/wm/maximize_mode/maximize_mode_controller.h"
#include "ash/common/wm/mru_window_tracker.h"
#include "ash/common/wm/overview/window_selector_controller.h"
#include "ash/common/wm/root_window_finder.h"
#include "ash/common/wm/system_modal_container_layout_manager.h"
#include "ash/common/wm/window_cycle_controller.h"
#include "ash/common/wm_root_window_controller.h"
#include "ash/common/wm_window.h"
#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "ui/app_list/presenter/app_list_presenter.h"
#include "ui/display/display.h"
#include "ui/views/focus/focus_manager_factory.h"

#if defined(OS_CHROMEOS)
#include "ash/common/system/chromeos/brightness/brightness_controller_chromeos.h"
#include "ash/common/system/chromeos/keyboard_brightness_controller.h"
#include "ash/common/system/chromeos/session/logout_confirmation_controller.h"
#endif

namespace ash {

// static
WmShell* WmShell::instance_ = nullptr;

// static
void WmShell::Set(WmShell* instance) {
  instance_ = instance;
}

// static
WmShell* WmShell::Get() {
  return instance_;
}

void WmShell::Initialize(const scoped_refptr<base::SequencedWorkerPool>& pool) {
  blocking_pool_ = pool;

  // Some delegates access WmShell during their construction. Create them here
  // instead of the WmShell constructor.
  accessibility_delegate_.reset(delegate_->CreateAccessibilityDelegate());
  media_delegate_.reset(delegate_->CreateMediaDelegate());
  palette_delegate_ = delegate_->CreatePaletteDelegate();
  toast_manager_.reset(new ToastManager);

  // Create the app list item in the shelf data model.
  AppListShelfItemDelegate::CreateAppListItemAndDelegate(shelf_model());

  // Install the custom factory early on so that views::FocusManagers for Tray,
  // Shelf, and WallPaper could be created by the factory.
  views::FocusManagerFactory::Install(new AshFocusManagerFactory);

  new_window_delegate_.reset(delegate_->CreateNewWindowDelegate());

  wallpaper_controller_.reset(new WallpaperController(blocking_pool_));
}

void WmShell::Shutdown() {
  // These members access WmShell in their destructors.
  wallpaper_controller_.reset();
  accessibility_delegate_.reset();

  // ShelfWindowWatcher has window observers and a pointer to the shelf model.
  shelf_window_watcher_.reset();
  // ShelfItemDelegate subclasses it owns have complex cleanup to run (e.g. ARC
  // shelf items in Chrome) so explicitly shutdown early.
  shelf_model()->DestroyItemDelegates();
  // Must be destroyed before FocusClient.
  shelf_delegate_.reset();

  // Balances the Install() in Initialize().
  views::FocusManagerFactory::Install(nullptr);
}

ShelfModel* WmShell::shelf_model() {
  return shelf_controller_->model();
}

void WmShell::ShowContextMenu(const gfx::Point& location_in_screen,
                              ui::MenuSourceType source_type) {
  // Bail if there is no active user session or if the screen is locked.
  if (GetSessionStateDelegate()->NumberOfLoggedInUsers() < 1 ||
      GetSessionStateDelegate()->IsScreenLocked()) {
    return;
  }

  WmWindow* root = wm::GetRootWindowAt(location_in_screen);
  root->GetRootWindowController()->ShowContextMenu(location_in_screen,
                                                   source_type);
}

void WmShell::CreateShelf() {
  // Must occur after SessionStateDelegate creation and user login.
  DCHECK(GetSessionStateDelegate());
  DCHECK_GT(GetSessionStateDelegate()->NumberOfLoggedInUsers(), 0);
  CreateShelfDelegate();

  for (WmWindow* root_window : GetAllRootWindows())
    root_window->GetRootWindowController()->CreateShelf();
}

void WmShell::ShowShelf() {
  for (WmWindow* root_window : GetAllRootWindows())
    root_window->GetRootWindowController()->ShowShelf();
}

void WmShell::CreateShelfDelegate() {
  // May be called multiple times as shelves are created and destroyed.
  if (shelf_delegate_)
    return;
  // Must occur after SessionStateDelegate creation and user login because
  // Chrome's implementation of ShelfDelegate assumes it can get information
  // about multi-profile login state.
  DCHECK(GetSessionStateDelegate());
  DCHECK_GT(GetSessionStateDelegate()->NumberOfLoggedInUsers(), 0);
  shelf_delegate_.reset(delegate_->CreateShelfDelegate(shelf_model()));
  shelf_window_watcher_.reset(new ShelfWindowWatcher(shelf_model()));
}

void WmShell::OnMaximizeModeStarted() {
  for (auto& observer : shell_observers_)
    observer.OnMaximizeModeStarted();
}

void WmShell::OnMaximizeModeEnded() {
  for (auto& observer : shell_observers_)
    observer.OnMaximizeModeEnded();
}

void WmShell::UpdateAfterLoginStatusChange(LoginStatus status) {
  for (WmWindow* root_window : GetAllRootWindows()) {
    root_window->GetRootWindowController()->UpdateAfterLoginStatusChange(
        status);
  }
}

void WmShell::NotifyFullscreenStateChanged(bool is_fullscreen,
                                           WmWindow* root_window) {
  for (auto& observer : shell_observers_)
    observer.OnFullscreenStateChanged(is_fullscreen, root_window);
}

void WmShell::NotifyPinnedStateChanged(WmWindow* pinned_window) {
  for (auto& observer : shell_observers_)
    observer.OnPinnedStateChanged(pinned_window);
}

void WmShell::NotifyVirtualKeyboardActivated(bool activated) {
  for (auto& observer : shell_observers_)
    observer.OnVirtualKeyboardStateChanged(activated);
}

void WmShell::NotifyShelfCreatedForRootWindow(WmWindow* root_window) {
  for (auto& observer : shell_observers_)
    observer.OnShelfCreatedForRootWindow(root_window);
}

void WmShell::NotifyShelfAlignmentChanged(WmWindow* root_window) {
  for (auto& observer : shell_observers_)
    observer.OnShelfAlignmentChanged(root_window);
}

void WmShell::NotifyShelfAutoHideBehaviorChanged(WmWindow* root_window) {
  for (auto& observer : shell_observers_)
    observer.OnShelfAutoHideBehaviorChanged(root_window);
}

void WmShell::AddShellObserver(ShellObserver* observer) {
  shell_observers_.AddObserver(observer);
}

void WmShell::RemoveShellObserver(ShellObserver* observer) {
  shell_observers_.RemoveObserver(observer);
}

void WmShell::OnLockStateEvent(LockStateObserver::EventType event) {
  for (auto& observer : lock_state_observers_)
    observer.OnLockStateEvent(event);
}

void WmShell::AddLockStateObserver(LockStateObserver* observer) {
  lock_state_observers_.AddObserver(observer);
}

void WmShell::RemoveLockStateObserver(LockStateObserver* observer) {
  lock_state_observers_.RemoveObserver(observer);
}

void WmShell::SetShelfDelegateForTesting(
    std::unique_ptr<ShelfDelegate> test_delegate) {
  shelf_delegate_ = std::move(test_delegate);
}

void WmShell::SetPaletteDelegateForTesting(
    std::unique_ptr<PaletteDelegate> palette_delegate) {
  palette_delegate_ = std::move(palette_delegate);
}

WmShell::WmShell(std::unique_ptr<ShellDelegate> shell_delegate)
    : delegate_(std::move(shell_delegate)),
      focus_cycler_(base::MakeUnique<FocusCycler>()),
      immersive_context_(base::MakeUnique<ImmersiveContextAsh>()),
      shelf_controller_(base::MakeUnique<ShelfController>()),
      system_tray_controller_(base::MakeUnique<SystemTrayController>(
          delegate_->GetShellConnector())),
      system_tray_notifier_(base::MakeUnique<SystemTrayNotifier>()),
      wallpaper_delegate_(delegate_->CreateWallpaperDelegate()),
      window_cycle_controller_(base::MakeUnique<WindowCycleController>()),
      window_selector_controller_(
          base::MakeUnique<WindowSelectorController>()) {
#if defined(OS_CHROMEOS)
  brightness_control_delegate_.reset(new system::BrightnessControllerChromeos);
  keyboard_brightness_control_delegate_.reset(new KeyboardBrightnessController);
#endif
}

WmShell::~WmShell() {}

WmRootWindowController* WmShell::GetPrimaryRootWindowController() {
  return GetPrimaryRootWindow()->GetRootWindowController();
}

WmWindow* WmShell::GetRootWindowForNewWindows() {
  if (scoped_root_window_for_new_windows_)
    return scoped_root_window_for_new_windows_;
  return root_window_for_new_windows_;
}

bool WmShell::IsSystemModalWindowOpen() {
  if (simulate_modal_window_open_for_testing_)
    return true;

  // Traverse all system modal containers, and find its direct child window
  // with "SystemModal" setting, and visible.
  for (WmWindow* root : GetAllRootWindows()) {
    WmWindow* system_modal =
        root->GetChildByShellWindowId(kShellWindowId_SystemModalContainer);
    if (!system_modal)
      continue;
    for (const WmWindow* child : system_modal->GetChildren()) {
      if (child->IsSystemModal() && child->GetTargetVisibility()) {
        return true;
      }
    }
  }
  return false;
}

void WmShell::CreateModalBackground(WmWindow* window) {
  for (WmWindow* root_window : GetAllRootWindows()) {
    root_window->GetRootWindowController()
        ->GetSystemModalLayoutManager(window)
        ->CreateModalBackground();
  }
}

void WmShell::OnModalWindowRemoved(WmWindow* removed) {
  WmWindow::Windows root_windows = GetAllRootWindows();
  for (WmWindow* root_window : root_windows) {
    if (root_window->GetRootWindowController()
            ->GetSystemModalLayoutManager(removed)
            ->ActivateNextModalWindow()) {
      return;
    }
  }
  for (WmWindow* root_window : root_windows) {
    root_window->GetRootWindowController()
        ->GetSystemModalLayoutManager(removed)
        ->DestroyModalBackground();
  }
}

void WmShell::ShowAppList() {
  // Show the app list on the default display for new windows.
  int64_t display_id =
      GetRootWindowForNewWindows()->GetDisplayNearestWindow().id();
  delegate_->GetAppListPresenter()->Show(display_id);
}

void WmShell::DismissAppList() {
  delegate_->GetAppListPresenter()->Dismiss();
}

void WmShell::ToggleAppList() {
  // Show the app list on the default display for new windows.
  int64_t display_id =
      GetRootWindowForNewWindows()->GetDisplayNearestWindow().id();
  delegate_->GetAppListPresenter()->ToggleAppList(display_id);
}

bool WmShell::IsApplistVisible() const {
  return delegate_->GetAppListPresenter()->IsVisible();
}

bool WmShell::GetAppListTargetVisibility() const {
  return delegate_->GetAppListPresenter()->GetTargetVisibility();
}

void WmShell::SetKeyboardUI(std::unique_ptr<KeyboardUI> keyboard_ui) {
  keyboard_ui_ = std::move(keyboard_ui);
}

void WmShell::SetSystemTrayDelegate(
    std::unique_ptr<SystemTrayDelegate> delegate) {
  DCHECK(delegate);
  system_tray_delegate_ = std::move(delegate);
  system_tray_delegate_->Initialize();
#if defined(OS_CHROMEOS)
  logout_confirmation_controller_.reset(new LogoutConfirmationController(
      base::Bind(&SystemTrayDelegate::SignOut,
                 base::Unretained(system_tray_delegate_.get()))));
#endif
}

void WmShell::DeleteSystemTrayDelegate() {
  DCHECK(system_tray_delegate_);
#if defined(OS_CHROMEOS)
  logout_confirmation_controller_.reset();
#endif
  system_tray_delegate_.reset();
}

void WmShell::DeleteWindowCycleController() {
  window_cycle_controller_.reset();
}

void WmShell::DeleteWindowSelectorController() {
  window_selector_controller_.reset();
}

void WmShell::CreateMaximizeModeController() {
  maximize_mode_controller_.reset(new MaximizeModeController);
}

void WmShell::DeleteMaximizeModeController() {
  maximize_mode_controller_.reset();
}

void WmShell::CreateMruWindowTracker() {
  mru_window_tracker_.reset(new MruWindowTracker);
}

void WmShell::DeleteMruWindowTracker() {
  mru_window_tracker_.reset();
}

void WmShell::DeleteToastManager() {
  toast_manager_.reset();
}

void WmShell::SetAcceleratorController(
    std::unique_ptr<AcceleratorController> accelerator_controller) {
  accelerator_controller_ = std::move(accelerator_controller);
}

}  // namespace ash

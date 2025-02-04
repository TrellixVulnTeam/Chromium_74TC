// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_CHROME_LAUNCHER_PREFS_H_
#define CHROME_BROWSER_UI_ASH_CHROME_LAUNCHER_PREFS_H_

#include <memory>
#include <string>
#include <vector>

#include "ash/public/cpp/shelf_types.h"
#include "base/macros.h"
#include "components/syncable_prefs/pref_service_syncable_observer.h"

class LauncherControllerHelper;
class PrefService;
class Profile;

namespace base {
class DictionaryValue;
}

namespace syncable_prefs {
class PrefServiceSyncable;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace ash {
namespace launcher {

// Path within the dictionary entries in the prefs::kPinnedLauncherApps list
// specifying the extension ID of the app to be pinned by that entry.
extern const char kPinnedAppsPrefAppIDPath[];

extern const char kPinnedAppsPrefPinnedByPolicy[];

// Value used as a placeholder in the list of pinned applications.
// This is NOT a valid extension identifier so pre-M31 versions ignore it.
extern const char kPinnedAppsPlaceholder[];

// Values used for prefs::kShelfAutoHideBehavior.
extern const char kShelfAutoHideBehaviorAlways[];
extern const char kShelfAutoHideBehaviorNever[];

// Values used for prefs::kShelfAlignment.
extern const char kShelfAlignmentBottom[];
extern const char kShelfAlignmentLeft[];
extern const char kShelfAlignmentRight[];

void RegisterChromeLauncherUserPrefs(
    user_prefs::PrefRegistrySyncable* registry);

std::unique_ptr<base::DictionaryValue> CreateAppDict(const std::string& app_id);

// Get or set the shelf auto hide behavior preference for a particular display.
ShelfAutoHideBehavior GetShelfAutoHideBehaviorPref(PrefService* prefs,
                                                   int64_t display_id);
void SetShelfAutoHideBehaviorPref(PrefService* prefs,
                                  int64_t display_id,
                                  ShelfAutoHideBehavior behavior);

// Get or set the shelf alignment preference for a particular display.
ShelfAlignment GetShelfAlignmentPref(PrefService* prefs, int64_t display_id);
void SetShelfAlignmentPref(PrefService* prefs,
                           int64_t display_id,
                           ShelfAlignment alignment);

// Get the list of pinned apps from preferences.
std::vector<std::string> GetPinnedAppsFromPrefs(
    const PrefService* prefs,
    LauncherControllerHelper* helper);

// Removes information about pin position from sync model for the app.
void RemovePinPosition(Profile* profile, const std::string& app_id);

// Updates information about pin position in sync model for the app |app_id|.
// |app_id_before| optionally specifies an app that exists right before the
// target app. |app_ids_after| optionally specifies sorted by position apps that
// exist right after the target app.
void SetPinPosition(Profile* profile,
                    const std::string& app_id,
                    const std::string& app_id_before,
                    const std::vector<std::string>& app_ids_after);

// Used to propagate remote preferences to local during the first run.
class ChromeLauncherPrefsObserver
    : public syncable_prefs::PrefServiceSyncableObserver {
 public:
  // Creates and returns an instance of ChromeLauncherPrefsObserver if the
  // profile prefs do not contain all the necessary local settings for the
  // shelf. If the local settings are present, returns null.
  static std::unique_ptr<ChromeLauncherPrefsObserver> CreateIfNecessary(
      Profile* profile);

  ~ChromeLauncherPrefsObserver() override;

 private:
  explicit ChromeLauncherPrefsObserver(
      syncable_prefs::PrefServiceSyncable* prefs);

  // syncable_prefs::PrefServiceSyncableObserver:
  void OnIsSyncingChanged() override;

  // Profile prefs. Not owned.
  syncable_prefs::PrefServiceSyncable* prefs_;

  DISALLOW_COPY_AND_ASSIGN(ChromeLauncherPrefsObserver);
};

}  // namespace launcher
}  // namespace ash

#endif  // CHROME_BROWSER_UI_ASH_CHROME_LAUNCHER_PREFS_H_

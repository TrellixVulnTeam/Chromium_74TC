// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_ARC_ARC_NAVIGATION_THROTTLE_H_
#define CHROME_BROWSER_CHROMEOS_ARC_ARC_NAVIGATION_THROTTLE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "components/arc/intent_helper/activity_icon_loader.h"
#include "content/public/browser/navigation_throttle.h"
#include "ui/gfx/image/image.h"

class GURL;

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace arc {

// A class that allow us to retrieve ARC app's information and handle URL
// traffic initiated on Chrome browser, either on Chrome or an ARC's app.
class ArcNavigationThrottle : public content::NavigationThrottle {
 public:
  // These enums are used to define the buckets for an enumerated UMA histogram
  // and need to be synced with histograms.xml. This enum class should also be
  // treated as append-only.
  enum class CloseReason : int {
    ERROR = 0,
    DIALOG_DEACTIVATED = 1,
    ALWAYS_PRESSED = 2,
    JUST_ONCE_PRESSED = 3,
    PREFERRED_ACTIVITY_FOUND = 4,
    SIZE,
    INVALID = SIZE,
  };

  // Restricts the amount of apps displayed to the user without the need of a
  // ScrollView.
  enum { kMaxAppResults = 3 };

  using NameAndIcon = std::pair<std::string, gfx::Image>;
  using ShowIntentPickerCallback =
      base::Callback<void(content::WebContents* web_contents,
                          const std::vector<NameAndIcon>& app_info,
                          const base::Callback<void(size_t, CloseReason)>& cb)>;
  ArcNavigationThrottle(content::NavigationHandle* navigation_handle,
                        const ShowIntentPickerCallback& show_intent_picker_cb);
  ~ArcNavigationThrottle() override;

  static bool ShouldOverrideUrlLoadingForTesting(const GURL& previous_url,
                                                 const GURL& current_url);

 private:
  // content::Navigation implementation:
  NavigationThrottle::ThrottleCheckResult WillStartRequest() override;
  NavigationThrottle::ThrottleCheckResult WillRedirectRequest() override;

  NavigationThrottle::ThrottleCheckResult HandleRequest();
  void OnAppCandidatesReceived(
      mojo::Array<mojom::IntentHandlerInfoPtr> handlers);
  void OnAppIconsReceived(
      mojo::Array<mojom::IntentHandlerInfoPtr> handlers,
      std::unique_ptr<ActivityIconLoader::ActivityToIconsMap> icons);
  void OnIntentPickerClosed(mojo::Array<mojom::IntentHandlerInfoPtr> handlers,
                            size_t selected_app_index,
                            CloseReason close_reason);
  // A callback object that allow us to display an IntentPicker when Run() is
  // executed, it also allow us to report the user's selection back to
  // OnIntentPickerClosed().
  ShowIntentPickerCallback show_intent_picker_callback_;

  // A cache of the action the user took the last time this navigation throttle
  // popped up the intent picker dialog.  If the dialog has never been popped up
  // before, this will have a value of CloseReason::INVALID.  Used to avoid
  // popping up the dialog multiple times on chains of multiple redirects.
  CloseReason previous_user_action_;

  // This has to be the last member of the class.
  base::WeakPtrFactory<ArcNavigationThrottle> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ArcNavigationThrottle);
};

}  // namespace arc

#endif  // CHROME_BROWSER_CHROMEOS_ARC_ARC_NAVIGATION_THROTTLE_H_

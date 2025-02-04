// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/welcome_handler.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/profile_chooser_constants.h"
#include "chrome/common/url_constants.h"
#include "components/signin/core/browser/signin_manager.h"
#include "components/signin/core/browser/signin_metrics.h"
#include "ui/base/page_transition_types.h"

WelcomeHandler::WelcomeHandler(content::WebUI* web_ui)
    : profile_(Profile::FromWebUI(web_ui)),
      browser_(chrome::FindBrowserWithWebContents(web_ui->GetWebContents())),
      oauth2_token_service_(
          ProfileOAuth2TokenServiceFactory::GetForProfile(profile_)),
      result_(WelcomeResult::DEFAULT) {
  oauth2_token_service_->AddObserver(this);
}

WelcomeHandler::~WelcomeHandler() {
  oauth2_token_service_->RemoveObserver(this);
  // TODO(tmartino): Log to UMA according to WelcomeResult.
}

// Override from OAuth2TokenService::Observer. Occurs when a new auth token is
// available.
void WelcomeHandler::OnRefreshTokenAvailable(const std::string& account_id) {
  result_ = WelcomeResult::SIGNED_IN;
  GoToNewTabPage();
}

// Handles backend events necessary when user clicks "Sign in."
void WelcomeHandler::HandleActivateSignIn(const base::ListValue* args) {
  if (SigninManagerFactory::GetForProfile(profile_)->IsAuthenticated()) {
    // In general, this page isn't shown to signed-in users; however, if one
    // should arrive here, then opening the sign-in dialog will likely lead
    // to a crash. Thus, we just act like sign-in was "successful" and whisk
    // them away to the NTP instead.
    GoToNewTabPage();
  } else {
    browser_->ShowModalSigninWindow(
        profiles::BubbleViewMode::BUBBLE_VIEW_MODE_GAIA_SIGNIN,
        signin_metrics::AccessPoint::ACCESS_POINT_START_PAGE);
  }
}

// Handles backend events necessary when user clicks "No thanks."
void WelcomeHandler::HandleUserDecline(const base::ListValue* args) {
  result_ = WelcomeResult::DECLINED;
  GoToNewTabPage();
}

// Override from WebUIMessageHandler.
void WelcomeHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "handleActivateSignIn", base::Bind(&WelcomeHandler::HandleActivateSignIn,
                                         base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "handleUserDecline",
      base::Bind(&WelcomeHandler::HandleUserDecline, base::Unretained(this)));
}

void WelcomeHandler::GoToNewTabPage() {
  chrome::NavigateParams params(browser_, GURL(chrome::kChromeUINewTabURL),
                                ui::PageTransition::PAGE_TRANSITION_LINK);
  chrome::Navigate(&params);
}

// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/result_codes.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/process_manager.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/result_catcher.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(USE_ASH)
#include "extensions/browser/app_window/app_window_registry.h"
#endif

using content::OpenURLParams;
using content::Referrer;
using content::WebContents;

// The test uses the chrome.browserAction.openPopup API, which requires that the
// window can automatically be activated.
// See comments at BrowserActionInteractiveTest::ShouldRunPopupTest
// Fails flakily on all platforms. https://crbug.com/477691
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, DISABLED_WindowOpen) {
  extensions::ResultCatcher catcher;
  ASSERT_TRUE(LoadExtensionIncognito(test_data_dir_
      .AppendASCII("window_open").AppendASCII("spanning")));
  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

bool WaitForTabsAndPopups(Browser* browser,
                          int num_tabs,
                          int num_popups) {
  SCOPED_TRACE(
      base::StringPrintf("WaitForTabsAndPopups tabs:%d, popups:%d",
                         num_tabs, num_popups));
  // We start with one tab and one browser already open.
  ++num_tabs;
  size_t num_browsers = static_cast<size_t>(num_popups) + 1;

  const base::TimeDelta kWaitTime = base::TimeDelta::FromSeconds(10);
  base::TimeTicks end_time = base::TimeTicks::Now() + kWaitTime;
  while (base::TimeTicks::Now() < end_time) {
    if (chrome::GetBrowserCount(browser->profile()) == num_browsers &&
        browser->tab_strip_model()->count() == num_tabs)
      break;

    content::RunAllPendingInMessageLoop();
  }

  EXPECT_EQ(num_browsers, chrome::GetBrowserCount(browser->profile()));
  EXPECT_EQ(num_tabs, browser->tab_strip_model()->count());

  int num_popups_seen = 0;
  for (auto* b : *BrowserList::GetInstance()) {
    if (b == browser)
      continue;

    EXPECT_TRUE(b->is_type_popup());
    ++num_popups_seen;
  }
  EXPECT_EQ(num_popups, num_popups_seen);

  return ((num_browsers == chrome::GetBrowserCount(browser->profile())) &&
          (num_tabs == browser->tab_strip_model()->count()) &&
          (num_popups == num_popups_seen));
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, BrowserIsApp) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("window_open").AppendASCII("browser_is_app")));

  EXPECT_TRUE(WaitForTabsAndPopups(browser(), 0, 2));

  for (auto* b : *BrowserList::GetInstance()) {
    if (b == browser())
      ASSERT_FALSE(b->is_app());
    else
      ASSERT_TRUE(b->is_app());
  }
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, WindowOpenPopupDefault) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("window_open").AppendASCII("popup")));

  const int num_tabs = 1;
  const int num_popups = 0;
  EXPECT_TRUE(WaitForTabsAndPopups(browser(), num_tabs, num_popups));
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, WindowOpenPopupIframe) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  base::FilePath test_data_dir;
  PathService::Get(chrome::DIR_TEST_DATA, &test_data_dir);
  embedded_test_server()->ServeFilesFromDirectory(test_data_dir);
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("window_open").AppendASCII("popup_iframe")));

  const int num_tabs = 1;
  const int num_popups = 0;
  EXPECT_TRUE(WaitForTabsAndPopups(browser(), num_tabs, num_popups));
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, WindowOpenPopupLarge) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("window_open").AppendASCII("popup_large")));

  // On other systems this should open a new popup window.
  const int num_tabs = 0;
  const int num_popups = 1;
  EXPECT_TRUE(WaitForTabsAndPopups(browser(), num_tabs, num_popups));
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, WindowOpenPopupSmall) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("window_open").AppendASCII("popup_small")));

  // On ChromeOS this should open a new panel (acts like a new popup window).
  // On other systems this should open a new popup window.
  const int num_tabs = 0;
  const int num_popups = 1;
  EXPECT_TRUE(WaitForTabsAndPopups(browser(), num_tabs, num_popups));
}

// Disabled on Windows. Often times out or fails: crbug.com/177530
#if defined(OS_WIN)
#define MAYBE_PopupBlockingExtension DISABLED_PopupBlockingExtension
#else
#define MAYBE_PopupBlockingExtension PopupBlockingExtension
#endif
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, MAYBE_PopupBlockingExtension) {
  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());

  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("window_open").AppendASCII("popup_blocking")
      .AppendASCII("extension")));

  EXPECT_TRUE(WaitForTabsAndPopups(browser(), 5, 3));
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, PopupBlockingHostedApp) {
  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(embedded_test_server()->Start());

  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("window_open").AppendASCII("popup_blocking")
      .AppendASCII("hosted_app")));

  // The app being tested owns the domain a.com .  The test URLs we navigate
  // to below must be within that domain, so that they fall within the app's
  // web extent.
  GURL::Replacements replace_host;
  replace_host.SetHostStr("a.com");

  const std::string popup_app_contents_path(
      "/extensions/api_test/window_open/popup_blocking/hosted_app/");

  GURL open_tab = embedded_test_server()
                      ->GetURL(popup_app_contents_path + "open_tab.html")
                      .ReplaceComponents(replace_host);
  GURL open_popup = embedded_test_server()
                        ->GetURL(popup_app_contents_path + "open_popup.html")
                        .ReplaceComponents(replace_host);

  browser()->OpenURL(OpenURLParams(open_tab, Referrer(),
                                   WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                   ui::PAGE_TRANSITION_TYPED, false));
  browser()->OpenURL(OpenURLParams(open_popup, Referrer(),
                                   WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                   ui::PAGE_TRANSITION_TYPED, false));

  EXPECT_TRUE(WaitForTabsAndPopups(browser(), 3, 1));
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, WindowArgumentsOverflow) {
  ASSERT_TRUE(RunExtensionTest("window_open/argument_overflow")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, DISABLED_WindowOpener) {
  ASSERT_TRUE(RunExtensionTest("window_open/opener")) << message_;
}

#if defined(OS_MACOSX)
// Extension popup windows are incorrectly sized on OSX, crbug.com/225601
#define MAYBE_WindowOpenSized DISABLED_WindowOpenSized
#else
#define MAYBE_WindowOpenSized WindowOpenSized
#endif
// Ensure that the width and height properties of a window opened with
// chrome.windows.create match the creation parameters. See crbug.com/173831.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, MAYBE_WindowOpenSized) {
  ASSERT_TRUE(RunExtensionTest("window_open/window_size")) << message_;
  EXPECT_TRUE(WaitForTabsAndPopups(browser(), 0, 1));
}

// Tests that an extension page can call window.open to an extension URL and
// the new window has extension privileges.
IN_PROC_BROWSER_TEST_F(ExtensionBrowserTest, WindowOpenExtension) {
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("uitest").AppendASCII("window_open")));

  GURL start_url(std::string(extensions::kExtensionScheme) +
                     url::kStandardSchemeSeparator +
                     last_loaded_extension_id() + "/test.html");
  ui_test_utils::NavigateToURL(browser(), start_url);
  WebContents* newtab = NULL;
  ASSERT_NO_FATAL_FAILURE(
      OpenWindow(browser()->tab_strip_model()->GetActiveWebContents(),
      start_url.Resolve("newtab.html"), true, &newtab));

  bool result = false;
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(newtab, "testExtensionApi()",
                                                   &result));
  EXPECT_TRUE(result);
}

// Tests that if an extension page calls window.open to an invalid extension
// URL, the browser doesn't crash.
IN_PROC_BROWSER_TEST_F(ExtensionBrowserTest, WindowOpenInvalidExtension) {
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("uitest").AppendASCII("window_open")));

  GURL start_url(std::string(extensions::kExtensionScheme) +
                     url::kStandardSchemeSeparator +
                     last_loaded_extension_id() + "/test.html");
  ui_test_utils::NavigateToURL(browser(), start_url);
  ASSERT_NO_FATAL_FAILURE(
      OpenWindow(browser()->tab_strip_model()->GetActiveWebContents(),
      GURL("chrome-extension://thisissurelynotavalidextensionid/newtab.html"),
      false, NULL));

  // If we got to this point, we didn't crash, so we're good.
}

// Tests that calling window.open from the newtab page to an extension URL
// gives the new window extension privileges - even though the opening page
// does not have extension privileges, we break the script connection, so
// there is no privilege leak.
IN_PROC_BROWSER_TEST_F(ExtensionBrowserTest, WindowOpenNoPrivileges) {
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("uitest").AppendASCII("window_open")));

  ui_test_utils::NavigateToURL(browser(), GURL("about:blank"));
  WebContents* newtab = NULL;
  ASSERT_NO_FATAL_FAILURE(
      OpenWindow(browser()->tab_strip_model()->GetActiveWebContents(),
                 GURL(std::string(extensions::kExtensionScheme) +
                     url::kStandardSchemeSeparator +
                     last_loaded_extension_id() + "/newtab.html"),
                 false,
                 &newtab));

  // Extension API should succeed.
  bool result = false;
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(newtab, "testExtensionApi()",
                                                   &result));
  EXPECT_TRUE(result);
}

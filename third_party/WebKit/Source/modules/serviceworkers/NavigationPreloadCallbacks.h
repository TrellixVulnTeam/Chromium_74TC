// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NavigationPreloadCallbacks_h
#define NavigationPreloadCallbacks_h

#include "public/platform/modules/serviceworker/WebServiceWorkerRegistration.h"

namespace blink {

class ScriptPromiseResolver;
struct WebServiceWorkerError;

class EnableNavigationPreloadCallbacks final
    : public WebServiceWorkerRegistration::WebEnableNavigationPreloadCallbacks {
 public:
  EnableNavigationPreloadCallbacks(ScriptPromiseResolver*);
  ~EnableNavigationPreloadCallbacks() override;

  // WebEnableNavigationPreloadCallbacks interface.
  void onSuccess() override;
  void onError(const WebServiceWorkerError&) override;

 private:
  Persistent<ScriptPromiseResolver> m_resolver;
  WTF_MAKE_NONCOPYABLE(EnableNavigationPreloadCallbacks);
};

class DisableNavigationPreloadCallbacks final
    : public WebServiceWorkerRegistration::
          WebDisableNavigationPreloadCallbacks {
 public:
  DisableNavigationPreloadCallbacks(ScriptPromiseResolver*);
  ~DisableNavigationPreloadCallbacks() override;

  // WebDisableNavigationPreloadCallbacks interface.
  void onSuccess() override;
  void onError(const WebServiceWorkerError&) override;

 private:
  Persistent<ScriptPromiseResolver> m_resolver;
  WTF_MAKE_NONCOPYABLE(DisableNavigationPreloadCallbacks);
};

}  // namespace blink

#endif  // NavigationPreloadCallbacks_h

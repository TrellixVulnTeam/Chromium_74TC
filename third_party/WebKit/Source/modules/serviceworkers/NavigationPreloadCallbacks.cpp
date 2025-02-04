// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/serviceworkers/NavigationPreloadCallbacks.h"

#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "core/dom/DOMException.h"
#include "modules/serviceworkers/ServiceWorkerError.h"

namespace blink {

EnableNavigationPreloadCallbacks::EnableNavigationPreloadCallbacks(
    ScriptPromiseResolver* resolver)
    : m_resolver(resolver) {
  DCHECK(m_resolver);
}

EnableNavigationPreloadCallbacks::~EnableNavigationPreloadCallbacks() {}

void EnableNavigationPreloadCallbacks::onSuccess() {
  if (!m_resolver->getExecutionContext() ||
      m_resolver->getExecutionContext()->activeDOMObjectsAreStopped())
    return;
  m_resolver->resolve();
}

void EnableNavigationPreloadCallbacks::onError(
    const WebServiceWorkerError& error) {
  if (!m_resolver->getExecutionContext() ||
      m_resolver->getExecutionContext()->activeDOMObjectsAreStopped())
    return;
  m_resolver->reject(ServiceWorkerError::take(m_resolver.get(), error));
}

DisableNavigationPreloadCallbacks::DisableNavigationPreloadCallbacks(
    ScriptPromiseResolver* resolver)
    : m_resolver(resolver) {
  DCHECK(m_resolver);
}

DisableNavigationPreloadCallbacks::~DisableNavigationPreloadCallbacks() {}

void DisableNavigationPreloadCallbacks::onSuccess() {
  if (!m_resolver->getExecutionContext() ||
      m_resolver->getExecutionContext()->activeDOMObjectsAreStopped())
    return;
  m_resolver->resolve();
}

void DisableNavigationPreloadCallbacks::onError(
    const WebServiceWorkerError& error) {
  if (!m_resolver->getExecutionContext() ||
      m_resolver->getExecutionContext()->activeDOMObjectsAreStopped())
    return;
  m_resolver->reject(ServiceWorkerError::take(m_resolver.get(), error));
}

}  // namespace blink

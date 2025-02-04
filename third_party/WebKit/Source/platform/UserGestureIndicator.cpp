/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "platform/UserGestureIndicator.h"

#include "wtf/Assertions.h"
#include "wtf/CurrentTime.h"

namespace blink {

// User gestures timeout in 1 second.
const double userGestureTimeout = 1.0;

// For out of process tokens we allow a 10 second delay.
const double userGestureOutOfProcessTimeout = 10.0;

UserGestureToken::UserGestureToken(Status status)
    : m_consumableGestures(0),
      m_timestamp(WTF::currentTime()),
      m_timeoutPolicy(Default),
      m_usageCallback(nullptr) {
  if (status == NewGesture || !UserGestureIndicator::currentToken())
    m_consumableGestures++;
}

bool UserGestureToken::hasGestures() {
  return m_consumableGestures && !hasTimedOut();
}

void UserGestureToken::transferGestureTo(UserGestureToken* other) {
  if (!hasGestures())
    return;
  m_consumableGestures--;
  other->m_consumableGestures++;
  other->m_timestamp = WTF::currentTime();
}

bool UserGestureToken::consumeGesture() {
  if (!m_consumableGestures)
    return false;
  m_consumableGestures--;
  return true;
}

void UserGestureToken::setTimeoutPolicy(TimeoutPolicy policy) {
  if (!hasTimedOut() && hasGestures() && policy > m_timeoutPolicy)
    m_timeoutPolicy = policy;
}

bool UserGestureToken::hasTimedOut() const {
  if (m_timeoutPolicy == HasPaused)
    return false;
  double timeout = m_timeoutPolicy == OutOfProcess
                       ? userGestureOutOfProcessTimeout
                       : userGestureTimeout;
  return WTF::currentTime() - m_timestamp > timeout;
}

void UserGestureToken::setUserGestureUtilizedCallback(
    UserGestureUtilizedCallback* callback) {
  CHECK(this == UserGestureIndicator::currentToken());
  m_usageCallback = callback;
}

void UserGestureToken::userGestureUtilized() {
  if (m_usageCallback) {
    m_usageCallback->userGestureUtilized();
    m_usageCallback = nullptr;
  }
}

UserGestureToken* UserGestureIndicator::s_rootToken = nullptr;
bool UserGestureIndicator::s_processedUserGestureSinceLoad = false;

UserGestureIndicator::UserGestureIndicator(PassRefPtr<UserGestureToken> token)
    : m_token(token) {
  // Silently ignore UserGestureIndicators on non-main threads.
  if (!isMainThread() || !m_token)
    return;

  if (!s_rootToken)
    s_rootToken = m_token.get();
  else
    m_token->transferGestureTo(s_rootToken);
  s_processedUserGestureSinceLoad = true;
}

UserGestureIndicator::~UserGestureIndicator() {
  if (isMainThread() && m_token && m_token == s_rootToken) {
    s_rootToken->setUserGestureUtilizedCallback(nullptr);
    s_rootToken = nullptr;
  }
}

// static
bool UserGestureIndicator::utilizeUserGesture() {
  if (UserGestureIndicator::processingUserGesture()) {
    s_rootToken->userGestureUtilized();
    return true;
  }
  return false;
}

bool UserGestureIndicator::processingUserGesture() {
  if (auto* token = currentToken()) {
    ASSERT(isMainThread());
    return token->hasGestures();
  }

  return false;
}

// static
bool UserGestureIndicator::consumeUserGesture() {
  if (auto* token = currentToken()) {
    ASSERT(isMainThread());
    if (token->consumeGesture()) {
      token->userGestureUtilized();
      return true;
    }
  }
  return false;
}

// static
UserGestureToken* UserGestureIndicator::currentToken() {
  if (!isMainThread() || !s_rootToken)
    return nullptr;
  return s_rootToken;
}

// static
void UserGestureIndicator::clearProcessedUserGestureSinceLoad() {
  if (isMainThread())
    s_processedUserGestureSinceLoad = false;
}

// static
bool UserGestureIndicator::processedUserGestureSinceLoad() {
  if (!isMainThread())
    return false;
  return s_processedUserGestureSinceLoad;
}

}  // namespace blink

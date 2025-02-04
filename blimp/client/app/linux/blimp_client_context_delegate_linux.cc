// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/ptr_util.h"
#include "blimp/client/app/linux/blimp_client_context_delegate_linux.h"
#include "blimp/client/support/session/blimp_default_identity_provider.h"
#include "net/base/net_errors.h"

namespace blimp {
namespace client {

BlimpClientContextDelegateLinux::BlimpClientContextDelegateLinux() = default;

BlimpClientContextDelegateLinux::~BlimpClientContextDelegateLinux() = default;

void BlimpClientContextDelegateLinux::AttachBlimpContentsHelpers(
    BlimpContents* blimp_contents) {}

void BlimpClientContextDelegateLinux::OnAssignmentConnectionAttempted(
    AssignmentRequestResult result,
    const Assignment& assignment) {
  // TODO(xingliu): Update this to use the new error strings and logging helper
  // methods, and access the string from grd files. https://crbug.com/630687
  switch (result) {
    case AssignmentRequestResult::ASSIGNMENT_REQUEST_RESULT_OK:
      VLOG(0) << "Assignment request success";
      break;
    case AssignmentRequestResult::ASSIGNMENT_REQUEST_RESULT_UNKNOWN:
      LOG(WARNING) << "Assignment request result unknown";
      break;
    case AssignmentRequestResult::ASSIGNMENT_REQUEST_RESULT_BAD_REQUEST:
      LOG(WARNING) << "Assignment request error: Bad request";
      break;
    case AssignmentRequestResult::ASSIGNMENT_REQUEST_RESULT_BAD_RESPONSE:
      LOG(WARNING) << "Assignment request error: Bad response";
      break;
    case AssignmentRequestResult::
        ASSIGNMENT_REQUEST_RESULT_INVALID_PROTOCOL_VERSION:
      LOG(WARNING) << "Assignment request error: Invalid protocol version";
      break;
    case AssignmentRequestResult::
        ASSIGNMENT_REQUEST_RESULT_EXPIRED_ACCESS_TOKEN:
      LOG(WARNING) << "Assignment request error: Expired access token";
      break;
    case AssignmentRequestResult::ASSIGNMENT_REQUEST_RESULT_USER_INVALID:
      LOG(WARNING) << "Assignment request error: User invalid";
      break;
    case AssignmentRequestResult::ASSIGNMENT_REQUEST_RESULT_OUT_OF_VMS:
      LOG(WARNING) << "Assignment request error: Out of VMs";
      break;
    case AssignmentRequestResult::ASSIGNMENT_REQUEST_RESULT_SERVER_ERROR:
      LOG(WARNING) << "Assignment request error: Server error";
      break;
    case AssignmentRequestResult::ASSIGNMENT_REQUEST_RESULT_SERVER_INTERRUPTED:
      LOG(WARNING) << "Assignment request error: Server interrupted";
      break;
    case AssignmentRequestResult::ASSIGNMENT_REQUEST_RESULT_NETWORK_FAILURE:
      LOG(WARNING) << "Assignment request error: Network failure";
      break;
    case AssignmentRequestResult::ASSIGNMENT_REQUEST_RESULT_INVALID_CERT:
      LOG(WARNING) << "Assignment request error: Invalid cert";
      break;
  }
}

std::unique_ptr<IdentityProvider>
BlimpClientContextDelegateLinux::CreateIdentityProvider() {
  return base::MakeUnique<BlimpDefaultIdentityProvider>();
}

void BlimpClientContextDelegateLinux::OnAuthenticationError(
    const GoogleServiceAuthError& error) {
  LOG(WARNING) << "GoogleAuth error : " << error.ToString();
}

void BlimpClientContextDelegateLinux::OnConnected() {
  VLOG(1) << "Connected.";
}

void BlimpClientContextDelegateLinux::OnEngineDisconnected(int result) {
  LOG(WARNING) << "Disconnected from the engine, reason: " << result;
}

void BlimpClientContextDelegateLinux::OnNetworkDisconnected(int result) {
  LOG(WARNING) << "Disconnected, reason: " << net::ErrorToShortString(result);
}

}  // namespace client
}  // namespace blimp

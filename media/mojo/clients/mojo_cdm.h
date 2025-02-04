// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_MOJO_CLIENTS_MOJO_CDM_H_
#define MEDIA_MOJO_CLIENTS_MOJO_CDM_H_

#include <stdint.h>

#include <memory>
#include <utility>
#include <vector>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_checker.h"
#include "media/base/cdm_context.h"
#include "media/base/cdm_initialized_promise.h"
#include "media/base/media_keys.h"
#include "media/mojo/interfaces/content_decryption_module.mojom.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace media {

class MojoDecryptor;

// A MediaKeys that proxies to a mojom::ContentDecryptionModule. That
// mojom::ContentDecryptionModule proxies back to the MojoCdm via the
// mojom::ContentDecryptionModuleClient interface.
class MojoCdm : public MediaKeys,
                public CdmContext,
                public mojom::ContentDecryptionModuleClient {
 public:
  static void Create(
      const std::string& key_system,
      const GURL& security_origin,
      const media::CdmConfig& cdm_config,
      mojom::ContentDecryptionModulePtr remote_cdm,
      const media::SessionMessageCB& session_message_cb,
      const media::SessionClosedCB& session_closed_cb,
      const media::SessionKeysChangeCB& session_keys_change_cb,
      const media::SessionExpirationUpdateCB& session_expiration_update_cb,
      const media::CdmCreatedCB& cdm_created_cb);

  // MediaKeys implementation.
  void SetServerCertificate(const std::vector<uint8_t>& certificate,
                            std::unique_ptr<SimpleCdmPromise> promise) final;
  void CreateSessionAndGenerateRequest(
      SessionType session_type,
      EmeInitDataType init_data_type,
      const std::vector<uint8_t>& init_data,
      std::unique_ptr<NewSessionCdmPromise> promise) final;
  void LoadSession(SessionType session_type,
                   const std::string& session_id,
                   std::unique_ptr<NewSessionCdmPromise> promise) final;
  void UpdateSession(const std::string& session_id,
                     const std::vector<uint8_t>& response,
                     std::unique_ptr<SimpleCdmPromise> promise) final;
  void CloseSession(const std::string& session_id,
                    std::unique_ptr<SimpleCdmPromise> promise) final;
  void RemoveSession(const std::string& session_id,
                     std::unique_ptr<SimpleCdmPromise> promise) final;
  CdmContext* GetCdmContext() final;

  // CdmContext implementation. Can be called on a different thread.
  // All GetDecryptor() calls must be made on the same thread.
  media::Decryptor* GetDecryptor() final;
  int GetCdmId() const final;

 private:
  MojoCdm(mojom::ContentDecryptionModulePtr remote_cdm,
          const SessionMessageCB& session_message_cb,
          const SessionClosedCB& session_closed_cb,
          const SessionKeysChangeCB& session_keys_change_cb,
          const SessionExpirationUpdateCB& session_expiration_update_cb);

  ~MojoCdm() final;

  void InitializeCdm(const std::string& key_system,
                     const GURL& security_origin,
                     const media::CdmConfig& cdm_config,
                     std::unique_ptr<CdmInitializedPromise> promise);

  void OnConnectionError();

  // mojom::ContentDecryptionModuleClient implementation.
  void OnSessionMessage(const std::string& session_id,
                        mojom::CdmMessageType message_type,
                        const std::vector<uint8_t>& message) final;
  void OnSessionClosed(const std::string& session_id) final;
  void OnSessionKeysChange(
      const std::string& session_id,
      bool has_additional_usable_key,
      std::vector<mojom::CdmKeyInformationPtr> keys_info) final;
  void OnSessionExpirationUpdate(const std::string& session_id,
                                 double new_expiry_time_sec) final;

  // Callback for InitializeCdm.
  void OnCdmInitialized(mojom::CdmPromiseResultPtr result,
                        int cdm_id,
                        mojom::DecryptorPtr decryptor);

  // Callback when new decryption key is available.
  void OnKeyAdded();

  // Callbacks to handle CDM promises.
  void OnSimpleCdmPromiseResult(std::unique_ptr<SimpleCdmPromise> promise,
                                mojom::CdmPromiseResultPtr result);
  void OnNewSessionCdmPromiseResult(
      std::unique_ptr<NewSessionCdmPromise> promise,
      mojom::CdmPromiseResultPtr result,
      const std::string& session_id);

  base::ThreadChecker thread_checker_;

  mojom::ContentDecryptionModulePtr remote_cdm_;
  mojo::Binding<ContentDecryptionModuleClient> binding_;

  // Protects |cdm_id_|, |decryptor_ptr_|, |decryptor_| and
  // |decryptor_task_runner_| which could be accessed from other threads.
  // See CdmContext implementation above.
  mutable base::Lock lock_;

  // CDM ID of the remote CDM. Set after initialization is completed. Must not
  // be invalid if initialization succeeded.
  int cdm_id_;

  // The DecryptorPtrInfo exposed by the remote CDM. Set after initialization
  // is completed and cleared after |decryptor_| is created. May be invalid
  // after initialization if the CDM doesn't support a Decryptor.
  mojom::DecryptorPtrInfo decryptor_ptr_info_;

  // Decryptor based on |decryptor_ptr_|, lazily created in GetDecryptor().
  // Since GetDecryptor() can be called on a different thread, use
  // |decryptor_task_runner_| to bind |decryptor_| to that thread.
  std::unique_ptr<MojoDecryptor> decryptor_;
  scoped_refptr<base::SingleThreadTaskRunner> decryptor_task_runner_;

  // Callbacks for firing session events.
  SessionMessageCB session_message_cb_;
  SessionClosedCB session_closed_cb_;
  SessionKeysChangeCB session_keys_change_cb_;
  SessionExpirationUpdateCB session_expiration_update_cb_;

  // Pending promise for InitializeCdm().
  std::unique_ptr<CdmInitializedPromise> pending_init_promise_;

  // This must be the last member.
  base::WeakPtrFactory<MojoCdm> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(MojoCdm);
};

}  // namespace media

#endif  // MEDIA_MOJO_CLIENTS_MOJO_CDM_H_

// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_RENDERER_API_BINDING_H_
#define EXTENSIONS_RENDERER_API_BINDING_H_

#include <map>
#include <memory>
#include <string>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "extensions/renderer/argument_spec.h"
#include "v8/include/v8.h"

namespace base {
class ListValue;
}

namespace gin {
class Arguments;
}

namespace extensions {

// A class that vends v8::Objects for extension APIs. These APIs have function
// interceptors for all exposed methods, which call back into the APIBinding.
// The APIBinding then matches the calling arguments against an expected method
// signature, throwing an error if they don't match.
// There should only need to be a single APIBinding object for each API, and
// each can vend multiple v8::Objects for different contexts.
// TODO(devlin): What's the lifetime of this object?
class APIBinding {
 public:
  // The callback to called when an API method is invoked with matching
  // arguments. This passes the name of the api method and the arguments it
  // was passed.
  using APIMethodCallback =
      base::Callback<void(const std::string&,
                          std::unique_ptr<base::ListValue>)>;
  using HandlerCallback = base::Callback<void(gin::Arguments*)>;

  // The ArgumentSpec::RefMap is required to outlive this object.
  APIBinding(const std::string& name,
             const base::ListValue& function_definitions,
             const base::ListValue& type_definitions,
             const APIMethodCallback& callback,
             ArgumentSpec::RefMap* type_refs);
  ~APIBinding();

  // Returns a new v8::Object for the API this APIBinding represents.
  v8::Local<v8::Object> CreateInstance(v8::Local<v8::Context> context,
                                       v8::Isolate* isolate);

 private:
  using APISignature = std::vector<std::unique_ptr<ArgumentSpec>>;

  // Per-context data that stores the callbacks that are used within the
  // context. Since these callbacks are used within v8::Externals, v8 itself
  // does not clean them up.
  struct APIPerContextData : public base::SupportsUserData::Data {
    APIPerContextData();
    ~APIPerContextData() override;

    std::vector<std::unique_ptr<HandlerCallback>> context_callbacks;
  };

  // Handles a call an API method with the given |name| and matches the
  // arguments against |signature|.
  void HandleCall(const std::string& name,
                  const APISignature* signature,
                  gin::Arguments* args);

  // A map from method name to expected signature.
  std::map<std::string, std::unique_ptr<APISignature>> signatures_;

  // The callback to use when an API is invoked with valid arguments.
  APIMethodCallback method_callback_;

  // The reference map for all known types; required to outlive this object.
  const ArgumentSpec::RefMap* type_refs_;

  base::WeakPtrFactory<APIBinding> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(APIBinding);
};

}  // namespace extensions

#endif  // EXTENSIONS_RENDERER_API_BINDING_H_

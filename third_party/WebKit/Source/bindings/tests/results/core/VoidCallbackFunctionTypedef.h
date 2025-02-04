// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file has been auto-generated by code_generator_v8.py. DO NOT MODIFY!

// clang-format off

#ifndef VoidCallbackFunctionTypedef_h
#define VoidCallbackFunctionTypedef_h

#include "bindings/core/v8/ScopedPersistent.h"
#include "core/CoreExport.h"
#include "platform/heap/Handle.h"
#include "wtf/text/WTFString.h"

namespace blink {

class ScriptState;

class CORE_EXPORT VoidCallbackFunctionTypedef final : public GarbageCollectedFinalized<VoidCallbackFunctionTypedef> {
public:
    static VoidCallbackFunctionTypedef* create(v8::Isolate* isolate, v8::Local<v8::Function> callback)
    {
        return new VoidCallbackFunctionTypedef(isolate, callback);
    }

    ~VoidCallbackFunctionTypedef() = default;

    DECLARE_TRACE();

    bool call(ScriptState* scriptState, ScriptWrappable* scriptWrappable, const String& arg);

    v8::Local<v8::Function> v8Value(v8::Isolate* isolate)
    {
        return m_callback.newLocal(isolate);
    }

    void setWrapperReference(v8::Isolate* isolate, const v8::Persistent<v8::Object>& wrapper)
    {
        DCHECK(!m_callback.isEmpty());
        m_callback.setReference(wrapper, isolate);
    }

private:
    VoidCallbackFunctionTypedef(v8::Isolate* isolate, v8::Local<v8::Function>);
    ScopedPersistent<v8::Function> m_callback;
};

} // namespace blink

#endif // VoidCallbackFunctionTypedef_h

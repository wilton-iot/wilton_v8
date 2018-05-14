/*
 * Copyright 2018, alex at staticlibs.net
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * File:   v8_engine.cpp
 * Author: alex
 * 
 * Created on May 8, 2018, 9:59 PM
 */

#include "v8_engine.hpp"

#include <cstdio>
#include <functional>
#include <memory>

#include "v8.h"
#include "libplatform/libplatform.h"

#include "staticlib/io.hpp"
#include "staticlib/json.hpp"
#include "staticlib/pimpl/forward_macros.hpp"
#include "staticlib/support.hpp"
#include "staticlib/utils.hpp"

#include "wilton/wiltoncall.h"
#include "wilton/wilton_loader.h"

#include "wilton/support/exception.hpp"
#include "wilton/support/logging.hpp"

namespace wilton {
namespace v8eng {

class v8_engine::impl : public sl::pimpl::object::impl {
    v8::Isolate* isolate = nullptr;

public:

    ~impl() STATICLIB_NOEXCEPT {
        if (nullptr != isolate) {
            isolate->Dispose();
        }
    }

    impl(sl::io::span<const char> init_code) {
        wilton::support::log_info("wilton.engine.v8.init", "Initializing engine instance ...");
        v8::Isolate::CreateParams create_params;
        create_params.array_buffer_allocator =
                v8::ArrayBuffer::Allocator::NewDefaultAllocator();
        this->isolate = v8::Isolate::New(create_params);

        wilton::support::log_info("wilton.engine.v8.init", "Engine initialization complete");
    }

    support::buffer run_callback_script(v8_engine&, sl::io::span<const char> callback_script_json) {
        wilton::support::log_debug("wilton.engine.v8.run",
                "Running callback script: [" + std::string(callback_script_json.data(), callback_script_json.size()) + "] ...");
//        wilton::support::log_debug("wilton.engine.v8.run",
//                "Callback run complete, result: [" + sl::support::to_string_bool(nullptr != res) + "]");
        return support::make_null_buffer();
    }

    void run_garbage_collector(v8_engine&) {
    }

    static void initialize() {
        v8::Platform* platform = v8::platform::CreateDefaultPlatform();
        v8::V8::InitializePlatform(platform);
        v8::V8::Initialize();
    }
};

PIMPL_FORWARD_CONSTRUCTOR(v8_engine, (sl::io::span<const char>), (), support::exception)
PIMPL_FORWARD_METHOD(v8_engine, support::buffer, run_callback_script, (sl::io::span<const char>), (), support::exception)
PIMPL_FORWARD_METHOD(v8_engine, void, run_garbage_collector, (), (), support::exception)
PIMPL_FORWARD_METHOD_STATIC(v8_engine, void, initialize, (), (), support::exception)

} // namespace
}

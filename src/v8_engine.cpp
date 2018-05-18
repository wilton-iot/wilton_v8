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

namespace { // anonymous

v8::Local<v8::String> string_to_jsval(v8::Isolate* isolate, const char* str, size_t str_len) STATICLIB_NOEXCEPT {
    v8::EscapableHandleScope handle_scope(isolate);
    auto maybe = v8::String::NewFromUtf8(isolate, str, v8::NewStringType::kNormal, static_cast<int>(str_len));
    if (maybe.IsEmpty()) {
        auto empty = v8::String::NewFromUtf8(isolate, "", v8::NewStringType::kNormal).ToLocalChecked();
        return handle_scope.Escape(empty);
    }
    auto res = maybe.ToLocalChecked();
    return handle_scope.Escape(res);
}

v8::Local<v8::String> string_to_jsval(v8::Isolate* isolate, const std::string& str) STATICLIB_NOEXCEPT {
    return string_to_jsval(isolate, str.data(), str.length());
}

v8::Local<v8::Value> json_to_jsval(v8::Local<v8::Context>& ctx, const sl::json::value& json) {
    auto isolate = ctx->GetIsolate();
    v8::EscapableHandleScope handle_scope(isolate);
    auto json_val = string_to_jsval(isolate, json.dumps());
    auto maybe = v8::JSON::Parse(ctx, json_val);
    if (maybe.IsEmpty()) {
        auto empty = v8::String::NewFromUtf8(isolate, "", v8::NewStringType::kNormal).ToLocalChecked();
        return handle_scope.Escape(empty);
    }
    auto res = maybe.ToLocalChecked();
    return handle_scope.Escape(res);
}

void throw_js_exception(v8::Local<v8::Context>& ctx, const std::string& msg) {
    // v8::Exception::Error segfaults for some reason
    auto isolate = ctx->GetIsolate();
    v8::HandleScope handle_scope(isolate);
    auto json = sl::json::value({
        {"message", msg},
        {"stack", ""}
    });
    auto json_val = string_to_jsval(isolate, json.dumps());
    auto err_maybe = v8::JSON::Parse(ctx, json_val);
    if (err_maybe.IsEmpty()) {
        auto str = string_to_jsval(isolate, msg);
        isolate->ThrowException(str);
    }
    auto err = err_maybe.ToLocalChecked();
    isolate->ThrowException(err);
}

std::string jsval_to_string(v8::Isolate* isolate, const v8::Local<v8::Value>& value) STATICLIB_NOEXCEPT {
    v8::String::Utf8Value utf8(isolate, value);
    if (utf8.length() > 0) {
        return std::string(*utf8, static_cast<size_t>(utf8.length()));
    }
    return std::string();
}

std::string format_stack_trace(v8::Local<v8::Context>& ctx, const v8::TryCatch& trycatch) STATICLIB_NOEXCEPT {
    auto isolate = ctx->GetIsolate();
    v8::HandleScope handle_scope(isolate);
    auto stack_maybe = trycatch.StackTrace(ctx);
    auto stack = std::string();
    if (!stack_maybe.IsEmpty()) {
        auto stack_val = stack_maybe.ToLocalChecked();
        stack = jsval_to_string(isolate, stack_val);
    }
    auto vec = sl::utils::split(stack, '\n');
    auto res = std::string();
    for (size_t i = 0; i < vec.size(); i++) {
        auto& line = vec.at(i);
        if (line.length() > 1 && !(std::string::npos != line.find("wilton-requirejs/require.js:")) &&
                !(std::string::npos != line.find("wilton-require.js:"))) {
            res += line;
            res.push_back('\n');
        }
    }
    if (res.length() > 0 && '\n' == res.back()) {
        res.pop_back();
    }
    return res;
}

std::string eval_js(v8::Local<v8::Context>& ctx, const char* code, size_t code_len, const std::string& path) {
    auto isolate = ctx->GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Context::Scope ctx_scope(ctx);
    // compile
    auto code_val = string_to_jsval(isolate, code, code_len);
    auto path_val = string_to_jsval(isolate, path);
    v8::ScriptOrigin origin(path_val);
    v8::ScriptCompiler::Source source(code_val, origin);
    v8::TryCatch trycatch(isolate);
    auto script_maybe = v8::ScriptCompiler::Compile(ctx, std::addressof(source));
    if (script_maybe.IsEmpty()) {
        auto stack = format_stack_trace(ctx, trycatch);
        throw support::exception(TRACEMSG(stack));
    }
    auto script = script_maybe.ToLocalChecked();
    // run
    auto run_maybe = script->Run(ctx);
    if (run_maybe.IsEmpty()) {
        auto stack = format_stack_trace(ctx, trycatch);
        throw support::exception(TRACEMSG(stack));
    }
    auto run = run_maybe.ToLocalChecked();
    return jsval_to_string(isolate, run);
}

void print_func(const v8::FunctionCallbackInfo<v8::Value>& args) STATICLIB_NOEXCEPT {
    auto isolate = args.GetIsolate();
    if (args.Length() > 0) {
        auto str = jsval_to_string(isolate, args[0]);
        puts(str.c_str());
    } else {
        puts("");
    }
}

void load_func(const v8::FunctionCallbackInfo<v8::Value>& args) STATICLIB_NOEXCEPT {
    auto isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    auto ctx = isolate->GetCurrentContext();
    v8::Context::Scope ctx_scope(ctx);
    auto path = std::string();
    try {
        if (args.Length() < 1 || !args[0]->IsString()) {
            throw support::exception(TRACEMSG("Invalid arguments specified"));
        }
        path = jsval_to_string(isolate, args[0]);
        // load code
        char* code = nullptr;
        int code_len = 0;
        auto err_load = wilton_load_resource(path.c_str(), static_cast<int>(path.length()),
                std::addressof(code), std::addressof(code_len));
        if (nullptr != err_load) {
            support::throw_wilton_error(err_load, TRACEMSG(err_load));
        }
        auto deferred = sl::support::defer([code] () STATICLIB_NOEXCEPT {
            wilton_free(code);
        });
        auto path_short = support::script_engine_map_detail::shorten_script_path(path);
        wilton::support::log_debug("wilton.engine.v8.eval",
                "Evaluating source file, path: [" + path + "] ...");
        eval_js(ctx, code, static_cast<size_t>(code_len), path_short);
        wilton::support::log_debug("wilton.engine.v8.eval", "Eval complete");
    } catch (const std::exception& e) {
        auto msg = TRACEMSG(e.what() + "\nError loading script, path: [" + path + "]");
        throw_js_exception(ctx, msg);
    } catch (...) {
        auto msg = TRACEMSG("Error(...) loading script, path: [" + path + "]");
        throw_js_exception(ctx, msg);
    }
}

void wiltoncall_func(const v8::FunctionCallbackInfo<v8::Value>& args) STATICLIB_NOEXCEPT {
    auto isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    auto ctx = isolate->GetCurrentContext();
    v8::Context::Scope ctx_scope(ctx);
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        auto msg = TRACEMSG("Invalid arguments specified");
        throw_js_exception(ctx, msg);
        return;
    }
    auto name = jsval_to_string(isolate, args[0]);
    auto input = jsval_to_string(isolate, args[1]);
    // call wilton
    char* out = nullptr;
    int out_len = 0;
    wilton::support::log_debug("wilton.wiltoncall." + name,
            "Performing a call, input length: [" + sl::support::to_string(input.length()) + "] ...");
    auto err = wiltoncall(name.c_str(), static_cast<int> (name.length()),
            input.c_str(), static_cast<int> (input.length()),
            std::addressof(out), std::addressof(out_len));
    wilton::support::log_debug("wilton.wiltoncall." + name,
            "Call complete, result: [" + (nullptr != err ? std::string(err) : "") + "]");
    if (nullptr == err) {
        if (nullptr != out) {
            auto deferred = sl::support::defer([out]() STATICLIB_NOEXCEPT {
                wilton_free(out);
            });
            auto jout = string_to_jsval(isolate, out, static_cast<size_t>(out_len));
            args.GetReturnValue().Set(jout);
        } else {
            args.GetReturnValue().Set(v8::Null(isolate));
        }
    } else {
        auto deferred = sl::support::defer([err]() STATICLIB_NOEXCEPT {
            wilton_free(err);
        });
        auto msg = TRACEMSG(err + "\n'wiltoncall' error for name: [" + name + "]");
        throw_js_exception(ctx, msg);
    }
}

} // namespace

class v8_engine::impl : public sl::pimpl::object::impl {
    v8::Isolate* isolate = nullptr;
    v8::Global<v8::Context> ctx_global;

public:

    ~impl() STATICLIB_NOEXCEPT {
        ctx_global.Reset();
        isolate->Dispose();
    }

    impl(sl::io::span<const char> init_code) {
        wilton::support::log_info("wilton.engine.v8.init", "Initializing engine instance ...");
        v8::Isolate::CreateParams create_params;
        create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
        this->isolate = v8::Isolate::New(create_params);
        v8::HandleScope handle_scope(isolate);
        auto global = v8::ObjectTemplate::New(isolate);
        global->Set(string_to_jsval(isolate, "print"), v8::FunctionTemplate::New(isolate, print_func));
        global->Set(string_to_jsval(isolate, "WILTON_load"), v8::FunctionTemplate::New(isolate, load_func));
        global->Set(string_to_jsval(isolate, "WILTON_wiltoncall"), v8::FunctionTemplate::New(isolate, wiltoncall_func));
        this->ctx_global = v8::Global<v8::Context>(isolate, v8::Context::New(isolate, nullptr, global));
        auto ctx = v8::Local<v8::Context>::New(isolate, ctx_global);
        eval_js(ctx, init_code.data(), init_code.size(), "wilton-require.js");
        wilton::support::log_info("wilton.engine.v8.init", "Engine initialization complete");
    }

    support::buffer run_callback_script(v8_engine&, sl::io::span<const char> callback_script_json) {
        wilton::support::log_debug("wilton.engine.v8.run",
                "Running callback script: [" + std::string(callback_script_json.data(), callback_script_json.size()) + "] ...");
        v8::HandleScope handle_scope(isolate);
        auto ctx = v8::Local<v8::Context>::New(isolate, ctx_global);
        v8::Context::Scope ctx_scope(ctx);
        // get function
        auto global = ctx->Global();
        auto name = string_to_jsval(isolate, "WILTON_run");
        auto fun_maybe = global->Get(ctx, name);
        if (fun_maybe.IsEmpty()) {
            throw support::exception(TRACEMSG("Error accessing 'WILTON_run' function: undefined"));
        }
        auto fun_val = fun_maybe.ToLocalChecked();
        if (!fun_val->IsFunction()) {
            throw support::exception(TRACEMSG("Error accessing 'WILTON_run' function: not a function"));
        }
        auto fun = v8::Local<v8::Function>::Cast(fun_val);
        // run
        v8::TryCatch trycatch(isolate);
        v8::Local<v8::Value> args[1];
        args[0] = string_to_jsval(isolate, callback_script_json.data(), callback_script_json.size());
        auto res_maybe = fun->Call(ctx, v8::Null(isolate), 1, args);
        wilton::support::log_debug("wilton.engine.v8.run",
                "Callback run complete, result: [" + sl::support::to_string_bool(!res_maybe.IsEmpty()) + "]");
        if (res_maybe.IsEmpty()) {
            auto stack = format_stack_trace(ctx, trycatch);
            throw support::exception(TRACEMSG(stack));
        }
        auto res = res_maybe.ToLocalChecked();
        if (res->IsString()) {
            auto res_str = jsval_to_string(isolate, res);
            return support::make_string_buffer(res_str);
        }
        return support::make_null_buffer();
    }

    void run_garbage_collector(v8_engine&) {
        isolate->IdleNotificationDeadline(1);
    }

    static void initialize() {
        v8::Platform* platform = v8::platform::CreateDefaultPlatform(/* thread_pool_size */ 2);
        v8::V8::InitializePlatform(platform);
        v8::V8::InitializeICU();
        v8::V8::Initialize();
    }
};

PIMPL_FORWARD_CONSTRUCTOR(v8_engine, (sl::io::span<const char>), (), support::exception)
PIMPL_FORWARD_METHOD(v8_engine, support::buffer, run_callback_script, (sl::io::span<const char>), (), support::exception)
PIMPL_FORWARD_METHOD(v8_engine, void, run_garbage_collector, (), (), support::exception)
PIMPL_FORWARD_METHOD_STATIC(v8_engine, void, initialize, (), (), support::exception)

} // namespace
}

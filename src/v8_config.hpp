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
 * File:   v8_options.hpp
 * Author: alex
 *
 * Created on May 22, 2018, 1:07 PM
 */

#ifndef WILTON_V8_CONFIG_HPP
#define WILTON_V8_CONFIG_HPP

#include <cstdint>

#include "staticlib/json.hpp"
#include "staticlib/support.hpp"
#include "staticlib/utils.hpp"

namespace wilton {
namespace v8eng {

class v8_config {
public:
    uint16_t thread_pool_size = 2;
    uint32_t max_semi_space_size_in_kb = 0;
    uint16_t max_old_space_size = 0;
    uint16_t code_range_size = 0;
    uint16_t max_zone_pool_size = 0;

    v8_config(const sl::json::value& env_json) {
        for (const sl::json::field& fi : env_json.as_object()) {
            auto& name = fi.name();
            if (sl::utils::starts_with(name, "V8_")) {
                if ("V8_thread_pool_size" == name) {
                    this->thread_pool_size = str_as_u16(fi, name);
                } else if ("V8_max_semi_space_size_in_kb" == name) {
                    this->max_semi_space_size_in_kb = str_as_u32(fi, name);
                } else if ("V8_max_old_space_size" == name) {
                    this->max_old_space_size = str_as_u16(fi, name);
                } else if ("V8_code_range_size" == name) {
                    this->code_range_size = str_as_u16(fi, name);
                } else if ("V8_zone_pool_size" == name) {
                    this->max_old_space_size = str_as_u16(fi, name);
                } else {
                    throw support::exception(TRACEMSG("Unknown 'v8_config' field: [" + name + "]"));
                }
            }
        }
    }

    v8_config(const v8_config& other):
    thread_pool_size(other.thread_pool_size),
    max_semi_space_size_in_kb(other.max_semi_space_size_in_kb),
    max_old_space_size(other.max_old_space_size),
    code_range_size(other.code_range_size),
    max_zone_pool_size(other.max_zone_pool_size) { }

    v8_config& operator=(const v8_config&  other) {
        this->thread_pool_size = other.thread_pool_size;
        this->max_semi_space_size_in_kb = other.max_semi_space_size_in_kb;
        this->max_old_space_size = other.max_old_space_size;
        this->code_range_size = other.code_range_size;
        this->max_zone_pool_size = other.max_zone_pool_size;
        return *this;
    }

    sl::json::value to_json() const {
        return {
            { "thread_pool_size", thread_pool_size },
            { "max_semi_space_size_in_kb", max_semi_space_size_in_kb },
            { "max_old_space_size", max_old_space_size },
            { "code_range_size", code_range_size },
            { "max_zone_pool_size", max_zone_pool_size }
        };
    }

private:
    static uint16_t str_as_u16(const sl::json::field& fi, const std::string& name) {
        auto str = fi.as_string_nonempty_or_throw(name);
        try {
            return sl::utils::parse_uint16(str);
        } catch (std::exception& e) {
            throw support::exception(TRACEMSG(e.what() + 
                    "\nError parsing parameter: [" + name + "], value: [" + str + "]"));
        }
    }

    static uint32_t str_as_u32(const sl::json::field& fi, const std::string& name) {
        auto str = fi.as_string_nonempty_or_throw(name);
        try {
            return sl::utils::parse_uint32(str);
        } catch (std::exception& e) {
            throw support::exception(TRACEMSG(e.what() + 
                    "\nError parsing parameter: [" + name + "], value: [" + str + "]"));
        }
    }

};

} // namespace
}

#endif /* WILTON_V8_CONFIG_HPP */


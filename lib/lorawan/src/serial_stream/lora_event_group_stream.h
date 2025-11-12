/*
 * Copyright (c) 2020 Particle Industries, Inc.  All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

// Note: on Mac doing #include "stream.h" includes user/inc/Stream.h instead of system/inc/stream.h
// since Mac doesn't distinguish between case-sensitive and case-insensitive file names. So we have
// to copy system/inc/stream.h
#include "lora_stream.h"
#include <FreeRTOS.h>
#include <event_groups.h>

namespace particle {

class EventGroupBasedStream: public LoraStream {
public:
    virtual EventGroupHandle_t eventGroup() = 0;
};

} // particle

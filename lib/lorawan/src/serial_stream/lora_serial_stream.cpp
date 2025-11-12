/*
 * Copyright (c) 2018 Particle Industries, Inc.  All rights reserved.
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

#include "lora_serial_stream.h"

#include "concurrent_hal.h"
#include "timer_hal.h"
#include "service_debug.h"
#include "system_error.h"

#include "logging.h"

#include "spark_wiring_usartserial.h"

// Stubs for undefined hal_usart_ functions not available in DYNALIB
//
ssize_t hal_usart_write_buffer(hal_usart_interface_t serial, const void* buffer, size_t size, size_t elementSize) {
    // auto usart = CHECK_TRUE_RETURN(Usart::getInstance(serial), SYSTEM_ERROR_NOT_FOUND);
    // CHECK_TRUE(elementSize == sizeof(uint8_t), SYSTEM_ERROR_INVALID_ARGUMENT);
    // return usart->write((const uint8_t*)buffer, size);

    for (size_t x = 0; x < size; x++) {
        Serial1.write(*((const uint8_t*)buffer + x));
    }

    return size;
}
ssize_t hal_usart_read_buffer(hal_usart_interface_t serial, void* buffer, size_t size, size_t elementSize) {
    // auto usart = CHECK_TRUE_RETURN(Usart::getInstance(serial), SYSTEM_ERROR_NOT_FOUND);
    // CHECK_TRUE(elementSize == sizeof(uint8_t), SYSTEM_ERROR_INVALID_ARGUMENT);
    // return usart->read((uint8_t*)buffer, size);

    size_t x = 0;
    while (Serial1.available()) {
        ((uint8_t*)buffer)[x] = Serial1.read();
        x++;
    }

    return x;
}
ssize_t hal_usart_peek_buffer(hal_usart_interface_t serial, void* buffer, size_t size, size_t elementSize) {
    // auto usart = CHECK_TRUE_RETURN(Usart::getInstance(serial), SYSTEM_ERROR_NOT_FOUND);
    // CHECK_TRUE(elementSize == sizeof(uint8_t), SYSTEM_ERROR_INVALID_ARGUMENT);
    // return usart->peek((uint8_t*)buffer, size);

    return 0;
}
int hal_usart_pvt_get_event_group_handle(hal_usart_interface_t serial, EventGroupHandle_t* handle) {
    // auto usart = CHECK_TRUE_RETURN(Usart::getInstance(serial), SYSTEM_ERROR_NOT_FOUND);
    // auto grp = usart->eventGroup();
    // CHECK_TRUE(grp, SYSTEM_ERROR_INVALID_STATE);
    // *handle = grp;
    // return SYSTEM_ERROR_NONE;

    return 0;
}
int hal_usart_pvt_wait_event(hal_usart_interface_t serial, uint32_t events, system_tick_t timeout) {
    // auto usart = CHECK_TRUE_RETURN(Usart::getInstance(serial), SYSTEM_ERROR_NOT_FOUND);
    // return usart->waitEvent(events, timeout);

    return 0;
}

namespace {

const auto SERIAL_STREAM_BUFFER_SIZE_RX = 2048;
const auto SERIAL_STREAM_BUFFER_SIZE_TX = 2048;

} // anonymous

namespace particle {

int InputStream::seek(size_t offset) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

LoraSerialStream::LoraSerialStream(hal_usart_interface_t serial, uint32_t baudrate, uint32_t config,
        size_t rxBufferSize, size_t txBufferSize)
        : serial_(serial),
          config_(config),
          baudrate_(baudrate),
          enabled_(true),
          phyOn_(false) {

    if (!rxBufferSize) {
        rxBufferSize = SERIAL_STREAM_BUFFER_SIZE_RX;
    }
    if (!txBufferSize) {
        txBufferSize = SERIAL_STREAM_BUFFER_SIZE_TX;
    }

    rxBuffer_.reset(new (std::nothrow) char[rxBufferSize]);
    txBuffer_.reset(new (std::nothrow) char[txBufferSize]);
    SPARK_ASSERT(rxBuffer_);
    SPARK_ASSERT(txBuffer_);

    hal_usart_buffer_config_t c = {};
    c.size = sizeof(c);
    c.rx_buffer = (uint8_t*)rxBuffer_.get();
    c.tx_buffer = (uint8_t*)txBuffer_.get();
    c.rx_buffer_size = rxBufferSize;
    c.tx_buffer_size = txBufferSize;
    int ret = hal_usart_init_ex(serial_, &c, nullptr);
    // LOG(INFO, "hal_usart_init_ex: %d", ret);
    hal_usart_begin_config(serial_, baudrate, config, 0);
    phyOn_ = true;
}

LoraSerialStream::~LoraSerialStream() {
    hal_usart_end(serial_);
}

int LoraSerialStream::read(char* data, size_t size) {
    if (!phyOn_ || !enabled_) {
        return SYSTEM_ERROR_INVALID_STATE;
    }
    if (size == 0) {
        return 0;
    }
    // LOG(INFO, "read");
    // LOG_DUMP(TRACE, data, size);
    // LOG_PRINTF(TRACE, "\r\n");
    auto r = hal_usart_read_buffer(serial_, data, size, sizeof(char));
    if (r == SYSTEM_ERROR_NO_MEMORY) {
        return 0;
    }
    return r;
}

int LoraSerialStream::peek(char* data, size_t size) {
    if (!phyOn_ || !enabled_) {
        return SYSTEM_ERROR_INVALID_STATE;
    }
    if (size == 0) {
        return 0;
    }
    // LOG(INFO, "peek");
    // LOG_DUMP(TRACE, data, size);
    // LOG_PRINTF(TRACE, "\r\n");
    auto r = hal_usart_peek_buffer(serial_, data, size, sizeof(char));
    if (r == SYSTEM_ERROR_NO_MEMORY) {
        return 0;
    }
    return r;
}

int LoraSerialStream::skip(size_t size) {
    return read(nullptr, size);
}

int LoraSerialStream::write(const char* data, size_t size) {
    if (!phyOn_ || !enabled_) {
        return SYSTEM_ERROR_INVALID_STATE;
    }
    if (size == 0) {
        return 0;
    }
    // LOG(INFO, "write");
    // LOG_DUMP(TRACE, data, size);
    // LOG_PRINTF(TRACE, "\r\n");
    auto r = hal_usart_write_buffer(serial_, data, size, sizeof(char));
    if (r == SYSTEM_ERROR_NO_MEMORY) {
        return 0;
    }
    return r;
}

int LoraSerialStream::flush() {
    if (!phyOn_ || !enabled_) {
        return SYSTEM_ERROR_INVALID_STATE;
    }
    hal_usart_flush(serial_);
    return 0;
}

int LoraSerialStream::availForRead() {
    if (!phyOn_ || !enabled_) {
        return SYSTEM_ERROR_INVALID_STATE;
    }
    return hal_usart_available(serial_);
}

int LoraSerialStream::availForWrite() {
    if (!phyOn_ || !enabled_) {
        return SYSTEM_ERROR_INVALID_STATE;
    }
    return hal_usart_available_data_for_write(serial_);
}

int LoraSerialStream::waitEvent(unsigned flags, unsigned timeout) {
    if (!phyOn_ || !enabled_) {
        return SYSTEM_ERROR_INVALID_STATE;
    }
    if (!flags) {
        return 0;
    }

    // NOTE: non-Stream events may be passed here

    return hal_usart_pvt_wait_event(serial_, flags, timeout);
}

int LoraSerialStream::setBaudRate(unsigned int baudrate) {
    if (!phyOn_ || !enabled_) {
        return SYSTEM_ERROR_INVALID_STATE;
    }
    hal_usart_end(serial_);
    phyOn_ = false;
    hal_usart_begin_config(serial_, baudrate, config_, 0);
    baudrate_ = baudrate;
    phyOn_ = true;
    return 0;
}

int LoraSerialStream::setConfig(uint32_t config, unsigned int baudrate /* optional */) {
    if (!phyOn_ || !enabled_) {
        return SYSTEM_ERROR_INVALID_STATE;
    }
    hal_usart_end(serial_);
    phyOn_ = false;
    if (baudrate != 0) {
        baudrate_ = baudrate;
    }
    config_ = config;
    hal_usart_begin_config(serial_, baudrate_, config_, 0);
    phyOn_ = true;
    return 0;
}

int LoraSerialStream::on(bool on) {
    if (on) {
        CHECK_FALSE(phyOn_, SYSTEM_ERROR_NONE);
        hal_usart_begin_config(serial_, baudrate_, config_, 0);
        phyOn_ = true;
    } else {
        CHECK_TRUE(phyOn_, SYSTEM_ERROR_NONE);
        hal_usart_end(serial_);
        phyOn_ = false;
    }
    return SYSTEM_ERROR_NONE;
}

EventGroupHandle_t LoraSerialStream::eventGroup() {
    EventGroupHandle_t ev = nullptr;
    hal_usart_pvt_get_event_group_handle(serial_, &ev);
    return ev;
}

} // particle

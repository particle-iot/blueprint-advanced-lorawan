/*
 * Copyright (c) 2024 Particle Industries, Inc.  All rights reserved.
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

#include "Particle.h"

#include "at_parser/at_parser.h"
#include "serial_stream/lora_serial_stream.h"
#include "system_error.h"
#include "cloud_protocol.h"
#include "../../mcp23s17/src/mcp23s17.h"

#include <optional>

#define LORA_TYPE_SERIAL1 (0)
#define LORA_TYPE_SPI (1)
// TODO: LORA_TYPE_I2C

const auto NW_JOIN_INIT = 0;
const auto NW_JOIN_SUCCESS = 1;
const auto NW_JOIN_FAILED = 2;

namespace particle {

class LoRaWANConfig {

public:
    LoRaWANConfig();

    LoRaWANConfig& devEui(const uint8_t* devEui);
    LoRaWANConfig& defaultDevEui();
    const uint8_t* devEui() const;

    LoRaWANConfig& joinEui(const uint8_t* joinEui);
    const uint8_t* joinEui() const;

    LoRaWANConfig& appKey(const uint8_t* appKey);
    const uint8_t* appKey() const;


private:
    uint8_t devEui_[8];
    uint8_t joinEui_[8];
    uint8_t appKey_[16];
};

inline LoRaWANConfig::LoRaWANConfig()
{
}

inline LoRaWANConfig& LoRaWANConfig::devEui(const uint8_t* devEui) {
    memcpy(devEui_, devEui, 8);
    return *this;
}

inline LoRaWANConfig& LoRaWANConfig::defaultDevEui() {
    // default DevEUI is derived from the Wi-Fi MAC
    uint8_t mac[6];
    WiFi.macAddress(mac);

    // copy the bytes from Wi-Fi MAC to DevEUI, with 2 bytes of 0x00 in the middle
    memcpy(devEui_, mac, 3);
    memset(devEui_ + 3, 0x00, 2);
    memcpy(devEui_ + 5, mac + 3, 3);

    return *this;
}

inline const uint8_t* LoRaWANConfig::devEui() const {
    return devEui_;
}


inline LoRaWANConfig& LoRaWANConfig::joinEui(const uint8_t* joinEui) {
    memcpy(joinEui_, joinEui, 8);
    return *this;
}

inline const uint8_t* LoRaWANConfig::joinEui() const {
    return joinEui_;
}

inline LoRaWANConfig& LoRaWANConfig::appKey(const uint8_t* appKey) {
    memcpy(appKey_, appKey, 16);
    return *this;
}

inline const uint8_t* LoRaWANConfig::appKey() const {
    return appKey_;
}

class LoraSerialStream;

class LoRaWAN {

public:

    LoRaWAN( int t, bool isMuon = true);
    ~LoRaWAN();

    int begin(const LoRaWANConfig& conf);
    int status(int& statusVal);
    void destroy(void);
    int initParser(particle::LoraStream* stream);
    int join(void);
    int firmwareVersion(String& version);
    int updateFirmware(bool force = false);
    int tx(const uint8_t* buf, size_t len, int port);
    int disconnect(void);

    int publish(int code, const Variant& data) {
        return proto_.publish(code, data);
    }

    int subscribe(int code, constrained::CloudProtocol::OnEvent onEvent) {
        return proto_.subscribe(code, std::move(onEvent));
    }

    uint16_t available(void) const;
    int process();
    int waitAtResponse(unsigned int timeout, unsigned int period = 1000);
    // int checkParser();
    void parserError(int error);
    AtParser* atParser();
    int getNwJoinStatus(void);

private:

    bool begun_;                        // true if begin() previously called
    bool isMuon_;
    std::pair<uint8_t, uint8_t> intPin_     = {MCP23S17_PORT_A, 7};
    std::pair<uint8_t, uint8_t> bootPin_    = {MCP23S17_PORT_B, 0};
    std::pair<uint8_t, uint8_t> busSelPin_  = {MCP23S17_PORT_B, 1};
    std::pair<uint8_t, uint8_t> resetPin_   = {MCP23S17_PORT_B, 2};
    int type_;                          // LoRa NCP interface type
    uint8_t rxDataBuffer_[512];         // RX receive buffer
    uint16_t rxDataLen_;                // bytes available in read buffer
    volatile bool rxDataReadActive_;    // Prevents reading and writing to the buffer at the same time

    AtParser parser_;
    std::unique_ptr<LoraSerialStream> serial_;
    int parserError_ = 0;
    uint8_t nwJoined = NW_JOIN_INIT;

    LoRaWANConfig conf_;

    constrained::CloudProtocol proto_;

    int publishImpl(int code, const std::optional<Variant>& data = std::nullopt);
};

inline AtParser* LoRaWAN::atParser() {
    return &parser_;
}

inline void LoRaWAN::parserError(int error) {
    Log.error("%d", error);
    parserError_ = error;
}

} // particle
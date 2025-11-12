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

#include "LoRaWAN.h"

#include "logging.h"
LOG_SOURCE_CATEGORY("ncp.client");

#include "at_parser/at_command.h"
#include "at_parser/at_response.h"
#include "serial_stream/lora_serial_stream.h"
#include "check.h"
#include "scope_guard.h"
#include "stream_util.h"
#include "hex_to_bytes.h"
#include "STM32_Flash.h"

#include <str_util.h>

// protobuf test code includes
#include <memory>
#include <cstdint>
#include <pb_encode.h>
#include <cloud/cloud_new.pb.h>

/*
// List of all defined system errors
    NONE                        0
    UNKNOWN                  -100
    BUSY                     -110
    NOT_SUPPORTED            -120
    NOT_ALLOWED              -130
    CANCELLED                -140
    ABORTED                  -150
    TIMEOUT                  -160
    NOT_FOUND                -170
    ALREADY_EXISTS           -180
    TOO_LARGE                -190
    NOT_ENOUGH_DATA          -191
    LIMIT_EXCEEDED           -200
    END_OF_STREAM            -201
    INVALID_STATE            -210
    FLASH_IO                 -219
    IO                       -220
    WOULD_BLOCK              -221
    FILE                     -225
    PATH_TOO_LONG            -226
    NETWORK                  -230
    PROTOCOL                 -240
    INTERNAL                 -250
    NO_MEMORY                -260
    INVALID_ARGUMENT         -270
    BAD_DATA                 -280
    OUT_OF_RANGE             -290
    DEPRECATED               -300
    ...
    AT_NOT_OK               -1200
    AT_RESPONSE_UNEXPECTED  -1210
    ...
*/

#define CHECK_PARSER(_expr) \
        ({ \
            const auto _r = _expr; \
            if (_r < 0) { \
                this->parserError(_r); \
                return _r; \
            } \
            _r; \
        })

#define CHECK_PARSER_OK(_expr) \
        do { \
            const auto _r = _expr; \
            if (_r < 0) { \
                this->parserError(_r); \
                return _r; \
            } \
            if (_r != ::particle::AtResponse::OK) { \
                return SYSTEM_ERROR_AT_NOT_OK; \
            } \
        } while (false)

#define CHECK_PARSER_URC(_expr) \
        ({ \
            const auto _r = _expr; \
            if (_r < 0) { \
                self->parserError(_r); \
                return _r; \
            } \
            _r; \
        })

// Increase Serial1 buffer size
hal_usart_buffer_config_t acquireSerial1Buffer()
{
    const size_t bufferSize = 4095;
    hal_usart_buffer_config_t config = {
        .size = sizeof(hal_usart_buffer_config_t),
        .rx_buffer = new (std::nothrow) uint8_t[bufferSize],
        .rx_buffer_size = bufferSize,
        .tx_buffer = new (std::nothrow) uint8_t[bufferSize],
        .tx_buffer_size = bufferSize
    };

    return config;
}

namespace particle {

using namespace constrained;

namespace {

#define LORA_NCP_DEFAULT_SERIAL_BAUDRATE (9600)
#define LORA_NCP_RX_DATA_READ_TIMEOUT (3000)

} // annonymous

LoRaWAN::LoRaWAN(int t, bool isMuon) :
         begun_(false), isMuon_(isMuon), type_(t), rxDataReadActive_(false)
{
}

LoRaWAN::~LoRaWAN() {
    if (begun_) {
        Mcp23s17::getInstance().setPinMode(resetPin_.first, resetPin_.second, INPUT);
        Mcp23s17::getInstance().setPinMode(bootPin_.first, bootPin_.second, INPUT);
    }
}

int LoRaWAN::waitAtResponse(unsigned int timeout, unsigned int period) {
    const auto t1 = millis();
    for (;;) {
        const int r = parser_.execCommand(period, "ATQ");
        if (r < 0 && r != SYSTEM_ERROR_TIMEOUT) {
            return r;
        }
        if (r == AtResponse::OK) {
            return SYSTEM_ERROR_NONE;
        }
        const auto t2 = millis();
        if (t2 - t1 >= timeout) {
            break;
        }
    }
    return SYSTEM_ERROR_TIMEOUT;
}

int LoRaWAN::getNwJoinStatus(void) {
    return nwJoined;
}

int LoRaWAN::begin(const LoRaWANConfig& conf) {
    begun_ = true;
    conf_ = conf;

    if(isMuon_) {
        Mcp23s17::getInstance().begin();

        Mcp23s17::getInstance().setPinMode(busSelPin_.first, busSelPin_.second, OUTPUT);
        Mcp23s17::getInstance().writePinValue(busSelPin_.first, busSelPin_.second, LOW);
    }

    if (type_ == LORA_TYPE_SERIAL1) {
        Serial1.begin(9600);
        std::unique_ptr<LoraSerialStream> serial(new (std::nothrow) LoraSerialStream(HAL_USART_SERIAL1,
                LORA_NCP_DEFAULT_SERIAL_BAUDRATE, SERIAL_8N1 ));
        CHECK_TRUE(serial, SYSTEM_ERROR_NO_MEMORY);
        CHECK(initParser(serial.get()));
        serial_ = std::move(serial);
        parserError_ = 0;
    } else if (type_ == LORA_TYPE_SPI) {
        // TODO
    }

    // BOOT LOW to boot LORA user application 
    Mcp23s17::getInstance().setPinMode(bootPin_.first, bootPin_.second, OUTPUT);
    Mcp23s17::getInstance().writePinValue(bootPin_.first, bootPin_.second, LOW);

    // Force reset the module (reset logic inverted due to mosfet)
    Mcp23s17::getInstance().setPinMode(resetPin_.first, resetPin_.second, OUTPUT);
    Mcp23s17::getInstance().writePinValue(resetPin_.first, resetPin_.second, HIGH);
    uint32_t s = millis();
    while (millis() - s < 500) {
        process();
    }
    Mcp23s17::getInstance().writePinValue(resetPin_.first, resetPin_.second, LOW);

    // flush KG200Z bootup messages, or else buffer overrun will occur resulting in SOS 15
    s = millis();
    while (millis() - s < 2000) {
        process();
    }

    CHECK(waitAtResponse(10000)); // Check if the module is alive
    // CHECK_PARSER_OK(parser_.execCommand(10000, "ATQ?")); // DEBUG, see all AT commands

    Log.trace("Initializing protocol handler");
    CloudProtocolConfig protoConf;
    protoConf.onSend([this](auto data, auto port, auto /* onAck */) {
        return tx((const uint8_t*)data.data(), data.size(), port);
    });
    int r = proto_.init(protoConf);
    if (r < 0) {
        Log.error("CloudProtocol::init() failed: %d", r);
        return r;
    }

    // Check if the module state was saved to NVM so no additional setup is necessary
    int statusVal;
    CHECK(status(statusVal));
    if (statusVal == 1) {
#if 0 // don't need this yet, if we are not using AT+QCS
        parser_.execCommand(2000, "AT+QRFS"); // Factory reset is the only way we've found to recover
        System.reset();
#endif
        disconnect();
    }

    CHECK_PARSER_OK(parser_.execCommand(2000, "AT+QVL=3")); // default is 2
    CHECK_PARSER_OK(parser_.execCommand(2000, "AT+QBAND=8")); // Band 8 is US
    CHECK_PARSER_OK(parser_.execCommand(2000, "AT+QADR=0")); // disable auto data rate changes
    CHECK_PARSER_OK(parser_.execCommand(2000, "AT+QDR=3")); // set data rate 3 for larger messages

    // Set the JoinEUI (AppEUI is the old name)
    char joinEUICmd[40];
    auto joinEui = conf.joinEui();
    snprintf(joinEUICmd, sizeof(joinEUICmd), "AT+QAPPEUI=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", joinEui[0], joinEui[1], joinEui[2], joinEui[3], joinEui[4], joinEui[5], joinEui[6], joinEui[7]);
    CHECK_PARSER_OK(parser_.execCommand(2000, joinEUICmd));

    // Set the device EUI
    char devEUICmd[40];
    auto devEui = conf.devEui();
    snprintf(devEUICmd, sizeof(devEUICmd), "AT+QDEUI=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", devEui[0], devEui[1], devEui[2], devEui[3], devEui[4], devEui[5], devEui[6], devEui[7]);
    CHECK_PARSER_OK(parser_.execCommand(2000, devEUICmd));

    // Set the AppKey (in LoRaWAN 1.0.4, there is only one key used for both network and application session keys)
    char keyCmd[80];
    auto appKey = conf.appKey();
    snprintf(keyCmd, sizeof(keyCmd), "AT+QAPPKEY=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
            appKey[0], appKey[1], appKey[2], appKey[3], appKey[4], appKey[5], appKey[6], appKey[7],
            appKey[8], appKey[9], appKey[10], appKey[11], appKey[12], appKey[13], appKey[14], appKey[15]);
    CHECK_PARSER_OK(parser_.execCommand(2000, keyCmd));
    snprintf(keyCmd, sizeof(keyCmd), "AT+QNWKKEY=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
        appKey[0], appKey[1], appKey[2], appKey[3], appKey[4], appKey[5], appKey[6], appKey[7],
        appKey[8], appKey[9], appKey[10], appKey[11], appKey[12], appKey[13], appKey[14], appKey[15]);
    CHECK_PARSER_OK(parser_.execCommand(2000, keyCmd));

    return 0;
}

int LoRaWAN::status(int& statusVal) {
    // Check if JOINED already (QSTATUS: 1 joined vs. QSTATUS: 0 not joined)
    auto qstatResp = parser_.sendCommand(1000, "AT+QSTATUS=?");
    char qstatResponse[64] = {};
    auto r = 0;
    if (qstatResp.hasNextLine()) {
        CHECK_PARSER(qstatResp.readLine(qstatResponse, sizeof(qstatResponse)));
        r = ::sscanf(qstatResponse, "QSTATUS: %d", &statusVal);
    }
    CHECK_PARSER_OK(qstatResp.readResult());
    CHECK_TRUE(r == 1, SYSTEM_ERROR_AT_RESPONSE_UNEXPECTED);
    return 0;
}

void LoRaWAN::destroy() {
    if (type_ == LORA_TYPE_SERIAL1) {
        parser_.destroy();
        serial_.reset();
    } else if (type_ == LORA_TYPE_SPI) {
        // TODO
    }
}

int LoRaWAN::initParser(LoraStream* stream) {
    // Initialize AT parser
    auto parserConf = AtParserConfig().stream(stream).commandTerminator(AtCommandTerminator::CRLF);
    parser_.destroy();
    CHECK(parser_.init(std::move(parserConf)));

    // NOTE: These URC handlers need to take care of both the URCs and direct responses to the commands.
    // See CH28408

    CHECK(parser_.addUrcHandler("+QEVT:JOINED", [](AtResponseReader* reader, const char* prefix, void* data) -> int {
        const auto self = (LoRaWAN*)data;
        self->nwJoined = NW_JOIN_SUCCESS;
        return SYSTEM_ERROR_NONE;
    }, this));

    CHECK(parser_.addUrcHandler("+QEVT:JOIN FAILED", [](AtResponseReader* reader, const char* prefix, void* data) -> int {
        const auto self = (LoRaWAN*)data;
        self->nwJoined = NW_JOIN_FAILED;
        return SYSTEM_ERROR_NONE;
    }, this));


    // +QEVT:1:05:0102030405 RX URC
    CHECK(parser_.addUrcHandler("+QEVT:223:", [](AtResponseReader* reader, const char* prefix, void* data) -> int {
        const auto self = (LoRaWAN*)data;
        CString atResponse = reader->readLine();
        CHECK_PARSER_URC(reader->error());

        const char* rxData = (const char*)atResponse + ::strlen("+QEVT:223:"); // skip the prefix
        uint8_t dataLen = 0;
        size_t n = hexToBytes(rxData, (char*) &dataLen, 1);
        CHECK_TRUE(n == 1, SYSTEM_ERROR_AT_RESPONSE_UNEXPECTED);
        rxData += 2;
        CHECK_TRUE(*rxData == ':', SYSTEM_ERROR_AT_RESPONSE_UNEXPECTED);
        rxData += 1;

        auto dataBuf = util::Buffer(dataLen);
        hexToBytes(rxData, dataBuf.data(), dataLen);

        self->proto_.receive(dataBuf, 223);

        return SYSTEM_ERROR_NONE;
    }, this));

    return SYSTEM_ERROR_NONE;
}

int LoRaWAN::join() {
    // TODO: Add a timeout, and reset, retry process
    // TODO: Sometimes URCs stop coming and we only see JOIN/OK happening over and over, detect this and reset/fix this state.
    //       > AT+QJOIN=1
    //       < 542s442:TX on freq 903000000 Hz at DR 4
    //       < OK
    //       < 542s472:MAC txDone
    //       < 547s452:RX_1 on freq 923300000 Hz at DR 13
    //       < 547s493:IRQ_RX_TX_TIMEOUT
    //       < 547s494:MAC rxTimeOut
    //       < 548s466:RX_2 on freq 923300000 Hz at DR 8
    //       < 548s533:IRQ_RX_TX_TIMEOUT
    //       < 548s533:MAC rxTimeOut
    //       < +QEVT:JOIN FAILED
    //       > AT+QJOIN=1
    //       < OK
    //       > AT+QJOIN=1
    //       < OK
    //       > AT+QJOIN=1
    //       < OK
    // Wait for NW_JOIN_SUCCESS URC
    while (nwJoined != NW_JOIN_SUCCESS) {
        auto r = parser_.sendCommand(1000, "AT+QJOIN=1");
        CHECK_PARSER(r.readResult());
        auto s = millis();
        while (millis() - s < 10000 && nwJoined != NW_JOIN_SUCCESS) {
            process();
            Particle.process();
        }

        // Store DevNonce in NVM after each join attempt
        // Not doing this will cause the DevNonce to be reset to 0 on each boot and the join server rejecting joins with "DevNonce is too small"
        CHECK_PARSER_OK(parser_.execCommand(2000, "AT+QCS"));
    }

    CHECK_PARSER_OK(parser_.execCommand(2000, "AT+QCLASS=C")); // must be set after the join process completes

    CHECK_PARSER_OK(parser_.execCommand(2000, "AT+QDR=3")); // set data rate 3 for larger messages

    Log.trace("Connecting to the Cloud");
    int r = proto_.connect();
    if (r < 0) {
        Log.error("CloudProtocol::connect() failed: %d", r);
        return r;
    }

    return 0;
}

int LoRaWAN::disconnect() {
    CHECK_PARSER_OK(parser_.execCommand(1000, "AT+QDISC"));
    CHECK_PARSER_OK(parser_.execCommand(2000, "AT+QCS"));
    nwJoined = NW_JOIN_INIT;

    return 0;
}

int LoRaWAN::firmwareVersion(String& version) {
    auto qverResp = parser_.sendCommand(1000, "AT+QVER=?");
    const char prefix[] = "Version Information: ";
    const size_t prefixLen = sizeof(prefix) - 1;
    char qverResponse[80] = {};
    while (qverResp.hasNextLine()) {
        CHECK_PARSER(qverResp.readLine(qverResponse, sizeof(qverResponse)));
        if (::strncmp(qverResponse, prefix, prefixLen) == 0) {
            version = "";
            const char* versionStr = qverResponse + prefixLen;
            while (*versionStr != '.' && *versionStr != '\0') {
                version.concat(*versionStr);
                versionStr++;
            }
        }
    }
    
    CHECK_PARSER_OK(qverResp.readResult());
    return 0;
}

int LoRaWAN::updateFirmware(bool force) {
    // Update the firmware if a new one is available
    for (auto& asset: System.assetsAvailable()) {
        if (asset.name().startsWith("KG200Z")) {
            String updatedVersion = asset.name().substring(0, asset.name().indexOf('.'));

            if (force) {
                Log.info("Reflashing firmware %s", updatedVersion.c_str());
            } else {
                String version;
                firmwareVersion(version);
                if (version == updatedVersion) {
                    Log.info("Firmware up to date. Current: %s", version.c_str());
                    return 0;
                } else {
                    Log.info("Updating firmware from %s to %s", version.c_str(), updatedVersion.c_str());
                }
            }
            int result = flashStm32Binary(asset, bootPin_, resetPin_, STM32_BOOT_NONINVERTED);

            if (result) {
                // wait a bit before retrying after reboot
                delay(10s);
            }

            // Reinitialize everything after reflashing the module
            System.reset();
        }
    }

    return 0;
}

int LoRaWAN::tx(const uint8_t* buf, size_t len, int port) {
    auto hexBufSize = len * 2 + 1; // Includes term. null
    std::unique_ptr<char[]> hexBuf(new(std::nothrow) char[hexBufSize]);
    if (!hexBuf) {
        return Error::NO_MEMORY;
    }
    toHex(buf, len, hexBuf.get(), hexBufSize);
    CHECK_PARSER_OK(parser_.execCommand(1000, "AT+QSEND=%d:%d:%s", port, 1 /* ack */, hexBuf.get()));
    return 0;
}

uint16_t LoRaWAN::available(void) const {
    return rxDataLen_ > 0;
}

int LoRaWAN::process() {

    parser_.processUrc(); // Ignore errors
    proto_.run();

    // process received data
    // XXX: This was causing SOS 15 after it exited, and process() was called again, and processUrc() was called again.
    // if (!rxDataReadActive_ && rxDataLen_ > 0) {
    //     rxDataReadActive_ = true;

    //     char* hexData = (char*)calloc(2 * rxDataLen_ + 1, sizeof(char));
    //     for (int i = 0; i < rxDataLen_; i++) {
    //         sprintf(hexData + 2 * i * sizeof(char), "%02x", rxDataBuffer_[i]);
    //     }
    //     LOG_PRINTF_C(TRACE, LOG_THIS_CATEGORY(), "%010lu [%s] TRACE: RX[%u] %s\r\n", millis(), LOG_THIS_CATEGORY(), rxDataLen_, hexData);
    //     Log.info("test 1");
    //     free(hexData);
    //     rxDataLen_ = 0;
    //     rxDataReadActive_ = false;
    //     Log.info("test 2");
    // }

    return 0;
}

} // namespace particle



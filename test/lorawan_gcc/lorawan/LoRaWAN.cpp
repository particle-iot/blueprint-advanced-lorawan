#include <cstdlib>
#include <stdexcept>

#include <boost/json.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/beast/core/detail/base64.ipp> // FIXME

#include <spark_wiring_error.h>
#include <spark_wiring_logging.h>

#include "LoRaWAN.h"

using namespace std::literals;

namespace particle {

namespace {

const auto DEFAULT_DEV_EUI = "2bf7f1503230a158";
const auto DEFAULT_HELIUM_APP_ID = "d6016fe8-2c08-41a2-82d6-f92c5608d63f";
const auto DEFAULT_MQTT_PORT = 8883;

std::string toBase64(const util::Buffer& buf) {
    auto size = boost::beast::detail::base64::encoded_size(buf.size());
    std::string s;
    s.resize(size);
    size = boost::beast::detail::base64::encode(s.data(), buf.data(), buf.size());
    s.resize(size);
    return s;
}

util::Buffer fromBase64(std::string_view str) {
    auto size = boost::beast::detail::base64::decoded_size(str.size());
    util::Buffer buf;
    if (buf.resize(size) < 0) {
        throw std::bad_alloc();
    }
    auto r = boost::beast::detail::base64::decode(buf.data(), str.data(), str.size());
    buf.resize(r.first);
    return buf;
}

std::string getEnv(const char* name) {
    auto s = std::getenv(name);
    if (!s) {
        return std::string();
    }
    return s;
}

} // namespace

int Lorawan::init(LorawanConfig conf) {
    try {
        if (inited_) {
            return 0;
        }
        std::string mqttHost = getEnv("MQTT_HOST");
        if (mqttHost.empty()) {
            throw std::runtime_error("MQTT_HOST is not defined");
        }
        int mqttPort = DEFAULT_MQTT_PORT;
        std::string mqttPortStr = getEnv("MQTT_PORT");
        if (!mqttPortStr.empty()) {
            mqttPort = boost::lexical_cast<int>(mqttPortStr);
        }
        std::string mqttUser = getEnv("MQTT_USER");
        if (mqttUser.empty()) {
            throw std::runtime_error("MQTT_USER is not defined");
        }
        std::string mqttPasswd = getEnv("MQTT_PASSWORD");
        if (mqttPasswd.empty()) {
            throw std::runtime_error("MQTT_PASSWORD is not defined");
        }
        devEui_ = getEnv("DEV_EUI");
        if (devEui_.empty()) {
            devEui_ = DEFAULT_DEV_EUI;
        }
        heliumAppId_ = getEnv("HELIUM_APP_ID");
        if (heliumAppId_.empty()) {
            heliumAppId_ = DEFAULT_HELIUM_APP_ID;
        }
        mqtt_.host(mqttHost);
        mqtt_.port(mqttPort);
        mqtt_.user(mqttUser);
        mqtt_.password(mqttPasswd);
        mqtt_.connected([this]() {
            auto topic = "application/"s + heliumAppId_ + "/device/" + devEui_ + "/command/down";
            mqtt_.subscribe(topic, [this](std::string_view /* topic */, std::string_view data) {
                util::Buffer buf;
                int port = 0;
                parseMessage(data, buf, port);
                if (conf_.onRecv_) {
                    conf_.onRecv_(std::move(buf), port);
                }
            });
            if (conf_.onConn_) {
                conf_.onConn_();
            }
        });
        mqtt_.disconnected([this](int error) {
            if (conf_.onDisconn_) {
                conf_.onDisconn_(error);
            }
        });
        conf_ = std::move(conf);
        inited_ = true;
        return 0;
    } catch (const std::exception& e) {
        Log.error("Lorawan::init() failed: %s", e.what());
        return Error::NETWORK;
    }
}

int Lorawan::connect() {
    try {
        if (!inited_) {
            throw std::runtime_error("Not initialized");
        }
        mqtt_.connect();
        return 0;
    } catch (const std::exception& e) {
        Log.error("Lorawan::connect() failed: %s", e.what());
        return Error::NETWORK;
    }
}

int Lorawan::send(util::Buffer data, int port, OnAck /* onAck */) {
    try {
        if (!inited_) {
            throw std::runtime_error("Not initialized");
        }
        auto msg = formatMessage(data, port, (uint16_t)(fcntUp_++));
        auto topic = "application/"s + heliumAppId_ + "/device/" + devEui_ + "/event/up";
        mqtt_.publish(topic, msg);
        return 0;
    } catch (const std::exception& e) {
        Log.error("Lorawan::send() failed: %s", e.what());
        return Error::NETWORK;
    }
}

int Lorawan::run() {
    try {
        if (!inited_) {
            return 0;
        }
        mqtt_.run();
        return 0;
    } catch (const std::exception& e) {
        Log.error("Lorawan::run() failed: %s", e.what());
        return Error::NETWORK;
    }
}

std::string Lorawan::formatMessage(const util::Buffer& payload, int port, int fcnt) {
    boost::json::object devInfo;
    devInfo["devEui"] = devEui_;

    boost::json::object msg;
    msg["deviceInfo"] = devInfo;
    msg["data"] = toBase64(payload);
    msg["fPort"] = port;
    msg["fake_device"] = true;

    return boost::json::serialize(msg);
}

void Lorawan::parseMessage(std::string_view json, util::Buffer& payload, int& port) {
    auto msg = boost::json::parse(std::string(json));
    payload = fromBase64(msg.at("data").as_string());
    port = msg.at("fPort").as_int64();
}

} // namespace particle

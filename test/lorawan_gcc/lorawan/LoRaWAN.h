#pragma once

#include <functional>
#include <string>
#include <cstdint>

#include "mqtt_client.h"
#include "util/buffer.h"

namespace particle {

class LorawanBase {
public:
    typedef std::function<void()> OnConnect;
    typedef std::function<void(int error)> OnDisconnect;
    typedef std::function<void(util::Buffer data, int port)> OnReceive;
    typedef std::function<void(int error)> OnAck;
};

class LorawanConfig {
public:
    LorawanConfig() = default;

    LorawanConfig& onConnect(LorawanBase::OnConnect fn) {
        onConn_ = std::move(fn);
        return *this;
    }

    LorawanConfig& onDisconnect(LorawanBase::OnDisconnect fn) {
        onDisconn_ = std::move(fn);
        return *this;
    }

    LorawanConfig& onReceive(LorawanBase::OnReceive fn) {
        onRecv_ = std::move(fn);
        return *this;
    }

private:
    LorawanBase::OnConnect onConn_;
    LorawanBase::OnDisconnect onDisconn_;
    LorawanBase::OnReceive onRecv_;

    friend class Lorawan;
};

class Lorawan: public LorawanBase {
public:
    Lorawan() :
            fcntUp_(0),
            inited_(false) {
    }

    int init(LorawanConfig conf);

    int connect();
    int send(util::Buffer data, int port, OnAck onAck);

    int run();

private:
    MqttClient mqtt_;
    LorawanConfig conf_;
    std::string devEui_;
    std::string heliumAppId_;
    uint32_t fcntUp_;
    bool inited_;

    std::string formatMessage(const util::Buffer& payload, int port, int fcnt);
    void parseMessage(std::string_view json, util::Buffer& payload, int& port);
};

} // namespace particle

#include <application.h>
#include <check.h>

#include <cloud/cloud_new.pb.h>

#include "protocol/cloud_protocol.h"
#include "util/buffer.h"
#include "util/protobuf.h"
#include "LoRaWAN.h"

SYSTEM_MODE(SEMI_AUTOMATIC)

using namespace particle::constrained;

namespace {

CloudProtocol proto;
Lorawan lorawan;

} // namespace

void setup() {
    LorawanConfig lorawanConf;
    lorawanConf.onConnect([]() {
        proto.connect();
        EventRequest req;
        req.code(1234);
        req.data("Hello world");
        proto.sendEvent(req);
    });
    lorawanConf.onReceive([](util::Buffer buf, int port) {
        proto.receive(std::move(buf), port);
    });
    lorawan.init(lorawanConf);
    
    CloudProtocolConfig protoConf;
    protoConf.onSend([](auto data, int port, auto onAck) {
        return lorawan.send(data, port, onAck);
    });
    proto.init(protoConf);

    lorawan.connect();
}

void loop() {
    int r = proto.run();
    if (r < 0) {
        Log.error("CloudProtocol::run() failed: %d", r);
    }
    r = lorawan.run();
    if (r < 0) {
        Log.error("Lorawan::run() failed: %d", r);
    }
}

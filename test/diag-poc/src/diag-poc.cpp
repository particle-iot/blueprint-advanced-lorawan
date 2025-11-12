/* 
 * Project myProject
 * Author: Your Name
 * Date: 
 * For comprehensive documentation and examples, please visit:
 * https://docs.particle.io/firmware/best-practices/firmware-template/
 */

// Include Particle Device OS APIs
#include "Particle.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <cloud/cloud_new.pb.h>
#include <map>
#include <vector>

SYSTEM_MODE(SEMI_AUTOMATIC);

SYSTEM_THREAD(ENABLED);

SerialLogHandler logHandler(LOG_LEVEL_ALL);

void getDiagValueUint(uint8_t id, uint32_t* res) {
    // Retrieve the source for diag
    const diag_source* DiagSource = nullptr;
    int result = diag_get_source((diag_id)id, &DiagSource, nullptr);
    
    if (result == 0 && DiagSource != nullptr) {
        if (DiagSource->callback != nullptr) {
            // Prepare command data
            diag_source_get_cmd_data cmdData;
            cmdData.size = sizeof(diag_source_get_cmd_data);
            cmdData.reserved = 0;
            cmdData.data = res;
            cmdData.data_size = sizeof(uint32_t);

            // Call the diag source's callback function to get the diag value
            result = DiagSource->callback(DiagSource, DIAG_SOURCE_CMD_GET, &cmdData);
        }
    }

    return;
}

void getDiagValueInt(uint8_t id, int32_t* res) {
    // Retrieve the source for diag
    const diag_source* DiagSource = nullptr;
    int result = diag_get_source((diag_id)id, &DiagSource, nullptr);
    
    if (result == 0 && DiagSource != nullptr) {
        if (DiagSource->callback != nullptr) {
            // Prepare command data
            diag_source_get_cmd_data cmdData;
            cmdData.size = sizeof(diag_source_get_cmd_data);
            cmdData.reserved = 0;
            cmdData.data = res;
            cmdData.data_size = sizeof(int32_t);

            // Call the diag source's callback function to get the diag value
            result = DiagSource->callback(DiagSource, DIAG_SOURCE_CMD_GET, &cmdData);
        }
    }

    return;
}

// Helper classes for working with nanopb's string fields
struct EncodedUint8Bytes {
    const uint8_t* data;
    size_t size;

    explicit EncodedUint8Bytes(pb_callback_t* cb, const uint8_t* data = nullptr, size_t size = 0) :
            data(data),
            size(size) {
        cb->arg = this;
        cb->funcs.encode = [](pb_ostream_t* strm, const pb_field_iter_t* field, void* const* arg) {
            const auto str = (const EncodedUint8Bytes*)*arg;
            if (str->data && str->size > 0 && (!pb_encode_tag_for_field(strm, field) ||
                    !pb_encode_string(strm, (const uint8_t*)str->data, str->size))) {
                return false;
            }
            return true;
        };
    }
};

bool encodeDiagVector(pb_ostream_t *stream, const pb_field_t *field, void* const *arg) {
    Serial.println("callback called");

    // std::vector<std::vector<uint8_t>> values = *(std::vector<std::vector<uint8_t>>*)*arg;
    const auto& values = *((const std::vector<std::vector<uint8_t>>*)*arg);
    LOG_PRINTF(TRACE, "\r\n");
    LOG_PRINTF(TRACE, "values.size(): %d\r\n", values.size());

    uint8_t ids[3] = {6, 10, 37}; // TODO: this should be obtained from arg

    int numDiags = values.size();
    for (int i=0; i<numDiags; i++) {
        LOG_PRINTF(TRACE, "Logging values[%d]\r\n", i);
        LOG_DUMP(TRACE, values[i].data(), values[i].size());
        LOG_PRINTF(TRACE, "\r\n");
        particle_cloud_DiagnosticsResponse_Source s = particle_cloud_DiagnosticsResponse_Source_init_default;
        s.id = ids[i];

        EncodedUint8Bytes myDataBytes(&s.data, (const uint8_t*)values[i].data(), values[i].size());
        // memcpy(s.data.bytes, values[i].data(), values[i].size());
        // s.data.size = values[i].size();

        if (!pb_encode_tag_for_field(stream, field)) {
            Serial.printlnf("Tag Encoding failed");
            return false;
        }

        if (!pb_encode_submessage(stream, particle_cloud_DiagnosticsResponse_Source_fields, &s)) {
            Serial.printlnf("Encoding failed");
            return false;
        }
    }
    return true;
}

bool readIds(pb_istream_t *stream, const pb_field_iter_t *field, void **arg)
{
    auto temp = (std::vector<uint32_t>*)*arg;
    // nanopb suggest using pb_decode_varint to decode uint32
    uint64_t value = 0;
    if (!pb_decode_varint(stream, &value)) {
        return false;
    }
    Serial.printlnf("Value decoded: %d", value);
    temp->push_back(value);
    return true;
}

std::vector<uint8_t> uintToBytes(unsigned int value) {
    std::vector<uint8_t> bytes;

    for (size_t i = 0; i < sizeof(unsigned int); ++i) {
        uint8_t byte = (value >> (i * 8)) & 0xFF;
        bytes.push_back(byte);
    }

    std::reverse(bytes.begin(), bytes.end());
    return bytes;
}

std::vector<uint8_t> intToBytes(int value) {
    std::vector<uint8_t> bytes;

    for (size_t i = 0; i < sizeof(int); ++i) {
        uint8_t byte = (value >> (i * 8)) & 0xFF;
        bytes.push_back(byte);
    }

    std::reverse(bytes.begin(), bytes.end());
    return bytes;
}


std::vector<uint32_t> diagIds;
std::vector<uint32_t>* diagIdsPtr = &diagIds;

void setup() {
    Serial.begin();
    waitUntil(Serial.isConnected);

    Particle.connect();
    waitUntil(Particle.connected);

    uint8_t buffer[256] = {0};
    
    // TODO: This is the incoming bytes of the encoded pb request msg.
    // This msg, when decoded, gives a vector of diagIDs that should be queried
    uint8_t temp[5] = {0x12, 0x03, 0x06, 0x0a, 0x25};
    memcpy(buffer, temp, 5);
    int message_length = 0;
    
    {
        // Decode the incoming pb request which has the list of diag IDs to query
        pb_istream_t stream = pb_istream_from_buffer(buffer, 5);
        
        // Decode incoming DiagnosticsRequest message
        particle_cloud_DiagnosticsRequest request = particle_cloud_DiagnosticsRequest_init_zero;
        // particle_cloud_DReq req = particle_cloud_DReq_init_zero;

        request.has_categories = false;
        request.ids.arg = (void*)diagIdsPtr;
        request.ids.funcs.decode = &readIds;

        bool status = pb_decode(&stream, particle_cloud_DiagnosticsRequest_fields, &request);

        if (!status) {
            Serial.printlnf("Decoding failed");
        }
    }

#if 0
    {
        // Diagnostics are queried here and simply printed
        Serial.printlnf("Diagnostics to be queried: ");
        for (const auto &diagId: *diagIdsPtr) {
            Serial.printlnf("Diag: %lu", diagId);
        }
        Serial.println();
        Serial.printlnf("Value of each diagnostic: ");
        for (const auto &diagId: *diagIdsPtr) {
            const diag_source* DiagSource = nullptr;
            int result = diag_get_source((diag_id)diagId, &DiagSource, nullptr);

            if (DiagSource->type == (diag_type)DIAG_TYPE_INT) {
                int32_t val = 0;
                getDiagValueInt((diag_id)diagId, &val);
                Serial.printlnf("Diag: %lu --- type: %d --- Value: %ld", diagId, DiagSource->type, val);
            }

            if (DiagSource->type == (diag_type)DIAG_TYPE_UINT) {
                uint32_t val = 0;
                getDiagValueUint((diag_id)diagId, &val);
                Serial.printlnf("Diag: %lu --- type: %d --- Value: %u", diagId, DiagSource->type, val);
            }
        }
    }
#endif

    {
        memset(buffer, 0x00, sizeof(buffer));
        // Encode DiagnosticsResponse message
        particle_cloud_DiagnosticsResponse response = particle_cloud_DiagnosticsResponse_init_zero;
        pb_ostream_t ostream = pb_ostream_from_buffer(buffer, sizeof(buffer));
        
        std::vector<std::vector<uint8_t>> diagValues;
        for (const auto &diagId: *diagIdsPtr) {
            std::vector<uint8_t> eachValVec;
            const diag_source* DiagSource = nullptr;
            int result = diag_get_source((diag_id)diagId, &DiagSource, nullptr);

            if (DiagSource->type == (diag_type)DIAG_TYPE_INT) {
                int32_t val = 0;
                getDiagValueInt((diag_id)diagId, &val);
                Serial.printlnf("Diag: %lu --- type: %d --- Value: %ld", diagId, DiagSource->type, val);
                eachValVec = intToBytes(val);
            }

            if (DiagSource->type == (diag_type)DIAG_TYPE_UINT) {
                uint32_t val = 0;
                getDiagValueUint((diag_id)diagId, &val);
                Serial.printlnf("Diag: %lu --- type: %d --- Value: %lu", diagId, DiagSource->type, val);
                eachValVec = uintToBytes(val);
            }
            diagValues.push_back(eachValVec);
        }

        response.sources.arg = (void*)&diagValues;
        response.sources.funcs.encode = encodeDiagVector;
    
        if (!pb_encode(&ostream, particle_cloud_DiagnosticsResponse_fields, &response)) {
            Serial.printlnf("Encoding failed");
        }

        Serial.println("Encoded bytes: ");
        message_length = ostream.bytes_written;
        for (size_t i = 0; i < message_length; ++i) {
            Serial.printf("%02X", buffer[i]);
        }
        Serial.println();
    }
}

void loop() {

}

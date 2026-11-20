#pragma once
#include "ValveLink.h"
namespace valve_api {
inline uint8_t action(const char* text) {
    return !std::strcmp(text,"manual") ? valve_link::manual :
        !std::strcmp(text,"auto") ? valve_link::automatic :
        !std::strcmp(text,"open") ? valve_link::open :
        !std::strcmp(text,"close") ? valve_link::close :
        !std::strcmp(text,"stop") ? valve_link::stop : valve_link::status;
}
inline uint8_t action(const char* text,size_t length) {
    return std::strlen(text)==length ? action(text) : uint8_t(valve_link::status);
}
inline bool request_valid(size_t length,unsigned args,bool has_action) {
    return length<=64 && args==1 && has_action;
}
inline bool fresh(bool seen,uint32_t received,uint32_t now) {
    return seen && uint32_t(now-received)<=3000;
}
}

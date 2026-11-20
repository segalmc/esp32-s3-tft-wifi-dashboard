#include "ValveApiPolicy.h"
#include <cassert>
#include <initializer_list>
int main(){
 for(const char* a : {"auto","manual","open","close","stop"})assert(valve_api::action(a));
 for(const char* a : {"","OPEN","on","open ","reboot"})assert(!valve_api::action(a));
 assert(!valve_api::action("open\0extra",10));
 assert(valve_api::request_valid(11,1,true));assert(!valve_api::request_valid(65,1,true));
 assert(!valve_api::request_valid(11,2,true));assert(!valve_api::request_valid(11,1,false));
 assert(!valve_api::fresh(false,100,100));assert(valve_api::fresh(true,100,3100));
 assert(!valve_api::fresh(true,100,3101));assert(!valve_api::fresh(true,100,99));
 assert(valve_api::fresh(true,UINT32_MAX-100,100));
 valve_link::Frame f;f.kind=2;f.mode=1;f.sequence=1;f.device=1;f.original_boot=2;f.s3_boot=3;
 assert(valve_link::valid(f,2,1,2,3));assert(!valve_link::valid(f,2,1,3,3));
 f.allowed=2;assert(!valve_link::valid(f,2,1,2,3));
}

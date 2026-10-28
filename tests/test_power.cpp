#include "../components/rc_ex3/rc_ex3.cpp"
#include <cassert>
#include <iostream>
using namespace esphome;
using namespace esphome::climate;
class TestClimate : public rc_ex3::RcEx3Climate { public:
 void status(bool on, char mode, char fan='0', int temp=42) {
   std::string b(32, '0'); b.replace(0,5,"RSSL1"); b[13]=on?'1':'0'; b[17]=mode; b[21]=fan;
   char hex[3]; snprintf(hex,3,"%02X",temp); b.replace(30,2,hex);
   parse_status_response(b.c_str(),b.size());
 }
 void packet(const std::string &p) {parse_packet(p.c_str(),p.size());}
 bool pending_power() {return pending_fields_ & PENDING_POWER;}
};
std::string body(const TestClimate &c) {
 assert(c.tx.front()==2 && c.tx.back()==3);
 std::string b(c.tx.begin()+1,c.tx.end()-3);
 unsigned sum=0; for(char x:b) sum+=static_cast<uint8_t>(x);
 char hex[3];snprintf(hex,3,"%02X",sum&255);
 assert(c.tx[c.tx.size()-3]==hex[0] && c.tx[c.tx.size()-2]==hex[1]);
 return b;
}
int main() {
 for(char wire='0';wire<='4';++wire) {
   TestClimate c;c.setup();c.set_use_standard_fan_modes(true);c.status(false,wire,'1',47);
   c.set_power(true);
   assert(body(c)=="RSSL12FF00010102FF03FF04FF05FF06FF0FFF43FF");
   assert(c.mode!=CLIMATE_MODE_OFF && c.target_temperature==23.5f && c.fan_mode==CLIMATE_FAN_MEDIUM);
   auto expected=c.mode;c.status(false,wire);assert(c.mode==expected && c.pending_power());
   c.status(true,wire,'1',47);assert(c.mode==expected && !c.pending_power());
   c.tx.clear();c.set_power(false);assert(body(c)=="RSSL12FF00010002FF03FF04FF05FF06FF0FFF43FF");
   assert(c.mode==CLIMATE_MODE_OFF);
   c.status(true,wire);assert(c.mode==CLIMATE_MODE_OFF);
   c.status(false,wire,'1',47);assert(!c.pending_power());
 }
 // Controller can retain a different mode from the bridge's display cache.
 TestClimate c;c.setup();c.status(false,'4');c.set_power(true);
 assert(body(c).find("02FF03FF")!=std::string::npos);
 c.status(true,'2');assert(c.mode==CLIMATE_MODE_COOL);
 // Idempotent requests are sent even if our cached power already matches.
 c.tx.clear();c.set_power(true);assert(!c.tx.empty());
 // Rejection after settling reconciles to authoritative controller state.
 test_millis+=751;c.status(false,'2');assert(c.mode==CLIMATE_MODE_OFF && !c.pending_power());
 // Fresh boot: don't invent an operating mode, but still send ON.
 TestClimate fresh;fresh.setup();fresh.set_power(true);assert(fresh.mode==CLIMATE_MODE_OFF);
 fresh.status(true,'3');assert(fresh.mode==CLIMATE_MODE_FAN_ONLY);
 // Explicit mode supersedes a pending power request; other fields stay untouched.
 c.tx.clear();c.set_power(false);c.tx.clear();ClimateCall call;call.requested_mode=CLIMATE_MODE_COOL;c.control(call);
 assert(!c.pending_power());assert(body(c)=="RSSL12FF000101020203ff04FF05FF06FF0FFF43FF");
 // Temperature pending confirmation remains independent of power.
 call={};call.requested_temp=24;c.control(call);c.set_power(true);c.status(true,'2','0',42);
 assert(c.target_temperature==24);test_millis+=751;c.status(true,'2','0',48);assert(c.target_temperature==24);
 // Invalid status and ACKs must not report a guessed operating mode.
 c.status(true,'9');assert(c.mode==CLIMATE_MODE_COOL);c.packet("RSSL000000");assert(c.mode==CLIMATE_MODE_COOL);
 std::cout<<"Power payload, checksum, all modes, stale replies, reconciliation and explicit controls passed\n";
}

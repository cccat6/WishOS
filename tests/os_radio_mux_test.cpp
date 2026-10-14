#include "drivers/esp32-c6-radio/RadioMux.h"
#include <esp_ieee802154.h>
#include <cassert>
static unsigned threadCalls=0,zigbeeCalls=0;static uint8_t*expected=nullptr;
extern "C" {
#define IMPL(prefix,count) \
 void prefix##_receive_done(uint8_t*f,esp_ieee802154_frame_info_t*){assert(f==expected);++count;} \
 void prefix##_transmit_done(const uint8_t*f,const uint8_t*,esp_ieee802154_frame_info_t*){assert(f==expected);++count;} \
 void prefix##_transmit_failed(const uint8_t*f,esp_ieee802154_tx_error_t){assert(f==expected);++count;} \
 void prefix##_receive_sfd_done(){++count;} \
 void prefix##_transmit_sfd_done(uint8_t*f){assert(f==expected);++count;} \
 void prefix##_energy_detect_done(int8_t p){assert(p==-80);++count;} \
 void prefix##_cca_done(bool free){assert(free);++count;}
IMPL(notes_thread,threadCalls)
IMPL(notes_zigbee,zigbeeCalls)
void notes_thread_receive_at_done(){++threadCalls;}
esp_err_t notes_thread_enh_ack_generator(uint8_t*f,esp_ieee802154_frame_info_t*,uint8_t*){assert(f==expected);++threadCalls;return 42;}
}
static void callbacks(){uint8_t f=0;expected=&f;esp_ieee802154_frame_info_t info{};
 esp_ieee802154_receive_done(&f,&info);esp_ieee802154_transmit_done(&f,&f,&info);esp_ieee802154_transmit_failed(&f,TEST_ERROR);esp_ieee802154_receive_sfd_done();esp_ieee802154_transmit_sfd_done(&f);esp_ieee802154_energy_detect_done(-80);esp_ieee802154_cca_done(true);esp_ieee802154_receive_at_done();
 int ack=esp_ieee802154_enh_ack_generator(&f,&info,&f);assert(ack==(notes_radio_boot_mode()==1?42:ESP_FAIL));}
int main(){notes_radio_select_at_boot(0);callbacks();assert(threadCalls==0&&zigbeeCalls==0);notes_radio_select_at_boot(1);callbacks();assert(threadCalls==9&&zigbeeCalls==0);notes_radio_select_at_boot(2);callbacks();assert(threadCalls==9&&zigbeeCalls==7);}

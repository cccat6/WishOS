#include "RadioMux.h"
#include <esp_attr.h>
#include <esp_ieee802154.h>
static DRAM_ATTR volatile uint8_t bootMode=0;
extern "C" void notes_radio_select_at_boot(uint8_t mode){bootMode=mode;}
extern "C" uint8_t notes_radio_boot_mode(){return bootMode;}
extern "C" {
#define DECLARE(prefix) \
 void prefix##_receive_done(uint8_t*,esp_ieee802154_frame_info_t*); \
 void prefix##_transmit_done(const uint8_t*,const uint8_t*,esp_ieee802154_frame_info_t*); \
 void prefix##_transmit_failed(const uint8_t*,esp_ieee802154_tx_error_t); \
 void prefix##_receive_sfd_done(void); \
 void prefix##_transmit_sfd_done(uint8_t*); \
 void prefix##_energy_detect_done(int8_t); \
 void prefix##_cca_done(bool);
DECLARE(notes_thread)
DECLARE(notes_zigbee)
void notes_thread_receive_at_done(void);
esp_err_t notes_thread_enh_ack_generator(uint8_t*,esp_ieee802154_frame_info_t*,uint8_t*);
#define ROUTE(name, ...) do{if(bootMode==1)notes_thread_##name(__VA_ARGS__);else if(bootMode==2)notes_zigbee_##name(__VA_ARGS__);}while(0)
void IRAM_ATTR esp_ieee802154_receive_done(uint8_t*f,esp_ieee802154_frame_info_t*i){ROUTE(receive_done,f,i);}
void IRAM_ATTR esp_ieee802154_transmit_done(const uint8_t*f,const uint8_t*a,esp_ieee802154_frame_info_t*i){ROUTE(transmit_done,f,a,i);}
void IRAM_ATTR esp_ieee802154_transmit_failed(const uint8_t*f,esp_ieee802154_tx_error_t e){ROUTE(transmit_failed,f,e);}
void IRAM_ATTR esp_ieee802154_receive_sfd_done(){ROUTE(receive_sfd_done);}
void IRAM_ATTR esp_ieee802154_transmit_sfd_done(uint8_t*f){ROUTE(transmit_sfd_done,f);}
void IRAM_ATTR esp_ieee802154_energy_detect_done(int8_t p){ROUTE(energy_detect_done,p);}
// In the pinned 3.3.11 SDK both implementations of this legacy callback are no-op stubs.
void IRAM_ATTR esp_ieee802154_cca_done(bool clear){ROUTE(cca_done,clear);}
esp_err_t IRAM_ATTR esp_ieee802154_enh_ack_generator(uint8_t*f,esp_ieee802154_frame_info_t*i,uint8_t*a){return bootMode==1?notes_thread_enh_ack_generator(f,i,a):ESP_FAIL;}
void IRAM_ATTR esp_ieee802154_receive_at_done(){if(bootMode==1)notes_thread_receive_at_done();}
}

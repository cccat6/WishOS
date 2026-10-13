#pragma once
#include <stdint.h>
using esp_err_t=int;constexpr int ESP_FAIL=-1;
struct esp_ieee802154_frame_info_t{int value;};
enum esp_ieee802154_tx_error_t{TEST_ERROR};
extern "C" {
void esp_ieee802154_receive_done(uint8_t*,esp_ieee802154_frame_info_t*);
void esp_ieee802154_transmit_done(const uint8_t*,const uint8_t*,esp_ieee802154_frame_info_t*);
void esp_ieee802154_transmit_failed(const uint8_t*,esp_ieee802154_tx_error_t);
void esp_ieee802154_receive_sfd_done();void esp_ieee802154_transmit_sfd_done(uint8_t*);
void esp_ieee802154_energy_detect_done(int8_t);void esp_ieee802154_cca_done(bool);
void esp_ieee802154_receive_at_done();esp_err_t esp_ieee802154_enh_ack_generator(uint8_t*,esp_ieee802154_frame_info_t*,uint8_t*);
}

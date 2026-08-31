#ifndef __NRF_SPECTRUM_H
#define __NRF_SPECTRUM_H
#include "menu/nrf24/nrf_common.h"
#include <RF24.h>

void nrf_spectrum();

String scanChannels(bool web = false);

#endif

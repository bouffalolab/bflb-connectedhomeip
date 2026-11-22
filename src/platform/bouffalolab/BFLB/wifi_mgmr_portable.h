#ifndef __WIFI_MGMR_PORTABLE_H__
#define __WIFI_MGMR_PORTABLE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <async_event.h>

// STA netif accessor. Defined by the Matter platform Wi-Fi commissioning
// driver (NetworkCommissioningDriver.cpp, merged wifi_mgmr_portable.c glue);
// shared with the SDK Matter examples' Wi-Fi backbone (matter_wifi.c), route hooks
// and the platform diagnostics/configuration managers.
struct netif * deviceInterface_getNetif(void);

// SDK-async-event -> ChipDeviceEvent adapter, defined and registered by the
// Matter platform itself (NetworkCommissioningDriver.cpp, BflbWiFiDriver::Init).
void wifi_event_handler(async_input_event_t ev, void * priv);

#ifdef __cplusplus
}
#endif

#endif

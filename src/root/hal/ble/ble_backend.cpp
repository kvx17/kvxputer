#include "root/hal/ble/ble_backend.h"
#include "root/ui/display.h"

bool bleFeatureOrExplain(BleFeature feature, const char *what) {
    BleBackend &b = bleBackend();
    if (b.isSupported() && b.has(feature)) return true;
    String msg = what ? String(what) + "\n" : String();
    msg += b.unsupportedReason(feature);
    displayError(msg, true);
    return false;
}

bool bleNimbleProfileOrExplain(const char *what) {
#if defined(KVX_NO_NIMBLE)
    String msg = what ? String(what) : String("BLE");
    msg += "\nneeds NimBLE-Arduino profiles\n(hosted C6 is GAP scan/adv)";
    displayError(msg, true);
    return false;
#else
    (void)what;
    return true;
#endif
}

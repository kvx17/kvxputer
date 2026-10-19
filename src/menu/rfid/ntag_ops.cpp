#include "ntag_ops.h"
#include "RFIDInterface.h"

bool ntagIsConfigKnown(int totalPages) {
    return totalPages == 45 || totalPages == 135 || totalPages == 231 || totalPages == 41;
}

static void cfgPages(int totalPages, uint8_t &dynLock, uint8_t &cfg0, uint8_t &cfg1, uint8_t &pwd,
                     uint8_t &pack) {
    // NTAG213/215/216: last 5 pages are DynLock, CFG0, CFG1, PWD, PACK
    pack = (uint8_t)(totalPages - 1);
    pwd = (uint8_t)(totalPages - 2);
    cfg1 = (uint8_t)(totalPages - 3);
    cfg0 = (uint8_t)(totalPages - 4);
    dynLock = (uint8_t)(totalPages - 5);
}

int ntagLock(int totalPages, NtagWritePageFn writePage, NtagReadPageFn readPage) {
    if (!ntagIsConfigKnown(totalPages)) return RFIDInterface::NOT_IMPLEMENTED;
    uint8_t page2[4] = {0};
    if (readPage && !readPage(2, page2)) return RFIDInterface::FAILURE;
    // Preserve byte0; set static lock bytes (irreversible).
    page2[1] |= 0xFF;
    page2[2] |= 0xFF;
    if (!writePage(2, page2)) return RFIDInterface::FAILURE;

    uint8_t dynLock, cfg0, cfg1, pwd, pack;
    cfgPages(totalPages, dynLock, cfg0, cfg1, pwd, pack);
    uint8_t dyn[4] = {0xFF, 0xFF, 0xFF, 0x00};
    if (!writePage(dynLock, dyn)) return RFIDInterface::FAILURE;
    return RFIDInterface::SUCCESS;
}

int ntagSetPassword(int totalPages, const uint8_t pwdBytes[4], const uint8_t packBytes[2],
                    NtagWritePageFn writePage) {
    if (!ntagIsConfigKnown(totalPages)) return RFIDInterface::NOT_IMPLEMENTED;
    uint8_t dynLock, cfg0, cfg1, pwd, pack;
    cfgPages(totalPages, dynLock, cfg0, cfg1, pwd, pack);

    uint8_t pwdPage[4] = {pwdBytes[0], pwdBytes[1], pwdBytes[2], pwdBytes[3]};
    uint8_t packPage[4] = {packBytes[0], packBytes[1], 0x00, 0x00};
    uint8_t cfg0Page[4] = {0x00, 0x00, 0x00, 0x04}; // AUTH0 = page 4 (protect user mem)
    uint8_t cfg1Page[4] = {0x00, 0x00, 0x00, 0x00}; // ACCESS defaults

    if (!writePage(pwd, pwdPage)) return RFIDInterface::FAILURE;
    if (!writePage(pack, packPage)) return RFIDInterface::FAILURE;
    if (!writePage(cfg0, cfg0Page)) return RFIDInterface::FAILURE;
    if (!writePage(cfg1, cfg1Page)) return RFIDInterface::FAILURE;
    return RFIDInterface::SUCCESS;
}

int ntagClearPassword(int totalPages, NtagWritePageFn writePage) {
    if (!ntagIsConfigKnown(totalPages)) return RFIDInterface::NOT_IMPLEMENTED;
    uint8_t dynLock, cfg0, cfg1, pwd, pack;
    cfgPages(totalPages, dynLock, cfg0, cfg1, pwd, pack);
    uint8_t zero[4] = {0, 0, 0, 0};
    uint8_t cfg0Open[4] = {0x00, 0x00, 0x00, 0xFF}; // AUTH0 beyond end = no protection
    if (!writePage(cfg0, cfg0Open)) return RFIDInterface::FAILURE;
    if (!writePage(pwd, zero)) return RFIDInterface::FAILURE;
    if (!writePage(pack, zero)) return RFIDInterface::FAILURE;
    return RFIDInterface::SUCCESS;
}

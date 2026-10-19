#ifndef __NTAG_OPS_H__
#define __NTAG_OPS_H__

#include <Arduino.h>
#include <functional>

// Shared NTAG21x config math. writePage(page, 4 bytes) must return true on success.
// totalPages from GET_VERSION / dump (45/135/231 for 213/215/216).

using NtagWritePageFn = std::function<bool(uint8_t page, const uint8_t data[4])>;
using NtagReadPageFn = std::function<bool(uint8_t page, uint8_t data[4])>;

bool ntagIsConfigKnown(int totalPages);
int ntagLock(int totalPages, NtagWritePageFn writePage, NtagReadPageFn readPage);
int ntagSetPassword(int totalPages, const uint8_t pwd[4], const uint8_t pack[2], NtagWritePageFn writePage);
int ntagClearPassword(int totalPages, NtagWritePageFn writePage);

#endif

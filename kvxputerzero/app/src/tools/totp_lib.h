#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tools::totp {

struct Account {
    std::string name;
    std::string secret_b32;
};

bool base32_decode(const std::string& in, std::vector<uint8_t>& out);
uint32_t hotp(const uint8_t* key, size_t key_len, uint64_t counter);
std::string format_code(uint32_t code);
// Returns 6-digit code and seconds remaining in the 30s window.
bool code_at(const Account& account, time_t now, std::string& code, int& remaining, std::string& error);

bool load_accounts(std::vector<Account>& accounts, std::string& error);
bool save_accounts(const std::vector<Account>& accounts, std::string& error);
std::string storage_path();

} // namespace tools::totp

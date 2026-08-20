#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform::ui::settings_store
{

void put_int(const char* ns, const char* key, int value);
void put_bool(const char* ns, const char* key, bool value);
void put_uint(const char* ns, const char* key, uint32_t value);
bool put_string(const char* ns, const char* key, const char* value);
bool put_blob(const char* ns, const char* key, const void* data, std::size_t len);
int get_int(const char* ns, const char* key, int default_value);
bool get_bool(const char* ns, const char* key, bool default_value);
uint32_t get_uint(const char* ns, const char* key, uint32_t default_value);
bool get_string(const char* ns, const char* key, std::string& out);
// Bounded counterpart to get_string.  ESP persistence paths must prefer this
// overload so a settings backup never allocates a string sized by NVS input.
bool get_string_into(const char* ns,
                     const char* key,
                     char* out,
                     std::size_t capacity,
                     std::size_t* out_len);
bool get_blob(const char* ns, const char* key, std::vector<uint8_t>& out);
bool get_blob_into(const char* ns,
                   const char* key,
                   void* out,
                   std::size_t capacity,
                   std::size_t* out_len);
void remove_keys(const char* ns, const char* const* keys, std::size_t key_count);
void clear_namespace(const char* ns);

} // namespace platform::ui::settings_store

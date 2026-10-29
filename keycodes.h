#pragma once

#include <string>
#include <unordered_map>

extern std::unordered_map<std::string, int> keyCodes;

#include <atomic>

// HbbTV keyset of the running application (Application.privateData.keyset)
#define KEYSET_RED          0x001
#define KEYSET_GREEN        0x002
#define KEYSET_YELLOW       0x004
#define KEYSET_BLUE         0x008
#define KEYSET_NAVIGATION   0x010
#define KEYSET_VCR          0x020
#define KEYSET_SCROLL       0x040
#define KEYSET_INFO         0x080
#define KEYSET_NUMERIC      0x100
#define KEYSET_ALPHA        0x200
#define KEYSET_OTHER        0x400

// keys an application gets without calling keyset.setValue()
#define KEYSET_DEFAULT      (KEYSET_RED | KEYSET_GREEN | KEYSET_YELLOW | KEYSET_BLUE | KEYSET_NAVIGATION | KEYSET_VCR | KEYSET_SCROLL | KEYSET_INFO)

extern std::atomic<int> currentKeySet;

// keyset group of a key like "VK_RED", KEYSET_OTHER if unknown
int keySetGroup(const std::string& key);

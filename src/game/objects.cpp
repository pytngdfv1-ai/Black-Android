// Discovery for the cheats page's object markers: LOG OBJECTS writes the game's live objects to the log, grouped by
// class (the first word of an object points to a table of code addresses). The counts of a class are the way to
// tell what each is: a level has so many intel, laptops and weapons to find. Run it in a level, then send the log
#include <algorithm>
#include <cstdio>
#include <map>
#include <vector>

#include "core/log.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

using namespace kernel;

namespace {

// the image: code below, its tables above; objects sit past it
constexpr uint32_t kCodeLow = 0x00010000;
constexpr uint32_t kCodeHigh = 0x00290000;
constexpr uint32_t kTableHigh = 0x00400000;
constexpr uint32_t kHeapLow = 0x00400000;
constexpr uint32_t kHeapHigh = 0x40000000;
constexpr uint32_t kMaxPerClass = 4096;
constexpr uint32_t kShown = 2;     // objects of a class dumped
constexpr uint32_t kBytes = 0x100;  // from each

bool Code(uint32_t va) { return va >= kCodeLow && va < kCodeHigh; }

bool Readable(uint32_t va, uint32_t size) {
    for (uint32_t p = va & ~0xFFFu; p < va + size; p += 0x1000)
        if (!mem::IsCommitted(p)) return false;
    return true;
}

// the table at v holds code addresses: a class's virtual functions
bool Table(uint32_t v) {
    if (v < kCodeLow || v >= kTableHigh || (v & 3) || !Readable(v, 8)) return false;
    return Code(R32(v)) && Code(R32(v + 4));
}

}  // namespace

namespace game {

void DumpObjects() {
    std::map<uint32_t, std::vector<uint32_t>> classes;
    std::map<uint32_t, bool> known;  // a table seen: is it one
    for (uint32_t page = kHeapLow; page < kHeapHigh; page += 0x1000) {
        if (!mem::IsCommitted(page)) continue;
        for (uint32_t at = page; at < page + 0x1000; at += 4) {
            uint32_t v = R32(at);
            if (v < kCodeLow || v >= kTableHigh) continue;
            auto it = known.find(v);
            if (it == known.end()) it = known.emplace(v, Table(v)).first;
            if (!it->second) continue;
            std::vector<uint32_t>& list = classes[v];
            if (list.size() < kMaxPerClass) list.push_back(at);
        }
    }
    std::vector<std::pair<uint32_t, uint32_t>> order;  // count, table
    for (auto& [v, list] : classes) order.push_back({uint32_t(list.size()), v});
    std::sort(order.begin(), order.end());
    LOGI("objects: %zu classes", order.size());
    for (auto& [count, v] : order) {
        if (count > 600) continue;
        LOGI("objects: class %08X count %u", v, count);
        const std::vector<uint32_t>& list = classes[v];
        for (uint32_t i = 0; i < std::min<uint32_t>(kShown, count); i++) {
            uint32_t at = list[i];
            float f[3];
            for (int k = 0; k < 3; k++) f[k] = G<float>(at + 0xA0 + k * 4);
            LOGI("objects:   %08X  +A0 %.2f %.2f %.2f  +C4 %u", at, f[0], f[1], f[2], R32(at + 0xC4));
            for (uint32_t off = 0; off < kBytes; off += 0x20) {
                if (!Readable(at + off, 0x20)) break;
                char line[160];
                int n = 0;
                for (uint32_t w = 0; w < 8; w++) n += std::snprintf(line + n, sizeof(line) - n, " %08X", R32(at + off + w * 4));
                LOGI("objects:     +%02X%s", off, line);
            }
        }
    }
    LOGI("objects: done");
}

}  // namespace game

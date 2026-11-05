// Discovery for the cheats page's object markers: LOG OBJECTS writes the game's live objects to the log, grouped by
// class (the first word of an object points to a table of code addresses). The counts of a class are the way to
// tell what each is: a level has so many intel, laptops and weapons to find. Run it in a level, then send the log
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include "core/log.h"
#include "game/layout.h"
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

// characters are an entity with the position at +A0 and a small kind at +C4 (the player's is 2). Everything in the heap
// with a plausible position near the player and a small kind is listed by kind, to tell what else is one
void DumpObjects() {
    uint32_t me = R32(layout::kWorld) ? R32(layout::kWorld) + layout::kWorldPlayer : 0;
    if (!me) return;
    float p[3];
    for (int k = 0; k < 3; k++) p[k] = G<float>(me + 0xA0 + k * 4);
    LOGR("objects: player %08X at %.2f %.2f %.2f, kind %u", me, p[0], p[1], p[2], R32(me + 0xC4));
    struct Kind {
        uint32_t count = 0;
        std::vector<uint32_t> at;
    };
    std::map<uint32_t, Kind> kinds;
    auto Sane = [](float v) { return std::isnormal(v) && std::fabs(v) < 3000; };
    for (uint32_t page = kHeapLow; page < kHeapHigh; page += 0x1000) {
        if (!mem::IsCommitted(page) || !mem::IsCommitted(page + 0x1000 - 4)) continue;
        for (uint32_t at = page; at + 0xD0 <= page + 0x1000; at += 4) {
            uint32_t kind = R32(at + 0xC4);
            if (kind > 0x40) continue;
            float f[3] = {G<float>(at + 0xA0), G<float>(at + 0xA4), G<float>(at + 0xA8)};
            if (!Sane(f[0]) || !Sane(f[1]) || !Sane(f[2])) continue;
            if (std::fabs(f[0] - p[0]) > 300 || std::fabs(f[1] - p[1]) > 60 || std::fabs(f[2] - p[2]) > 300) continue;
            Kind& k = kinds[kind];
            if (k.count++ < 400) k.at.push_back(at);
        }
    }
    LOGR("objects: %zu kinds", kinds.size());
    for (auto& [kind, k] : kinds) {
        LOGR("objects: kind %u count %u", kind, k.count);
        for (size_t i = 0; i < k.at.size() && i < 8; i++) {
            uint32_t at = k.at[i];
            LOGR("objects:   %08X  pos %.2f %.2f %.2f  w0 %08X", at, G<float>(at + 0xA0), G<float>(at + 0xA4), G<float>(at + 0xA8), R32(at));
        }
        if (!k.at.empty()) {
            uint32_t at = k.at[0];
            for (uint32_t off = 0; off < 0x100; off += 0x20) {
                char line[160];
                int n = 0;
                for (uint32_t w = 0; w < 8; w++) n += std::snprintf(line + n, sizeof(line) - n, " %08X", R32(at + off + w * 4));
                LOGR("objects:     +%02X%s", off, line);
            }
        }
    }
    LOGR("objects: done");
}

}  // namespace game

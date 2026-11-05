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

// the entity base holds the position at +A0 and the kind at +C4 (characters do): every class whose objects have one
// near the player is a candidate for intel, laptops and weapons, with its kinds by count
void DumpObjects() {
    uint32_t me = R32(layout::kWorld) ? R32(layout::kWorld) + layout::kWorldPlayer : 0;
    if (!me) return;
    float p[3];
    for (int k = 0; k < 3; k++) p[k] = G<float>(me + 0xA0 + k * 4);
    LOGR("objects: player %.2f %.2f %.2f", p[0], p[1], p[2]);
    struct Kind {
        uint32_t count = 0;
        std::vector<uint32_t> at;
    };
    struct Class {
        uint32_t total = 0, near = 0;
        std::map<uint32_t, Kind> kinds;
    };
    std::map<uint32_t, Class> classes;
    std::map<uint32_t, bool> known;
    for (uint32_t page = kHeapLow; page < kHeapHigh; page += 0x1000) {
        if (!mem::IsCommitted(page)) continue;
        for (uint32_t at = page; at < page + 0x1000; at += 4) {
            uint32_t v = R32(at);
            if (v < kCodeLow || v >= kTableHigh) continue;
            auto it = known.find(v);
            if (it == known.end()) it = known.emplace(v, Table(v)).first;
            if (!it->second) continue;
            Class& c = classes[v];
            c.total++;
            if (!Readable(at + 0xA0, 0xD0)) continue;
            float f[3];
            for (int k = 0; k < 3; k++) f[k] = G<float>(at + 0xA0 + k * 4);
            if (!(std::fabs(f[0] - p[0]) < 400 && std::fabs(f[1] - p[1]) < 120 && std::fabs(f[2] - p[2]) < 400)) continue;
            if (f[0] == 0 && f[1] == 0 && f[2] == 0) continue;
            c.near++;
            Kind& k = c.kinds[R32(at + 0xC4)];
            if (k.count++ < 3) k.at.push_back(at);
        }
    }
    LOGR("objects: %zu classes", classes.size());
    for (auto& [v, c] : classes) {
        if (!c.near) continue;
        LOGR("objects: class %08X total %u near %u kinds %zu", v, c.total, c.near, c.kinds.size());
        int shown = 0;
        for (auto& [kind, k] : c.kinds) {
            LOGR("objects:  kind %08X count %u", kind, k.count);
            if (shown++ >= 10) break;
            uint32_t at = k.at[0];
            LOGR("objects:   %08X  pos %.2f %.2f %.2f  d %.1f", at, G<float>(at + 0xA0), G<float>(at + 0xA4), G<float>(at + 0xA8),
                 std::sqrt(std::pow(G<float>(at + 0xA0) - p[0], 2) + std::pow(G<float>(at + 0xA8) - p[2], 2)));
            for (uint32_t off = 0; off < 0x40; off += 0x20) {
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

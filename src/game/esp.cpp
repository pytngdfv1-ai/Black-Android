// The cheats page's ESP LINES: each living enemy gets a box round it and a line to it from the bottom of the screen,
// drawn over the finished frame with the view the scene was drawn with. AIMBOT: while the player aims in the view
// turns to the body of the living enemy nearest the crosshair
#include "core/log.h"
#include <chrono>
#include <cmath>
#include <cstring>

#include "common/options.h"
#include "game/layout.h"
#include "xdk/hle.h"
#include "xdk/layout.h"

using namespace kernel;
using namespace layout;

namespace {

// character: kind 1 an enemy (the game's aim assist takes only those), its position the one its update makes a
// velocity of. The world is Y up: that update zeroes Y for the speed along the ground
constexpr uint32_t kCharKind = 0xC4;
constexpr uint32_t kCharPosition = 0xA0;
constexpr uint32_t kCharDying = 0x384;
constexpr uint32_t kEnemyKind = 1;

// the player's controller: yaw positive left, pitch positive down, in degrees (mouse.cpp)
constexpr uint32_t kControllerYaw = 0x18;
constexpr uint32_t kControllerPitch = 0x1C;
// the player's zoom of the aim, above 1 while it aims in (hooks.cpp's ZoomStep)
constexpr uint32_t kPlayerZoom = 0x2A4;

constexpr float kHeight = 1.8f;      // world units are metres: the near plane is 0.08
constexpr float kBoxWidth = 0.45f;   // of the box's height
constexpr float kNear = 0.08f;       // clip w nearer than this is behind the camera
constexpr float kThickness = 1.5f;   // lines of a 480 frame
constexpr float kChest = 1.2f;       // above the feet, where the aimbot aims: the body, a bigger hit than the head
constexpr float kCone = 30.0f;       // degrees from the crosshair an enemy is taken in
constexpr float kPull = 1.0f;        // of the turn to the target, a frame
constexpr float kDegrees = 180.0f / 3.14159265f;
constexpr float kColour[4] = {1.0f, 0.125f, 0.125f, 1.0f};  // rgba

// an enemy is drawn while its update ran this recently: none once the game pauses, no flicker between ticks
constexpr auto kSeen = std::chrono::milliseconds(250);
constexpr size_t kMaxEnemies = 64;

struct Seen {
    uint32_t who;
    std::chrono::steady_clock::time_point at;
};
Seen g_seen[kMaxEnemies];
size_t g_count;

// the scene's view and projection, row vectors: clip = position * view * projection
float g_view[16], g_projection[16];
bool g_camera;
uint32_t g_camera_frame;  // counts the scene's draws: the aimbot turns once for each view it reads

// the 2D layer's untextured triangles (eax triangles, ecx their points; float x, y of the points' offset, then a
// float rgba of each point, 16 aligned): in its batch, drawn with its shader and states at the next flush
constexpr uint32_t kTriangles = 0xD5A00;
struct Vertex {
    float x, y;
};
constexpr size_t kSegments = 5;  // the line and the box's four sides
constexpr size_t kMaxVertices = kMaxEnemies * kSegments * 6;

void Remember(uint32_t who) {
    auto now = std::chrono::steady_clock::now();
    for (size_t i = 0; i < g_count; i++)
        if (g_seen[i].who == who) {
            g_seen[i].at = now;
            return;
        }
    if (g_count < kMaxEnemies) {
        g_seen[g_count++] = {who, now};
        return;
    }
    // full with the ESP off, which keeps the list: the one seen longest ago goes
    Seen* oldest = g_seen;
    for (Seen& s : g_seen)
        if (s.at < oldest->at) oldest = &s;
    *oldest = {who, now};
}

// clip space; false behind the camera
bool Clip(const float p[3], float clip[4]) {
    float eye[4];
    for (int c = 0; c < 4; c++)
        eye[c] = p[0] * g_view[c] + p[1] * g_view[4 + c] + p[2] * g_view[8 + c] + g_view[12 + c];
    for (int c = 0; c < 4; c++)
        clip[c] = eye[0] * g_projection[c] + eye[1] * g_projection[4 + c] + eye[2] * g_projection[8 + c] +
                  eye[3] * g_projection[12 + c];
    return clip[3] > kNear;
}

// screen position in viewport pixels; false behind the camera
bool Project(const float p[3], float vx, float vy, float vw, float vh, float out[2]) {
    float clip[4];
    if (!Clip(p, clip)) return false;
    out[0] = vx + (clip[0] / clip[3] + 1) * 0.5f * vw;
    out[1] = vy + (1 - clip[1] / clip[3]) * 0.5f * vh;
    return true;
}

// a segment as a quad of two triangles, its width across it
Vertex* Segment(Vertex* v, float x0, float y0, float x1, float y1, float width) {
    float dx = x1 - x0, dy = y1 - y0;
    float length = std::sqrt(dx * dx + dy * dy);
    if (!(length > 0)) return v;
    float nx = -dy / length * width * 0.5f, ny = dx / length * width * 0.5f;
    const float corner[4][2] = {{x0 + nx, y0 + ny}, {x0 - nx, y0 - ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}};
    for (int i : {0, 1, 2, 2, 1, 3}) *v++ = {corner[i][0], corner[i][1]};
    return v;
}

// BLOOD: Black has none, so a hit on an enemy splatters: a splat on the screen at the hit, flying streaks and round
// drops thrown from it that fall under gravity (projected with the scene's view), and drips. Three styles, picked by the
// hit: a burst with spikes, a directional spray, a big blob with drips. Drawn in the 2D layer like the ESP
constexpr size_t kSpurts = 8;                    // hits shown at once
constexpr size_t kBloodVertices = kSpurts * 1800;
constexpr float kBloodLife = 1.6f;               // seconds a splat stays
constexpr float kDropLife = 0.9f;                // seconds the drops fly
constexpr float kBloodThickness = 1.5f;          // of a 480 frame
constexpr float kBloodColours[4][4] = {{0.30f, 0.0f, 0.0f, 1.0f}, {0.20f, 0.0f, 0.0f, 1.0f},
                                       {0.45f, 0.01f, 0.01f, 1.0f}, {0.12f, 0.0f, 0.0f, 1.0f}};
constexpr float kPi2 = 6.2831853f;

struct Spurt {
    bool live = false;
    float at[3];
    float away[2];  // along the ground, from the player to the enemy
    uint32_t seed;
    std::chrono::steady_clock::time_point when;
};
Spurt g_spurts[kSpurts];
size_t g_next_spurt;

float Random(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return float(seed >> 8) / float(1 << 24);
}

}  // namespace

namespace game {
void DumpObjects();  // objects.cpp

// the device's view and projection as the scene draws them (D3DDevice_SetTransform, view 0 and projection 1)
void EspCamera() {
    uint32_t transforms = SYM(D3D_Device) + XDK_CDevice_Transforms;
    std::memcpy(g_view, HostPtr(transforms), sizeof(g_view));
    std::memcpy(g_projection, HostPtr(transforms + 64), sizeof(g_projection));
    g_camera = true;
    g_camera_frame++;
}

// esi the player's controller in its look update, before the game clamps the pitch and wraps the yaw; while the
// player aims in. The turn is read off the projection, the angles of the target from the middle of the view
void Aimbot(X86Context& ctx) {
    static uint32_t turned;
    uint32_t c = ctx.esi;
    uint32_t me = R32(kWorld) ? R32(kWorld) + kWorldPlayer : 0;
    if (!Opt().aimbot || !g_camera || !me || turned == g_camera_frame || !(G<float>(me + kPlayerZoom) > 1.01f)) return;
    turned = g_camera_frame;
    auto now = std::chrono::steady_clock::now();
    float best = kCone, yaw = 0, pitch = 0;
    for (size_t i = 0; i < g_count; i++) {
        uint32_t who = g_seen[i].who;
        if (now - g_seen[i].at > kSeen || R32(who + kCharKind) != kEnemyKind || R32(who + kCharDying)) continue;
        const float* at = &G<float>(who + kCharPosition);
        float chest[3] = {at[0], at[1] + kChest, at[2]}, clip[4];
        if (!Clip(chest, clip)) continue;
        float y = -std::atan(clip[0] / clip[3] / g_projection[0]) * kDegrees;
        float p = -std::atan(clip[1] / clip[3] / g_projection[5]) * kDegrees;
        float off = std::sqrt(y * y + p * p);
        if (off < best) best = off, yaw = y, pitch = p;
    }
    G<float>(c + kControllerYaw) += yaw * kPull;
    G<float>(c + kControllerPitch) += pitch * kPull;
}

// D3DVIEWPORT8: x, y, width, height. Called between the 2D layer's begin and end
static void DrawLines(X86Context& ctx, float unit) {
    uint32_t viewport = SYM(D3D_Device) + XDK_CDevice_Viewport;
    float vx = float(R32(viewport)), vy = float(R32(viewport + 4));
    float vw = float(R32(viewport + 8)), vh = float(R32(viewport + 12));
    static const uint32_t buffer = mem::AllocSystem(kMaxVertices * sizeof(Vertex), 16);
    static const uint32_t offset = mem::AllocSystem(8, 16);
    static const uint32_t colours = [] {
        uint32_t c = mem::AllocSystem(kMaxVertices * sizeof(kColour), 16);
        for (size_t i = 0; i < kMaxVertices; i++) std::memcpy(HostPtr(c + i * sizeof(kColour)), kColour, sizeof(kColour));
        return c;
    }();
    Vertex* first = static_cast<Vertex*>(HostPtr(buffer));
    Vertex* v = first;
    float width = kThickness * unit;
    auto now = std::chrono::steady_clock::now();
    size_t kept = 0;
    for (size_t i = 0; i < g_count; i++) {
        uint32_t who = g_seen[i].who;
        if (now - g_seen[i].at > kSeen || R32(who + kCharKind) != kEnemyKind || R32(who + kCharDying)) continue;
        g_seen[kept++] = g_seen[i];
        const float* at = &G<float>(who + kCharPosition);
        float feet[3] = {at[0], at[1], at[2]};
        float head[3] = {at[0], at[1] + kHeight, at[2]};
        float f[2], h[2];
        if (!Project(feet, vx, vy, vw, vh, f) || !Project(head, vx, vy, vw, vh, h)) continue;
        float half = (f[1] - h[1]) * kBoxWidth * 0.5f;
        float left = f[0] - half, right = f[0] + half;
        v = Segment(v, vx + vw * 0.5f, vy + vh, f[0], f[1], width);
        v = Segment(v, left, h[1], right, h[1], width);
        v = Segment(v, left, f[1], right, f[1], width);
        v = Segment(v, left, h[1], left, f[1], width);
        v = Segment(v, right, h[1], right, f[1], width);
    }
    g_count = kept;
    if (v == first) return;
    W32(offset, 0);
    W32(offset + 4, 0);
    uint32_t eax = ctx.eax, ecx = ctx.ecx;
    ctx.eax = uint32_t(v - first) / 3;
    ctx.ecx = buffer;
    CallGuest(ctx, kTriangles, {offset, colours});
    ctx.eax = eax;
    ctx.ecx = ecx;
}

// a hit on the enemy: the blood starts from its chest
void BloodSpurt(uint32_t victim) {
    if (!Opt().blood) return;
    const float* at = &G<float>(victim + kCharPosition);
    Spurt& s = g_spurts[g_next_spurt++ % kSpurts];
    s.at[0] = at[0];
    s.at[1] = at[1] + kChest;
    s.at[2] = at[2];
    s.away[0] = s.away[1] = 0;
    if (uint32_t me = R32(kWorld) ? R32(kWorld) + kWorldPlayer : 0) {
        const float* from = &G<float>(me + kCharPosition);
        float dx = at[0] - from[0], dz = at[2] - from[2];
        float length = std::sqrt(dx * dx + dz * dz);
        if (length > 0.01f) s.away[0] = dx / length, s.away[1] = dz / length;
    }
    s.when = std::chrono::steady_clock::now();
    s.seed = uint32_t(s.when.time_since_epoch().count()) ^ victim;
    s.live = true;
}

struct BloodOut {
    Vertex* v;
    Vertex* end;
    float* colour;  // a colour a vertex, in step with v
    Vertex* first;
};

static void BloodTri(BloodOut& o, float x0, float y0, float x1, float y1, float x2, float y2, const float* c) {
    if (o.v + 3 > o.end) return;
    const float p[3][2] = {{x0, y0}, {x1, y1}, {x2, y2}};
    for (int i = 0; i < 3; i++) {
        *o.v = {p[i][0], p[i][1]};
        std::memcpy(o.colour + (o.v - o.first) * 4, c, 4 * sizeof(float));
        o.v++;
    }
}

static void BloodQuad(BloodOut& o, float x0, float y0, float x1, float y1, float width, const float* c) {
    float dx = x1 - x0, dy = y1 - y0, length = std::sqrt(dx * dx + dy * dy);
    if (!(length > 0)) return;
    float nx = -dy / length * width * 0.5f, ny = dx / length * width * 0.5f;
    BloodTri(o, x0 + nx, y0 + ny, x0 - nx, y0 - ny, x1 + nx, y1 + ny, c);
    BloodTri(o, x1 + nx, y1 + ny, x0 - nx, y0 - ny, x1 - nx, y1 - ny, c);
}

// an irregular blob round (cx, cy); spiky pushes some points far out
static void BloodBlob(BloodOut& o, float cx, float cy, float r, int points, float spiky, uint32_t seed, const float* c) {
    float px = 0, py = 0, fx = 0, fy = 0;
    float turn = Random(seed) * kPi2;
    for (int i = 0; i <= points; i++) {
        float x, y;
        if (i == points) {
            x = fx, y = fy;
        } else {
            float angle = turn + kPi2 * (float(i) + Random(seed) * 0.5f) / float(points);
            float radius = r * (0.55f + Random(seed) * 0.45f);
            if (Random(seed) < spiky) radius *= 1.6f + Random(seed) * 1.8f;
            x = cx + std::cos(angle) * radius, y = cy + std::sin(angle) * radius;
            if (i == 0) fx = x, fy = y;
        }
        if (i) BloodTri(o, cx, cy, px, py, x, y, c);
        px = x, py = y;
    }
}

static void DrawBlood(X86Context& ctx, float unit) {
    uint32_t viewport = SYM(D3D_Device) + XDK_CDevice_Viewport;
    float vx = float(R32(viewport)), vy = float(R32(viewport + 4));
    float vw = float(R32(viewport + 8)), vh = float(R32(viewport + 12));
    static const uint32_t buffer = mem::AllocSystem(kBloodVertices * sizeof(Vertex), 16);
    static const uint32_t offset = mem::AllocSystem(8, 16);
    static const uint32_t colours = mem::AllocSystem(kBloodVertices * 4 * sizeof(float), 16);
    BloodOut o;
    o.first = o.v = static_cast<Vertex*>(HostPtr(buffer));
    o.end = o.first + kBloodVertices;
    o.colour = static_cast<float*>(HostPtr(colours));
    auto now = std::chrono::steady_clock::now();
    for (Spurt& s : g_spurts) {
        if (!s.live) continue;
        float age = std::chrono::duration<float>(now - s.when).count();
        if (age > kBloodLife) {
            s.live = false;
            continue;
        }
        uint32_t seed = s.seed;
        int style = std::min(int(Random(seed) * 3), 2);  // 0 burst, 1 spray, 2 blob
        const float* main = kBloodColours[std::min(int(Random(seed) * 3), 2)];
        // each part has its own stream, so what is on screen never shifts the rest
        uint32_t splat = seed ^ 0x9E3779B9u, thrown = seed ^ 0x85EBCA6Bu, spikes = seed ^ 0xC2B2AE35u;
        // the splat's size: grows fast, holds, shrinks away at the end
        float grow = std::min(age / 0.10f, 1.0f);
        float fade = std::min((kBloodLife - age) / 0.5f, 1.0f);
        float scale = grow * fade;
        float centre[2], up[2];
        float above[3] = {s.at[0], s.at[1] + 0.5f, s.at[2]};
        bool seen = Project(s.at, vx, vy, vw, vh, centre) && Project(above, vx, vy, vw, vh, up);
        float ppm = seen ? std::sqrt((centre[0] - up[0]) * (centre[0] - up[0]) + (centre[1] - up[1]) * (centre[1] - up[1])) / 0.5f : 0;
        // per style: splat radius in metres, spike share, satellites, streaks, round drops, drips
        static const float kRadius[3] = {0.28f, 0.18f, 0.42f};
        static const float kSpiky[3] = {0.55f, 0.25f, 0.15f};
        static const int kSatellites[3] = {10, 6, 14};
        static const int kStreaks[3] = {44, 80, 20};
        static const int kRound[3] = {28, 36, 20};
        static const int kDrips[3] = {0, 0, 8};
        // the splat, its satellites and drips
        if (seen && ppm > 0 && scale > 0) {
            float r = std::min(ppm * kRadius[style], vh * 0.08f) * scale;  // capped: close up it would fill the screen
            BloodBlob(o, centre[0], centre[1], r, 16, kSpiky[style], splat, main);
            for (int i = 0; i < kSatellites[style]; i++) {
                float angle = Random(splat) * kPi2, dist = r * (1.3f + Random(splat) * 1.6f), size = r * (0.12f + Random(splat) * 0.25f);
                BloodBlob(o, centre[0] + std::cos(angle) * dist, centre[1] + std::sin(angle) * dist * 0.8f, size, 8, 0.1f, splat,
                          kBloodColours[std::min(int(Random(splat) * 3), 2)]);
            }
            for (int i = 0; i < kDrips[style]; i++) {
                float across = (Random(splat) - 0.5f) * r * 1.4f, length = r * (0.8f + Random(splat) * 1.6f);
                float run = std::min(age / 0.9f, 1.0f) * length;
                float width = r * (0.10f + Random(splat) * 0.08f);
                BloodQuad(o, centre[0] + across, centre[1], centre[0] + across, centre[1] + run, width, main);
                BloodBlob(o, centre[0] + across, centre[1] + run, width * 0.9f, 7, 0.0f, splat, main);
            }
        }
        // streaks and round drops thrown from the hit
        float dropAge = std::min(age, kDropLife);
        bool flying = age < kDropLife;
        float thrust = style == 1 ? 3.0f : 1.5f;
        for (int d = 0; d < kStreaks[style] + kRound[style]; d++) {
            bool streak = d < kStreaks[style];
            float angle = Random(thrown) * kPi2;
            float speed = 0.5f + Random(thrown) * (style == 1 ? 2.0f : 1.5f);
            float rise = 0.3f + Random(thrown) * 1.8f;
            float size = Random(thrown);
            const float* col = kBloodColours[std::min(int(Random(thrown) * 4), 3)];
            if (!flying) continue;
            float vel[3] = {std::cos(angle) * speed + s.away[0] * thrust, rise, std::sin(angle) * speed + s.away[1] * thrust};
            // never towards the player: that is what jumps at the screen. Mirror it to the far side
            float toward = vel[0] * s.away[0] + vel[2] * s.away[1];
            if (toward < 0) vel[0] -= 2 * toward * s.away[0], vel[2] -= 2 * toward * s.away[1];
            auto place = [&](float t, float out[3]) {
                out[0] = s.at[0] + vel[0] * t;
                out[1] = s.at[1] + vel[1] * t - 4.9f * t * t;
                out[2] = s.at[2] + vel[2] * t;
            };
            float head[3], a[2], b[2];
            place(dropAge, head);
            if (head[1] < s.at[1] - 1.2f) continue;  // it hit the ground
            if (!Project(head, vx, vy, vw, vh, b)) continue;
            if (streak) {
                float tail[3];
                place(std::max(dropAge - 0.06f, 0.0f), tail);
                if (!Project(tail, vx, vy, vw, vh, a)) continue;
                BloodQuad(o, a[0], a[1], b[0], b[1], kBloodThickness * unit * (0.5f + size * 1.5f), col);
            } else if (ppm > 0) {
                BloodBlob(o, b[0], b[1], ppm * (0.012f + size * 0.035f), 6, 0.0f, thrown + d, col);
            }
        }
        if (style == 0 && seen && ppm > 0 && scale > 0) {  // spikes of the burst, thin and long
            float r = ppm * kRadius[0] * scale;
            for (int i = 0; i < 14; i++) {
                float angle = Random(spikes) * kPi2, length = r * (1.4f + Random(spikes) * 2.6f), width = r * 0.07f;
                float tx = centre[0] + std::cos(angle) * length, ty = centre[1] + std::sin(angle) * length;
                float bx = centre[0] + std::cos(angle) * r * 0.5f, by = centre[1] + std::sin(angle) * r * 0.5f;
                BloodTri(o, bx - std::sin(angle) * width, by + std::cos(angle) * width, bx + std::sin(angle) * width,
                         by - std::cos(angle) * width, tx, ty, main);
            }
        }
    }
    if (o.v == o.first) return;
    W32(offset, 0);
    W32(offset + 4, 0);
    uint32_t eax = ctx.eax, ecx = ctx.ecx;
    ctx.eax = uint32_t(o.v - o.first) / 3;
    ctx.ecx = buffer;
    CallGuest(ctx, kTriangles, {offset, colours});
    ctx.eax = eax;
    ctx.ecx = ecx;
}

// the ESP's lines and the blood, between the 2D layer's begin and end
void DrawEsp(X86Context& ctx, float unit) {
    if (!g_camera || !R32(kWorld)) return;
    if (Opt().esp) DrawLines(ctx, unit);
    if (Opt().blood) DrawBlood(ctx, unit);
}

}  // namespace game

// The characters' update, a frame (ecx the character, its class's own; the player's is 0x1E5FA0): the enemies
// that ran it are the ones drawn
X86_EXTERN_FUNC(__imp__sub_001DFD90);
X86_FUNC(sub_001DFD90) {
    uint32_t who = ctx.ecx;
    // discovery: a level's objects go to the log once, 15 s after its first enemy runs (they are placed by then)
    static std::chrono::steady_clock::time_point since;
    static int stage = 0;
    if (!R32(kWorld)) stage = 0;
    else if (stage == 0 && who && R32(who + kCharKind) == kEnemyKind) stage = 1, since = std::chrono::steady_clock::now(), LOGR("objects: enemy up, dump in 15 s");
    else if (stage == 1 && std::chrono::steady_clock::now() - since > std::chrono::seconds(15)) stage = 2, game::DumpObjects();
    if ((game::Opt().esp || game::Opt().aimbot) && who && R32(who + kCharKind) == kEnemyKind && !R32(who + kCharDying)) Remember(who);
    __imp__sub_001DFD90(ctx, base);
}

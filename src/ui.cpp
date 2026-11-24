#include "ui.h"

#include <windows.h>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace ui
{
namespace
{
float g_scale = 1.0f;

ImFont* g_fontHuge = nullptr;
ImFont* g_fontTitle = nullptr;
ImFont* g_fontBody = nullptr;
ImFont* g_fontSmall = nullptr;

ImVec2 g_menuPos(0.0f, 0.0f);
ImVec2 g_menuSize(0.0f, 0.0f);
bool g_placed = false;

const ImU32 kAccent = IM_COL32(163, 255, 18, 255);
const ImU32 kAccentSoft = IM_COL32(198, 255, 92, 255);
const ImU32 kAccentDeep = IM_COL32(118, 200, 0, 255);
const ImU32 kText = IM_COL32(236, 243, 232, 255);
const ImU32 kTextDim = IM_COL32(142, 154, 138, 255);
const ImU32 kDanger = IM_COL32(255, 96, 96, 255);
const ImU32 kWarn = IM_COL32(255, 190, 70, 255);

struct Ripple
{
    ImVec2 pos;
    float start;
    float radius;
};

struct Particle
{
    ImVec2 pos;
    ImVec2 vel;
    float radius;
    float alpha;
};

std::vector<Ripple> g_ripples;
std::vector<Particle> g_particles;
std::unordered_map<ImGuiID, float> g_anim;
bool g_effectsReady = false;

float S(float value)
{
    return value * g_scale;
}

ImU32 Mix(ImU32 a, ImU32 b, float t)
{
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
    ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(ca.x + (cb.x - ca.x) * t,
                                                 ca.y + (cb.y - ca.y) * t,
                                                 ca.z + (cb.z - ca.z) * t,
                                                 ca.w + (cb.w - ca.w) * t));
}

ImU32 Alpha(ImU32 color, float alpha)
{
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
    c.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
}

float Smooth(float current, float target, float rate, float dt)
{
    if (dt <= 0.0f) return current;
    return current + (target - current) * (1.0f - expf(-rate * dt));
}

float& Anim(ImGuiID id)
{
    auto it = g_anim.find(id);
    if (it == g_anim.end())
        it = g_anim.emplace(id, 0.0f).first;
    return it->second;
}

float CornerInset(float y, float top, float bottom, float radius)
{
    if (radius <= 0.5f) return 0.0f;
    if (y < top + radius)
    {
        float d = (top + radius) - y;
        if (d > radius) d = radius;
        return radius - sqrtf(radius * radius - d * d);
    }
    if (y > bottom - radius)
    {
        float d = y - (bottom - radius);
        if (d > radius) d = radius;
        return radius - sqrtf(radius * radius - d * d);
    }
    return 0.0f;
}

void GlassRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, ImU32 top, ImU32 bottom)
{
    const float height = b.y - a.y;
    if (height <= 0.5f || b.x - a.x <= 0.5f)
        return;

    int steps = static_cast<int>(height / fmaxf(1.0f, S(1.0f)));
    if (steps < 6) steps = 6;
    if (steps > 720) steps = 720;

    for (int i = 0; i < steps; ++i)
    {
        const float t0 = static_cast<float>(i) / static_cast<float>(steps);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(steps);
        const float y0 = a.y + height * t0;
        const float y1 = a.y + height * t1;
        const float d0 = CornerInset(y0, a.y, b.y, radius);
        const float d1 = CornerInset(y1, a.y, b.y, radius);
        const float inset = d0 > d1 ? d0 : d1;
        dl->AddRectFilled(ImVec2(a.x + inset, y0), ImVec2(b.x - inset, y1 + 0.6f), Mix(top, bottom, t0));
    }
}

void SoftShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, float strength)
{
    for (int i = 12; i >= 1; --i)
    {
        const float grow = static_cast<float>(i) * S(1.6f);
        const int alpha = static_cast<int>(strength * (13.0f - static_cast<float>(i)) * 0.9f);
        if (alpha <= 0) continue;
        dl->AddRectFilled(ImVec2(a.x - grow, a.y - grow + S(3.0f)),
                          ImVec2(b.x + grow, b.y + grow + S(3.0f)),
                          IM_COL32(0, 0, 0, alpha), radius + grow);
    }
}

void AccentGlow(ImDrawList* dl, ImVec2 center, float radius, float strength)
{
    const int layers = 14;
    for (int i = layers; i >= 1; --i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(layers);
        const float r = radius * t;
        const int alpha = static_cast<int>(strength * (1.0f - t) * (1.0f - t) * 255.0f * 0.5f);
        if (alpha <= 0) continue;
        dl->AddCircleFilled(center, r, IM_COL32(163, 255, 18, alpha), 48);
    }
}

void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text)
{
    dl->AddText(font, size, pos, color, text);
}

ImVec2 TextSize(ImFont* font, float size, const char* text)
{
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
}

void TextCentered(ImDrawList* dl, ImFont* font, float size, ImVec2 center, ImU32 color, const char* text)
{
    const ImVec2 ts = TextSize(font, size, text);
    dl->AddText(font, size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), color, text);
}

void SpawnRipple(ImVec2 pos)
{
    g_ripples.push_back(Ripple{ pos, static_cast<float>(ImGui::GetTime()), 0.0f });
    if (g_ripples.size() > 24)
        g_ripples.erase(g_ripples.begin());
}

void DrawRipples(ImDrawList* dl)
{
    const float now = static_cast<float>(ImGui::GetTime());
    for (size_t i = 0; i < g_ripples.size();)
    {
        const float age = now - g_ripples[i].start;
        if (age > 0.55f)
        {
            g_ripples.erase(g_ripples.begin() + static_cast<long long>(i));
            continue;
        }
        const float t = age / 0.55f;
        const float radius = S(6.0f) + t * S(46.0f);
        const int alpha = static_cast<int>((1.0f - t) * (1.0f - t) * 120.0f);
        dl->AddCircle(g_ripples[i].pos, radius, IM_COL32(163, 255, 18, alpha), 40, S(1.6f));
        ++i;
    }
}

void InitParticles(ImVec2 a, ImVec2 b)
{
    g_particles.clear();
    g_particles.reserve(64);
    unsigned int seed = 0x9E3779B9u;
    for (int i = 0; i < 64; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const float rx = static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
        seed = seed * 1664525u + 1013904223u;
        const float ry = static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
        seed = seed * 1664525u + 1013904223u;
        const float rv = static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
        seed = seed * 1664525u + 1013904223u;
        const float ra = static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;

        Particle p;
        p.pos = ImVec2(a.x + rx * (b.x - a.x), a.y + ry * (b.y - a.y));
        p.vel = ImVec2((rv - 0.5f) * S(5.0f), -S(4.0f) - rv * S(9.0f));
        p.radius = S(0.7f) + ra * S(1.5f);
        p.alpha = 0.06f + ra * 0.16f;
        g_particles.push_back(p);
    }
    g_effectsReady = true;
}

void UpdateParticles(ImDrawList* dl, ImVec2 a, ImVec2 b, float dt)
{
    for (auto& p : g_particles)
    {
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        if (p.pos.y < a.y - S(10.0f))
        {
            p.pos.y = b.y + S(6.0f);
            p.pos.x = a.x + (static_cast<float>(rand() % 1000) / 1000.0f) * (b.x - a.x);
        }
        if (p.pos.x < a.x) p.pos.x = b.x;
        if (p.pos.x > b.x) p.pos.x = a.x;
        dl->AddCircleFilled(p.pos, p.radius, IM_COL32(163, 255, 18, static_cast<int>(p.alpha * 255.0f)), 8);
    }
}

void IconCrosshair(ImDrawList* dl, ImVec2 c, float r, ImU32 color)
{
    dl->AddCircle(c, r, color, 28, S(1.4f));
    dl->AddCircleFilled(c, r * 0.22f, color, 12);
    dl->AddLine(ImVec2(c.x, c.y - r - S(3.0f)), ImVec2(c.x, c.y - r + S(2.0f)), color, S(1.4f));
    dl->AddLine(ImVec2(c.x, c.y + r - S(2.0f)), ImVec2(c.x, c.y + r + S(3.0f)), color, S(1.4f));
    dl->AddLine(ImVec2(c.x - r - S(3.0f), c.y), ImVec2(c.x - r + S(2.0f), c.y), color, S(1.4f));
    dl->AddLine(ImVec2(c.x + r - S(2.0f), c.y), ImVec2(c.x + r + S(3.0f), c.y), color, S(1.4f));
}

void IconChip(ImDrawList* dl, ImVec2 c, float r, ImU32 color)
{
    const ImVec2 a(c.x - r, c.y - r);
    const ImVec2 b(c.x + r, c.y + r);
    dl->AddRect(a, b, color, S(3.0f), 0, S(1.4f));
    dl->AddRect(ImVec2(a.x + r * 0.55f, a.y + r * 0.55f), ImVec2(b.x - r * 0.55f, b.y - r * 0.55f),
                color, S(1.5f), 0, S(1.2f));
    for (int i = -1; i <= 1; ++i)
    {
        const float off = static_cast<float>(i) * r * 0.55f;
        dl->AddLine(ImVec2(c.x + off, a.y - S(2.5f)), ImVec2(c.x + off, a.y), color, S(1.2f));
        dl->AddLine(ImVec2(c.x + off, b.y), ImVec2(c.x + off, b.y + S(2.5f)), color, S(1.2f));
    }
}

void IconInfo(ImDrawList* dl, ImVec2 c, float r, ImU32 color)
{
    dl->AddCircle(c, r, color, 28, S(1.4f));
    dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.42f), S(1.5f), color, 8);
    dl->AddLine(ImVec2(c.x, c.y - r * 0.05f), ImVec2(c.x, c.y + r * 0.48f), color, S(1.6f));
}

void IconLayers(ImDrawList* dl, ImVec2 c, float r, ImU32 color)
{
    const float halfW = r * 1.05f;
    for (int i = 0; i < 3; ++i)
    {
        const float y = c.y - r * 0.75f + static_cast<float>(i) * r * 0.72f;
        dl->AddLine(ImVec2(c.x - halfW, y), ImVec2(c.x + halfW, y), color, S(1.6f));
        dl->AddCircleFilled(ImVec2(c.x - halfW, y), S(1.7f), color, 8);
        dl->AddCircleFilled(ImVec2(c.x + halfW, y), S(1.7f), color, 8);
    }
}

void DrawIcon(ImDrawList* dl, int index, ImVec2 c, float r, ImU32 color)
{
    switch (index)
    {
    case 0: IconCrosshair(dl, c, r, color); break;
    case 1: IconLayers(dl, c, r, color); break;
    case 2: IconChip(dl, c, r, color); break;
    default: IconInfo(dl, c, r, color); break;
    }
}

bool LimeButton(const char* id, const char* label, ImVec2 pos, ImVec2 size, bool primary, const char* hint)
{
    const float dt = ImGui::GetIO().DeltaTime;

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const bool clicked = ImGui::IsItemClicked();
    const ImGuiID key = ImGui::GetItemID();

    float& t = Anim(key);
    t = Smooth(t, hovered ? 1.0f : 0.0f, 16.0f, dt);

    if (clicked)
        SpawnRipple(ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float radius = S(9.0f);
    const ImVec2 a = pos;
    const ImVec2 b(pos.x + size.x, pos.y + size.y);
    const float lift = held ? S(1.0f) : 0.0f;

    if (primary)
    {
        AccentGlow(dl, ImVec2(a.x + size.x * 0.5f, a.y + size.y * 0.5f), size.y * 1.15f, 0.22f + t * 0.30f);
        GlassRect(dl, ImVec2(a.x, a.y + lift), ImVec2(b.x, b.y + lift), radius,
                  Mix(IM_COL32(186, 255, 60, 255), IM_COL32(206, 255, 104, 255), t),
                  Mix(IM_COL32(128, 214, 4, 255), IM_COL32(163, 255, 18, 255), t));

        dl->PushClipRect(ImVec2(a.x + S(2.0f), a.y + S(2.0f)), ImVec2(b.x - S(2.0f), b.y - S(2.0f)), true);
        const float phase = fmodf(static_cast<float>(ImGui::GetTime()) * 0.35f, 1.4f) - 0.2f;
        const float band = a.x + phase * (size.x * 1.5f);
        dl->AddQuadFilled(ImVec2(band, a.y), ImVec2(band + S(22.0f), a.y),
                          ImVec2(band + S(22.0f) - S(16.0f), b.y), ImVec2(band - S(16.0f), b.y),
                          IM_COL32(255, 255, 255, 30));
        dl->PopClipRect();

        TextCentered(dl, g_fontBody, S(15.0f), ImVec2(a.x + size.x * 0.5f, a.y + size.y * 0.5f + lift),
                     IM_COL32(10, 20, 4, 255), label);
    }
    else
    {
        GlassRect(dl, ImVec2(a.x, a.y + lift), ImVec2(b.x, b.y + lift), radius,
                  Mix(IM_COL32(26, 32, 24, 225), IM_COL32(36, 44, 32, 235), t),
                  Mix(IM_COL32(14, 18, 13, 235), IM_COL32(20, 26, 18, 240), t));
        dl->AddRect(ImVec2(a.x, a.y + lift), ImVec2(b.x, b.y + lift),
                    Mix(Alpha(kAccent, 0.28f), Alpha(kAccent, 0.85f), t), radius, 0, S(1.2f));
        TextCentered(dl, g_fontBody, S(15.0f), ImVec2(a.x + size.x * 0.5f, a.y + size.y * 0.5f + lift),
                     Mix(kText, kAccentSoft, t), label);
    }

    if (hint != nullptr && hovered)
    {
        const ImVec2 ts = TextSize(g_fontSmall, S(11.5f), hint);
        dl->AddText(g_fontSmall, S(11.5f), ImVec2(b.x - ts.x, b.y + S(6.0f)), Alpha(kTextDim, 0.9f), hint);
    }

    return clicked;
}

bool LimeToggle(const char* id, ImVec2 pos, bool* value)
{
    const float dt = ImGui::GetIO().DeltaTime;
    const ImVec2 size(S(50.0f), S(26.0f));

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    if (clicked)
        *value = !*value;

    const ImGuiID key = ImGui::GetItemID();
    float& t = Anim(key);
    t = Smooth(t, *value ? 1.0f : 0.0f, 18.0f, dt);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = pos;
    const ImVec2 b(pos.x + size.x, pos.y + size.y);
    const float radius = size.y * 0.5f;

    if (t > 0.02f)
        AccentGlow(dl, ImVec2(a.x + size.x * 0.5f, a.y + size.y * 0.5f), size.x * 0.7f, t * 0.5f);

    GlassRect(dl, a, b, radius,
              Mix(IM_COL32(30, 36, 28, 230), IM_COL32(150, 235, 20, 255), t),
              Mix(IM_COL32(18, 22, 17, 235), IM_COL32(120, 205, 0, 255), t));
    dl->AddRect(a, b, Mix(IM_COL32(255, 255, 255, 26), Alpha(kAccent, 0.95f), t), radius, 0,
                S(1.0f) + S(0.3f) * (hovered ? 1.0f : 0.0f));

    const float knobR = size.y * 0.5f - S(3.0f);
    const float knobX = a.x + radius + (size.x - size.y) * t;
    dl->AddCircleFilled(ImVec2(knobX, a.y + size.y * 0.5f + S(1.2f)), knobR, IM_COL32(0, 0, 0, 60), 24);
    dl->AddCircleFilled(ImVec2(knobX, a.y + size.y * 0.5f), knobR, Mix(IM_COL32(228, 236, 224, 255),
                                                                       IM_COL32(12, 20, 6, 255), t), 24);

    return clicked;
}

bool LimeSlider(const char* id, ImVec2 pos, float width, int* value, int lo, int hi, const char* label)
{
    const float dt = ImGui::GetIO().DeltaTime;
    const float height = S(30.0f);

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y));
    ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    if (active)
    {
        const float mx = ImGui::GetIO().MousePos.x;
        float t = (mx - pos.x) / (width > 0.0f ? width : 1.0f);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        *value = lo + static_cast<int>(lroundf(t * static_cast<float>(hi - lo)));
    }

    const ImGuiID key = ImGui::GetItemID();
    float& t = Anim(key);
    t = Smooth(t, (hovered || active) ? 1.0f : 0.0f, 16.0f, dt);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = pos.y + height * 0.5f;
    const float trackH = S(6.0f);
    const float frac = static_cast<float>(*value - lo) / static_cast<float>(hi - lo > 0 ? hi - lo : 1);

    dl->AddRectFilled(ImVec2(pos.x, cy - trackH * 0.5f), ImVec2(pos.x + width, cy + trackH * 0.5f),
                      IM_COL32(255, 255, 255, 18), trackH * 0.5f);

    const float fillW = width * frac;
    if (fillW > 1.0f)
    {
        GlassRect(dl, ImVec2(pos.x, cy - trackH * 0.5f), ImVec2(pos.x + fillW, cy + trackH * 0.5f),
                  trackH * 0.5f, IM_COL32(198, 255, 92, 255), IM_COL32(140, 220, 0, 255));
        AccentGlow(dl, ImVec2(pos.x + fillW, cy), S(26.0f), 0.25f + t * 0.35f);
    }

    const float knobR = S(8.0f) + t * S(1.5f);
    dl->AddCircleFilled(ImVec2(pos.x + fillW, cy + S(1.0f)), knobR, IM_COL32(0, 0, 0, 70), 24);
    dl->AddCircleFilled(ImVec2(pos.x + fillW, cy), knobR, IM_COL32(246, 255, 238, 255), 24);
    dl->AddCircleFilled(ImVec2(pos.x + fillW, cy), knobR * 0.45f, kAccentDeep, 20);

    char buffer[32];
    sprintf_s(buffer, "%d", *value);
    const ImVec2 ts = TextSize(g_fontSmall, S(12.0f), buffer);
    dl->AddText(g_fontSmall, S(12.0f), ImVec2(pos.x + width - ts.x, cy - ts.y - S(14.0f)),
                Alpha(kAccentSoft, 0.85f + 0.15f * t), buffer);

    if (label != nullptr)
        dl->AddText(g_fontSmall, S(11.0f), ImVec2(pos.x + width - ts.x, cy + S(8.0f)),
                    Alpha(kTextDim, 0.55f), label);

    return active;
}

bool SideTab(const char* id, const char* label, ImVec2 pos, ImVec2 size, bool selected, int icon)
{
    const float dt = ImGui::GetIO().DeltaTime;

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    const ImGuiID key = ImGui::GetItemID();

    float& t = Anim(key);
    t = Smooth(t, selected ? 1.0f : (hovered ? 0.45f : 0.0f), 16.0f, dt);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = pos;
    const ImVec2 b(pos.x + size.x, pos.y + size.y);

    if (t > 0.01f)
    {
        GlassRect(dl, a, b, S(9.0f), Alpha(kAccent, 0.10f * t), Alpha(kAccent, 0.02f * t));
        dl->AddRect(a, b, Alpha(kAccent, 0.18f * t), S(9.0f), 0, S(1.0f));
    }

    const ImU32 iconColor = Mix(Alpha(kTextDim, 0.85f), kAccent, t);
    DrawIcon(dl, icon, ImVec2(a.x + S(24.0f), a.y + size.y * 0.5f), S(8.0f), iconColor);

    dl->AddText(g_fontBody, S(14.0f), ImVec2(a.x + S(46.0f), a.y + size.y * 0.5f - S(9.0f)),
                Mix(kTextDim, kText, t), label);

    if (selected)
    {
        const float barH = size.y * 0.52f;
        dl->AddRectFilled(ImVec2(a.x + S(1.0f), a.y + (size.y - barH) * 0.5f),
                          ImVec2(a.x + S(3.5f), a.y + (size.y + barH) * 0.5f), kAccent, S(2.0f));
    }

    return clicked;
}

void DrawHeaderStatus(ImDrawList* dl, ImVec2 pos, ImVec2 size, MemStatus status)
{
    const bool ok = status == MemStatus::Writing || status == MemStatus::Ready;
    const bool error = status == MemStatus::OpenFailed || status == MemStatus::WriteFailed;
    const ImU32 dot = error ? kDanger : (ok ? kAccent : kWarn);

    const char* text = "EN ESPERA";
    if (status == MemStatus::Writing) text = "BLOQUEANDO";
    else if (status == MemStatus::Ready) text = "CONECTADO";
    else if (status == MemStatus::Searching) text = "BUSCANDO PROCESO";
    else if (status == MemStatus::OpenFailed) text = "ACCESO DENEGADO";
    else if (status == MemStatus::WriteFailed) text = "ERROR DE ESCRITURA";

    GlassRect(dl, pos, ImVec2(pos.x + size.x, pos.y + size.y), size.y * 0.5f,
              IM_COL32(20, 26, 18, 200), IM_COL32(12, 16, 11, 210));
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), Alpha(dot, 0.35f), size.y * 0.5f, 0, S(1.0f));

    const float pulse = 0.55f + 0.45f * sinf(static_cast<float>(ImGui::GetTime()) * 3.4f);
    const ImVec2 c(pos.x + S(14.0f), pos.y + size.y * 0.5f);
    dl->AddCircleFilled(c, S(5.0f), Alpha(dot, 0.18f * pulse), 16);
    dl->AddCircleFilled(c, S(3.0f), dot, 16);

    dl->AddText(g_fontSmall, S(11.5f), ImVec2(c.x + S(10.0f), c.y - S(7.5f)), Mix(kText, dot, 0.35f), text);
}

void StatRow(ImDrawList* dl, ImVec2 pos, float width, const char* label, const char* value, ImU32 valueColor)
{
    dl->AddText(g_fontSmall, S(11.0f), ImVec2(pos.x, pos.y), Alpha(kTextDim, 0.75f), label);
    const ImVec2 ts = TextSize(g_fontBody, S(13.5f), value);
    dl->AddText(g_fontBody, S(13.5f), ImVec2(pos.x + width - ts.x, pos.y - S(3.0f)), valueColor, value);
}
}

void LoadFonts(float dpiScale)
{
    g_scale = dpiScale > 0.5f ? dpiScale : 1.0f;
    g_menuSize = ImVec2(S(660.0f), S(500.0f));

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.PixelSnapH = false;

    const char* regular = "C:\\Windows\\Fonts\\segoeui.ttf";
    const char* semibold = "C:\\Windows\\Fonts\\seguisb.ttf";
    const char* bold = "C:\\Windows\\Fonts\\segoeuib.ttf";

    auto load = [&](const char* path, float size) -> ImFont*
    {
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
            return nullptr;
        return io.Fonts->AddFontFromFileTTF(path, S(size), &config);
    };

    g_fontBody = load(regular, 14.5f);
    g_fontSmall = load(regular, 12.0f);
    g_fontTitle = load(bold, 19.0f);
    g_fontHuge = load(bold, 42.0f);

    if (g_fontBody == nullptr) g_fontBody = io.Fonts->AddFontDefault();
    if (g_fontSmall == nullptr) g_fontSmall = g_fontBody;
    if (g_fontTitle == nullptr) g_fontTitle = g_fontBody;
    if (g_fontHuge == nullptr) g_fontHuge = g_fontBody;

    io.Fonts->Build();
}

void ApplyStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameRounding = 8.0f;
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
}

ImVec2 MenuPosition()
{
    return g_menuPos;
}

ImVec2 MenuSize()
{
    return g_menuSize;
}

void Draw(UiState& state, MemoryWriter& memory)
{
    if (!state.open)
    {
        g_ripples.clear();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const float dt = io.DeltaTime;

    if (!g_placed)
    {
        g_menuPos = ImVec2((io.DisplaySize.x - g_menuSize.x) * 0.5f,
                           (io.DisplaySize.y - g_menuSize.y) * 0.5f);
        if (g_menuPos.x < 0.0f) g_menuPos.x = 0.0f;
        if (g_menuPos.y < 0.0f) g_menuPos.y = 0.0f;
        g_placed = true;
    }

    ImGui::SetNextWindowPos(g_menuPos);
    ImGui::SetNextWindowSize(g_menuSize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));

    ImGui::Begin("##assault_panel", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 P = g_menuPos;
    const ImVec2 M = g_menuSize;
    const ImVec2 A = P;
    const ImVec2 B(P.x + M.x, P.y + M.y);

    const float pad = S(20.0f);
    const float headerH = S(92.0f);
    const float footerH = S(34.0f);
    const float sideW = S(176.0f);

    if (!g_effectsReady)
        InitParticles(A, B);

    SoftShadow(dl, A, B, S(16.0f), 1.0f);
    GlassRect(dl, A, B, S(16.0f), IM_COL32(19, 24, 18, 216), IM_COL32(7, 10, 7, 236));

    dl->PushClipRect(A, B, true);

    AccentGlow(dl, ImVec2(A.x + S(60.0f), A.y + S(10.0f)), S(240.0f), 0.16f);
    AccentGlow(dl, ImVec2(B.x - S(40.0f), B.y - S(20.0f)), S(220.0f), 0.10f);

    UpdateParticles(dl, A, B, dt);

    const float scan = fmodf(static_cast<float>(ImGui::GetTime()) * 0.06f, 1.0f);
    const float scanY = A.y + scan * M.y;
    dl->AddRectFilledMultiColor(ImVec2(A.x, scanY - S(60.0f)), ImVec2(B.x, scanY),
                                IM_COL32(163, 255, 18, 0), IM_COL32(163, 255, 18, 0),
                                IM_COL32(163, 255, 18, 12), IM_COL32(163, 255, 18, 12));

    if (io.MousePos.x > A.x && io.MousePos.x < B.x && io.MousePos.y > A.y && io.MousePos.y < B.y)
        AccentGlow(dl, io.MousePos, S(150.0f), 0.05f);

    const ImVec2 logoA(A.x + pad, A.y + S(24.0f));
    const ImVec2 logoB(logoA.x + S(44.0f), logoA.y + S(44.0f));
    GlassRect(dl, logoA, logoB, S(12.0f), IM_COL32(163, 255, 18, 235), IM_COL32(104, 186, 0, 235));
    dl->AddRect(logoA, logoB, Alpha(kAccentSoft, 0.55f), S(12.0f), 0, S(1.0f));
    dl->AddCircleFilled(ImVec2((logoA.x + logoB.x) * 0.5f, (logoA.y + logoB.y) * 0.5f), S(9.0f),
                        IM_COL32(9, 16, 4, 255), 24);
    dl->AddCircleFilled(ImVec2((logoA.x + logoB.x) * 0.5f, (logoA.y + logoB.y) * 0.5f), S(3.2f),
                        IM_COL32(200, 255, 90, 255), 16);

    Text(dl, g_fontTitle, S(19.0f), ImVec2(logoB.x + S(14.0f), A.y + S(22.0f)), kText, "ASSAULT CONTROL");
    Text(dl, g_fontSmall, S(11.5f), ImVec2(logoB.x + S(15.0f), A.y + S(50.0f)),
         Alpha(kAccentSoft, 0.8f), "MEMORY SUITE  /  MUNICION INFINITA");

    const ImVec2 pillSize(S(150.0f), S(26.0f));
    DrawHeaderStatus(dl, ImVec2(B.x - pad - S(34.0f) - pillSize.x, A.y + S(30.0f)), pillSize, memory.Status());

    dl->AddLine(ImVec2(A.x + pad, A.y + headerH), ImVec2(B.x - pad, A.y + headerH),
                IM_COL32(255, 255, 255, 16), S(1.0f));

    const ImVec2 sideA(A.x + S(16.0f), A.y + headerH + S(14.0f));
    const ImVec2 sideB(sideA.x + sideW, B.y - footerH - S(12.0f));
    GlassRect(dl, sideA, sideB, S(12.0f), IM_COL32(255, 255, 255, 10), IM_COL32(255, 255, 255, 3));
    dl->AddRect(sideA, sideB, IM_COL32(255, 255, 255, 12), S(12.0f), 0, S(1.0f));

    const char* tabNames[4] = { "MUNICION", "DIRECCIONES", "PROCESO", "ACERCA" };
    const float tabH = S(42.0f);
    for (int i = 0; i < 4; ++i)
    {
        char id[32];
        sprintf_s(id, "##tab_%d", i);
        const ImVec2 tabPos(sideA.x + S(8.0f), sideA.y + S(10.0f) + static_cast<float>(i) * (tabH + S(6.0f)));
        if (SideTab(id, tabNames[i], tabPos, ImVec2(sideW - S(16.0f), tabH), state.tab == i, i))
            state.tab = i;
    }

    const ImVec2 cardA(sideA.x + S(12.0f), sideB.y - S(96.0f));
    const ImVec2 cardB(sideB.x - S(12.0f), sideB.y - S(12.0f));
    GlassRect(dl, cardA, cardB, S(10.0f), Alpha(kAccent, 0.10f), Alpha(kAccent, 0.03f));
    dl->AddRect(cardA, cardB, Alpha(kAccent, 0.20f), S(10.0f), 0, S(1.0f));
    Text(dl, g_fontSmall, S(10.5f), ImVec2(cardA.x + S(10.0f), cardA.y + S(9.0f)), Alpha(kTextDim, 0.9f), "HOTKEYS");
    Text(dl, g_fontSmall, S(11.0f), ImVec2(cardA.x + S(10.0f), cardA.y + S(26.0f)), kText, "INSERT  MENU");
    Text(dl, g_fontSmall, S(11.0f), ImVec2(cardA.x + S(10.0f), cardA.y + S(43.0f)), kText, "END     SALIR");
    Text(dl, g_fontSmall, S(10.5f), ImVec2(cardA.x + S(10.0f), cardA.y + S(62.0f)), Alpha(kAccentSoft, 0.75f),
         "x64  /  RELEASE");

    const float cx0 = sideB.x + S(18.0f);
    const float cx1 = B.x - pad;
    const float cw = cx1 - cx0;
    float y = A.y + headerH + S(16.0f);

    ImGui::PushClipRect(ImVec2(cx0 - S(4.0f), A.y + headerH), ImVec2(cx1 + S(4.0f), B.y - footerH), true);

    if (state.tab == 0)
    {
        const ImVec2 c0(cx0, y);
        const ImVec2 c1(cx1, y + S(112.0f));
        GlassRect(dl, c0, c1, S(12.0f), IM_COL32(30, 38, 26, 190), IM_COL32(16, 21, 14, 210));
        dl->AddRect(c0, c1, IM_COL32(255, 255, 255, 16), S(12.0f), 0, S(1.0f));
        dl->AddRectFilledMultiColor(ImVec2(c0.x, c0.y), ImVec2(c1.x, c0.y + S(52.0f)),
                                    IM_COL32(163, 255, 18, 20), IM_COL32(163, 255, 18, 4),
                                    IM_COL32(163, 255, 18, 0), IM_COL32(163, 255, 18, 0));

        Text(dl, g_fontSmall, S(10.5f), ImVec2(c0.x + S(18.0f), c0.y + S(14.0f)),
             Alpha(kAccentSoft, 0.85f), "MUNICION BLOQUEADA");

        char valueText[16];
        sprintf_s(valueText, "%d", state.ammoValue);
        dl->AddText(g_fontHuge, S(42.0f), ImVec2(c0.x + S(16.0f), c0.y + S(30.0f)), kText, valueText);
        const ImVec2 vs = TextSize(g_fontHuge, S(42.0f), valueText);
        dl->AddText(g_fontSmall, S(12.0f), ImVec2(c0.x + S(18.0f) + vs.x, c0.y + S(58.0f)),
                    Alpha(kTextDim, 0.9f), "BALAS");

        const bool active = state.ammoLock;
        const ImVec2 chipSize(S(96.0f), S(24.0f));
        const ImVec2 chipA(c1.x - S(18.0f) - chipSize.x, c0.y + S(16.0f));
        GlassRect(dl, chipA, ImVec2(chipA.x + chipSize.x, chipA.y + chipSize.y), chipSize.y * 0.5f,
                  Alpha(active ? kAccent : kWarn, 0.18f), Alpha(active ? kAccent : kWarn, 0.06f));
        dl->AddRect(chipA, ImVec2(chipA.x + chipSize.x, chipA.y + chipSize.y),
                    Alpha(active ? kAccent : kWarn, 0.45f), chipSize.y * 0.5f, 0, S(1.0f));
        TextCentered(dl, g_fontSmall, S(11.0f), ImVec2(chipA.x + chipSize.x * 0.5f, chipA.y + chipSize.y * 0.5f),
                     active ? kAccentSoft : kWarn, active ? "ACTIVO" : "PAUSADO");

        const char* infoWrite = "SE REESCRIBE CADA 12 ms";
        const char* infoAddr = "0x00907840  /  4 BYTES";
        const ImVec2 iw = TextSize(g_fontSmall, S(11.0f), infoWrite);
        const ImVec2 ia = TextSize(g_fontSmall, S(11.0f), infoAddr);
        dl->AddText(g_fontSmall, S(11.0f), ImVec2(c1.x - S(18.0f) - iw.x, c0.y + S(52.0f)),
                    Alpha(kTextDim, 0.8f), infoWrite);
        dl->AddText(g_fontSmall, S(11.0f), ImVec2(c1.x - S(18.0f) - ia.x, c0.y + S(68.0f)),
                    Alpha(kTextDim, 0.8f), infoAddr);

        y += S(112.0f) + S(16.0f);

        Text(dl, g_fontBody, S(14.0f), ImVec2(cx0, y + S(4.0f)), kText, "Bloqueo infinito");
        Text(dl, g_fontSmall, S(11.0f), ImVec2(cx0, y + S(24.0f)), Alpha(kTextDim, 0.8f),
             "Mantiene la municion fija mientras el proceso este abierto");
        if (LimeToggle("##ammo_lock", ImVec2(cx1 - S(50.0f), y), &state.ammoLock))
        {
            memory.SetLock(state.ammoLock);
        }
        y += S(50.0f);

        Text(dl, g_fontSmall, S(11.0f), ImVec2(cx0, y), Alpha(kTextDim, 0.8f), "VALOR A ESCRIBIR");
        if (LimeSlider("##ammo_value", ImVec2(cx0, y + S(16.0f)), cw, &state.ammoValue, 1, 100, nullptr))
        {
            memory.SetValue(state.ammoValue);
        }
        y += S(66.0f);

        if (LimeButton("##apply", "APLICAR AHORA", ImVec2(cx0, y), ImVec2(S(196.0f), S(44.0f)), true, nullptr))
        {
            memory.SetValue(state.ammoValue);
            memory.SetLock(state.ammoLock);
            memory.RequestAttach();
        }
        if (LimeButton("##reconnect", "RECONECTAR", ImVec2(cx0 + S(208.0f), y), ImVec2(S(150.0f), S(44.0f)), false, nullptr))
        {
            memory.RequestAttach();
        }
        y += S(60.0f);

        char addr[32];
        sprintf_s(addr, "0x%08X", static_cast<unsigned int>(memory.PrimaryAddress()));
        StatRow(dl, ImVec2(cx0, y), cw * 0.5f - S(10.0f), "DIRECCION", addr, kAccentSoft);

        char writes[32];
        sprintf_s(writes, "%llu", memory.Writes());
        StatRow(dl, ImVec2(cx0 + cw * 0.5f + S(18.0f), y), cw * 0.5f - S(28.0f), "ESCRITURAS", writes, kText);

        char pidText[32];
        sprintf_s(pidText, "%lu", static_cast<unsigned long>(memory.Pid()));
        StatRow(dl, ImVec2(cx0, y + S(26.0f)), cw * 0.5f - S(10.0f), "PID", pidText, kText);
        StatRow(dl, ImVec2(cx0 + cw * 0.5f + S(18.0f), y + S(26.0f)), cw * 0.5f - S(28.0f), "ESTADO",
                memory.Attached() ? "ENLAZADO" : "SIN ENLACE", memory.Attached() ? kAccentSoft : kWarn);
    }
    else if (state.tab == 1)
    {
        const std::vector<TargetState> targets = memory.Snapshot();
        float rowY = y;

        for (size_t i = 0; i < targets.size(); ++i)
        {
            const TargetState& target = targets[i];
            const ImVec2 r0(cx0, rowY);
            const ImVec2 r1(cx1, rowY + S(58.0f));

            GlassRect(dl, r0, r1, S(12.0f), IM_COL32(30, 38, 26, 190), IM_COL32(16, 21, 14, 210));
            dl->AddRect(r0, r1, target.enabled ? Alpha(kAccent, 0.24f) : IM_COL32(255, 255, 255, 14),
                        S(12.0f), 0, S(1.0f));

            char address[32];
            sprintf_s(address, "0x%08X", static_cast<unsigned int>(target.address));
            Text(dl, g_fontBody, S(15.0f), ImVec2(r0.x + S(16.0f), r0.y + S(9.0f)),
                 target.enabled ? kText : Alpha(kTextDim, 0.7f), address);

            const char* kind = target.guarded ? "DINAMICA" : "FIJA";
            const ImU32 kindColor = target.guarded ? kWarn : kAccent;
            const ImVec2 badgePos(r0.x + S(16.0f), r0.y + S(33.0f));
            const ImVec2 badgeSize(S(76.0f), S(17.0f));
            GlassRect(dl, badgePos, ImVec2(badgePos.x + badgeSize.x, badgePos.y + badgeSize.y), S(8.0f),
                      Alpha(kindColor, 0.16f), Alpha(kindColor, 0.05f));
            dl->AddRect(badgePos, ImVec2(badgePos.x + badgeSize.x, badgePos.y + badgeSize.y),
                        Alpha(kindColor, 0.40f), S(8.0f), 0, S(1.0f));
            TextCentered(dl, g_fontSmall, S(10.0f),
                         ImVec2(badgePos.x + badgeSize.x * 0.5f, badgePos.y + badgeSize.y * 0.5f),
                         kindColor, kind);

            char info[80];
            const bool invalid = target.guarded && !target.valid;
            if (invalid)
                sprintf_s(info, "direccion no valida");
            else
                sprintf_s(info, "%llu escrituras  /  valor %d", target.writes, target.lastValue);
            Text(dl, g_fontSmall, S(10.5f), ImVec2(badgePos.x + badgeSize.x + S(10.0f), r0.y + S(36.0f)),
                 invalid ? kDanger : Alpha(kTextDim, 0.85f), info);

            bool enabled = target.enabled;
            char id[32];
            sprintf_s(id, "##target_%zu", i);
            if (LimeToggle(id, ImVec2(r1.x - S(66.0f), r0.y + S(16.0f)), &enabled))
                memory.SetTargetEnabled(i, enabled);

            rowY += S(66.0f);
        }

        rowY += S(4.0f);
        if (LimeButton("##revalidate", "REVALIDAR", ImVec2(cx0, rowY), ImVec2(S(150.0f), S(44.0f)), true, nullptr))
            memory.Revalidate();
        if (LimeButton("##attach_now", "RECONECTAR", ImVec2(cx0 + S(162.0f), rowY), ImVec2(S(150.0f), S(44.0f)), false, nullptr))
            memory.RequestAttach();

        rowY += S(58.0f);
        Text(dl, g_fontSmall, S(11.0f), ImVec2(cx0, rowY), Alpha(kTextDim, 0.85f),
             "Las direcciones dinamicas cambian al reiniciar el juego.");
        Text(dl, g_fontSmall, S(11.0f), ImVec2(cx0, rowY + S(17.0f)), Alpha(kTextDim, 0.85f),
             "Solo se escriben si contienen una municion coherente.");
    }
    else if (state.tab == 2)
    {
        const ImVec2 c0(cx0, y);
        const ImVec2 c1(cx1, y + S(196.0f));
        GlassRect(dl, c0, c1, S(12.0f), IM_COL32(30, 38, 26, 190), IM_COL32(16, 21, 14, 210));
        dl->AddRect(c0, c1, IM_COL32(255, 255, 255, 16), S(12.0f), 0, S(1.0f));

        Text(dl, g_fontSmall, S(10.5f), ImVec2(c0.x + S(18.0f), c0.y + S(14.0f)),
             Alpha(kAccentSoft, 0.85f), "PROCESO OBJETIVO");

        char pidText[32];
        sprintf_s(pidText, "%lu", static_cast<unsigned long>(memory.Pid()));
        char addr[32];
        sprintf_s(addr, "0x%08X", static_cast<unsigned int>(memory.PrimaryAddress()));

        const float rowY = c0.y + S(44.0f);
        const float rowH = S(36.0f);
        const char* labels[4] = { "EJECUTABLE", "PID", "DIRECCION", "ESCRITURA" };
        char exeName[64] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, memory.ExeName().c_str(), -1, exeName, sizeof(exeName) - 1, nullptr, nullptr);
        const char* values[4] = { exeName, pidText, addr, "WriteProcessMemory" };

        for (int i = 0; i < 4; ++i)
        {
            const float ry = rowY + static_cast<float>(i) * rowH;
            if (i > 0)
                dl->AddLine(ImVec2(c0.x + S(18.0f), ry), ImVec2(c1.x - S(18.0f), ry), IM_COL32(255, 255, 255, 10), S(1.0f));
            Text(dl, g_fontSmall, S(11.5f), ImVec2(c0.x + S(18.0f), ry + S(12.0f)), Alpha(kTextDim, 0.9f), labels[i]);
            const ImVec2 ts = TextSize(g_fontBody, S(13.0f), values[i]);
            dl->AddText(g_fontBody, S(13.0f), ImVec2(c1.x - S(18.0f) - ts.x, ry + S(10.0f)), kText, values[i]);
        }

        y += S(196.0f) + S(18.0f);

        if (LimeButton("##reattach", "RECONECTAR AHORA", ImVec2(cx0, y), ImVec2(S(210.0f), S(44.0f)), true, nullptr))
            memory.RequestAttach();
        if (LimeButton("##restore", "REAPLICAR VALOR", ImVec2(cx0 + S(222.0f), y), ImVec2(S(178.0f), S(44.0f)), false, nullptr))
        {
            memory.SetValue(state.ammoValue);
            memory.SetLock(true);
            state.ammoLock = true;
        }
        y += S(64.0f);

        Text(dl, g_fontSmall, S(11.5f), ImVec2(cx0, y), Alpha(kTextDim, 0.85f),
             "El escritor se reconecta solo si el juego se cierra y se vuelve a abrir.");
        Text(dl, g_fontSmall, S(11.5f), ImVec2(cx0, y + S(18.0f)), Alpha(kTextDim, 0.85f),
             "Ejecuta el panel como administrador si el juego corre con privilegios.");
    }
    else
    {
        const ImVec2 c0(cx0, y);
        const ImVec2 c1(cx1, y + S(210.0f));
        GlassRect(dl, c0, c1, S(12.0f), IM_COL32(30, 38, 26, 190), IM_COL32(16, 21, 14, 210));
        dl->AddRect(c0, c1, IM_COL32(255, 255, 255, 16), S(12.0f), 0, S(1.0f));

        Text(dl, g_fontSmall, S(10.5f), ImVec2(c0.x + S(18.0f), c0.y + S(14.0f)),
             Alpha(kAccentSoft, 0.85f), "INFORMACION");

        const char* lines[6] = {
            "Assault Control es un panel externo x64 que escribe en la",
            "memoria del cliente de AssaultCube sin inyectar codigo en el",
            "proceso. Solo necesita permisos de lectura y escritura.",
            "",
            "La direccion 0x00907840 guarda el contador de municion y se",
            "reescribe de forma constante para que nunca disminuya."
        };

        for (int i = 0; i < 6; ++i)
        {
            if (lines[i][0] == '\0') continue;
            Text(dl, g_fontBody, S(13.0f), ImVec2(c0.x + S(18.0f), c0.y + S(44.0f) + static_cast<float>(i) * S(22.0f)),
                 i == 0 || i == 1 || i == 2 ? kText : Alpha(kTextDim, 0.9f), lines[i]);
        }

        y += S(210.0f) + S(18.0f);

        const ImVec2 d0(cx0, y);
        const ImVec2 d1(cx1, y + S(64.0f));
        GlassRect(dl, d0, d1, S(12.0f), Alpha(kAccent, 0.10f), Alpha(kAccent, 0.03f));
        dl->AddRect(d0, d1, Alpha(kAccent, 0.22f), S(12.0f), 0, S(1.0f));
        Text(dl, g_fontSmall, S(11.0f), ImVec2(d0.x + S(16.0f), d0.y + S(12.0f)), Alpha(kTextDim, 0.9f), "COMPILACION");
        Text(dl, g_fontBody, S(13.0f), ImVec2(d0.x + S(16.0f), d0.y + S(32.0f)), kText,
             "x64  /  RELEASE  /  DIRECT3D 11  /  IMGUI");
    }

    ImGui::PopClipRect();

    DrawRipples(dl);

    dl->AddLine(ImVec2(A.x + pad, B.y - footerH), ImVec2(B.x - pad, B.y - footerH),
                IM_COL32(255, 255, 255, 14), S(1.0f));
    Text(dl, g_fontSmall, S(10.5f), ImVec2(A.x + pad, B.y - footerH + S(11.0f)), Alpha(kTextDim, 0.8f),
         "INSERT  MENU        END  SALIR");
    const char* footerRight = "v1.0  /  x64";
    const ImVec2 fr = TextSize(g_fontSmall, S(10.5f), footerRight);
    dl->AddText(g_fontSmall, S(10.5f), ImVec2(B.x - pad - fr.x, B.y - footerH + S(11.0f)),
                Alpha(kAccentSoft, 0.55f), footerRight);

    dl->AddRect(A, B, IM_COL32(255, 255, 255, 18), S(16.0f), 0, S(1.0f));
    dl->AddRect(ImVec2(A.x + S(1.0f), A.y + S(1.0f)), ImVec2(B.x - S(1.0f), B.y - S(1.0f)),
                IM_COL32(255, 255, 255, 8), S(15.0f), 0, S(1.0f));

    dl->PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(A.x, A.y));
    ImGui::InvisibleButton("##drag", ImVec2(M.x, headerH));
    if (ImGui::IsItemActive())
    {
        g_menuPos.x += io.MouseDelta.x;
        g_menuPos.y += io.MouseDelta.y;
        if (g_menuPos.x < 0.0f) g_menuPos.x = 0.0f;
        if (g_menuPos.y < 0.0f) g_menuPos.y = 0.0f;
        if (g_menuPos.x > io.DisplaySize.x - M.x) g_menuPos.x = io.DisplaySize.x - M.x;
        if (g_menuPos.y > io.DisplaySize.y - M.y) g_menuPos.y = io.DisplaySize.y - M.y;
    }

    const ImVec2 closeSize(S(24.0f), S(24.0f));
    const ImVec2 closePos(B.x - pad - closeSize.x, A.y + S(31.0f));
    ImGui::SetCursorScreenPos(closePos);
    ImGui::InvisibleButton("##close", closeSize);
    const bool closeHovered = ImGui::IsItemHovered();
    const bool closeClicked = ImGui::IsItemClicked();
    dl->AddRectFilled(closePos, ImVec2(closePos.x + closeSize.x, closePos.y + closeSize.y),
                      closeHovered ? IM_COL32(255, 90, 90, 40) : IM_COL32(255, 255, 255, 8), S(8.0f));
    const ImU32 xColor = closeHovered ? kDanger : Alpha(kTextDim, 0.9f);
    const float xo = S(9.0f);
    dl->AddLine(ImVec2(closePos.x + xo, closePos.y + xo),
                ImVec2(closePos.x + closeSize.x - xo, closePos.y + closeSize.y - xo), xColor, S(1.6f));
    dl->AddLine(ImVec2(closePos.x + closeSize.x - xo, closePos.y + xo),
                ImVec2(closePos.x + xo, closePos.y + closeSize.y - xo), xColor, S(1.6f));

    if (closeClicked)
        state.open = false;

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
}

#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// The roles a palette can fill, for menus and checks
struct PieceRole {
    const char* id;
    const char* label;       // the build menu's name for it
    const char* models;      // which generators use it: w(all), p(illar), d(oor)
};
inline const std::vector<PieceRole>& pieceRoles() {
    static const std::vector<PieceRole> r = {
        { "fill",  "Wall",          "w"  },   // the wall's own material (its core)
        { "post",  "Posts",         "wp" },   // upright timbers; a pillar's shaft
        { "beam",  "Beams",  "wpd" },  // rails, sills, frames; a pillar's cap; a door's ledges
        { "base",  "Base",          "wp" },   // a stone plinth; a pillar's foot
        { "inlay", "Inlay",         "w"  },   // the outside band's pattern
        { "paint", "Paint",  "w"  },   // the inside face
        { "panel", "Panels", "w"  },   // the boards below the inside rail
        { "glass", "Glass",  "w"  },   // "none": open
        { "door",  "Door",   "d"  },
    };
    return r;
}

// ══════════════════════════════════════════════════════════
// PieceSpec — one placed piece's own sizes and blocks
// ══════════════════════════════════════════════════════════
// Some build pieces are made by a generator (structures.toml
// shape = "wall", "pillar", or a door) instead of a fixed
// shape, so each placed copy can have its own:
//   w, h, d       width (along the piece), height, depth (a
//                 foundation's), voxels
//   ow, oh, sill  a doorway's or window's hole: width, height,
//                 and a window's height off the floor
//   out, in       a wall's outside and inside finish (timber,
//                 plinth, inlay, frame, plain / paint, panel, plain)
//   blocks        a block per role (the palette): the timber
//                 posts, the inside paint, the window glass …
// Anything left out is the type's own (structures.toml), so an
// empty spec is the piece exactly as the registry has it — old
// saves and blueprints need nothing new.
//
// As text (structures.dat, blueprints, the server):
//   "w=24;h=36;ow=9;oh=21;post=dark_log;paint=whitewash"
// The generators clamp every size to what works (a door is never
// smaller than a person fits through), StructureRegistry::resolve.
struct PieceSpec {
    int w = 0, h = 0;                // 0: the type's
    int d = 0;                       // depth (foundations); 0: the type's
    int openW = 0, openH = 0;        // 0: the type's
    int sill = -1;                   // -1: the type's
    std::string outside, inside;     // "": the type's
    std::map<std::string, std::string> blocks;   // role → block name ("none": air)

    bool empty() const { return !w && !h && !d && !openW && !openH && sill < 0 && outside.empty() && inside.empty() && blocks.empty(); }
    bool operator==(const PieceSpec& o) const {
        return w == o.w && h == o.h && d == o.d && openW == o.openW && openH == o.openH && sill == o.sill &&
               outside == o.outside && inside == o.inside && blocks == o.blocks;
    }
    bool operator!=(const PieceSpec& o) const { return !(*this == o); }

    std::string block(const std::string& role) const {
        auto it = blocks.find(role);
        return it == blocks.end() ? std::string() : it->second;
    }

    std::string text() const {
        std::ostringstream s;
        auto put = [&](const char* k, int v) { if (s.tellp() > 0) s << ";"; s << k << "=" << v; };
        if (w) put("w", w);
        if (h) put("h", h);
        if (d) put("d", d);
        if (openW) put("ow", openW);
        if (openH) put("oh", openH);
        if (sill >= 0) put("sill", sill);
        if (!outside.empty()) { if (s.tellp() > 0) s << ";"; s << "out=" << outside; }
        if (!inside.empty())  { if (s.tellp() > 0) s << ";"; s << "in=" << inside; }
        for (auto& [k, v] : blocks) { if (s.tellp() > 0) s << ";"; s << k << "=" << v; }
        return s.str();
    }

    // Anything odd is left out: names are a-z 0-9 _, sizes 0 .. 240,
    // only the known roles (it comes from other players too)
    static PieceSpec parse(const std::string& t) {
        PieceSpec p;
        std::stringstream in(t);
        std::string item;
        auto role = [](const std::string& k) {
            for (const PieceRole& r : pieceRoles()) if (k == r.id) return true;
            return false;
        };
        auto name = [](const std::string& s) {
            if (s.empty() || s.size() > 48) return false;
            for (char c : s) if (!(std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '_')) return false;
            return true;
        };
        while (std::getline(in, item, ';')) {
            size_t eq = item.find('=');
            if (eq == std::string::npos) continue;
            std::string k = item.substr(0, eq), v = item.substr(eq + 1);
            auto num = [&](int lo) { return std::clamp(std::atoi(v.c_str()), lo, 240); };
            if (k == "w") p.w = num(0);
            else if (k == "h") p.h = num(0);
            else if (k == "d") p.d = num(0);
            else if (k == "ow") p.openW = num(0);
            else if (k == "oh") p.openH = num(0);
            else if (k == "sill") p.sill = num(0);
            else if (k == "out") { if (name(v)) p.outside = v; }
            else if (k == "in")  { if (name(v)) p.inside = v; }
            else if (role(k) && name(v)) p.blocks[k] = v;
        }
        return p;
    }

    // b laid over a: b's sizes where it sets them, its blocks over a's
    static PieceSpec over(PieceSpec a, const PieceSpec& b) {
        if (b.w) a.w = b.w;
        if (b.h) a.h = b.h;
        if (b.d) a.d = b.d;
        if (b.openW) a.openW = b.openW;
        if (b.openH) a.openH = b.openH;
        if (b.sill >= 0) a.sill = b.sill;
        if (!b.outside.empty()) a.outside = b.outside;
        if (!b.inside.empty()) a.inside = b.inside;
        for (auto& [k, v] : b.blocks) a.blocks[k] = v;
        return a;
    }
};

// A wall's finishes (StructureRegistry::wallModel)
inline const std::vector<std::string>& wallOutsides() {
    static const std::vector<std::string> v = { "plain", "timber", "plinth", "inlay", "frame" };
    return v;
}
inline const std::vector<std::string>& wallInsides() {
    static const std::vector<std::string> v = { "plain", "paint", "panel" };
    return v;
}


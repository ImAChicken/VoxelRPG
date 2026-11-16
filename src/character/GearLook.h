#pragma once
#include <glm/glm.hpp>
#include <string>

// ══════════════════════════════════════════════════════════
// GearLook — how a worn item shows on the body
// ══════════════════════════════════════════════════════════
// items.toml: gear = { shape = "helmet", color = "#8c9098",
// trim = "#5f4636" }. CharacterModel builds the piece around
// the body it's worn on (lofts a little outside the body part),
// so it fits every build, height and slider.
//
// Shapes, by where they're worn:
//   head    cap (leather cap), helmet (iron: brim, nasal,
//           cheek guards). Both hide the hair and hats
//   torso   jerkin (leather, belted, short skirt), mail (sleeves
//           and a skirt to mid thigh), cuirass (plate: breast
//           and back, pauldrons, a short fauld)
//   hands   gauntlets (cuffs and plated hands)
//   legs    greaves (shins, knee cops, thigh plates)
//   feet    boots (to mid shin)
//   back    pack (a pack with a bedroll and straps)
struct GearLook {
    std::string shape;
    glm::vec3   color{120.0f};     // 0..255
    glm::vec3   trim{70.0f};
    bool hidesHair() const { return shape == "cap" || shape == "helmet"; }
    bool operator==(const GearLook& o) const { return shape == o.shape && color == o.color && trim == o.trim; }
};

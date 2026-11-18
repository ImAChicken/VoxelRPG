// Headless checks for item icons drawn from models
// (render/IconRaster.h) and for worn gear built onto bodies
// (CharacterModel::gearParts). Writes every icon to
// /tmp/item_icons.ppm (a contact sheet) for a look.
// Run from the repo root.
#include "TestWorld.h"
#include "character/HeldItemModel.h"
#include "render/IconRaster.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <toml++/toml.hpp>

using namespace testworld;

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gHeldModels.loadDir("data/models/held");
    CharacterModel ref = CharacterModel::build(Appearance());

    std::printf("icons\n");
    const int N = 64;
    std::vector<std::pair<std::string, iconraster::Image>> pics;
    int noModel = 0;
    for (uint16_t id : gItems.inCategory(IC_TOOL | IC_WEAPON | IC_SPELLBOOK | IC_SHIELD | IC_DEV | IC_WORN)) {
        const ItemDef& d = gItems.get(id);
        iconraster::Image im;
        if (!d.heldModel.empty()) {
            const HeldModelDef* h = gHeldModels.find(d.heldModel);
            check(h != nullptr, d.name + ": its held model " + d.heldModel + " loads");
            if (h) im = iconraster::draw(h->meshLocal(HairMesh::Voxels), iconraster::heldView(), N, 0.06f);
        } else if (!d.gear.shape.empty()) {
            im = iconraster::draw(iconraster::gearAtRest(ref, d.gear), iconraster::gearView(), N, 0.1f);
        } else { noModel++; std::printf("  (no model: %s)\\n", d.name.c_str()); continue; }
        check(!im.empty() && im.coverage() > N * N / 40, d.name + ": a solid picture (" + std::to_string(im.coverage()) + " px)");
        pics.push_back({ d.name, std::move(im) });
    }
    check(noModel == 0, "every tool, weapon, dev tool and worn item has a model");

    // contact sheet, on a mid-grey so the outline shows
    int cols = 8, rows = ((int)pics.size() + cols - 1) / cols, W = cols * N, H = rows * N;
    std::vector<uint8_t> img((size_t)W * H * 3, 96);
    for (size_t n = 0; n < pics.size(); n++) {
        int cx = (int)(n % cols) * N, cy = (int)(n / cols) * N;
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
            const uint8_t* p = &pics[n].second.rgba[((size_t)y * N + x) * 4];
            uint8_t* o = &img[((size_t)(cy + y) * W + cx + x) * 3];
            float a = p[3] / 255.0f;
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)(o[k] * (1 - a) + p[k] * a);
        }
    }
    std::ofstream f("/tmp/item_icons.ppm", std::ios::binary);
    f << "P6 " << W << " " << H << " 255\n";
    f.write((const char*)img.data(), (std::streamsize)img.size());

    std::printf("gear on bodies\n");
    {
        std::vector<GearLook> all;
        for (uint16_t id : gItems.inCategory(IC_WORN)) if (!gItems.get(id).gear.shape.empty()) all.push_back(gItems.get(id).gear);
        check(all.size() >= 9, "every worn item has a gear look");
        Appearance small; small.build = 0; small.heightVoxels = 15;
        Appearance big;   big.build = 2;   big.heightVoxels = 18; big.shoulders = 1; big.chest = 1;
        for (const Appearance& a : { small, big }) {
            Appearance baldA = a; baldA.hairStyle = 18; baldA.extra = 0;
            for (const GearLook& g : all) {
                // head gear hides the hair: compare it with the same body bald
                CharacterModel bare = CharacterModel::build(g.hidesHair() ? baldA : a);
                std::vector<GearLook> one{ g };
                CharacterModel worn = CharacterModel::build(a, HairMesh::Shards, &one);
                check(worn.triangleCount() > bare.triangleCount(), g.shape + " adds to a body");
            }
        }
        // gear sits outside the body it covers: the cuirass is wider than the chest
        CharacterModel b = CharacterModel::build(big);
        auto parts = CharacterModel::gearParts(b.fit, { gItems.get(gItems.find("plate_cuirass")).gear });
        float bodyX = 0, gearX = 0;
        for (auto& v : b.parts[B_SPINE]) bodyX = std::max(bodyX, std::fabs(v.x));
        for (auto& v : parts[B_SPINE]) gearX = std::max(gearX, std::fabs(v.x));
        check(gearX > bodyX, "the cuirass is wider than the chest it covers");
        // a helmet hides the hair
        Appearance hairy; hairy.hairStyle = 4;
        std::vector<GearLook> helm{ gItems.get(gItems.find("iron_helmet")).gear };
        size_t withHair = CharacterModel::build(hairy).parts[B_HEAD].size();
        CharacterModel bald = CharacterModel::build([&] { Appearance x = hairy; x.hairStyle = 18; return x; }());
        CharacterModel helmed = CharacterModel::build(hairy, HairMesh::Shards, &helm);
        check(helmed.parts[B_HEAD].size() < withHair + CharacterModel::gearParts(bald.fit, helm)[B_HEAD].size(), "under a helmet the hair is gone");
    }

    // Every pack's atlas: a tile a sprite spans (span = [w, h]: a
    // plant drawn over several tiles) belongs to that sprite alone.
    // A block given one of its tiles (its "blank" corner) draws over
    // the plant: the ice tile once landed in a fern's corner.
    std::printf("resource pack atlases\n");
    for (const char* pack : { "jrpg", "default", "darktest" }) {
        auto cfg = toml::parse_file(std::string("data/resourcepacks/") + pack + "/atlas_config.toml");
        std::map<std::pair<int, int>, std::string> spanned;              // tile → the sprite spanning it
        std::vector<std::pair<std::pair<int, int>, std::string>> single; // other face_maps' tiles
        if (auto* fm = cfg["face_map"].as_array())
            for (auto& n : *fm) {
                auto* t = n.as_table();
                if (!t) continue;
                std::string name = (*t)["name"].value_or(std::string());
                int w = 1, h = 1;
                if (auto* sp = (*t)["span"].as_array(); sp && sp->size() == 2) { w = (int)sp->get(0)->value_or(1); h = (int)sp->get(1)->value_or(1); }
                for (const char* face : { "top", "side", "bottom" })
                    if (auto* c = (*t)[face].as_array(); c && c->size() == 2) {
                        int x = (int)c->get(0)->value_or(0), y = (int)c->get(1)->value_or(0);
                        for (int dx = 0; dx < w; dx++)
                            for (int dy = 0; dy < h; dy++) {
                                if (w > 1 || h > 1) spanned[{ x + dx, y + dy }] = name;
                                else single.push_back({ { x, y }, name });
                            }
                    }
            }
        std::string clash;
        for (auto& [tile, name] : single) {
            auto it = spanned.find(tile);
            if (it != spanned.end() && it->second != name && clash.empty())
                clash = name + " uses a tile of " + it->second + " (" + std::to_string(tile.first) + ", " + std::to_string(tile.second) + ")";
        }
        check(clash.empty(), std::string(pack) + ": no block uses a tile a sprite spans" + (clash.empty() ? "" : ": " + clash));
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}

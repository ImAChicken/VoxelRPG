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

using namespace testworld;

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gHeldModels.loadDir("data/models/held");
    CharacterModel ref = CharacterModel::build(Appearance());

    std::printf("icons\n");
    const int N = 64;
    std::vector<std::pair<std::string, iconraster::Image>> pics;
    int noModel = 0;
    for (uint16_t id : gItems.inCategory(IC_TOOL | IC_WEAPON | IC_DEV | IC_WORN)) {
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

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}

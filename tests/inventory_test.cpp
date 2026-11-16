// Headless checks for the inventory (game/Inventory.h): the
// 50 slots and hotbar keys, the off-hand, equipment that points
// at inventory slots, moving items, and loading saves (new and
// old format).
//
// Run from the repo root (it loads data/registry/*.toml).
#include "entity/Player.h"
#include <cstdio>
#include <sstream>
#include <string>

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) failures++;
}

static Inventory roundTrip(const Inventory& a) {
    std::ostringstream out;
    a.write(out, "inv");
    Inventory b;
    std::istringstream in(out.str());
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq != std::string::npos) b.read(line.substr(0, eq), line.substr(eq + 1), "inv");
    }
    b.finishLoad();
    return b;
}

int main() {
    if (!gRegistry.load("data/registry/blocks.toml") || !gItems.load(gRegistry, "data/registry/items.toml")) {
        std::printf("could not load the registries (run from the repo root)\n");
        return 1;
    }
    const uint16_t SWORD = gItems.find("iron_sword"), AXE = gItems.find("iron_axe"),
                   CAP = gItems.find("leather_cap"), HELM = gItems.find("iron_helmet"),
                   STONE = gItems.find("stone");
    check(SWORD && AXE && CAP && HELM && STONE, "test items exist");

    std::printf("hotbar\n");
    {
        Inventory inv;
        check(inv.selected == 2, "the right hand starts on hotbar slot 3");
        inv.pressHotbarKey(9);
        check(inv.selected == 9, "key 0 picks slot 10");
        inv.selectNext();
        check(inv.selected == 2, "the wheel wraps from 10 to 3, skipping the off-hand");
        inv.selectPrev();
        check(inv.selected == 9, "and back");
        inv.pressHotbarKey(0);
        check(inv.offhand == -1 && inv.selected == 9, "key 1 on an empty off-hand slot does nothing");
        inv.setSlot(0, STONE, 5);
        inv.setSlot(1, STONE, 5);
        inv.pressHotbarKey(0);
        check(inv.offhand == 0 && !inv.offhandItem().empty(), "key 1 takes off-hand item 1 in hand");
        check(inv.selected == 9, "the right hand stays as it was");
        inv.pressHotbarKey(1);
        check(inv.offhand == 1, "key 2 swaps to off-hand item 2");
        inv.pressHotbarKey(1);
        check(inv.offhand == -1, "pressing 2 again puts it away");
    }

    std::printf("adding\n");
    {
        Inventory inv;
        check(inv.add(STONE, 64) == 0, "64 stone fit");
        check(inv.slots[0].empty() && inv.slots[1].empty() && inv.slots[2].itemID == STONE,
              "new items go to the main hotbar, never the off-hand");
        for (int i = 0; i < Inventory::SLOTS; i++) inv.setSlot(i, 0, 0);
        int cap = std::max<int>(1, gItems.get(STONE).maxStack);
        int left = inv.add(STONE, cap * 49);
        check(left == cap, "48 slots fill and the rest is returned (" + std::to_string(left) + ")");
    }

    std::printf("equipment\n");
    {
        Inventory inv;
        inv.add(SWORD, 1);                    // slot 2
        inv.add(AXE, 1);                      // slot 3
        inv.add(CAP, 1);                      // slot 4
        check(inv.toggleEquip(2) && inv.equip[(int)EquipSlot::Weapon1] == 2, "shift-click equips the sword as weapon 1");
        check(inv.slots[2].itemID == SWORD, "and it stays in its inventory slot");
        check(inv.toggleEquip(3) && inv.equip[(int)EquipSlot::Weapon2] == 3, "the axe goes to the free weapon 2");
        check(!Inventory::fits(AXE, EquipSlot::Head), "an axe is no helmet");
        check(!inv.equipFrom(3, EquipSlot::Head), "so it can't be equipped there");
        check(inv.toggleEquip(4) && inv.worn(WornSlot::Head).itemID == CAP, "the cap goes on the head");
        check(inv.weapon().itemID == SWORD, "weapon 1 is drawn");
        inv.swapWeapons();
        check(inv.weapon().itemID == AXE, "after the swap, weapon 2 (the axe)");
        inv.select(3);
        check(inv.heldDef().id == AXE && inv.weapon().itemID == AXE, "the axe is held and equipped at once");
        inv.move(2, 20);
        check(inv.equip[(int)EquipSlot::Weapon1] == 20 && inv.slots[20].itemID == SWORD, "moving the sword moves its equip link");
        inv.move(4, 20);
        check(inv.equip[(int)EquipSlot::Weapon1] == 4 && inv.worn(WornSlot::Head).itemID == CAP,
              "swapping two equipped items keeps both links right");
        inv.setSlot(4, 0, 0);
        check(inv.equip[(int)EquipSlot::Weapon1] == -1, "a removed item comes off");
        check(inv.toggleEquip(20) && inv.worn(WornSlot::Head).itemID == 0, "shift-click again takes the cap off");
        inv.add(HELM, 1);
        check(inv.wear(HELM) && inv.worn(WornSlot::Head).itemID == HELM, "wear() equips a carried helmet");
        check(inv.count(HELM) == 1, "without adding a second one");
    }

    std::printf("stacks\n");
    {
        Inventory inv;
        inv.setSlot(10, STONE, 40);
        inv.move(10, 11, true);
        check(inv.slots[10].count == 20 && inv.slots[11].count == 20, "a right drag splits the stack");
        inv.move(11, 10);
        check(inv.slots[10].count == 40 && inv.slots[11].empty(), "dragging back stacks them");
        inv.setSlot(12, STONE, 1);
        inv.add(SWORD, 1);
        int s = inv.find(SWORD);
        inv.toggleEquip(s);
        check(inv.take(STONE, 41) && inv.count(STONE) == 0, "take() uses up stacks");
        check(inv.isEquipped(s), "and leaves the equipped sword alone");
    }

    std::printf("save / load\n");
    {
        Inventory a;
        a.add(STONE, 10);
        a.add(SWORD, 1);
        a.add(CAP, 1);
        a.setSlot(1, AXE, 1);
        a.toggleEquip(a.find(SWORD));
        a.toggleEquip(a.find(CAP));
        a.pressHotbarKey(1);
        a.select(5);
        a.swapWeapons();
        Inventory b = roundTrip(a);
        bool same = true;
        for (int i = 0; i < Inventory::SLOTS; i++)
            same = same && a.slots[i].itemID == b.slots[i].itemID && a.slots[i].count == b.slots[i].count;
        check(same, "every slot comes back");
        bool eq = true;
        for (int e = 0; e < EQUIP_SLOTS; e++) eq = eq && a.equip[e] == b.equip[e];
        check(eq, "equipment comes back");
        check(b.offhand == 1 && b.selected == 5 && b.activeWeapon == 1, "hands and the weapon set come back");

        // an old save: 8 toolbar slots, a weapon slot, worn gear
        Inventory o;
        const char* lines[][2] = { { "inv.selected", "1" }, { "inv.slot0", "stone 12" }, { "inv.slot1", "iron_axe 1" },
                                   { "inv.weapon", "iron_sword 1" }, { "inv.worn.head", "leather_cap 1" } };
        for (auto& l : lines) o.read(l[0], l[1], "inv");
        o.finishLoad();
        check(o.slots[2].itemID == STONE && o.slots[2].count == 12 && o.slots[3].itemID == AXE,
              "old toolbar slots 1..8 land in hotbar 3..10");
        check(o.selected == 3, "the old selection moves with them");
        check(o.weapon().itemID == SWORD && o.worn(WornSlot::Head).itemID == CAP, "the old weapon and cap are equipped");
        check(o.count(SWORD) == 1 && o.count(CAP) == 1, "from inventory slots of their own");
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}

// Headless checks for the controller layout and the combat kit:
// what each pad button means in each layer (input/PadLayout.h), the
// weapon sets and X / Y / B lists (game/CombatKit.h), using an item
// through the intent (and over the wire), and the Windburst spell
// (sim/Simulation.h castEffects). Run from the repo root.
#include "TestWorld.h"
#include "input/PadLayout.h"
#include "game/CombatKit.h"
#include "net/Protocol.h"
#include <fstream>
#include <cstdio>
#include <sstream>

using namespace testworld;

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }

    std::printf("the pad layout\n");
    {
        using A = InputAction;
        auto press = [](std::initializer_list<pad::Button> bs, pad::Layer layer, float aHeld = 0.0f, bool aTap = false) {
            pad::State s;
            s.connected = true;
            for (pad::Button b : bs) s.down[b] = true;
            std::array<bool, (int)A::COUNT> held{};
            pad::apply(s, layer, aHeld, aTap, held);
            return held;
        };
        auto c = press({ pad::RB, pad::RT, pad::LT, pad::LB }, pad::Layer::Combat);
        check(c[(int)A::UseLeft] && c[(int)A::UseRight] && c[(int)A::Block] && c[(int)A::Sprint] && c[(int)A::Roll],
              "combat: RB light, RT heavy, LT special, LB sprint and dodge");
        auto f = press({ pad::X, pad::Y, pad::B, pad::Left, pad::Up, pad::Right, pad::Down }, pad::Layer::Combat);
        check(f[(int)A::UseSlotX] && f[(int)A::UseSlotY] && f[(int)A::UseSlotB] && f[(int)A::CycleSlotX] &&
              f[(int)A::CycleSlotY] && f[(int)A::CycleSlotB] && f[(int)A::SwapWeapon],
              "  X / Y / B use, D-pad left / up / right cycle them, down the weapon set");
        check(!f[(int)A::Inventory] && !press({ pad::Start }, pad::Layer::Combat)[(int)A::Pause], "  no menus in combat");
        auto q = press({ pad::X, pad::Down, pad::A }, pad::Layer::Quick);
        check(q[(int)A::QuickLayer] && q[(int)A::ToolbarSlot5] && q[(int)A::ToolbarSlot4] && q[(int)A::ToolbarSlot8] &&
              !q[(int)A::UseSlotX] && !q[(int)A::Jump], "Start held: the D-pad and face buttons pick quick options");
        auto e = press({ pad::LB, pad::RB, pad::LT, pad::RT, pad::B }, pad::Layer::Explore);
        check(e[(int)A::ToolbarPrev] && e[(int)A::ToolbarNext] && e[(int)A::UseLeft] && e[(int)A::UseRight] && e[(int)A::Roll],
              "out of combat: LB / RB the hotbar, LT / RT left and right click, B sprint and roll");
        check(press({ pad::Back }, pad::Layer::Combat)[(int)A::DrawWeapon] && press({ pad::Back }, pad::Layer::Explore)[(int)A::DrawWeapon],
              "Select toggles combat in both");
        check(!press({ pad::A }, pad::Layer::Combat, 0.1f)[(int)A::Jump] && press({}, pad::Layer::Combat, 0.1f, true)[(int)A::Jump],
              "A: a tap jumps when it comes up");
        auto h = press({ pad::A }, pad::Layer::Combat, 0.5f);
        check(h[(int)A::Interact] && !h[(int)A::Jump], "A held: interact, no jump");
        check(pad::layerFor(true, true) == pad::Layer::Quick && pad::layerFor(false, true) == pad::Layer::Explore,
              "the quick layer only in combat");
    }

    std::printf("rebinding the pad\n");
    {
        using A = InputAction;
        // jump (and interact, held) moved to B; the quick layer to Select
        for (pad::Map& m : pad::current()) {
            if (m.action == A::Jump) m.button = pad::B;
            if (m.action == A::QuickLayer) m.button = pad::Back;
        }
        check(pad::jumpButton() == pad::B && pad::quickButton() == pad::Back, "the jump and quick buttons follow the layout");
        pad::State s; s.connected = true;
        std::array<bool, (int)A::COUNT> held{};
        pad::apply(s, pad::Layer::Combat, 0.1f, true, held);
        check(held[(int)A::Jump], "  a tap of B jumps");
        s.down[pad::B] = true; held = {};
        pad::apply(s, pad::Layer::Combat, 0.5f, false, held);
        check(held[(int)A::Interact] && !held[(int)A::Jump], "  holding B interacts");
        check(pad::layerFor(true, true) == pad::Layer::Quick, "  (the quick layer: whichever button is held)");
        pad::resetLayout();
        check(pad::jumpButton() == pad::A && pad::quickButton() == pad::Start && pad::layout().size() == pad::defaults().size(),
              "reset: back to the defaults");
        check(pad::buttonFromName("Select") == pad::Back && pad::buttonFromName("RT") == pad::RT && pad::buttonFromName("Q") < 0,
              "button names read back (pad.toml)");
        // its size, from the PNG header
        int w = 0, h = 0;
        {
            std::ifstream f("data/ui/pad_glyphs.png", std::ios::binary);
            unsigned char hd[24] = {};
            if (f.read((char*)hd, 24)) {
                w = (hd[16] << 24) | (hd[17] << 16) | (hd[18] << 8) | hd[19];
                h = (hd[20] << 24) | (hd[21] << 16) | (hd[22] << 8) | hd[23];
            }
        }
        bool sheet = w >= 8 * 48 && h >= 3 * 48;
        check(sheet, "the button pictures (data/ui/pad_glyphs.png): " + std::to_string(w) + " x " + std::to_string(h));
    }

    std::printf("the combat kit\n");
    const uint16_t SWORD = gItems.find("iron_sword"), CLUB = gItems.find("wooden_club"), WIND = gItems.find("windburst"),
                   TORCH = gItems.find("torch");
    check(SWORD && CLUB && WIND && gItems.get(WIND).is(IC_SPELL) && gItems.get(WIND).cast == "windburst_cast",
          "Windburst is a spell item that casts windburst_cast");
    {
        Inventory inv;
        inv.add(SWORD, 1); inv.add(CLUB, 1); inv.add(WIND, 1); inv.add(TORCH, 4);
        inv.equipFrom(inv.find(SWORD), EquipSlot::Weapon1);
        inv.equipFrom(inv.find(CLUB), EquipSlot::Weapon2);
        CombatKit k;
        k.tidy(inv);
        check(k.primary(inv, 0) == SWORD && k.primary(inv, 1) == CLUB, "loadout 1 and 2: the Weapon 1 and 2 equipment slots");
        check(k.slotItem(0) == WIND && k.sets[1].slots[0][0] == WIND, "  the spells carried start in X, in both");
        check(inv.find(WIND) >= 0 && inv.find(SWORD) >= 0, "  and stay in the inventory");
        inv.swapWeapons();
        k.follow(inv);
        check(k.setAt == 1 && inv.weapon().itemID == CLUB, "switching loadouts draws the other primary");
        k.sets[1].slots[1] = { 0, WIND, TORCH };
        check(k.slotItem(1) == WIND, "Y shows the first item set in it");
        k.cycleSlot(1);
        check(k.slotItem(1) == TORCH, "  the D-pad steps to the next");
        k.cycleSlot(1);
        check(k.slotItem(1) == WIND, "  skipping empty slots, round again");
        uint16_t cast = 0;
        check(k.special(CLUB, &cast) == CombatKit::Special::Guard, "no off hand: LT is the primary's block / parry");
        k.sets[1].off = WIND;
        check(k.special(CLUB, &cast) == CombatKit::Special::Cast && cast == WIND, "  a spell in the off hand: LT casts it");
        check(CombatKit::fitsOff(SWORD) && CombatKit::fitsOff(WIND) && !CombatKit::fitsOff(TORCH), "the off hand takes weapons, shields and spells");
        check(!CombatKit::fitsCharm(TORCH), "charm slots take only charms");
        k.quick[0] = TORCH;
        std::stringstream ss;
        k.write(ss);
        CombatKit back;
        std::string line;
        while (std::getline(ss, line)) { size_t eq = line.find('='); back.read(line.substr(0, eq), line.substr(eq + 1)); }
        check(back.made && back.sets[1].off == WIND && back.sets[1].slots[1][2] == TORCH && back.sets[1].at[1] == 1 &&
              back.quick[0] == TORCH, "saves and loads (player.dat kit.*)");
        inv.slots[inv.find(TORCH)].clear();
        back.tidy(inv);
        check(back.sets[1].slots[1][2] == 0 && back.quick[0] == 0, "an item no longer carried drops out");
    }

    std::printf("the shield\n");
    {
        const uint16_t SHIELD = gItems.find("wooden_shield");
        const ItemDef& sd = gItems.get(SHIELD);
        check(SHIELD && sd.shield && sd.is(IC_SHIELD) && sd.shieldBlock >= 0.99f && !sd.canFight(),
              "Wooden Shield: a shield (stops a whole hit), not a weapon");
        check(CombatKit::fitsOff(SHIELD) && !CombatKit::fitsCharm(SHIELD), "  it goes in a loadout's off hand");
        Inventory inv;
        inv.add(SWORD, 1); inv.add(SHIELD, 1);
        inv.equipFrom(inv.find(SWORD), EquipSlot::Weapon1);
        CombatKit k;
        k.tidy(inv);
        check(k.sets[0].off == SHIELD && k.sets[1].off == SHIELD, "  a new kit puts the shield carried in both off hands");
        check(k.special(SWORD, nullptr) == CombatKit::Special::Guard, "  LT / Q: guard (with it)");

        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        p.inventory.add(SHIELD, 1);
        p.combatStance = true;
        p.kit.sets[0].off = SHIELD;
        p.refreshWeapon();
        check(p.guardStyle.shield && p.shieldUp() == &sd && p.offhandInCombat() == SHIELD, "in combat it's up, in the left hand");
        check(Simulation::offHandOf(p) == sd.heldModel, "  and drawn there (\"" + sd.heldModel + "\")");
        HumanIntent g; g.guard = true;
        w.sim.setIntent(p.uuid, g);
        w.run(0.4f);
        check(p.guarding, "  it guards with no weapon at all");
        p.combatStance = false;
        p.refreshWeapon();
        check(!p.guardStyle.shield, "  out of combat: put away");

        // a sword blow against a weapon's guard and a shield's, past the parry
        auto blow = [&](bool shield, float sideDeg, float& staminaUsed) {
            Human a, b;
            a.position = glm::vec3(0, GROUND, 0);
            float r = glm::radians(sideDeg);
            b.position = glm::vec3(0, GROUND, 6);
            b.facing = glm::vec3(std::sin(r), 0, -std::cos(r));        // 0: facing the attacker
            a.facing = glm::vec3(0, 0, 1);
            a.weaponDamage = 12.0f;
            a.attack.kind = AttackKind::Light;
            a.attack.d = gMovesets.attack("sword_light1");
            a.attack.dir = a.facing;
            b.weaponDamage = 12.0f;
            if (shield) b.setGuard(1.0f - sd.shieldBlock, sd.shieldCost, sd.shieldArc, sd.shieldParry, true);
            b.guarding = true;
            b.guardTime = 1.0f;
            float st = b.stamina;
            combat::HitEvent e = combat::land(a, b);
            staminaUsed = st - b.stamina;
            return e;
        };
        float stW = 0.0f, stS = 0.0f, stSide = 0.0f, stSideW = 0.0f;
        combat::HitEvent ew = blow(false, 0.0f, stW), es = blow(true, 0.0f, stS);
        check(ew.blocked && ew.damage > 0.0f && es.blocked && es.damage == 0.0f,
              "a weapon's guard lets " + std::to_string(ew.damage).substr(0, 4) + " through, the shield none");
        check(stS < stW, "  and the shield costs less stamina (" + std::to_string((int)stS) + " against " + std::to_string((int)stW) + ")");
        combat::HitEvent sw = blow(false, 90.0f, stSideW), ss = blow(true, 90.0f, stSide);
        check(!sw.blocked && ss.blocked, "a blow from the side: past a weapon's guard, not past the shield");
    }

    std::printf("using an item, over the wire\n");
    {
        HumanIntent in;
        in.useItem = WIND; in.move = { 0.0f, 1.0f };
        wire::Writer wr;
        proto::putIntent(wr, in);
        wire::Reader rd(wr.buf);
        HumanIntent out = proto::getIntent(rd);
        check(out.useItem == WIND && glm::length(out.move - in.move) < 1e-4f, "the intent carries the item used");
    }

    std::printf("Windburst\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        p.facing = glm::vec3(0, 0, 1);
        p.inventory.add(WIND, 1);
        w.run(0.3f);
        // bandits: close in front, far in front, behind
        Npc* nearF = w.sim.npcs().spawn("bandit", glm::vec3(256, GROUND, 264), glm::vec3(0, 0, -1));
        Npc* farF  = w.sim.npcs().spawn("bandit", glm::vec3(258, GROUND, 276), glm::vec3(0, 0, -1));
        Npc* behind = w.sim.npcs().spawn("bandit", glm::vec3(256, GROUND, 246), glm::vec3(0, 0, 1));
        nearF->poise = 8.0f;                                  // nearly broken: it goes down
        (void)farF;
        HumanIntent in;
        in.useItem = WIND;
        w.sim.setIntent(p.uuid, in);
        float stam = p.stamina;
        bool knocked = false, hitNear = false, hitFar = false, hitBehind = false;
        float vNear = 0.0f, vFar = 0.0f;                 // how far each is thrown, from where the wind hit it
        glm::vec3 nearAt(0.0f), farAt(0.0f);
        for (int i = 0; i < 60; i++) {
            w.sim.step(DT);
            for (const SimEvent& e : w.sim.takeEvents()) {
                if (e.kind != SimEvent::Kind::Hit || e.a != p.uuid) continue;
                if (e.b == nearF->uuid) { hitNear = true; knocked = knocked || e.knockedDown; nearAt = nearF->position; }
                if (e.b == farF->uuid) { hitFar = true; farAt = farF->position; }
                if (e.b == behind->uuid) hitBehind = true;
            }
            // how fast each is thrown (the push lands on the next step)
            if (hitNear) vNear = std::max(vNear, nearF->position.z - nearAt.z);
            if (hitFar) vFar = std::max(vFar, farF->position.z - farAt.z);
            if (i == 2) check(p.attack.active() && p.attack.kind == AttackKind::Cast, "using it plays the cast");
        }
        check(p.stamina < stam, "  which costs stamina");
        std::printf("    thrown back: near %.1f voxels, far %.1f\n", vNear, vFar);
        check(hitNear && vNear > 4.0f, "the bandit in front is thrown back");
        check(knocked, "  and, low on poise, knocked down");
        check(hitFar && vFar > 0.5f && vFar < vNear, "one farther off is pushed less");
        check(!hitBehind, "one behind isn't touched");
        // without the item: nothing
        Player& q = w.player(glm::vec3(100, GROUND, 100));
        w.run(0.2f);
        HumanIntent qi; qi.useItem = WIND;
        w.sim.setIntent(q.uuid, qi);
        w.sim.step(DT);
        check(!q.attack.active(), "a player without the spell can't cast it");
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}

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
        auto press = [](std::initializer_list<pad::Button> bs, pad::Layer layer) {
            pad::State s;
            s.connected = true;
            for (pad::Button b : bs) s.down[b] = true;
            std::array<bool, (int)A::COUNT> held{};
            pad::apply(s, layer, held);
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
        check(!press({ pad::Start }, pad::Layer::Explore)[(int)A::Pause], "  Start pauses nothing out of combat either (the menu is in the inventory)");
        auto q = press({ pad::X, pad::Down, pad::A }, pad::Layer::Quick);
        check(q[(int)A::QuickLayer] && q[(int)A::ToolbarSlot5] && q[(int)A::ToolbarSlot4] && q[(int)A::ToolbarSlot8] &&
              !q[(int)A::UseSlotX] && !q[(int)A::Jump], "Start held: the D-pad and face buttons pick quick options");
        auto e = press({ pad::LB, pad::Left, pad::Right, pad::Up, pad::LT, pad::RT }, pad::Layer::Explore);
        check(e[(int)A::ToolbarPrev] && e[(int)A::ToolbarNext] && e[(int)A::CycleOffhand] && e[(int)A::UseLeft] && e[(int)A::UseRight] &&
              e[(int)A::Sprint] && e[(int)A::Roll],
              "out of combat: D-pad left / right the hotbar, up the off hand, LT / RT left and right click, LB sprint and roll");
        check(!press({ pad::RB }, pad::Layer::Explore)[(int)A::ToolbarNext] && !press({ pad::B }, pad::Layer::Explore)[(int)A::Sprint],
              "  (the shoulders aren't the hotbar any more; B doesn't sprint)");
        {
            // Select: a tap, on release, draws; with Start it's the inventory
            pad::ChordState cs;
            auto step = [&](std::initializer_list<pad::Button> bs) {
                pad::State st; st.connected = true;
                for (pad::Button b : bs) st.down[b] = true;
                std::array<bool, (int)A::COUNT> h{};
                pad::apply(st, pad::layerFor(false, st.down[pad::quickButton()]), h);
                pad::chord(st, cs, h);
                return h;
            };
            bool downDraws = step({ pad::Back })[(int)A::DrawWeapon];
            bool upDraws = step({})[(int)A::DrawWeapon];
            check(!downDraws && upDraws, "Select: a tap draws, when it comes up");
            bool inv1 = step({ pad::Back })[(int)A::Inventory];
            bool inv2 = step({ pad::Back, pad::Start })[(int)A::Inventory];
            auto up1 = step({ pad::Start });
            auto up2 = step({});
            check(!inv1 && inv2 && !up1[(int)A::DrawWeapon] && !up2[(int)A::DrawWeapon],
                  "Select + Start: the inventory, and no draw when they come up");
            step({ pad::Start });
            bool inv3 = step({ pad::Start, pad::Back })[(int)A::Inventory];
            bool d3 = step({ pad::Start })[(int)A::DrawWeapon];
            step({});
            check(inv3 && !d3, "  either first");
            check(step({ pad::Back })[(int)A::DrawWeapon] == false && step({})[(int)A::DrawWeapon], "  then a plain tap draws again");
        }
        check(press({ pad::A }, pad::Layer::Combat)[(int)A::Jump] && press({ pad::A }, pad::Layer::Explore)[(int)A::Jump] &&
              !press({ pad::A }, pad::Layer::Explore)[(int)A::Interact], "A jumps at once (and only jumps)");
        check(!press({ pad::Y }, pad::Layer::Explore)[(int)A::Inventory], "Y no longer opens the inventory (Select + Start does)");
        {
            // a tool in hand: B, Y and the D-pad are the tool's, the rest stay explore's
            auto t = press({ pad::B, pad::Right, pad::LT, pad::X, pad::LB }, pad::Layer::Tool);
            check(t[(int)A::ToolCancel] && t[(int)A::Sprint] && !t[(int)A::NudgeRight] &&
                  t[(int)A::UseLeft] && t[(int)A::ToolbarNext] && t[(int)A::RotatePiece],
                  "tool: B lets go; the D-pad still picks the tool; LT, X as out of combat, LB sprints");
            auto ta = press({ pad::Y, pad::Up, pad::Left, pad::RB, pad::B }, pad::Layer::ToolAlt);
            check(ta[(int)A::ToolAlt] && ta[(int)A::NudgeForward] && ta[(int)A::NudgeLeft] && ta[(int)A::NudgeUp] && ta[(int)A::NudgeDown] &&
                  !ta[(int)A::ToolbarPrev] && !ta[(int)A::CycleOffhand] && !ta[(int)A::ToolCancel],
                  "  Y held: the D-pad moves the piece, RB / B up / down");
            check(pad::layerFor(false, false, true, false) == pad::Layer::Tool && pad::layerFor(false, false, true, true) == pad::Layer::ToolAlt &&
                  pad::layerFor(true, false, true, true) == pad::Layer::Combat && pad::layerFor(false, true, true, true) == pad::Layer::Quick,
                  "  the tool layers only out of combat, under the quick layer");
        }
        auto it = press({ pad::LB }, pad::Layer::Quick);
        check(it[(int)A::Interact] && !it[(int)A::Sprint] && !it[(int)A::ToolbarPrev], "Start + LB: interact");
        check(pad::layerFor(true, true) == pad::Layer::Quick && pad::layerFor(false, true) == pad::Layer::Quick &&
              pad::layerFor(false, false) == pad::Layer::Explore, "the quick layer in combat or out (Start held)");
    }

    std::printf("rebinding the pad\n");
    {
        using A = InputAction;
        // jump moved to B; the quick layer to Select
        for (pad::Map& m : pad::current()) {
            if (m.action == A::Jump) m.button = pad::B;
            if (m.action == A::QuickLayer) m.button = pad::Back;
        }
        check(pad::jumpButton() == pad::B && pad::quickButton() == pad::Back, "the jump and quick buttons follow the layout");
        pad::State s; s.connected = true;
        std::array<bool, (int)A::COUNT> held{};
        s.down[pad::B] = true;
        pad::apply(s, pad::Layer::Explore, held);
        check(held[(int)A::Jump], "  B jumps");
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
                   TORCH = gItems.find("torch"), ICE = gItems.find("ice_spike"), PICK = gItems.find("iron_pickaxe"),
                   AXE = gItems.find("iron_axe");
    check(SWORD && CLUB && WIND && gItems.get(WIND).is(IC_SPELL) && gItems.get(WIND).spell == "windburst",
          "Windburst is a spell item that casts the windburst spell");
    {
        Inventory inv;
        inv.add(SWORD, 1); inv.add(CLUB, 1); inv.add(WIND, 1); inv.add(TORCH, 4); inv.add(ICE, 1);
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
        k.sets[1].slots[1] = { 0, WIND, ICE };
        check(k.slotItem(1) == WIND, "Y shows the first item set in it");
        k.cycleSlot(1);
        check(k.slotItem(1) == ICE, "  the D-pad steps to the next");
        k.cycleSlot(1);
        check(k.slotItem(1) == WIND, "  skipping empty slots, round again");
        uint16_t cast = 0;
        check(k.special(CLUB, &cast) == CombatKit::Special::Guard, "no off hand: LT is the primary's block / parry");
        k.sets[1].off = WIND;
        check(k.special(CLUB, &cast) == CombatKit::Special::Cast && cast == WIND, "  a spell in the off hand: LT casts it");
        check(CombatKit::fitsOff(SWORD) && CombatKit::fitsOff(WIND) && !CombatKit::fitsOff(TORCH), "the off hand takes weapons, shields and spells");
        check(!CombatKit::fitsCharm(TORCH), "charm slots take only charms");
        check(CombatKit::fitsAbility(ICE) && !CombatKit::fitsAbility(PICK) && !CombatKit::fitsAbility(AXE) &&
              !CombatKit::fitsAbility(TORCH) && !CombatKit::fitsAbility(SWORD), "ability slots: spellbooks, not tools, torches or swords");
        check(Inventory::fits(AXE, EquipSlot::Weapon1) && !Inventory::fits(PICK, EquipSlot::Weapon1) && !CombatKit::fitsOff(AXE) && !CombatKit::fitsOff(PICK),
              "  the axe fits a Weapon slot (a tool that fights), nowhere else; the pickaxe nowhere");
        {
            CombatKit old; old.made = true;
            old.sets[0].slots[1][0] = PICK; old.quick[2] = PICK;
            Inventory with; with.add(PICK, 1);
            old.tidy(with);
            check(!old.sets[0].slots[1][0] && !old.quick[2], "  an old save's tool in an ability slot is taken out");
        }
        check(CombatKit::itemKind(PICK).rfind("Tool -", 0) == 0 && CombatKit::itemKind(AXE).rfind("Tool and weapon", 0) == 0 &&
              CombatKit::itemKind(ICE).rfind("Spellbook", 0) == 0, "each kind has a name shown in game (" + CombatKit::itemKind(AXE) + ")");
        k.quick[0] = ICE;
        std::stringstream ss;
        k.write(ss);
        CombatKit back;
        std::string line;
        while (std::getline(ss, line)) { size_t eq = line.find('='); back.read(line.substr(0, eq), line.substr(eq + 1)); }
        check(back.made && back.sets[1].off == WIND && back.sets[1].slots[1][2] == ICE && back.sets[1].at[1] == 1 &&
              back.quick[0] == ICE, "saves and loads (player.dat kit.*)");
        inv.slots[inv.find(ICE)].clear();
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

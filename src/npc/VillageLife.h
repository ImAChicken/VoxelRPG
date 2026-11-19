#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// VillageLife — where a villager lives, works and goes
// ══════════════════════════════════════════════════════════
// Given to an NPC when a village is built (game/Villagers.h): its
// bed (a home), its job and workplace, and the places it likes to
// go (the square, the well, the church). The "routine" brain state
// (npc/Routine.h) walks it between them by the time of day,
// through each building's door. Saved with the NPC (npcs.dat).
struct LifePlace {
    uint32_t  door = 0;               // the door to go through (a structure id; 0: outdoors)
    glm::vec3 out{ 0.0f };            // standing just outside that door
    glm::vec3 in{ 0.0f };             // … and just inside it
    glm::vec3 spot{ 0.0f };           // where it stands there (a bed: beside it)
    glm::vec3 facing{ 0, 0, 1 };      // which way it faces there (lying: toward its feet)
    bool      lie = false;            // a bed: it lies down here …
    glm::vec3 lieAt{ 0.0f };          // … its feet here (the body drawn lying on the mattress)
    // The way from outside its door to the village square, along the
    // path and roads (Routine walks these between places, not across
    // the village)
    std::vector<glm::vec3> toSquare;
    bool      valid = false;

    void write(std::ostream& f) const {
        f << " " << door << " " << out.x << " " << out.y << " " << out.z << " " << in.x << " " << in.y << " " << in.z
          << " " << spot.x << " " << spot.y << " " << spot.z << " " << facing.x << " " << facing.z << " " << (lie ? 1 : 0);
        if (lie) f << " " << lieAt.x << " " << lieAt.y << " " << lieAt.z;
        f << " " << toSquare.size();
        for (const glm::vec3& p : toSquare) f << " " << p.x << " " << p.y << " " << p.z;
    }
    bool read(std::istream& s) {
        int l = 0;
        if (!(s >> door >> out.x >> out.y >> out.z >> in.x >> in.y >> in.z >> spot.x >> spot.y >> spot.z >> facing.x >> facing.z >> l))
            return false;
        facing.y = 0.0f;
        lie = l != 0;
        if (lie && !(s >> lieAt.x >> lieAt.y >> lieAt.z)) return false;
        size_t n = 0;
        if (!(s >> n)) return false;
        toSquare.clear();
        for (size_t i = 0; i < n && i < 16; i++) { glm::vec3 p; if (!(s >> p.x >> p.y >> p.z)) return false; toSquare.push_back(p); }
        valid = true;
        return true;
    }
};

struct VillageLife {
    bool on = false;
    std::string job;                  // villagers.toml [[job]] id ("" : none, it keeps house)
    std::string title;                // what it's called ("Smith")
    LifePlace bed;                    // home: its bed
    LifePlace work;                   // its workplace (or its home, without a job)
    std::vector<LifePlace> haunts;    // where it goes when it's free: the square, the well, the church
    float lateBy = 0.0f;              // hours: everyone keeps a slightly different day
    glm::vec3 square{ 0.0f };         // the village square: where the roads meet

    // npcs.dat: " life <job> <title> <lateBy> <square> <bed> <work> <n> <haunts…>" (spaces in the title as _)
    void write(std::ostream& f) const {
        if (!on) return;
        std::string t = title.empty() ? "-" : title;
        for (char& c : t) if (c == ' ') c = '_';
        f << " life " << (job.empty() ? "-" : job) << " " << t << " " << lateBy << " " << square.x << " " << square.y << " " << square.z;
        bed.write(f);
        work.write(f);
        f << " " << haunts.size();
        for (const LifePlace& h : haunts) h.write(f);
    }
    bool read(std::istream& s) {
        size_t n = 0;
        if (!(s >> job >> title >> lateBy >> square.x >> square.y >> square.z)) return false;
        if (job == "-") job.clear();
        if (title == "-") title.clear();
        for (char& c : title) if (c == '_') c = ' ';
        if (!bed.read(s) || !work.read(s) || !(s >> n)) return false;
        haunts.clear();
        for (size_t i = 0; i < n && i < 16; i++) { LifePlace h; if (!h.read(s)) return false; haunts.push_back(h); }
        on = true;
        return true;
    }
};

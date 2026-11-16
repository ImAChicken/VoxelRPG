#define GLM_ENABLE_EXPERIMENTAL
#include "AnimClips.h"
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/memory/unique_ptr.h>
#include <toml++/toml.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <unordered_map>

AnimLibrary gAnims;

namespace {

// Our bones by their name in retarget.toml
const char* BONE_KEYS[BONE_COUNT] = {
    "root", "spine", "head",
    "shoulder_l", "elbow_l", "wrist_l",
    "shoulder_r", "elbow_r", "wrist_r",
    "hip_l", "knee_l", "ankle_l",
    "hip_r", "knee_r", "ankle_r",
};

std::string categoryOf(const std::string& n) {
    auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
    if (has("Sword") || has("Shield") || has("Melee") || has("Punch")) return "Combat";
    if (has("Pistol") || has("Spell") || has("Throw")) return "Ranged / magic";
    if (has("Hit") || has("Death") || has("Lay")) return "Reactions";
    if (has("Walk") || has("Jog") || has("Sprint") || has("Crouch") || has("Swim")) return "Locomotion";
    if (has("Jump") || has("Roll") || has("Slide") || has("Climb")) return "Movement";
    if (has("Zombie")) return "Zombie";
    if (has("Farm") || has("Tree") || has("Fixing") || has("PickUp") || has("Chest") || has("Interact") ||
        has("Consume") || has("Carry") || has("Push") || has("Driving")) return "Work";
    if (has("Idle") || has("Sitting") || has("Talking") || has("Dance") || has("Yes") || has("No")) return "Idle / social";
    return "Other";
}

glm::mat3 rotationOf(const ozz::math::Float4x4& m) {
    float c[4][4];
    for (int i = 0; i < 4; i++) ozz::math::StorePtrU(m.cols[i], c[i]);
    glm::vec3 x(c[0][0], c[0][1], c[0][2]), y(c[1][0], c[1][1], c[1][2]), z(c[2][0], c[2][1], c[2][2]);
    return glm::mat3(glm::normalize(x), glm::normalize(y), glm::normalize(z));
}
glm::vec3 positionOf(const ozz::math::Float4x4& m) {
    float c[4];
    ozz::math::StorePtrU(m.cols[3], c);
    return glm::vec3(c[0], c[1], c[2]);
}
// The rotation that turns direction a onto direction b
glm::mat3 arc(glm::vec3 a, glm::vec3 b) {
    a = glm::normalize(a); b = glm::normalize(b);
    float d = glm::clamp(glm::dot(a, b), -1.0f, 1.0f);
    if (d > 0.99999f) return glm::mat3(1.0f);
    glm::vec3 axis = glm::cross(a, b);
    if (glm::length(axis) < 1e-5f) {            // opposite: any perpendicular axis
        axis = glm::cross(a, std::fabs(a.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0));
    }
    return glm::mat3_cast(glm::angleAxis(std::acos(d), glm::normalize(axis)));
}

struct Pack {
    std::string name;
    ozz::animation::Skeleton skeleton;
    std::vector<ozz::animation::Animation> anims;
    // retargeting: per our bone, the source joint and its rest
    // model rotation and aim direction
    std::array<int, BONE_COUNT> joint{};
    std::array<glm::mat3, BONE_COUNT> restRot{};
    std::array<glm::vec3, BONE_COUNT> restAim{};
    std::array<bool, BONE_COUNT> hasAim{};
    int hips = -1;
    glm::vec3 hipsRest{0.0f};
};

bool readSkeleton(const std::string& path, ozz::animation::Skeleton& s) {
    ozz::io::File f(path.c_str(), "rb");
    if (!f.opened()) return false;
    ozz::io::IArchive a(&f);
    if (!a.TestTag<ozz::animation::Skeleton>()) return false;
    a >> s;
    return true;
}
bool readAnimation(const std::string& path, ozz::animation::Animation& anim) {
    ozz::io::File f(path.c_str(), "rb");
    if (!f.opened()) return false;
    ozz::io::IArchive a(&f);
    if (!a.TestTag<ozz::animation::Animation>()) return false;
    a >> anim;
    return true;
}

} // namespace

struct AnimLibrary::Impl {
    std::vector<std::unique_ptr<Pack>> packs;
    struct Ref { Pack* pack; size_t index; };
    std::vector<ClipInfo> infos;
    std::vector<Ref> refs;
    // which source joints each of our bones wants: candidates
    std::array<std::vector<std::pair<std::string, std::string>>, BONE_COUNT> map;
    std::string hipsName = "pelvis";
    std::array<bool, BONE_COUNT> driven{};

    // scratch
    mutable ozz::animation::SamplingJob::Context context;
    mutable std::vector<ozz::math::SoaTransform> locals;
    mutable std::vector<ozz::math::Float4x4> models;

    void readMap(const std::string& path) {
        for (auto& m : map) m.clear();
        try {
            auto tbl = toml::parse_file(path);
            hipsName = tbl["hips"].value_or(std::string("pelvis"));
            for (int b = 0; b < BONE_COUNT; b++) {
                auto* arr = tbl[BONE_KEYS[b]].as_array();
                if (!arr || arr->empty()) continue;
                auto pairOf = [](const toml::array& a) {
                    return std::make_pair(a.size() > 0 ? a[0].value_or(std::string()) : std::string(),
                                          a.size() > 1 ? a[1].value_or(std::string()) : std::string());
                };
                if (arr->get(0)->is_array()) {
                    for (auto& x : *arr) if (auto* a = x.as_array()) map[b].push_back(pairOf(*a));
                } else map[b].push_back(pairOf(*arr));
            }
        } catch (const toml::parse_error& e) {
            std::cerr << path << ": " << e.description() << "\n";
        }
    }

    // Rest pose in model space, and which joints our bones follow
    void prepare(Pack& p) {
        const auto& sk = p.skeleton;
        std::vector<ozz::math::Float4x4> rest(sk.num_joints());
        ozz::animation::LocalToModelJob ltm;
        ltm.skeleton = &sk;
        ltm.input = sk.joint_rest_poses();
        ltm.output = ozz::make_span(rest);
        ltm.Run();
        std::unordered_map<std::string, int> byName;
        auto names = sk.joint_names();
        for (int i = 0; i < sk.num_joints(); i++) byName[names[i]] = i;
        for (int b = 0; b < BONE_COUNT; b++) {
            p.joint[b] = -1;
            for (auto& [j, aim] : map[b]) {
                auto it = byName.find(j);
                if (it == byName.end()) continue;
                p.joint[b] = it->second;
                p.restRot[b] = rotationOf(rest[it->second]);
                p.hasAim[b] = false;
                if (!aim.empty()) {
                    auto ai = byName.find(aim);
                    if (ai != byName.end()) {
                        glm::vec3 d = positionOf(rest[ai->second]) - positionOf(rest[it->second]);
                        if (glm::length(d) > 1e-5f) { p.restAim[b] = glm::normalize(d); p.hasAim[b] = true; }
                    }
                }
                break;
            }
            if (p.joint[b] >= 0) driven[b] = true;
        }
        auto h = byName.find(hipsName);
        p.hips = h == byName.end() ? -1 : h->second;
        if (p.hips >= 0) p.hipsRest = positionOf(rest[p.hips]);
    }
};

AnimLibrary::AnimLibrary() : impl(std::make_unique<Impl>()) {}
AnimLibrary::~AnimLibrary() = default;

int AnimLibrary::load(const std::string& dir, const std::string& retarget) {
    impl = std::make_unique<Impl>();
    impl->readMap(retarget);
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> folders;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.is_directory() && fs::exists(e.path() / "skeleton.ozz")) folders.push_back(e.path());
    std::sort(folders.begin(), folders.end());
    for (auto& folder : folders) {
        auto pack = std::make_unique<Pack>();
        pack->name = folder.filename().string();
        if (!readSkeleton((folder / "skeleton.ozz").string(), pack->skeleton)) {
            std::cerr << "Animations: can't read " << (folder / "skeleton.ozz") << "\n";
            continue;
        }
        std::vector<fs::path> files;
        for (auto& e : fs::directory_iterator(folder, ec))
            if (e.path().extension() == ".ozz" && e.path().filename() != "skeleton.ozz") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (auto& f : files) {
            ozz::animation::Animation a;
            if (!readAnimation(f.string(), a)) { std::cerr << "Animations: can't read " << f << "\n"; continue; }
            if (a.num_tracks() != pack->skeleton.num_joints()) { std::cerr << "Animations: " << f << " doesn't match its skeleton\n"; continue; }
            pack->anims.push_back(std::move(a));
        }
        impl->prepare(*pack);
        for (size_t i = 0; i < pack->anims.size(); i++) {
            ClipInfo c;
            c.name = files[i].stem().string();
            c.pack = pack->name;
            c.id = c.pack + "/" + c.name;
            c.duration = pack->anims[i].duration();
            c.loop = c.name.size() > 5 && c.name.compare(c.name.size() - 5, 5, "_Loop") == 0;
            c.category = categoryOf(c.name);
            impl->infos.push_back(c);
            impl->refs.push_back({ pack.get(), i });
        }
        impl->packs.push_back(std::move(pack));
    }
    std::cout << "Animations: " << impl->infos.size() << " clips in " << impl->packs.size() << " pack(s)\n";
    return (int)impl->infos.size();
}

int AnimLibrary::count() const { return (int)impl->infos.size(); }
const ClipInfo& AnimLibrary::info(int clip) const { return impl->infos[(size_t)clip]; }
bool AnimLibrary::drives(int bone) const { return bone >= 0 && bone < BONE_COUNT && impl->driven[bone]; }

int AnimLibrary::find(const std::string& key) const {
    for (size_t i = 0; i < impl->infos.size(); i++)
        if (impl->infos[i].id == key) return (int)i;
    for (size_t i = 0; i < impl->infos.size(); i++)
        if (impl->infos[i].name == key) return (int)i;
    return -1;
}

bool AnimLibrary::sample(int clip, float time, const CharacterModel& m,
                         std::array<glm::vec3, BONE_COUNT>& rot, glm::vec3& rootOffset) const {
    if (clip < 0 || clip >= count()) return false;
    const Impl::Ref& r = impl->refs[(size_t)clip];
    Pack& p = *r.pack;
    const ozz::animation::Animation& anim = p.anims[r.index];
    const auto& sk = p.skeleton;
    float dur = std::max(anim.duration(), 1e-4f);
    float t = impl->infos[(size_t)clip].loop ? std::fmod(std::fmod(time, dur) + dur, dur) : std::clamp(time, 0.0f, dur);

    impl->context.Resize(anim.num_tracks());
    impl->locals.resize((size_t)sk.num_soa_joints());
    impl->models.resize((size_t)sk.num_joints());
    ozz::animation::SamplingJob sj;
    sj.animation = &anim;
    sj.context = &impl->context;
    sj.ratio = t / dur;
    sj.output = ozz::make_span(impl->locals);
    if (!sj.Run()) return false;
    ozz::animation::LocalToModelJob ltm;
    ltm.skeleton = &sk;
    ltm.input = ozz::make_span(impl->locals);
    ltm.output = ozz::make_span(impl->models);
    if (!ltm.Run()) return false;

    // each of our bones: the source joint's turn from rest,
    // applied after lining our bone up with the source bone
    std::array<glm::mat3, BONE_COUNT> world;
    for (int b = 0; b < BONE_COUNT; b++) {
        const auto& def = m.bones[b];
        glm::mat3 parentWorld = def.parent >= 0 ? world[def.parent] : glm::mat3(1.0f);
        if (p.joint[b] < 0) {
            // not driven: keep what the caller had
            const glm::vec3& e = rot[b];
            glm::mat3 local = glm::mat3(glm::eulerAngleYXZ(glm::radians(e.y), glm::radians(e.x), glm::radians(e.z)));
            world[b] = parentWorld * local;
            continue;
        }
        glm::mat3 now = rotationOf(impl->models[(size_t)p.joint[b]]);
        glm::mat3 delta = now * glm::transpose(p.restRot[b]);
        glm::mat3 align(1.0f);
        if (p.hasAim[b]) {
            // our bone's direction at rest (model space: our rest
            // rotations are all zero)
            glm::vec3 ours(0, -1, 0);
            for (int c = 0; c < BONE_COUNT; c++)
                if (m.bones[c].parent == b && glm::length(m.bones[c].offset) > 1e-4f) {
                    if (b == B_ROOT && c != B_SPINE) continue;          // the root aims up the spine
                    if (b == B_SPINE && c != B_HEAD) continue;          // the spine aims at the head
                    ours = glm::normalize(m.bones[c].offset);
                    break;
                }
            align = arc(ours, p.restAim[b]);
        }
        world[b] = delta * align;
        glm::mat3 local = glm::transpose(parentWorld) * world[b];
        float y, x, z;
        glm::extractEulerAngleYXZ(glm::mat4(local), y, x, z);
        rot[b] = glm::degrees(glm::vec3(x, y, z));
    }
    if (p.hips >= 0 && p.hipsRest.y > 1e-4f) {
        float scale = m.bones[B_ROOT].offset.y / p.hipsRest.y;     // their hip height → ours (inches)
        rootOffset = (positionOf(impl->models[(size_t)p.hips]) - p.hipsRest) * scale;
    }
    return true;
}

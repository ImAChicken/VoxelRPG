# Importing animation packs

Animations are ozz-animation files in `data/animations/<pack>/`:
`skeleton.ozz` plus one `<Clip>.ozz` per clip. Every folder with a
`skeleton.ozz` is loaded at startup (and on F6) and its clips appear
in the Character screen's Animations tab.

## 1. Build the converter

Configure with the tools on (they're off by default):

    cmake -B build-tools -DVOXELRPG_ANIM_TOOLS=ON
    cmake --build build-tools --target gltf2ozz

The tool is under `build-tools/_deps/ozz-build/src/animation/offline/gltf/`.

## 2. Convert a glTF / GLB pack

The pack should be a humanoid with a T-pose (or A-pose) rest, Y up,
facing +Z. Quaternius UAL and most Mixamo-style rigs work. Use the
version *without* root motion (UAL's plain `.glb`, not `_RM`).

    mkdir data/animations/mypack && cd data/animations/mypack
    gltf2ozz --file=../../../MyPack.glb --config='{"skeleton":{"filename":"skeleton.ozz"},"animations":[{"clip":"*","filename":"*.ozz"}]}'

## 3. Map the joints

`data/animations/retarget.toml` maps each of our 15 bones to a
source joint and the joint it points at. The UE-mannequin names
(pelvis, spine_03, upperarm_l …) are there already. For a rig with
other names add them as extra candidates; the first one found in
the skeleton is used:

    shoulder_l = [["upperarm_l", "lowerarm_l"], ["mixamorig:LeftArm", "mixamorig:LeftForeArm"]]

`hips` names the joint whose movement becomes the root offset.

## 4. Use clips in the game

`data/animations/anim_map.toml` says which clip plays for each state
(`[locomotion]`), each sword attack (`[attacks]`), and how fast each
moving clip walks at normal speed (`[speeds]`, vox/s; measure it
from a root-motion version of the clip). A clip name ending in
`_Loop` loops. Press F6 in game to reload.

Keep the pack's licence file in `data/animations/`.

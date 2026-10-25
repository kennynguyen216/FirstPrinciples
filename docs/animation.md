# Skeletal meshes and animation

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Skeletal animation changes a mesh’s pose using bones and animation clips. The C++ state machine
coordinates transitions such as idle, walking and jumping without embedding those rules in the
renderer.

## Usage and behavior

A SkeletalMesh component draws a `.gltf` or `.glb` with a skin. Its bones bend the mesh on the GPU,
up to 128 bones and four per vertex. glTF primitives become mesh sections named after their glTF
materials, so the Material component's slots work the same as for `.obj` files. Dropping a skinned
glTF into the viewport makes a skeletal mesh with an Animator that loops its first clip.

The Animator loops its `Clip` while the game plays, at `Speed`. The Details panel lists the clips
the mesh has. For anything more, build a state machine from a C++ script:

```cpp
void OnStart() override
{
	Animator* animator = GetAnimator();
	animator->AddState("Idle", "Idle");
	animator->AddState("Walk", "Walk");
	animator->AddState("Jump", "Jump", false); // plays once and holds its last frame
	animator->AddTransition("Idle", "Walk", 0.2f).When("Speed", AnimCompare::Greater, 0.1f);
	animator->AddTransition("Walk", "Idle", 0.25f).When("Speed", AnimCompare::Less, 0.1f);
	animator->AddTransition("", "Jump", 0.1f).When("Jump", AnimCompare::Triggered); // from any state
	animator->AddTransition("Jump", "Idle").AfterExitTime(1);
}

void OnUpdate(float) override
{
	GetAnimator()->SetFloat("Speed", speed);
	if (Input::WasKeyPressed(SDL_SCANCODE_SPACE))
		GetAnimator()->SetTrigger("Jump");
}
```

- The first state added plays first. Each transition blends over its time in seconds.
- Parameters are floats. `SetBool` stores 1 or 0. A trigger stays set until a transition uses it.
- All of a transition's conditions must pass. `AfterExitTime(fraction)` also waits for that much of
  the clip, so `1` leaves at the end.
- `Play(state)` jumps straight to a state. `IsInState`, `GetNormalizedTime` and `GetClipLength` read back.
- Adding a state or transition that already exists replaces it, so `OnStart` can run again after a
  hot reload. Animators live while the game plays and reset when it stops. The editor shows the
  bind pose.

The `Noodle` in `Showcase3D.scene` uses `Source/Scripts/NoodleController.cpp`: WASD or the arrows
walk it and Space jumps. `Scripts/GenerateSampleCharacter.py` writes its `Characters/Noodle.gltf`.

## How it works

SkeletalModel loads skins and clips from glTF. Animator evaluates the selected states and blends
transitions. The renderer uploads each animated object’s bone matrices and skins its vertices on the
GPU. Animated meshes retain independent pose data and draw calls.

Source: [SkeletalModel.h](../src/Engine/Animation/SkeletalModel.h),
[Animator.h](../src/Engine/Animation/Animator.h), [Renderer.cpp](../src/Engine/Render/Renderer.cpp).

## Related guides

[Mesh sections](meshes-and-cameras.md), [Animation control scripts](scripting.md), [Batching
limits](batching.md).

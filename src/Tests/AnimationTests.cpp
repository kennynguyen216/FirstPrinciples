#include "Tests/TestFramework.h"
#include "Engine/Animation/SkeletalModel.h"

#include <cmath>

using namespace FP;

namespace
{
	bool Near(Vec3 a, Vec3 b) { return (a - b).Length() < 1e-4f; }

	// A root at the origin with a child one unit up, like an arm pointing at the sky.
	Skeleton MakeArm()
	{
		Skeleton skeleton;
		Bone root;
		root.Name = "Root";
		skeleton.Bones.Add(root);
		Bone tip;
		tip.Name = "Tip";
		tip.Parent = 0;
		tip.Rest.Translation = { 0, 1, 0 };
		tip.InverseBind = Mat4::Translation({ 0, -1, 0 });
		skeleton.Bones.Add(tip);
		return skeleton;
	}
}

FP_TEST(Animation_SkinMatricesAreIdentityAtRest)
{
	const Skeleton arm = MakeArm();
	FP_EXPECT(arm.FindBone("Tip") == 1);
	FP_EXPECT(arm.FindBone("Missing") == -1);

	Array<Mat4> skin;
	arm.ComputeSkinMatrices(arm.GetRestPose(), skin);
	FP_EXPECT(skin.Num() == 2);
	for (const Mat4& m : skin)
		FP_EXPECT(Near(m.TransformPoint({ 1, 2, 3 }), { 1, 2, 3 }));

	// Turning the root 90 degrees about Z swings a vertex at the tip over to -X.
	Pose pose = arm.GetRestPose();
	pose[0].Rotation = Quat::AxisAngle({ 0, 0, 1 }, 90);
	arm.ComputeSkinMatrices(pose, skin);
	FP_EXPECT(Near(skin[1].TransformPoint({ 0, 1, 0 }), { -1, 0, 0 }));
}

FP_TEST(Animation_ClipSamplesBetweenKeys)
{
	const Skeleton arm = MakeArm();
	AnimationClip clip;
	clip.Duration = 2;
	BoneTrack& track = clip.Tracks.Emplace();
	track.Bone = 0;
	track.RotationTimes = { 0, 2 };
	track.Rotations = { Quat(), Quat::AxisAngle({ 0, 1, 0 }, 90) };
	track.TranslationTimes = { 0, 1, 2 };
	track.Translations = { { 0, 0, 0 }, { 0, 4, 0 }, { 0, 0, 0 } };

	Pose pose = arm.GetRestPose();
	clip.Sample(1, pose);
	FP_EXPECT(Near(pose[0].Rotation.ToEuler(), { 0, 45, 0 }));
	FP_EXPECT(Near(pose[0].Translation, { 0, 4, 0 }));
	clip.Sample(0.5f, pose);
	FP_EXPECT(Near(pose[0].Translation, { 0, 2, 0 }));
	// Untouched bones keep their pose and times past the end hold the last key.
	FP_EXPECT(Near(pose[1].Translation, { 0, 1, 0 }));
	clip.Sample(10, pose);
	FP_EXPECT(Near(pose[0].Rotation.ToEuler(), { 0, 90, 0 }));

	Pose blended;
	Pose rest = arm.GetRestPose();
	BlendPoses(rest, pose, 0.5f, blended);
	FP_EXPECT(Near(blended[0].Rotation.ToEuler(), { 0, 45, 0 }));
}

namespace
{
	// Two bones, Root and Tip one unit above it, with the joints listed tip first.
	// One triangle: two corners on Root and one at y 2 on Tip. Bend turns Tip 90 degrees about Z.
	const char* const TestGltf = R"gltf({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,2]}],"nodes":[{"name":"Root","children":[1]},{"name":"Tip","translation":[0,1,0]},{"name":"Body","mesh":0,"skin":0}],"skins":[{"joints":[1,0],"inverseBindMatrices":5}],"materials":[{"name":"Skin"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"JOINTS_0":2,"WEIGHTS_0":3},"indices":4,"material":0}]}],"animations":[{"name":"Bend","samplers":[{"input":6,"output":7}],"channels":[{"sampler":0,"target":{"node":1,"path":"rotation"}}]}],"buffers":[{"byteLength":320,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAPwAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AQAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAABAAIAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAvwAAAAAAAIA/AACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAA8wQ1P/MENT8="}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":48},{"buffer":0,"byteOffset":144,"byteLength":6},{"buffer":0,"byteOffset":152,"byteLength":128},{"buffer":0,"byteOffset":280,"byteLength":8},{"buffer":0,"byteOffset":288,"byteLength":32}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[0.5,2,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5123,"count":3,"type":"VEC4"},{"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"},{"bufferView":4,"componentType":5123,"count":3,"type":"SCALAR"},{"bufferView":5,"componentType":5126,"count":2,"type":"MAT4"},{"bufferView":6,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},{"bufferView":7,"componentType":5126,"count":2,"type":"VEC4"}]})gltf";
}

FP_TEST(Animation_LoadsSkinnedGltf)
{
	SkeletalModel model;
	FP_EXPECT(ParseGltf(TestGltf, model));
	FP_EXPECT(model.IsSkinned());
	FP_EXPECT(model.Mesh.Vertices.Num() == 3 && model.Skin.Num() == 3);
	FP_EXPECT(model.Mesh.Sections.Num() == 1 && model.Mesh.Sections[0].Name == "Skin");

	// Parents come first whatever order the file lists them in.
	FP_EXPECT(model.Bones.Bones.Num() == 2);
	FP_EXPECT(model.Bones.Bones[0].Name == "Root" && model.Bones.Bones[0].Parent == -1);
	FP_EXPECT(model.Bones.Bones[1].Name == "Tip" && model.Bones.Bones[1].Parent == 0);
	FP_EXPECT(model.Skin[0].Bones[0] == 0 && model.Skin[2].Bones[0] == 1);

	Array<Mat4> skin;
	Pose pose = model.Bones.GetRestPose();
	model.Bones.ComputeSkinMatrices(pose, skin);
	FP_EXPECT(Near(skin[1].TransformPoint({ 0, 2, 0 }), { 0, 2, 0 }));

	const AnimationClip* bend = model.FindClip("Bend");
	FP_EXPECT(bend && bend->Duration == 1);
	FP_EXPECT(!model.FindClip("Missing"));
	bend->Sample(1, pose);
	model.Bones.ComputeSkinMatrices(pose, skin);
	FP_EXPECT(Near(skin[1].TransformPoint({ 0, 2, 0 }), { -1, 1, 0 }));

	FP_EXPECT(!ParseGltf("not gltf", model));
}

#include "Engine/Animation/AnimationStateMachine.h"

namespace
{
	// Idle and Walk loop for one second, Jump plays once over half a second.
	float ClipLength(std::string_view clip)
	{
		return clip == "JumpClip" ? 0.5f : clip.empty() ? 0.0f : 1.0f;
	}

	AnimationStateMachine MakeLocomotion()
	{
		AnimationStateMachine machine;
		machine.AddState("Idle", "IdleClip");
		machine.AddState("Walk", "WalkClip");
		machine.AddState("Jump", "JumpClip", false);
		machine.AddTransition("Idle", "Walk", 0.25f).When("Speed", AnimCompare::Greater, 0.1f);
		machine.AddTransition("Walk", "Idle", 0.25f).When("Speed", AnimCompare::Less, 0.1f);
		machine.AddTransition("", "Jump", 0.1f).When("Jump", AnimCompare::Triggered);
		machine.AddTransition("Jump", "Idle", 0.2f).AfterExitTime(1);
		return machine;
	}
}

FP_TEST(Animation_StateMachineFollowsParameters)
{
	AnimationStateMachine machine = MakeLocomotion();
	machine.Update(0.1f, ClipLength);
	FP_EXPECT(machine.GetCurrentState()->Name == "Idle");

	// A float condition starts a blend that finishes after its blend time.
	machine.SetFloat("Speed", 2);
	machine.Update(0.1f, ClipLength);
	FP_EXPECT(machine.GetCurrentState()->Name == "Walk");
	FP_EXPECT(machine.GetPreviousState()->Name == "Idle");
	FP_EXPECT(machine.GetBlend() == 0);
	machine.Update(0.125f, ClipLength);
	FP_EXPECT(std::fabs(machine.GetBlend() - 0.5f) < 1e-4f);
	machine.Update(0.2f, ClipLength);
	FP_EXPECT(machine.GetBlend() == 1 && !machine.GetPreviousState());

	// Looping clips wrap their time.
	machine.Update(0.9f, ClipLength);
	FP_EXPECT(std::fabs(machine.GetCurrent().Time - 0.225f) < 1e-4f);

	// A trigger is used up by the transition that takes it.
	machine.SetTrigger("Jump");
	machine.Update(0.01f, ClipLength);
	FP_EXPECT(machine.GetCurrentState()->Name == "Jump");
	FP_EXPECT(!machine.GetBool("Jump"));
	machine.Update(0.01f, ClipLength);
	FP_EXPECT(machine.GetCurrentState()->Name == "Jump");

	// Jump holds its last frame, then leaves on exit time even though Speed is still high.
	machine.Update(0.3f, ClipLength);
	FP_EXPECT(machine.GetCurrentState()->Name == "Jump");
	machine.Update(0.3f, ClipLength);
	FP_EXPECT(machine.GetCurrentState()->Name == "Idle");
	FP_EXPECT(machine.GetPrevious().Time == 0.5f);
	machine.Update(0.01f, ClipLength);
	FP_EXPECT(machine.GetCurrentState()->Name == "Walk");
}

FP_TEST(Animation_StateMachineRebuildsInPlace)
{
	AnimationStateMachine machine = MakeLocomotion();
	machine.Update(0.1f, ClipLength);
	machine.Play("Walk");

	// Building again, as a hot reload does, keeps one of each and the current state.
	AnimationStateMachine rebuilt = MakeLocomotion();
	machine.AddState("Idle", "IdleClip");
	machine.AddTransition("Idle", "Walk", 0.5f).When("Speed", AnimCompare::Greater, 0.1f);
	FP_EXPECT(machine.GetStates().Num() == rebuilt.GetStates().Num());
	FP_EXPECT(machine.GetTransitions().Num() == rebuilt.GetTransitions().Num());
	FP_EXPECT(machine.GetTransitions()[0].BlendTime == 0.5f && machine.GetTransitions()[0].Conditions.Num() == 1);
	FP_EXPECT(machine.GetCurrentState()->Name == "Walk");

	// Unknown states are ignored and transitions without conditions or exit time never fire.
	machine.Play("Missing");
	FP_EXPECT(machine.GetCurrentState()->Name == "Walk");
	machine.AddTransition("Walk", "Jump");
	machine.SetFloat("Speed", 2);
	machine.Update(5, ClipLength);
	FP_EXPECT(machine.GetCurrentState()->Name == "Walk");
}

#include "Engine/Animation/Animator.h"
#include "Engine/Engine.h"

FP_TEST(Animation_AnimatorBlendsClipsIntoPoses)
{
	SkeletalModel model;
	FP_EXPECT(ParseGltf(TestGltf, model));

	// Without states the fallback clip loops.
	AnimatorInstance animator;
	animator.Tick(model, 0.5f, "Bend");
	FP_EXPECT(Near(animator.GetPose()[1].Rotation.ToEuler(), { 0, 0, 45 }));
	animator.Tick(model, 1.0f, "Bend");
	FP_EXPECT(Near(animator.GetPose()[1].Rotation.ToEuler(), { 0, 0, 45 }));

	// Halfway through a blend from the rest pose to the end of Bend. Scripts see the base class.
	Animator& script = animator;
	script.AddState("Rest", "");
	script.AddState("Bent", "Bend", false);
	script.AddTransition("Rest", "Bent", 1).When("Go");
	animator.Tick(model, 0, "");
	FP_EXPECT(animator.IsInState("Rest"));
	script.SetBool("Go", true);
	animator.Tick(model, 0, "");
	FP_EXPECT(animator.IsInState("Bent"));
	animator.Tick(model, 0.5f, "");
	FP_EXPECT(Near(animator.GetPose()[1].Rotation.ToEuler(), { 0, 0, 22.5f }));
	FP_EXPECT(std::fabs(animator.GetNormalizedTime() - 0.5f) < 1e-4f);
	FP_EXPECT(animator.GetClipLength("Bend") == 1);
	animator.Tick(model, 1, "");
	FP_EXPECT(Near(animator.GetPose()[1].Rotation.ToEuler(), { 0, 0, 90 }));

	// Worlds hand out one animator per entity that has an Animator component.
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity plain = world->CreateEntity("Plain");
	const Entity animated = world->CreateEntity("Animated");
	world->Add<AnimatorComponent>(animated);
	FP_EXPECT(!world->GetAnimator(plain));
	Animator* first = world->GetAnimator(animated);
	FP_EXPECT(first && first == world->GetAnimator(animated));
}

#include "Engine/Animation/AnimationSystem.h"

FP_TEST(Animation_SystemPosesSampleCharacter)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity noodle = world->CreateEntity("Noodle");
	world->Add<SkeletalMeshComponent>(noodle).Mesh = "Characters/Noodle.gltf";
	world->Add<AnimatorComponent>(noodle).Clip = "Walk";
	const Entity still = world->CreateEntity("Still");
	world->Add<SkeletalMeshComponent>(still).Mesh = "Characters/Noodle.gltf";

	world->AddSystem<AnimationSystem>();
	world->BeginPlay();
	world->Update(0.2f);
	const SkeletalPoseComponent* pose = world->TryGet<SkeletalPoseComponent>(noodle);
	FP_EXPECT(pose && pose->Skin.Num() == 5);
	// Walking moves the head away from where it was bound.
	FP_EXPECT(pose && !Near(pose->Skin[4].TransformPoint({ 0, 1.6f, 0 }), { 0, 1.6f, 0 }));
	// Without an Animator a skeletal mesh keeps its bind pose.
	FP_EXPECT(!world->Has<SkeletalPoseComponent>(still));

	world->EndPlay();
	FP_EXPECT(!world->Has<SkeletalPoseComponent>(noodle));
}

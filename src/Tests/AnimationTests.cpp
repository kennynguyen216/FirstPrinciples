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

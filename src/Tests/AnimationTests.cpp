#include "Tests/TestFramework.h"
#include "Engine/Animation/AnimationClip.h"

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

#include "Engine/Animation/Skeleton.h"

namespace FP
{
	BoneTransform BoneTransform::Blend(const BoneTransform& a, const BoneTransform& b, float t)
	{
		BoneTransform out;
		out.Translation = a.Translation + (b.Translation - a.Translation) * t;
		out.Rotation = Quat::Slerp(a.Rotation, b.Rotation, t);
		out.Scale = a.Scale + (b.Scale - a.Scale) * t;
		return out;
	}

	int32 Skeleton::FindBone(std::string_view name) const
	{
		return Bones.FindIf([&](const Bone& bone) { return bone.Name == name; });
	}

	Pose Skeleton::GetRestPose() const
	{
		Pose pose;
		pose.Reserve(Bones.Num());
		for (const Bone& bone : Bones)
			pose.Add(bone.Rest);
		return pose;
	}

	void Skeleton::ComputeModelMatrices(const Pose& pose, Array<Mat4>& out) const
	{
		out.Resize(Bones.Num());
		for (int32 i = 0; i < Bones.Num(); ++i)
		{
			const Mat4 local = pose.IsValidIndex(i) ? pose[i].ToMatrix() : Bones[i].Rest.ToMatrix();
			// Parents come first, so theirs is already done.
			const int32 parent = Bones[i].Parent;
			out[i] = (parent >= 0 ? out[parent] : Root) * local;
		}
	}

	void Skeleton::ComputeSkinMatrices(const Pose& pose, Array<Mat4>& out) const
	{
		ComputeModelMatrices(pose, out);
		for (int32 i = 0; i < Bones.Num(); ++i)
			out[i] = out[i] * Bones[i].InverseBind;
	}

	void BlendPoses(const Pose& from, const Pose& to, float t, Pose& out)
	{
		const int32 count = from.Num() < to.Num() ? from.Num() : to.Num();
		out.Resize(count);
		for (int32 i = 0; i < count; ++i)
			out[i] = BoneTransform::Blend(from[i], to[i], t);
	}
}

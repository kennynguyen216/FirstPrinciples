#pragma once

#include "Core/CoreMinimal.h"
#include "Engine/Math.h"

namespace FP
{
	// One bone's transform relative to its parent.
	struct BoneTransform
	{
		Vec3 Translation;
		Quat Rotation;
		Vec3 Scale{ 1, 1, 1 };

		Mat4 ToMatrix() const { return Mat4::TRS(Translation, Rotation, Scale); }

		// t is 0 at a and 1 at b.
		static BoneTransform Blend(const BoneTransform& a, const BoneTransform& b, float t);
	};

	struct Bone
	{
		String Name;
		int32 Parent = -1; // always lower than the bone's own index, -1 for roots
		BoneTransform Rest;
		Mat4 InverseBind; // mesh space to bone space when the mesh was bound
	};

	// Local transforms for every bone of a skeleton, in bone order.
	using Pose = Array<BoneTransform>;

	// The bone hierarchy a skeletal mesh is bound to.
	class Skeleton
	{
	public:
		Array<Bone> Bones;
		Mat4 Root; // places the root bones in mesh space

		// -1 when no bone has the name.
		int32 FindBone(std::string_view name) const;

		Pose GetRestPose() const;

		// Bone to mesh space for every bone.
		void ComputeModelMatrices(const Pose& pose, Array<Mat4>& out) const;

		// What each vertex is multiplied by, weighted by bone. Identity in the bind pose.
		void ComputeSkinMatrices(const Pose& pose, Array<Mat4>& out) const;
	};

	// Blends every bone. from, to and out may be the same pose.
	void BlendPoses(const Pose& from, const Pose& to, float t, Pose& out);
}

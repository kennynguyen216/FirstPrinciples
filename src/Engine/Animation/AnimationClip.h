#pragma once

#include "Engine/Animation/Skeleton.h"

namespace FP
{
	// Keys for one bone. Each part has its own times so a track can key rotation alone.
	struct BoneTrack
	{
		int32 Bone = -1;
		Array<float> TranslationTimes;
		Array<Vec3> Translations;
		Array<float> RotationTimes;
		Array<Quat> Rotations;
		Array<float> ScaleTimes;
		Array<Vec3> Scales;
	};

	// Keyframes for some bones of one skeleton, linearly blended between keys.
	struct AnimationClip
	{
		String Name;
		float Duration = 0; // seconds
		Array<BoneTrack> Tracks;

		// Writes the keyed bones into pose and leaves the others alone. time is clamped to the clip.
		void Sample(float time, Pose& pose) const;
	};
}

#include "Engine/Animation/AnimationClip.h"

namespace FP
{
	namespace
	{
		// The key at or before time and how far toward the next one it is.
		void FindKey(const Array<float>& times, float time, int32& key, float& t)
		{
			key = 0;
			t = 0;
			if (times.Num() < 2 || time <= times[0])
				return;
			if (time >= times.Last())
			{
				key = times.Num() - 1;
				return;
			}
			int32 low = 0;
			int32 high = times.Num() - 1;
			while (high - low > 1)
			{
				const int32 middle = (low + high) / 2;
				if (times[middle] <= time)
					low = middle;
				else
					high = middle;
			}
			key = low;
			const float span = times[high] - times[low];
			t = span > 0 ? (time - times[low]) / span : 0;
		}

		Vec3 SampleVec3(const Array<float>& times, const Array<Vec3>& values, float time)
		{
			int32 key;
			float t;
			FindKey(times, time, key, t);
			if (key + 1 >= values.Num())
				return values[key < values.Num() ? key : values.Num() - 1];
			return values[key] + (values[key + 1] - values[key]) * t;
		}

		Quat SampleQuat(const Array<float>& times, const Array<Quat>& values, float time)
		{
			int32 key;
			float t;
			FindKey(times, time, key, t);
			if (key + 1 >= values.Num())
				return values[key < values.Num() ? key : values.Num() - 1];
			return Quat::Slerp(values[key], values[key + 1], t);
		}
	}

	void AnimationClip::Sample(float time, Pose& pose) const
	{
		time = time < 0 ? 0 : time > Duration ? Duration : time;
		for (const BoneTrack& track : Tracks)
		{
			if (!pose.IsValidIndex(track.Bone))
				continue;
			BoneTransform& bone = pose[track.Bone];
			if (!track.Translations.IsEmpty())
				bone.Translation = SampleVec3(track.TranslationTimes, track.Translations, time);
			if (!track.Rotations.IsEmpty())
				bone.Rotation = SampleQuat(track.RotationTimes, track.Rotations, time);
			if (!track.Scales.IsEmpty())
				bone.Scale = SampleVec3(track.ScaleTimes, track.Scales, time);
		}
	}
}

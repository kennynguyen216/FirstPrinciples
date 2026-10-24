#include "Engine/Animation/Animator.h"

#include <cmath>

namespace FP
{
	float AnimatorInstance::GetNormalizedTime() const
	{
		const AnimState* state = m_Machine.GetCurrentState();
		const float length = state ? GetClipLength(state->Clip.View()) : 0;
		return length > 0 ? m_Machine.GetCurrent().Played / length : 0;
	}

	float AnimatorInstance::GetClipLength(std::string_view clip) const
	{
		const AnimationClip* found = m_Model ? m_Model->FindClip(clip) : nullptr;
		return found ? found->Duration : 0;
	}

	void AnimatorInstance::Tick(const SkeletalModel& model, float deltaTime, std::string_view fallbackClip)
	{
		m_Model = &model;
		m_Pose = model.Bones.GetRestPose();

		if (!m_Machine.HasStates())
		{
			// No state machine, so just loop the component's clip.
			m_FallbackTime += deltaTime;
			if (const AnimationClip* clip = model.FindClip(fallbackClip); clip && clip->Duration > 0)
				clip->Sample(std::fmod(m_FallbackTime, clip->Duration), m_Pose);
			return;
		}

		m_Machine.Update(deltaTime, [this](std::string_view clip) { return GetClipLength(clip); });
		if (const AnimState* state = m_Machine.GetCurrentState())
		{
			if (const AnimationClip* clip = model.FindClip(state->Clip.View()))
				clip->Sample(m_Machine.GetCurrent().Time, m_Pose);
		}
		// Mid transition, the old state fades out under the new one.
		if (const AnimState* previous = m_Machine.GetPreviousState())
		{
			m_BlendFrom = model.Bones.GetRestPose();
			if (const AnimationClip* clip = model.FindClip(previous->Clip.View()))
				clip->Sample(m_Machine.GetPrevious().Time, m_BlendFrom);
			BlendPoses(m_BlendFrom, m_Pose, m_Machine.GetBlend(), m_Pose);
		}
	}

	AnimatorInstance* AnimatorInstance::For(World& world, Entity entity)
	{
		if (!world.IsValid(entity) || !world.Has<AnimatorComponent>(entity))
			return nullptr;
		AnimatorStateComponent& state = world.GetRegistry().get_or_emplace<AnimatorStateComponent>(entity);
		if (!state.Instance)
			state.Instance = std::make_unique<AnimatorInstance>();
		return state.Instance.get();
	}
}

#include "Engine/Animation/AnimationSystem.h"
#include "Engine/Animation/Animator.h"

namespace FP
{
	void AnimationSystem::Update(World& world, float deltaTime)
	{
		entt::registry& registry = world.GetRegistry();
		for (auto [entity, skeletal, animator] : world.View<SkeletalMeshComponent, AnimatorComponent>().each())
		{
			const SkeletalModel* model = SkeletalModelLibrary::Get().Load(skeletal.Mesh);
			AnimatorInstance* instance = AnimatorInstance::For(world, entity);
			if (!model || !model->IsSkinned() || !instance)
				continue;
			instance->Tick(*model, animator.Playing ? deltaTime * animator.Speed : 0, animator.Clip.View());
			SkeletalPoseComponent& pose = registry.get_or_emplace<SkeletalPoseComponent>(entity);
			model->Bones.ComputeSkinMatrices(instance->GetPose(), pose.Skin);
		}
	}

	void AnimationSystem::OnStop(World& world)
	{
		world.GetRegistry().clear<AnimatorStateComponent, SkeletalPoseComponent>();
	}
}

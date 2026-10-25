#include "Editor/AssetThumbnails.h"
#include "Engine/Material.h"
#include "Engine/Paths.h"
#include "Engine/Render/Renderer.h"

#include <QDir>

#include <cmath>

namespace FP::Editor
{
	AssetThumbnails::AssetThumbnails(GPUDevice& device) : m_Device(device) {}
	AssetThumbnails::~AssetThumbnails() = default;

	void AssetThumbnails::Clear()
	{
		m_Renderer.reset();
		m_Target = nullptr;
	}

	QImage AssetThumbnails::Render(const QString& path, bool material)
	{
		constexpr uint32 size = 128;
		if (!m_Renderer)
		{
			m_Renderer = std::make_unique<Renderer>(m_Device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
			m_Target = m_Device.CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, size, size, false);
		}
		if (!m_Renderer->IsValid() || !m_Target)
			return {};

		const QDir content(QString::fromUtf8(Paths::GetContentDir().CStr()));
		const String relative(content.relativeFilePath(path).toUtf8().constData());
		const String meshName = material ? String("Sphere") : relative;
		const GPUMesh* mesh = m_Renderer->GetMeshes().Get(meshName, !material);
		if (!mesh || !mesh->IndexCount)
			return {};

		World world;
		const Entity asset = world.CreateEntity("Thumbnail");
		world.Add<MeshComponent>(asset).Mesh = meshName;
		if (material)
		{
			Material* loaded = MaterialLibrary::Get().Load(relative);
			if (!loaded)
				return {};
			// Preserve unsaved edits while picking up changes on disk.
			if (!loaded->IsDirty() && !loaded->Load())
				return {};
			world.Add<MaterialComponent>(asset).Material = relative;
		}

		// Center and normalize the bounds so every mesh fits the same camera.
		const Vec3 center = (mesh->BoundsMin + mesh->BoundsMax) * 0.5f;
		const float radius = material ? 0.5f : (mesh->BoundsMax - mesh->BoundsMin).Length() * 0.5f;
		if (!std::isfinite(radius) || radius < 0.000001f)
			return {};
		auto& transform = world.Get<TransformComponent>(asset);
		const float scale = 1 / radius;
		transform.Scale = { scale, scale, scale };
		transform.Position = center * -scale;

		const Entity ambient = world.CreateEntity("Ambient");
		world.Add<AmbientLightComponent>(ambient).Intensity = 0.35f;
		const Entity key = world.CreateEntity("Key");
		world.Get<TransformComponent>(key).Rotation = { -35, -35, 0 };
		auto& light = world.Add<DirectionalLightComponent>(key);
		light.Intensity = 3;
		light.CastShadows = false;

		TransformComponent cameraTransform;
		cameraTransform.Rotation = { -20, 30, 0 };
		cameraTransform.Position = cameraTransform.GetForward() * -3.4f;
		CameraComponent camera;
		camera.Perspective = true;
		camera.FieldOfView = 40;
		camera.Background = { 0.12f, 0.12f, 0.14f, 1 };
		const RenderView view = RenderView::FromCamera(cameraTransform, camera, size, size);
		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_Device.GetHandle());
		if (!cmd)
			return {};
		m_Renderer->Render(cmd, m_Target->GetHandle(), size, size, world, view);
		if (!SDL_SubmitGPUCommandBuffer(cmd))
			return {};
		QImage image(size, size, QImage::Format_RGBA8888);
		if (!m_Device.ReadPixels(m_Target->GetHandle(), size, size, image.bits()))
			return {};
		return image;
	}
}

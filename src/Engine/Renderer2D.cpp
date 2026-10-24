#include "Engine/Renderer2D.h"
#include "Engine/Paths.h"

#include <SDL3/SDL.h>

#include <cmath>

namespace FP
{
	Texture::~Texture()
	{
		SDL_DestroyTexture(m_Texture);
	}

	Vec2 ViewParams::WorldToScreen(Vec2 world) const
	{
		const Vec2 offset = (world - Center) * PixelsPerUnit;
		return { Width * 0.5f + offset.X, Height * 0.5f - offset.Y };
	}

	Vec2 ViewParams::ScreenToWorld(Vec2 screen) const
	{
		const Vec2 offset{ screen.X - Width * 0.5f, Height * 0.5f - screen.Y };
		return Center + offset / PixelsPerUnit;
	}

	ViewParams Renderer2D::GetCameraView(World& world, int32 width, int32 height)
	{
		ViewParams view;
		view.Width = width;
		view.Height = height;
		for (auto [entity, transform, camera] : world.View<TransformComponent, CameraComponent>().each())
		{
			if (!camera.Primary)
				continue;
			view.Center = transform.Position;
			view.PixelsPerUnit = camera.Zoom > 0.01f ? camera.Zoom : 0.01f;
			view.Background = camera.Background;
			break;
		}
		return view;
	}

	void Renderer2D::Render(World& world, const ViewParams& view, bool drawGrid)
	{
		SDL_SetRenderDrawColorFloat(m_Renderer, view.Background.R, view.Background.G, view.Background.B, 1);
		SDL_RenderClear(m_Renderer);
		if (drawGrid)
			DrawGrid(view);

		struct DrawItem
		{
			int32 Layer;
			Entity Value;
		};
		Array<DrawItem> items;
		for (auto [entity, transform, sprite] : world.View<TransformComponent, SpriteComponent>().each())
			items.Add({ sprite.Layer, entity });
		std::stable_sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) { return a.Layer < b.Layer; });

		for (const DrawItem& item : items)
		{
			const TransformComponent& transform = world.Get<TransformComponent>(item.Value);
			const SpriteComponent& sprite = world.Get<SpriteComponent>(item.Value);
			RefPtr<Texture> texture = sprite.Texture.IsEmpty() ? nullptr : LoadTexture(sprite.Texture);
			DrawQuad(transform.Position, sprite.Size * transform.Scale, transform.Rotation, sprite.Tint, texture.Get(), view);
		}
	}

	void Renderer2D::DrawQuad(Vec2 center, Vec2 size, float rotation, Color color, Texture* texture, const ViewParams& view)
	{
		const Vec2 half = size * 0.5f;
		const Vec2 corners[4] = { { -half.X, half.Y }, { half.X, half.Y }, { half.X, -half.Y }, { -half.X, -half.Y } };
		const float uvs[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

		SDL_Vertex vertices[4];
		for (int i = 0; i < 4; ++i)
		{
			const Vec2 screen = view.WorldToScreen(center + corners[i].Rotated(rotation));
			vertices[i].position = { screen.X, screen.Y };
			vertices[i].color = { color.R, color.G, color.B, color.A };
			vertices[i].tex_coord = { uvs[i][0], uvs[i][1] };
		}
		const int indices[6] = { 0, 1, 2, 0, 2, 3 };
		SDL_RenderGeometry(m_Renderer, texture ? texture->GetHandle() : nullptr, vertices, 4, indices, 6);
	}

	void Renderer2D::DrawGrid(const ViewParams& view)
	{
		const Vec2 min = view.ScreenToWorld({ 0, static_cast<float>(view.Height) });
		const Vec2 max = view.ScreenToWorld({ static_cast<float>(view.Width), 0 });

		// Skip lines when zoomed far out so the grid stays readable.
		float step = 1;
		while (step * view.PixelsPerUnit < 16)
			step *= 2;

		SDL_SetRenderDrawBlendMode(m_Renderer, SDL_BLENDMODE_BLEND);
		for (float x = std::floor(min.X / step) * step; x <= max.X; x += step)
		{
			const bool axis = std::fabs(x) < step * 0.5f;
			SDL_SetRenderDrawColorFloat(m_Renderer, axis ? 0.8f : 1, axis ? 0.3f : 1, axis ? 0.3f : 1, axis ? 0.6f : 0.06f);
			const float sx = view.WorldToScreen({ x, 0 }).X;
			SDL_RenderLine(m_Renderer, sx, 0, sx, static_cast<float>(view.Height));
		}
		for (float y = std::floor(min.Y / step) * step; y <= max.Y; y += step)
		{
			const bool axis = std::fabs(y) < step * 0.5f;
			SDL_SetRenderDrawColorFloat(m_Renderer, axis ? 0.3f : 1, axis ? 0.8f : 1, axis ? 0.3f : 1, axis ? 0.6f : 0.06f);
			const float sy = view.WorldToScreen({ 0, y }).Y;
			SDL_RenderLine(m_Renderer, 0, sy, static_cast<float>(view.Width), sy);
		}
	}

	void Renderer2D::DrawRectOutline(Vec2 center, Vec2 size, float rotation, Color color, const ViewParams& view, int32 thickness)
	{
		SDL_SetRenderDrawColorFloat(m_Renderer, color.R, color.G, color.B, color.A);
		// Each ring grows by one pixel outward so the outline never covers the sprite.
		for (int32 ring = 1; ring <= thickness; ++ring)
		{
			const Vec2 half = size * 0.5f + Vec2(1, 1) * (static_cast<float>(ring) / view.PixelsPerUnit);
			const Vec2 corners[4] = { { -half.X, half.Y }, { half.X, half.Y }, { half.X, -half.Y }, { -half.X, -half.Y } };
			SDL_FPoint points[5];
			for (int i = 0; i < 5; ++i)
			{
				const Vec2 screen = view.WorldToScreen(center + corners[i % 4].Rotated(rotation));
				points[i] = { screen.X, screen.Y };
			}
			SDL_RenderLines(m_Renderer, points, 5);
		}
	}

	RefPtr<Texture> Renderer2D::LoadTexture(const String& path)
	{
		if (RefPtr<Texture>* cached = m_Textures.Find(path))
			return *cached;
		if (m_FailedTextures.Contains(path))
			return nullptr;

		const String fullPath = Paths::Content(path);
		SDL_Surface* surface = SDL_LoadBMP(fullPath.CStr());
		SDL_Texture* handle = surface ? SDL_CreateTextureFromSurface(m_Renderer, surface) : nullptr;
		if (!handle)
		{
			FP_LOG_WARN("Could not load texture %s: %s", fullPath.CStr(), SDL_GetError());
			if (surface)
				SDL_DestroySurface(surface);
			m_FailedTextures.Add(path);
			return nullptr;
		}
		SDL_SetTextureScaleMode(handle, SDL_SCALEMODE_NEAREST);
		RefPtr<Texture> texture = MakeRef<Texture>(handle, surface->w, surface->h);
		SDL_DestroySurface(surface);
		m_Textures.Add(path, texture);
		return texture;
	}
}

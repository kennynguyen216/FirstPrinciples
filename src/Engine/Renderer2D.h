#pragma once

#include "Engine/World.h"

struct SDL_Renderer;
struct SDL_Texture;

namespace FP
{
	// GPU or software texture owned through RefPtr so sprites can share it.
	class Texture : public RefCounted
	{
	public:
		Texture(SDL_Texture* texture, int32 width, int32 height) : m_Texture(texture), m_Width(width), m_Height(height) {}
		~Texture() override;

		SDL_Texture* GetHandle() const { return m_Texture; }
		int32 GetWidth() const { return m_Width; }
		int32 GetHeight() const { return m_Height; }

	private:
		SDL_Texture* m_Texture;
		int32 m_Width;
		int32 m_Height;
	};

	// Where the view looks and how big it is. World space is Y up with the origin at Center.
	struct ViewParams
	{
		Vec2 Center;
		float PixelsPerUnit = 64;
		int32 Width = 1;
		int32 Height = 1;
		Color Background{ 0.08f, 0.08f, 0.1f, 1 };

		Vec2 WorldToScreen(Vec2 world) const;
		Vec2 ScreenToWorld(Vec2 screen) const;
	};

	// Draws a World with SDL_Renderer. Works with a window renderer (runtime) or a software
	// renderer (editor viewport). Not thread safe.
	class Renderer2D
	{
	public:
		explicit Renderer2D(SDL_Renderer* renderer) : m_Renderer(renderer) {}

		// View from the world's primary camera, or a default view when it has none.
		static ViewParams GetCameraView(World& world, int32 width, int32 height);

		void Render(World& world, const ViewParams& view);

		// Editor helpers.
		void DrawGrid(const ViewParams& view);
		void DrawRectOutline(Vec2 center, Vec2 size, float rotation, Color color, const ViewParams& view);

		// Loads a .bmp relative to Content. Cached per path. Returns null if it fails.
		RefPtr<Texture> LoadTexture(const String& path);

		SDL_Renderer* GetSDLRenderer() const { return m_Renderer; }

	private:
		void DrawQuad(Vec2 center, Vec2 size, float rotation, Color color, Texture* texture, const ViewParams& view);

		SDL_Renderer* m_Renderer;
		HashMap<String, RefPtr<Texture>> m_Textures;
		Array<String> m_FailedTextures;
	};
}

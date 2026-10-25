#pragma once

#include "Engine/Render/GPUDevice.h"

#include <QImage>
#include <QString>

#include <memory>

namespace FP
{
	class Renderer;
}

namespace FP::Editor
{
	// Renders assets in a small world with fixed lighting.
	class AssetThumbnails
	{
	public:
		explicit AssetThumbnails(GPUDevice& device);
		~AssetThumbnails();

		QImage Render(const QString& path, bool material);
		void Clear();

	private:
		GPUDevice& m_Device;
		std::unique_ptr<Renderer> m_Renderer;
		RefPtr<GPUTexture> m_Target;
	};
}

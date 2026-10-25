#pragma once
#include "Editor/ViewportWidget.h"
#include <QPointer>
struct ImGuiContext;

namespace FP::Editor
{
	// A Qt host for the upstream ImGui SDL_GPU renderer. The main editor remains Qt.
	class MeshletDebugWidget : public QWidget
	{
	public:
		explicit MeshletDebugWidget(ViewportWidget& viewport, QWidget* parent = nullptr);
		~MeshletDebugWidget() override;
	protected:
		void paintEvent(QPaintEvent*) override;
		void mouseMoveEvent(QMouseEvent*) override;
		void mousePressEvent(QMouseEvent*) override;
		void mouseReleaseEvent(QMouseEvent*) override;
		void wheelEvent(QWheelEvent*) override;
		void focusOutEvent(QFocusEvent*) override;
	private:
		void RenderFrame();
		void MouseButton(QMouseEvent* event, bool down);
		QPointer<ViewportWidget> m_Viewport;
		GPUDevice& m_Device;
		ImGuiContext* m_Context = nullptr;
		bool m_Ready = false;
		RefPtr<GPUTexture> m_Target;
		QImage m_Image;
	};
	void ShowMeshletDebugger(ViewportWidget& viewport, QWidget* parent);
}

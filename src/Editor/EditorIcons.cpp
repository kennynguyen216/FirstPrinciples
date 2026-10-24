#include "Editor/EditorIcons.h"
#include "Editor/EditorStyle.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <cmath>

namespace FP::Editor::EditorIcons
{
	namespace
	{
		// Icons are drawn on a 16 unit grid and rendered at 4x so they stay crisp when scaled.
		constexpr int Units = 16;
		constexpr int Scale = 4;
		constexpr double Pi = 3.14159265358979323846;

		QColor DefaultColor(EditorIcon icon)
		{
			switch (icon)
			{
			case EditorIcon::Play: return EditorStyle::Play;
			case EditorIcon::Folder:
			case EditorIcon::FolderOpen: return EditorStyle::Folder;
			case EditorIcon::PointLight:
			case EditorIcon::DirectionalLight:
			case EditorIcon::AmbientLight: return QColor(0xf0, 0xe0, 0xa0);
			case EditorIcon::Scene:
			case EditorIcon::Level: return QColor(0xf0, 0xa0, 0x30);
			case EditorIcon::Material: return QColor(0x7c, 0xb3, 0x42);
			case EditorIcon::Texture: return QColor(0xd0, 0x5a, 0x5a);
			case EditorIcon::Script: return QColor(0x4f, 0xa3, 0xe0);
			default: return EditorStyle::Foreground;
			}
		}

		QPainterPath Star(QPointF center, float outer, float inner, int points)
		{
			QPainterPath path;
			for (int i = 0; i < points * 2; ++i)
			{
				const float radius = i % 2 == 0 ? outer : inner;
				const double angle = Pi * i / points - Pi / 2;
				const QPointF point(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
				if (i == 0)
					path.moveTo(point);
				else
					path.lineTo(point);
			}
			path.closeSubpath();
			return path;
		}

		void Draw(QPainter& p, EditorIcon icon, const QColor& color)
		{
			QPen pen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			auto fill = [&]() { p.setPen(Qt::NoPen); p.setBrush(color); };
			auto stroke = [&]() { p.setPen(pen); p.setBrush(Qt::NoBrush); };

			switch (icon)
			{
			case EditorIcon::Save:
				p.drawRoundedRect(QRectF(2.5, 2.5, 11, 11), 1.5, 1.5);
				fill();
				p.drawRect(QRectF(5, 2.5, 6, 3.5));
				p.drawRect(QRectF(4.5, 9, 7, 4.5));
				break;
			case EditorIcon::SaveAll:
				p.drawRoundedRect(QRectF(4.5, 1.5, 10, 10), 1.5, 1.5);
				p.drawPolyline(QPolygonF({ { 1.5, 4.5 }, { 1.5, 14.5 }, { 11.5, 14.5 } }));
				fill();
				p.drawRect(QRectF(6.5, 1.5, 6, 3));
				break;
			case EditorIcon::Play:
				fill();
				p.drawPolygon(QPolygonF({ { 3.5, 2 }, { 14, 8 }, { 3.5, 14 } }));
				break;
			case EditorIcon::Step:
				fill();
				p.drawRect(QRectF(2, 3, 2, 10));
				p.drawPolygon(QPolygonF({ { 6, 3 }, { 14, 8 }, { 6, 13 } }));
				break;
			case EditorIcon::Stop:
				fill();
				p.drawRoundedRect(QRectF(3, 3, 10, 10), 1, 1);
				break;
			case EditorIcon::Eject:
				fill();
				p.drawPolygon(QPolygonF({ { 8, 2.5 }, { 14, 9.5 }, { 2, 9.5 } }));
				p.drawRect(QRectF(2, 11.5, 12, 2));
				break;
			case EditorIcon::More:
				fill();
				for (int i = 0; i < 3; ++i)
					p.drawEllipse(QPointF(8, 3.5 + i * 4.5), 1.4, 1.4);
				break;
			case EditorIcon::Settings:
			{
				QPainterPath gear = Star({ 8, 8 }, 7, 5.2, 8);
				QPainterPath hole;
				hole.addEllipse(QPointF(8, 8), 2.4, 2.4);
				fill();
				p.drawPath(gear.subtracted(hole));
				break;
			}
			case EditorIcon::Search:
				p.drawEllipse(QPointF(6.5, 6.5), 4.2, 4.2);
				p.drawLine(QPointF(9.7, 9.7), QPointF(14, 14));
				break;
			case EditorIcon::Add:
				pen.setWidthF(2);
				p.setPen(pen);
				p.drawLine(QPointF(8, 2.5), QPointF(8, 13.5));
				p.drawLine(QPointF(2.5, 8), QPointF(13.5, 8));
				break;
			case EditorIcon::Folder:
			case EditorIcon::FolderOpen:
			{
				fill();
				QPainterPath path;
				path.moveTo(1, 3.5);
				path.lineTo(6, 3.5);
				path.lineTo(7.5, 5);
				path.lineTo(15, 5);
				path.lineTo(15, 13.5);
				path.lineTo(1, 13.5);
				path.closeSubpath();
				p.drawPath(path);
				p.setBrush(color.darker(125));
				p.drawRect(QRectF(1, 6.5, 14, 7));
				break;
			}
			case EditorIcon::Eye:
			{
				QPainterPath path;
				path.moveTo(1.5, 8);
				path.quadTo(8, 1.5, 14.5, 8);
				path.quadTo(8, 14.5, 1.5, 8);
				p.drawPath(path);
				fill();
				p.drawEllipse(QPointF(8, 8), 2.2, 2.2);
				break;
			}
			case EditorIcon::Close:
				p.drawLine(QPointF(4, 4), QPointF(12, 12));
				p.drawLine(QPointF(12, 4), QPointF(4, 12));
				break;
			case EditorIcon::Menu:
				for (int i = 0; i < 3; ++i)
					p.drawLine(QPointF(3, 4.5 + i * 3.5), QPointF(13, 4.5 + i * 3.5));
				break;
			case EditorIcon::Actor:
			{
				QRadialGradient shade(QPointF(6, 6), 8);
				shade.setColorAt(0, QColor(255, 255, 255));
				shade.setColorAt(1, color.darker(200));
				p.setPen(Qt::NoPen);
				p.setBrush(shade);
				p.drawEllipse(QPointF(8, 8), 6.5, 6.5);
				break;
			}
			case EditorIcon::Sprite:
			case EditorIcon::Texture:
				p.drawRoundedRect(QRectF(2, 2.5, 12, 11), 1.5, 1.5);
				fill();
				p.drawPolygon(QPolygonF({ { 3.5, 12 }, { 7, 7 }, { 9.5, 10 }, { 11, 8.5 }, { 12.5, 12 } }));
				p.drawEllipse(QPointF(11, 5.5), 1.2, 1.2);
				break;
			case EditorIcon::Mesh:
			case EditorIcon::Perspective:
				p.drawPolygon(QPolygonF({ { 8, 1.5 }, { 14, 4.5 }, { 14, 11.5 }, { 8, 14.5 }, { 2, 11.5 }, { 2, 4.5 } }));
				p.drawPolyline(QPolygonF({ { 2, 4.5 }, { 8, 7.5 }, { 14, 4.5 } }));
				p.drawLine(QPointF(8, 7.5), QPointF(8, 14.5));
				break;
			case EditorIcon::PointLight:
				fill();
				p.drawEllipse(QPointF(8, 6.5), 4.5, 4.5);
				p.drawRect(QRectF(6, 10, 4, 2.5));
				p.drawRoundedRect(QRectF(6.3, 13, 3.4, 1.8), 0.8, 0.8);
				break;
			case EditorIcon::DirectionalLight:
				fill();
				p.drawEllipse(QPointF(8, 8), 3, 3);
				stroke();
				for (int i = 0; i < 8; ++i)
				{
					const double angle = Pi * i / 4;
					p.drawLine(QPointF(8 + 4.8 * std::cos(angle), 8 + 4.8 * std::sin(angle)), QPointF(8 + 7 * std::cos(angle), 8 + 7 * std::sin(angle)));
				}
				break;
			case EditorIcon::AmbientLight:
				fill();
				p.drawPath(Star({ 8, 8 }, 7, 3, 4));
				break;
			case EditorIcon::Camera:
				fill();
				p.drawRoundedRect(QRectF(1.5, 4.5, 9.5, 7), 1.5, 1.5);
				p.drawPolygon(QPolygonF({ { 11, 8 }, { 14.5, 5 }, { 14.5, 11 } }));
				break;
			case EditorIcon::Script:
				p.drawPolyline(QPolygonF({ { 5.5, 4 }, { 1.5, 8 }, { 5.5, 12 } }));
				p.drawPolyline(QPolygonF({ { 10.5, 4 }, { 14.5, 8 }, { 10.5, 12 } }));
				p.drawLine(QPointF(9, 3), QPointF(7, 13));
				break;
			case EditorIcon::Scene:
			case EditorIcon::Level:
				fill();
				p.drawPolygon(QPolygonF({ { 1, 14 }, { 6, 5 }, { 9, 10 }, { 11, 7 }, { 15, 14 } }));
				break;
			case EditorIcon::Material:
			{
				QRadialGradient shade(QPointF(6, 6), 8);
				shade.setColorAt(0, color.lighter(170));
				shade.setColorAt(1, color.darker(160));
				p.setPen(Qt::NoPen);
				p.setBrush(shade);
				p.drawEllipse(QPointF(8, 8), 6.5, 6.5);
				break;
			}
			case EditorIcon::Lit:
				fill();
				p.drawEllipse(QPointF(8, 8), 6, 6);
				p.setBrush(EditorStyle::Panel);
				p.drawPie(QRectF(2, 2, 12, 12), -45 * 16, 180 * 16);
				break;
			case EditorIcon::Grid:
				p.drawRect(QRectF(2, 2, 12, 12));
				p.drawLine(QPointF(6, 2), QPointF(6, 14));
				p.drawLine(QPointF(10, 2), QPointF(10, 14));
				p.drawLine(QPointF(2, 6), QPointF(14, 6));
				p.drawLine(QPointF(2, 10), QPointF(14, 10));
				break;
			case EditorIcon::Snap:
				p.drawPolyline(QPolygonF({ { 2, 3 }, { 2, 13 }, { 13, 13 } }));
				fill();
				p.drawEllipse(QPointF(9, 7), 2.5, 2.5);
				break;
			case EditorIcon::Select:
				fill();
				p.drawPolygon(QPolygonF({ { 4, 1.5 }, { 13, 10 }, { 8.8, 10.2 }, { 11, 14.5 }, { 9.3, 15.2 }, { 7.2, 11 }, { 4, 13.8 } }));
				break;
			case EditorIcon::Move:
				p.drawLine(QPointF(8, 1.5), QPointF(8, 14.5));
				p.drawLine(QPointF(1.5, 8), QPointF(14.5, 8));
				fill();
				p.drawPolygon(QPolygonF({ { 8, 0.5 }, { 10.5, 3.5 }, { 5.5, 3.5 } }));
				p.drawPolygon(QPolygonF({ { 8, 15.5 }, { 10.5, 12.5 }, { 5.5, 12.5 } }));
				p.drawPolygon(QPolygonF({ { 0.5, 8 }, { 3.5, 5.5 }, { 3.5, 10.5 } }));
				p.drawPolygon(QPolygonF({ { 15.5, 8 }, { 12.5, 5.5 }, { 12.5, 10.5 } }));
				break;
			case EditorIcon::Details:
				p.drawRoundedRect(QRectF(2, 2, 12, 12), 1.5, 1.5);
				p.drawLine(QPointF(5, 6), QPointF(11, 6));
				p.drawLine(QPointF(5, 10), QPointF(11, 10));
				break;
			case EditorIcon::Outliner:
				p.drawLine(QPointF(2, 3), QPointF(14, 3));
				p.drawLine(QPointF(5, 8), QPointF(14, 8));
				p.drawLine(QPointF(5, 13), QPointF(14, 13));
				p.drawPolyline(QPolygonF({ { 2, 3 }, { 2, 13 }, { 4, 13 } }));
				p.drawLine(QPointF(2, 8), QPointF(4, 8));
				break;
			case EditorIcon::PlaceActors:
				fill();
				p.drawRect(QRectF(2, 2, 5, 5));
				p.drawEllipse(QRectF(9, 2, 5, 5));
				p.drawPolygon(QPolygonF({ { 4.5, 9 }, { 7, 14 }, { 2, 14 } }));
				p.drawRoundedRect(QRectF(9, 9, 5, 5), 1.5, 1.5);
				break;
			case EditorIcon::ContentBrowser:
				p.drawRoundedRect(QRectF(1.5, 3, 13, 10.5), 1.5, 1.5);
				fill();
				p.drawRect(QRectF(4, 6, 3, 2.5));
				p.drawRect(QRectF(9, 6, 3, 2.5));
				p.drawRect(QRectF(4, 9.5, 3, 2));
				p.drawRect(QRectF(9, 9.5, 3, 2));
				break;
			case EditorIcon::OutputLog:
				p.drawRoundedRect(QRectF(2.5, 1.5, 11, 13), 1.5, 1.5);
				p.drawLine(QPointF(5, 5), QPointF(11, 5));
				p.drawLine(QPointF(5, 8), QPointF(11, 8));
				p.drawLine(QPointF(5, 11), QPointF(9, 11));
				break;
			case EditorIcon::Console:
				p.drawRoundedRect(QRectF(1.5, 2.5, 13, 11), 1.5, 1.5);
				p.drawPolyline(QPolygonF({ { 4, 6 }, { 6.5, 8 }, { 4, 10 } }));
				p.drawLine(QPointF(8, 10.5), QPointF(12, 10.5));
				break;
			case EditorIcon::Import:
				p.drawLine(QPointF(8, 1.5), QPointF(8, 10));
				p.drawPolyline(QPolygonF({ { 4.5, 6.5 }, { 8, 10 }, { 11.5, 6.5 } }));
				p.drawPolyline(QPolygonF({ { 2, 10 }, { 2, 14 }, { 14, 14 }, { 14, 10 } }));
				break;
			case EditorIcon::Back:
				p.drawEllipse(QPointF(8, 8), 6.5, 6.5);
				p.drawPolyline(QPolygonF({ { 9, 5 }, { 6, 8 }, { 9, 11 } }));
				break;
			case EditorIcon::Forward:
				p.drawEllipse(QPointF(8, 8), 6.5, 6.5);
				p.drawPolyline(QPolygonF({ { 7, 5 }, { 10, 8 }, { 7, 11 } }));
				break;
			case EditorIcon::Chevron:
				p.drawPolyline(QPolygonF({ { 6, 3.5 }, { 10.5, 8 }, { 6, 12.5 } }));
				break;
			case EditorIcon::Filter:
				p.drawLine(QPointF(2, 4), QPointF(14, 4));
				p.drawLine(QPointF(4, 8), QPointF(12, 8));
				p.drawLine(QPointF(6, 12), QPointF(10, 12));
				break;
			case EditorIcon::Lock:
				p.drawArc(QRectF(4.5, 1.5, 7, 9), 0, 180 * 16);
				p.drawLine(QPointF(4.5, 6), QPointF(4.5, 7));
				fill();
				p.drawRoundedRect(QRectF(3, 7, 10, 7.5), 1.5, 1.5);
				break;
			case EditorIcon::Help:
			{
				p.drawEllipse(QPointF(8, 8), 6.5, 6.5);
				QFont font;
				font.setPixelSize(10);
				font.setBold(true);
				p.setFont(font);
				p.drawText(QRectF(0, 0, 16, 16), Qt::AlignCenter, "?");
				break;
			}
			}
		}
	}

	QIcon Get(EditorIcon icon, const QColor& color)
	{
		static QHash<QString, QIcon> cache;
		const QColor used = color.isValid() ? color : DefaultColor(icon);
		const QString key = QString::number(static_cast<int>(icon)) + used.name(QColor::HexArgb);
		if (const auto found = cache.find(key); found != cache.end())
			return found.value();

		QPixmap pixmap(Units * Scale, Units * Scale);
		pixmap.fill(Qt::transparent);
		{
			QPainter painter(&pixmap);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.scale(Scale, Scale);
			Draw(painter, icon, used);
		}
		QIcon result(pixmap);
		// Disabled buttons get a dimmed copy instead of Qt's default, which is too faint on dark panels.
		QPixmap dimmed(pixmap.size());
		dimmed.fill(Qt::transparent);
		{
			QPainter painter(&dimmed);
			painter.setOpacity(0.35);
			painter.drawPixmap(0, 0, pixmap);
		}
		result.addPixmap(dimmed, QIcon::Disabled);
		cache.insert(key, result);
		return result;
	}
}

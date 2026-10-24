#include "Tests/TestFramework.h"
#include "Engine/Render/ShadowMap.h"

#include <cmath>

using namespace FP;

namespace
{
	RenderView LookingDownZ(Vec3 position)
	{
		RenderView view;
		view.CameraPosition = position;
		view.CameraForward = { 0, 0, -1 };
		return view;
	}
}

FP_TEST(Shadow_BoxSitsAheadOfTheCamera)
{
	// Sun straight down, 20 units of shadow at 1000 pixels, so texels are 2 cm.
	const Quat down = Quat::AxisAngle({ 1, 0, 0 }, -90);
	const ShadowView shadow = FitShadowView(LookingDownZ({ 0, 2, 10 }), down, 20, 1000);
	FP_EXPECT(shadow.Radius == 10);
	FP_EXPECT((shadow.Center - Vec3(0, 2, 0)).Length() < 0.03f);

	// The middle of the box lands in the middle of the map, and lower things are deeper.
	const Vec3 middle = shadow.ToTexture.TransformPoint(shadow.Center);
	FP_EXPECT(std::fabs(middle.X - 0.5f) < 0.01f && std::fabs(middle.Y - 0.5f) < 0.01f);
	const Vec3 floor = shadow.ToTexture.TransformPoint(shadow.Center + Vec3(0, -5, 0));
	FP_EXPECT(floor.Z > middle.Z && floor.Z < 1);

	// Casters above the box toward the sun still count, ones off to the side do not.
	FP_EXPECT(shadow.Touches(shadow.Center + Vec3(0, 30, 0), 1));
	FP_EXPECT(!shadow.Touches(shadow.Center + Vec3(15, 0, 0), 1));
	FP_EXPECT(!shadow.Touches(shadow.Center + Vec3(0, -15, 0), 1));
}

FP_TEST(Shadow_CenterSnapsToTexels)
{
	// Moving less than a texel leaves the map where it was, so edges do not crawl.
	const Quat down = Quat::AxisAngle({ 1, 0, 0 }, -90);
	const ShadowView a = FitShadowView(LookingDownZ({ 0.101f, 2, 10 }), down, 20, 1000);
	const ShadowView b = FitShadowView(LookingDownZ({ 0.109f, 2, 10 }), down, 20, 1000);
	FP_EXPECT((a.Center - b.Center).Length() < 1e-5f);
	const ShadowView c = FitShadowView(LookingDownZ({ 0.131f, 2, 10 }), down, 20, 1000);
	FP_EXPECT(std::fabs(std::fabs(c.Center.X - a.Center.X) - 0.02f) < 1e-4f);
}

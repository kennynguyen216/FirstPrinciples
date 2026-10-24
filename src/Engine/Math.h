#pragma once

#include "HAL/Platform.h"

#include <cmath>

namespace FP
{
	constexpr float Pi = 3.14159265358979f;
	constexpr float DegToRad = Pi / 180.0f;
	constexpr float RadToDeg = 180.0f / Pi;

	struct Vec2
	{
		float X = 0;
		float Y = 0;

		constexpr Vec2() = default;
		constexpr Vec2(float x, float y) : X(x), Y(y) {}

		constexpr Vec2 operator+(Vec2 o) const { return { X + o.X, Y + o.Y }; }
		constexpr Vec2 operator-(Vec2 o) const { return { X - o.X, Y - o.Y }; }
		constexpr Vec2 operator*(float s) const { return { X * s, Y * s }; }
		constexpr Vec2 operator*(Vec2 o) const { return { X * o.X, Y * o.Y }; }
		constexpr Vec2 operator/(float s) const { return { X / s, Y / s }; }
		constexpr Vec2 operator-() const { return { -X, -Y }; }
		Vec2& operator+=(Vec2 o) { X += o.X; Y += o.Y; return *this; }
		Vec2& operator-=(Vec2 o) { X -= o.X; Y -= o.Y; return *this; }
		Vec2& operator*=(float s) { X *= s; Y *= s; return *this; }
		constexpr bool operator==(const Vec2&) const = default;

		float Length() const { return std::sqrt(X * X + Y * Y); }
		constexpr float LengthSquared() const { return X * X + Y * Y; }

		Vec2 Normalized() const
		{
			const float len = Length();
			return len > 1e-6f ? *this / len : Vec2();
		}

		// Rotates counter clockwise by degrees.
		Vec2 Rotated(float degrees) const
		{
			const float r = degrees * DegToRad;
			const float c = std::cos(r);
			const float s = std::sin(r);
			return { X * c - Y * s, X * s + Y * c };
		}
	};

	struct Vec3
	{
		float X = 0;
		float Y = 0;
		float Z = 0;

		constexpr Vec3() = default;
		constexpr Vec3(float x, float y, float z) : X(x), Y(y), Z(z) {}
		constexpr Vec3(Vec2 xy, float z = 0) : X(xy.X), Y(xy.Y), Z(z) {}

		constexpr Vec2 XY() const { return { X, Y }; }

		constexpr Vec3 operator+(Vec3 o) const { return { X + o.X, Y + o.Y, Z + o.Z }; }
		constexpr Vec3 operator-(Vec3 o) const { return { X - o.X, Y - o.Y, Z - o.Z }; }
		constexpr Vec3 operator*(float s) const { return { X * s, Y * s, Z * s }; }
		constexpr Vec3 operator*(Vec3 o) const { return { X * o.X, Y * o.Y, Z * o.Z }; }
		constexpr Vec3 operator/(float s) const { return { X / s, Y / s, Z / s }; }
		constexpr Vec3 operator-() const { return { -X, -Y, -Z }; }
		Vec3& operator+=(Vec3 o) { X += o.X; Y += o.Y; Z += o.Z; return *this; }
		Vec3& operator-=(Vec3 o) { X -= o.X; Y -= o.Y; Z -= o.Z; return *this; }
		Vec3& operator*=(float s) { X *= s; Y *= s; Z *= s; return *this; }
		constexpr bool operator==(const Vec3&) const = default;

		float Length() const { return std::sqrt(X * X + Y * Y + Z * Z); }
		constexpr float LengthSquared() const { return X * X + Y * Y + Z * Z; }

		Vec3 Normalized() const
		{
			const float len = Length();
			return len > 1e-6f ? *this / len : Vec3();
		}

		static constexpr float Dot(Vec3 a, Vec3 b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
		static constexpr Vec3 Cross(Vec3 a, Vec3 b) { return { a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X }; }
	};

	struct Vec4
	{
		float X = 0;
		float Y = 0;
		float Z = 0;
		float W = 0;

		constexpr Vec4() = default;
		constexpr Vec4(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}
		constexpr Vec4(Vec3 v, float w) : X(v.X), Y(v.Y), Z(v.Z), W(w) {}

		constexpr Vec3 XYZ() const { return { X, Y, Z }; }
	};

	// Unit quaternion rotation.
	struct Quat
	{
		float X = 0;
		float Y = 0;
		float Z = 0;
		float W = 1;

		static Quat AxisAngle(Vec3 axis, float degrees)
		{
			const float half = degrees * DegToRad * 0.5f;
			const float s = std::sin(half);
			const Vec3 a = axis.Normalized();
			return { a.X * s, a.Y * s, a.Z * s, std::cos(half) };
		}

		// Euler angles in degrees as (pitch X, yaw Y, roll Z). Roll applies first, then pitch, then yaw.
		static Quat FromEuler(Vec3 degrees)
		{
			return AxisAngle({ 0, 1, 0 }, degrees.Y) * AxisAngle({ 1, 0, 0 }, degrees.X) * AxisAngle({ 0, 0, 1 }, degrees.Z);
		}

		Quat operator*(const Quat& o) const
		{
			return {
				W * o.X + X * o.W + Y * o.Z - Z * o.Y,
				W * o.Y - X * o.Z + Y * o.W + Z * o.X,
				W * o.Z + X * o.Y - Y * o.X + Z * o.W,
				W * o.W - X * o.X - Y * o.Y - Z * o.Z,
			};
		}

		Vec3 Rotate(Vec3 v) const
		{
			const Vec3 q{ X, Y, Z };
			const Vec3 t = Vec3::Cross(q, v) * 2.0f;
			return v + t * W + Vec3::Cross(q, t);
		}
	};

	// 4x4 matrix stored column major so it uploads straight into GLSL.
	struct Mat4
	{
		float M[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

		float& At(int col, int row) { return M[col * 4 + row]; }
		float At(int col, int row) const { return M[col * 4 + row]; }

		static Mat4 Identity() { return {}; }

		Mat4 operator*(const Mat4& o) const
		{
			Mat4 r;
			for (int c = 0; c < 4; ++c)
			{
				for (int row = 0; row < 4; ++row)
				{
					float sum = 0;
					for (int k = 0; k < 4; ++k)
						sum += At(k, row) * o.At(c, k);
					r.At(c, row) = sum;
				}
			}
			return r;
		}

		Vec4 operator*(Vec4 v) const
		{
			return {
				At(0, 0) * v.X + At(1, 0) * v.Y + At(2, 0) * v.Z + At(3, 0) * v.W,
				At(0, 1) * v.X + At(1, 1) * v.Y + At(2, 1) * v.Z + At(3, 1) * v.W,
				At(0, 2) * v.X + At(1, 2) * v.Y + At(2, 2) * v.Z + At(3, 2) * v.W,
				At(0, 3) * v.X + At(1, 3) * v.Y + At(2, 3) * v.Z + At(3, 3) * v.W,
			};
		}

		Vec3 TransformPoint(Vec3 p) const { return (*this * Vec4(p, 1)).XYZ(); }
		Vec3 TransformVector(Vec3 v) const { return (*this * Vec4(v, 0)).XYZ(); }

		static Mat4 Translation(Vec3 t)
		{
			Mat4 r;
			r.At(3, 0) = t.X;
			r.At(3, 1) = t.Y;
			r.At(3, 2) = t.Z;
			return r;
		}

		static Mat4 Scale(Vec3 s)
		{
			Mat4 r;
			r.At(0, 0) = s.X;
			r.At(1, 1) = s.Y;
			r.At(2, 2) = s.Z;
			return r;
		}

		static Mat4 Rotation(const Quat& q)
		{
			Mat4 r;
			const float xx = q.X * q.X, yy = q.Y * q.Y, zz = q.Z * q.Z;
			const float xy = q.X * q.Y, xz = q.X * q.Z, yz = q.Y * q.Z;
			const float wx = q.W * q.X, wy = q.W * q.Y, wz = q.W * q.Z;
			r.At(0, 0) = 1 - 2 * (yy + zz);
			r.At(0, 1) = 2 * (xy + wz);
			r.At(0, 2) = 2 * (xz - wy);
			r.At(1, 0) = 2 * (xy - wz);
			r.At(1, 1) = 1 - 2 * (xx + zz);
			r.At(1, 2) = 2 * (yz + wx);
			r.At(2, 0) = 2 * (xz + wy);
			r.At(2, 1) = 2 * (yz - wx);
			r.At(2, 2) = 1 - 2 * (xx + yy);
			return r;
		}

		static Mat4 TRS(Vec3 t, const Quat& r, Vec3 s) { return Translation(t) * Rotation(r) * Scale(s); }

		Mat4 Transposed() const
		{
			Mat4 r;
			for (int c = 0; c < 4; ++c)
				for (int row = 0; row < 4; ++row)
					r.At(c, row) = At(row, c);
			return r;
		}

		// General inverse. Returns identity for singular matrices.
		Mat4 Inverse() const;

		// Right handed, looking down -Z, depth mapped to 0..1 like Metal, D3D and SDL_GPU.
		static Mat4 Orthographic(float left, float right, float bottom, float top, float nearZ, float farZ)
		{
			Mat4 r;
			r.At(0, 0) = 2 / (right - left);
			r.At(1, 1) = 2 / (top - bottom);
			r.At(2, 2) = -1 / (farZ - nearZ);
			r.At(3, 0) = -(right + left) / (right - left);
			r.At(3, 1) = -(top + bottom) / (top - bottom);
			r.At(3, 2) = -nearZ / (farZ - nearZ);
			return r;
		}

		static Mat4 Perspective(float fovYDegrees, float aspect, float nearZ, float farZ)
		{
			const float f = 1.0f / std::tan(fovYDegrees * DegToRad * 0.5f);
			Mat4 r;
			r.At(0, 0) = f / aspect;
			r.At(1, 1) = f;
			r.At(2, 2) = farZ / (nearZ - farZ);
			r.At(2, 3) = -1;
			r.At(3, 2) = nearZ * farZ / (nearZ - farZ);
			r.At(3, 3) = 0;
			return r;
		}
	};

	inline Mat4 Mat4::Inverse() const
	{
		const float* m = M;
		float inv[16];
		inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
		inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
		inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
		inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
		inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
		inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
		inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
		inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
		inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
		inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
		inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
		inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
		inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
		inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
		inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
		inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

		const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
		if (std::fabs(det) < 1e-12f)
			return Identity();
		Mat4 r;
		for (int i = 0; i < 16; ++i)
			r.M[i] = inv[i] / det;
		return r;
	}

	// Linear 0 to 1 color.
	struct Color
	{
		float R = 1;
		float G = 1;
		float B = 1;
		float A = 1;

		constexpr bool operator==(const Color&) const = default;

		static constexpr Color White() { return { 1, 1, 1, 1 }; }
		static constexpr Color Black() { return { 0, 0, 0, 1 }; }
	};
}

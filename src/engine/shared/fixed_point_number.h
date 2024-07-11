#ifndef ENGINE_SHARED_FIXED_POINT_NUMBER_H
#define ENGINE_SHARED_FIXED_POINT_NUMBER_H

#include <base/math.h>

#include <cstdint>

class CFixedPointNumber
{
public:
	constexpr CFixedPointNumber() = default;
	constexpr CFixedPointNumber(float Value);

	constexpr CFixedPointNumber &operator=(float Value);
	constexpr operator float() const;

	const char *AsStr() const;

protected:
	int32_t m_Value = 0;
};

inline constexpr CFixedPointNumber::CFixedPointNumber(float Value)
{
	m_Value = round_to_int(Value * 1000);
}

inline constexpr CFixedPointNumber &CFixedPointNumber::operator=(float Value)
{
	m_Value = round_to_int(Value * 1000);
	return *this;
}

inline constexpr CFixedPointNumber::operator float() const
{
	return m_Value / 1000.0f;
}

#endif // ENGINE_SHARED_FIXED_POINT_NUMBER_H

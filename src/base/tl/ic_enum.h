#ifndef BASE_TL_IC_ENUM_H
#define BASE_TL_IC_ENUM_H

#include <base/system.h>

template<typename T>
constexpr T GetEnumInvalidValue()
{
	if constexpr(requires { static_cast<int>(T::Invalid); })
		return T::Invalid;
	else
		return T::INVALID;
}

template<typename T>
constexpr int GetEnumKeysCount()
{
	if constexpr(requires { static_cast<int>(T::Count); })
		return static_cast<int>(T::Count);
	else
		return static_cast<int>(T::COUNT);
}

template<typename T, int NamesCount>
[[nodiscard]] const char *toStringImpl(T Value, const char *(&apNames)[NamesCount])
{
	// T::Invalid and T::Count are equal to avoid extra case in switch()
	static_assert(GetEnumKeysCount<T>() + 1 == NamesCount);
	int Index = static_cast<int>(Value);
	if((Index < 0) || (Index >= NamesCount))
	{
		dbg_msg("ic_enum", "toStringImpl(%d): out of range!", Index);
		return apNames[static_cast<int>(GetEnumInvalidValue<T>())];
	}
	return apNames[Index];
}

template<typename T>
[[nodiscard]] T fromString(const char *pString)
{
	for(int i = 0; i < GetEnumKeysCount<T>(); ++i)
	{
		const T Value = static_cast<T>(i);
		if(str_comp(pString, toString(Value)) == 0)
		{
			return Value;
		}
	}

	return GetEnumInvalidValue<T>();
}

#endif // BASE_TL_IC_ENUM_H

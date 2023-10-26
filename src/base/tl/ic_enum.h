#ifndef BASE_TL_IC_ENUM_H
#define BASE_TL_IC_ENUM_H

#include <base/system.h>

template<typename T, int NamesCount>
[[nodiscard]] const char *toStringImpl(T Value, const char *(&apNames)[NamesCount])
{
	// T::Invalid and T::Count are equal to avoid extra case in switch()
	static_assert(static_cast<int>(T::Count) + 1 == NamesCount);
	int Index = static_cast<int>(Value);
	if((Index < 0) || (Index >= NamesCount))
	{
		dbg_msg("ic_enum", "toStringImpl(%d): out of range!", Index);
		return apNames[static_cast<int>(T::Invalid)];
	}
	return apNames[Index];
}

template<typename T>
[[nodiscard]] T fromString(const char *pString)
{
	for(int i = 0; i < static_cast<int>(T::Count); ++i)
	{
		const T Value = static_cast<T>(i);
		if(str_comp(pString, toString(Value)) == 0)
		{
			return Value;
		}
	}

	return T::Invalid;
}

#endif // BASE_TL_IC_ENUM_H

#ifndef BASE_TL_ARRAY_ON_STACK_H
#define BASE_TL_ARRAY_ON_STACK_H

#include <initializer_list>

template <class T, int StackCapacity>
class array_on_stack
{
public:
	array_on_stack() = default;
	array_on_stack(std::initializer_list<T> list)
	{
		for(const T &Element : list)
		{
			Add(Element);
		}
	}

	const T &At(int Index) const { return m_Data[Index]; }
	T operator[](int Index) const { return m_Data[Index]; }
	T &operator[](int Index) { return m_Data[Index]; }

	const T &First() const { return m_Data[0]; }
	const T &Last() const { return m_Data[m_Size - 1]; }

	T *begin() { return &m_Data[0]; }
	T *end() { return &m_Data[m_Size]; }

	T *Data() { return &m_Data; }
	const T *Data() const { return &m_Data; }

	int Size() const
	{
		return m_Size;
	}

	int Capacity() const
	{
		return StackCapacity;
	}

	bool IsEmpty() const
	{
		return m_Size == 0;
	}

	int IndexOf(const T &Item) const
	{
		for(int i = 0; i < Size(); ++i)
		{
			if(m_Data[i] == Item)
				return i;
		}
		return -1;
	}

	bool Contains(const T &Item) const
	{
		return IndexOf(Item) >= 0;
	}

	void Add(const T &Value)
	{
		m_Data[m_Size] = Value;
		++m_Size;
	}

	void RemoveLast()
	{
		--m_Size;
	}

	void RemoveAt(int Index)
	{
		if(Index < m_Size - 1)
		{
			m_Data[Index] = Last();
		}
		--m_Size;
	}

	void Clear()
	{
		m_Size = 0;
	}

protected:
	T m_Data[StackCapacity];
	int m_Size = 0;
};

#endif // BASE_TL_ARRAY_ON_STACK_H

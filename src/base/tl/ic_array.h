#ifndef BASE_TL_IC_ARRAY_H
#define BASE_TL_IC_ARRAY_H

#include <initializer_list>

template <class T, int StackCapacity>
class icArray
{
public:
	icArray() = default;
	icArray(std::initializer_list<T> list)
	{
		for(const T &Element : list)
		{
			Add(Element);
		}
	}

	const T &At(int Index) const { return m_Data[Index]; }
	T operator[](int Index) const { return m_Data[Index]; }
	T &operator[](int Index) { return m_Data[Index]; }

	icArray &operator=(const icArray &Array) noexcept;

	T &First() { return m_Data[0]; }
	T &Last() { return m_Data[m_Size - 1]; }

	const T &First() const { return m_Data[0]; }
	const T &Last() const { return m_Data[m_Size - 1]; }

	T *begin() { return &m_Data[0]; }
	T *end() { return &m_Data[m_Size]; }

	const T *begin() const { return &m_Data[0]; }
	const T *end() const { return &m_Data[m_Size]; }

	T *Data() { return &m_Data; }
	const T *Data() const { return &m_Data; }

	int Size() const
	{
		return m_Size;
	}

	void Resize(int NewSize)
	{
		m_Size = NewSize;
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

	bool RemoveOne(const T &Item)
	{
		int Index = IndexOf(Item);
		if(Index < 0)
			return false;

		RemoveAt(Index);
		return true;
	}

	void RemoveAt(int Index)
	{
		for(int i = Index; i < m_Size - 1; ++i)
		{
			m_Data[i] = m_Data[i + 1];
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

template<class T, int StackCapacity>
icArray<T, StackCapacity> &icArray<T, StackCapacity>::operator=(const icArray<T, StackCapacity> &Array) noexcept
{
	m_Size = 0;

	for(const T &Item : Array)
	{
		Add(Item);
	}

	return *this;
}

#endif // BASE_TL_IC_ARRAY_H

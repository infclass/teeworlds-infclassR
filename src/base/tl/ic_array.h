#ifndef BASE_TL_IC_ARRAY_H
#define BASE_TL_IC_ARRAY_H

#include <cstddef>
#include <initializer_list>

template <class T, int StackCapacity>
class icArray
{
public:
	constexpr icArray() = default;
	icArray(std::initializer_list<T> list);

	icArray &operator=(const icArray &Array) = default;

	const T &At(std::size_t Index) const { return m_Data[Index]; }
	T operator[](std::size_t Index) const { return m_Data[Index]; }
	T &operator[](std::size_t Index) { return m_Data[Index]; }

	T *Data() { return &m_Data; }
	const T *Data() const { return &m_Data; }

	T &First() { return m_Data[0]; }
	T &Last() { return m_Data[m_Size - 1]; }

	const T &First() const { return m_Data[0]; }
	const T &Last() const { return m_Data[m_Size - 1]; }

	void erase(const T *pItem);

	int Size() const;

	void Resize(int NewSize);

	int Capacity() const;

	bool IsEmpty() const;

	int IndexOf(const T &Item) const;

	bool Contains(const T &Item) const;

	void Add(const T &Value);

	void RemoveLast();

	bool RemoveOne(const T &Item);

	void RemoveAt(std::size_t Index);

	void Clear();

	T *begin() { return &m_Data[0]; }
	T *end() { return &m_Data[m_Size]; }

	const T *begin() const { return &m_Data[0]; }
	const T *end() const { return &m_Data[m_Size]; }

protected:
	T m_Data[StackCapacity];
	int m_Size = 0;
};

template<class T, int StackCapacity>
inline icArray<T, StackCapacity>::icArray(std::initializer_list<T> list)
{
	for(const T &Element : list)
	{
		Add(Element);
	}
}

template<class T, int StackCapacity>
void icArray<T, StackCapacity>::erase(const T *pItem)
{
	std::ptrdiff_t Offset = pItem - begin();
	RemoveAt(Offset);
}

template<class T, int StackCapacity>
int icArray<T, StackCapacity>::Size() const
{
	return m_Size;
}

template<class T, int StackCapacity>
void icArray<T, StackCapacity>::Resize(int NewSize)
{
	m_Size = NewSize;
}

template<class T, int StackCapacity>
int icArray<T, StackCapacity>::Capacity() const
{
	return StackCapacity;
}

template<class T, int StackCapacity>
bool icArray<T, StackCapacity>::IsEmpty() const
{
	return m_Size == 0;
}

template<class T, int StackCapacity>
int icArray<T, StackCapacity>::IndexOf(const T &Item) const
{
	for(int i = 0; i < Size(); ++i)
	{
		if(m_Data[i] == Item)
			return i;
	}
	return -1;
}

template<class T, int StackCapacity>
inline bool icArray<T, StackCapacity>::Contains(const T &Item) const
{
	return IndexOf(Item) >= 0;
}

template<class T, int StackCapacity>
inline void icArray<T, StackCapacity>::Add(const T &Value)
{
	m_Data[m_Size] = Value;
	++m_Size;
}

template<class T, int StackCapacity>
void icArray<T, StackCapacity>::RemoveLast()
{
	--m_Size;
}

template<class T, int StackCapacity>
bool icArray<T, StackCapacity>::RemoveOne(const T &Item)
{
	int Index = IndexOf(Item);
	if(Index < 0)
		return false;

	RemoveAt(Index);
	return true;
}

template<class T, int StackCapacity>
void icArray<T, StackCapacity>::RemoveAt(std::size_t Index)
{
	for(std::size_t i = Index; i < m_Size - 1; ++i)
	{
		m_Data[i] = m_Data[i + 1];
	}
	--m_Size;
}

template<class T, int StackCapacity>
void icArray<T, StackCapacity>::Clear()
{
	m_Size = 0;
}

#endif // BASE_TL_IC_ARRAY_H

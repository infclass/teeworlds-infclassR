#ifndef BASE_TL_IC_FIFO_H
#define BASE_TL_IC_FIFO_H

#include <cstddef>
#include <initializer_list>

template<class T, int StackCapacity>
class icFifoArray
{
public:
	constexpr icFifoArray() = default;
	icFifoArray(std::initializer_list<T> list);

	icFifoArray &operator=(const icFifoArray &Array) = default;

	const T &At(std::size_t Index) const;
	T operator[](std::size_t Index) const;
	T &operator[](std::size_t Index);

	T &First() { return this->operator[](0); }
	T &Last() { return this->operator[](m_Size - 1); }

	const T &First() const { return At(0); }
	const T &Last() const { return At(m_Size - 1); }

	void erase(const T *pItem);

	int Size() const;

	void Resize(int NewSize);

	constexpr static int sCapacity = StackCapacity;
	constexpr int Capacity() const { return StackCapacity; }

	bool IsEmpty() const;

	int IndexOf(const T &Item) const;

	bool Contains(const T &Item) const;

	void Add(const T &Value);

	void RemoveFirst();
	void RemoveLast();

	bool RemoveOne(const T &Item);

	void RemoveAt(std::size_t Index);

	void Clear();

protected:
	void incFirst()
	{
		++m_FirstIndex;
		if(m_FirstIndex == StackCapacity)
		{
			m_FirstIndex = 0;
		}
	}

	T m_Data[StackCapacity];
	int m_Size = 0;
	int m_FirstIndex = 0;
};

template<class T, int StackCapacity>
inline icFifoArray<T, StackCapacity>::icFifoArray(std::initializer_list<T> list)
{
	for(const T &Element : list)
	{
		Add(Element);
	}
}

template<class T, int StackCapacity>
const T &icFifoArray<T, StackCapacity>::At(std::size_t Index) const
{
	return m_Data[(Index + m_FirstIndex) % StackCapacity];
}

template<class T, int StackCapacity>
T icFifoArray<T, StackCapacity>::operator[](std::size_t Index) const
{
	return m_Data[(Index + m_FirstIndex) % StackCapacity];
}

template<class T, int StackCapacity>
T &icFifoArray<T, StackCapacity>::operator[](std::size_t Index)
{
	return m_Data[(Index + m_FirstIndex) % StackCapacity];
}

template<class T, int StackCapacity>
int icFifoArray<T, StackCapacity>::Size() const
{
	return m_Size;
}

template<class T, int StackCapacity>
void icFifoArray<T, StackCapacity>::Resize(int NewSize)
{
	m_Size = NewSize;
}

template<class T, int StackCapacity>
bool icFifoArray<T, StackCapacity>::IsEmpty() const
{
	return m_Size == 0;
}

template<class T, int StackCapacity>
int icFifoArray<T, StackCapacity>::IndexOf(const T &Item) const
{
	for(int i = 0; i < Size(); ++i)
	{
		if(At(i) == Item)
			return i;
	}
	return -1;
}

template<class T, int StackCapacity>
inline bool icFifoArray<T, StackCapacity>::Contains(const T &Item) const
{
	return IndexOf(Item) >= 0;
}

template<class T, int StackCapacity>
inline void icFifoArray<T, StackCapacity>::Add(const T &Value)
{
	if(m_Size == StackCapacity)
	{
		m_Data[m_FirstIndex] = Value;
		incFirst();
	}
	else
	{
		m_Data[(m_Size + m_FirstIndex) % StackCapacity] = Value;
		++m_Size;
	}
}

template<class T, int StackCapacity>
void icFifoArray<T, StackCapacity>::RemoveFirst()
{
	incFirst();
	--m_Size;
}

template<class T, int StackCapacity>
void icFifoArray<T, StackCapacity>::RemoveLast()
{
	--m_Size;
}

template<class T, int StackCapacity>
bool icFifoArray<T, StackCapacity>::RemoveOne(const T &Item)
{
	int Index = IndexOf(Item);
	if(Index < 0)
		return false;

	RemoveAt(Index);
	return true;
}

template<class T, int StackCapacity>
void icFifoArray<T, StackCapacity>::RemoveAt(std::size_t Index)
{
	if(m_Size > Index * 2)
	{
		// Move the head
		for(std::size_t i = Index; i > 0; --i)
		{
			m_Data[(i + m_FirstIndex) % StackCapacity] = m_Data[(i - 1 + m_FirstIndex) % StackCapacity];
		}
		incFirst();
	}
	else
	{
		// Move the tail
		for(std::size_t i = Index; i < m_Size - 1; ++i)
		{
			m_Data[(i + m_FirstIndex) % StackCapacity] = m_Data[(i + 1 + m_FirstIndex) % StackCapacity];
		}
	}

	--m_Size;
}

template<class T, int StackCapacity>
void icFifoArray<T, StackCapacity>::Clear()
{
	m_Size = 0;
	m_FirstIndex = 0;
}

#endif // BASE_TL_IC_FIFO_H

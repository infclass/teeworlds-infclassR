#ifndef BASE_TL_IC_ARRAY_H
#define BASE_TL_IC_ARRAY_H

#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <optional>

template <class T, int StackCapacity>
class icArray
{
public:
	constexpr icArray() = default;
	constexpr icArray(std::initializer_list<T> list);
	icArray(T (&Array)[StackCapacity]);

	const T &At(std::size_t Index) const { return m_Data[Index]; }
	T operator[](std::size_t Index) const { return m_Data[Index]; }
	T &operator[](std::size_t Index) { return m_Data[Index]; }

	T *Data() { return &m_Data[0]; }
	const T *Data() const { return &m_Data[0]; }

	T &First() { return m_Data[0]; }
	T &Last() { return m_Data[m_Size - 1]; }

	const T &First() const { return m_Data[0]; }
	const T &Last() const { return m_Data[m_Size - 1]; }

	void erase(const T *pItem);

	constexpr std::size_t Size() const;

	void Resize(std::size_t NewSize);

	constexpr std::size_t Capacity() const;

	bool IsEmpty() const;

	std::optional<std::size_t> IndexOf(const T &Item) const;

	constexpr bool Contains(const T &Item) const;

	constexpr void Add(const T &Value);

	void RemoveLast();

	bool RemoveOne(const T &Item);

	void RemoveAt(std::size_t Index);

	void Clear();

	T *begin() { return &m_Data[0]; }
	T *end() { return &m_Data[m_Size]; }

	auto rbegin() { return std::reverse_iterator(end()); }
	auto rend() { return std::reverse_iterator(begin()); }

	const T *begin() const { return &m_Data[0]; }
	const T *end() const { return &m_Data[m_Size]; }

	using size_type = std::size_t;

protected:
	T m_Data[StackCapacity] = {};
	std::size_t m_Size = 0;
};

namespace std
{

template<class T, int StackCapacity, typename Predicate>
inline typename icArray<T, StackCapacity>::size_type
erase_if(icArray<T, StackCapacity> &container, Predicate predicate)
{
	const auto HadSize = container.Size();

	for (auto it = container.rbegin(); it != container.rend(); ++it) {
		if(predicate(*it))
			container.erase(&*it);
	}

	return HadSize - container.Size();
}

} // namespace std

template<class T, int StackCapacity>
inline constexpr icArray<T, StackCapacity>::icArray(std::initializer_list<T> list)
{
	for(const T &Element : list)
	{
		Add(Element);
	}
}

template<class T, int StackCapacity>
inline icArray<T, StackCapacity>::icArray(T (&Array)[StackCapacity])
{
	for(const T &Element : Array)
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
constexpr std::size_t icArray<T, StackCapacity>::Size() const
{
	return m_Size;
}

template<class T, int StackCapacity>
void icArray<T, StackCapacity>::Resize(std::size_t NewSize)
{
	m_Size = NewSize;
}

template<class T, int StackCapacity>
constexpr std::size_t icArray<T, StackCapacity>::Capacity() const
{
	return StackCapacity;
}

template<class T, int StackCapacity>
bool icArray<T, StackCapacity>::IsEmpty() const
{
	return m_Size == 0;
}

template<class T, int StackCapacity>
std::optional<std::size_t> icArray<T, StackCapacity>::IndexOf(const T &Item) const
{
	for(std::size_t i = 0; i < Size(); ++i)
	{
		if(m_Data[i] == Item)
			return i;
	}
	return {};
}

template<class T, int StackCapacity>
inline constexpr bool icArray<T, StackCapacity>::Contains(const T &Item) const
{
	return IndexOf(Item) >= 0;
}

template<class T, int StackCapacity>
inline constexpr void icArray<T, StackCapacity>::Add(const T &Value)
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
	auto OptIndex = IndexOf(Item);
	if(!OptIndex.has_value())
		return false;

	RemoveAt(OptIndex.value());
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

/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "snapshot.h"
#include "compression.h"
#include "uuid_manager.h"

#include <climits>
#include <cstdlib>

#include <base/math.h>
#include <base/system.h>

#include <game/generated/protocolglue.h>

// CSnapshot

const CSnapshotItem *CSnapshot::GetItem(int Index) const
{
	return (const CSnapshotItem *)(DataStart() + Offsets()[Index]);
}

const CSnapshot CSnapshot::ms_EmptySnapshot;

int CSnapshot::GetItemSize(int Index) const
{
	if(Index == m_NumItems - 1)
		return (m_DataSize - Offsets()[Index]) - sizeof(CSnapshotItem);
	return (Offsets()[Index + 1] - Offsets()[Index]) - sizeof(CSnapshotItem);
}

int CSnapshot::GetItemType(int Index) const
{
	int InternalType = GetItem(Index)->Type();
	return GetExternalItemType(InternalType);
}

int CSnapshot::GetExternalItemType(int InternalType) const
{
	if(InternalType < OFFSET_UUID_TYPE)
	{
		return InternalType;
	}

	int TypeItemIndex = GetItemIndex(InternalType); // NETOBJTYPE_EX
	if(TypeItemIndex == -1 || GetItemSize(TypeItemIndex) < (int)sizeof(CUuid))
	{
		return InternalType;
	}
	const CSnapshotItem *pTypeItem = GetItem(TypeItemIndex);
	CUuid Uuid;
	for(size_t i = 0; i < sizeof(CUuid) / sizeof(int32_t); i++)
		uint_to_bytes_be(&Uuid.m_aData[i * sizeof(int32_t)], pTypeItem->Data()[i]);

	return g_UuidManager.LookupUuid(Uuid);
}

int CSnapshot::GetItemIndex(int Key) const
{
	// TODO: OPT: this should not be a linear search. very bad
	for(int i = 0; i < m_NumItems; i++)
	{
		if(GetItem(i)->Key() == Key)
			return i;
	}
	return -1;
}

const void *CSnapshot::FindItem(int Type, int Id) const
{
	int InternalType = Type;
	if(Type >= OFFSET_UUID)
	{
		CUuid TypeUuid = g_UuidManager.GetUuid(Type);
		int aTypeUuidItem[sizeof(CUuid) / sizeof(int32_t)];
		for(size_t i = 0; i < sizeof(CUuid) / sizeof(int32_t); i++)
			aTypeUuidItem[i] = bytes_be_to_uint(&TypeUuid.m_aData[i * sizeof(int32_t)]);

		bool Found = false;
		for(int i = 0; i < m_NumItems; i++)
		{
			const CSnapshotItem *pItem = GetItem(i);
			if(pItem->Type() == 0 && pItem->Id() >= OFFSET_UUID_TYPE) // NETOBJTYPE_EX
			{
				if(mem_comp(pItem->Data(), aTypeUuidItem, sizeof(CUuid)) == 0)
				{
					InternalType = pItem->Id();
					Found = true;
					break;
				}
			}
		}
		if(!Found)
		{
			return nullptr;
		}
	}
	int Index = GetItemIndex((InternalType << 16) | Id);
	return Index < 0 ? nullptr : GetItem(Index)->Data();
}

unsigned CSnapshot::Crc()
{
	unsigned int Crc = 0;

	for(int i = 0; i < m_NumItems; i++)
	{
		const CSnapshotItem *pItem = GetItem(i);
		int Size = GetItemSize(i);

		for(size_t b = 0; b < Size / sizeof(int32_t); b++)
			Crc += pItem->Data()[b];
	}
	return Crc;
}

void CSnapshot::DebugDump()
{
	dbg_msg("snapshot", "data_size=%d num_items=%d", m_DataSize, m_NumItems);
	for(int i = 0; i < m_NumItems; i++)
	{
		const CSnapshotItem *pItem = GetItem(i);
		int Size = GetItemSize(i);
		dbg_msg("snapshot", "\ttype=%d id=%d", pItem->Type(), pItem->Id());
		for(size_t b = 0; b < Size / sizeof(int32_t); b++)
			dbg_msg("snapshot", "\t\t%3d %12d\t%08x", (int)b, pItem->Data()[b], pItem->Data()[b]);
	}
}

bool CSnapshot::IsValid(size_t ActualSize) const
{
	// validate total size
	if(ActualSize < sizeof(CSnapshot) || m_NumItems < 0 || m_DataSize < 0 || ActualSize != TotalSize())
		return false;

	// validate item offsets
	const int *pOffsets = Offsets();
	for(int Index = 0; Index < m_NumItems; Index++)
		if(pOffsets[Index] < 0 || pOffsets[Index] > m_DataSize)
			return false;

	// validate item sizes
	for(int Index = 0; Index < m_NumItems; Index++)
		if(GetItemSize(Index) < 0) // the offsets must be validated before using this
			return false;

	return true;
}

// CSnapshotDelta

enum
{
	HASHLIST_SIZE = 256,
	HASHLIST_BUCKET_SIZE = 64,
};

struct CItemList
{
	int m_Num;
	int m_aKeys[HASHLIST_BUCKET_SIZE];
	int m_aIndex[HASHLIST_BUCKET_SIZE];
};

inline size_t CalcHashId(int Key)
{
	// djb2 (http://www.cse.yorku.ca/~oz/hash.html)
	unsigned Hash = 5381;
	for(unsigned Shift = 0; Shift < sizeof(int); Shift++)
		Hash = ((Hash << 5) + Hash) + ((Key >> (Shift * 8)) & 0xFF);
	return Hash % HASHLIST_SIZE;
}

static void GenerateHash(CItemList *pHashlist, const CSnapshot *pSnapshot)
{
	for(int i = 0; i < HASHLIST_SIZE; i++)
		pHashlist[i].m_Num = 0;

	for(int i = 0; i < pSnapshot->NumItems(); i++)
	{
		int Key = pSnapshot->GetItem(i)->Key();
		size_t HashId = CalcHashId(Key);
		if(pHashlist[HashId].m_Num < HASHLIST_BUCKET_SIZE)
		{
			pHashlist[HashId].m_aIndex[pHashlist[HashId].m_Num] = i;
			pHashlist[HashId].m_aKeys[pHashlist[HashId].m_Num] = Key;
			pHashlist[HashId].m_Num++;
		}
	}
}

static int GetItemIndexHashed(int Key, const CItemList *pHashlist)
{
	size_t HashId = CalcHashId(Key);
	for(int i = 0; i < pHashlist[HashId].m_Num; i++)
	{
		if(pHashlist[HashId].m_aKeys[i] == Key)
			return pHashlist[HashId].m_aIndex[i];
	}

	return -1;
}

int CSnapshotDelta::DiffItem(const int *pPast, const int *pCurrent, int *pOut, int Size)
{
	int Needed = 0;
	while(Size)
	{
		// subtraction with wrapping by casting to unsigned
		*pOut = (unsigned)*pCurrent - (unsigned)*pPast;
		Needed |= *pOut;
		pOut++;
		pPast++;
		pCurrent++;
		Size--;
	}

	return Needed;
}

void CSnapshotDelta::UndiffItem(const int *pPast, const int *pDiff, int *pOut, int Size, int *pDataRate)
{
	while(Size)
	{
		// addition with wrapping by casting to unsigned
		*pOut = (unsigned)*pPast + (unsigned)*pDiff;

		if(*pDiff == 0)
			*pDataRate += 1;
		else
		{
			unsigned char aBuf[CVariableInt::MAX_BYTES_PACKED];
			unsigned char *pEnd = CVariableInt::Pack(aBuf, *pDiff, sizeof(aBuf));
			*pDataRate += (int)(pEnd - (unsigned char *)aBuf) * 8;
		}

		pOut++;
		pPast++;
		pDiff++;
		Size--;
	}
}

CSnapshotDelta::CSnapshotDelta()
{
	mem_zero(m_aItemSizes, sizeof(m_aItemSizes));
	mem_zero(m_aSnapshotDataRate, sizeof(m_aSnapshotDataRate));
	mem_zero(m_aSnapshotDataUpdates, sizeof(m_aSnapshotDataUpdates));
	mem_zero(&m_Empty, sizeof(m_Empty));
}

CSnapshotDelta::CSnapshotDelta(const CSnapshotDelta &Old)
{
	mem_copy(m_aItemSizes, Old.m_aItemSizes, sizeof(m_aItemSizes));
	mem_copy(m_aSnapshotDataRate, Old.m_aSnapshotDataRate, sizeof(m_aSnapshotDataRate));
	mem_copy(m_aSnapshotDataUpdates, Old.m_aSnapshotDataUpdates, sizeof(m_aSnapshotDataUpdates));
	mem_zero(&m_Empty, sizeof(m_Empty));
}

void CSnapshotDelta::SetStaticsize(int ItemType, int Size)
{
	if(ItemType < 0 || ItemType >= MAX_NETOBJSIZES)
		return;
	m_aItemSizes[ItemType] = Size;
}

const CSnapshotDelta::CData *CSnapshotDelta::EmptyDelta() const
{
	return &m_Empty;
}

// TODO: OPT: this should be made much faster
int CSnapshotDelta::CreateDelta(const CSnapshot *pFrom, CSnapshot *pTo, void *pDstData)
{
	CData *pDelta = (CData *)pDstData;
	int *pData = (int *)pDelta->m_aData;

	pDelta->m_NumDeletedItems = 0;
	pDelta->m_NumUpdateItems = 0;
	pDelta->m_NumTempItems = 0;

	CItemList aHashlist[HASHLIST_SIZE];
	GenerateHash(aHashlist, pTo);

	// pack deleted stuff
	for(int i = 0; i < pFrom->NumItems(); i++)
	{
		const CSnapshotItem *pFromItem = pFrom->GetItem(i);
		if(GetItemIndexHashed(pFromItem->Key(), aHashlist) == -1)
		{
			// deleted
			pDelta->m_NumDeletedItems++;
			*pData = pFromItem->Key();
			pData++;
		}
	}

	GenerateHash(aHashlist, pFrom);

	// fetch previous indices
	// we do this as a separate pass because it helps the cache
	int aPastIndices[CSnapshot::MAX_ITEMS];
	const int NumItems = pTo->NumItems();
	for(int i = 0; i < NumItems; i++)
	{
		const CSnapshotItem *pCurItem = pTo->GetItem(i); // O(1) .. O(n)
		aPastIndices[i] = GetItemIndexHashed(pCurItem->Key(), aHashlist); // O(n) .. O(n^n)
	}

	for(int i = 0; i < NumItems; i++)
	{
		// do delta
		const int ItemSize = pTo->GetItemSize(i); // O(1) .. O(n)
		const CSnapshotItem *pCurItem = pTo->GetItem(i); // O(1) .. O(n)
		const int PastIndex = aPastIndices[i];
		const bool IncludeSize = pCurItem->Type() >= MAX_NETOBJSIZES || !m_aItemSizes[pCurItem->Type()];

		if(PastIndex != -1)
		{
			int *pItemDataDst = pData + 3;

			const CSnapshotItem *pPastItem = pFrom->GetItem(PastIndex);

			if(!IncludeSize)
				pItemDataDst = pData + 2;

			if(DiffItem(pPastItem->Data(), pCurItem->Data(), pItemDataDst, ItemSize / sizeof(int32_t)))
			{
				*pData++ = pCurItem->Type();
				*pData++ = pCurItem->Id();
				if(IncludeSize)
					*pData++ = ItemSize / sizeof(int32_t);
				pData += ItemSize / sizeof(int32_t);
				pDelta->m_NumUpdateItems++;
			}
		}
		else
		{
			*pData++ = pCurItem->Type();
			*pData++ = pCurItem->Id();
			if(IncludeSize)
				*pData++ = ItemSize / sizeof(int32_t);

			mem_copy(pData, pCurItem->Data(), ItemSize);
			pData += ItemSize / sizeof(int32_t);
			pDelta->m_NumUpdateItems++;
		}
	}

	if(!pDelta->m_NumDeletedItems && !pDelta->m_NumUpdateItems && !pDelta->m_NumTempItems)
		return 0;

	return (int)((char *)pData - (char *)pDstData);
}

static int RangeCheck(void *pEnd, void *pPtr, int Size)
{
	if((const char *)pPtr + Size > (const char *)pEnd)
		return -1;
	return 0;
}

int CSnapshotDelta::UnpackDelta(const CSnapshot *pFrom, CSnapshot *pTo, const void *pSrcData, int DataSize)
{
	CData *pDelta = (CData *)pSrcData;
	int *pData = (int *)pDelta->m_aData;
	int *pEnd = (int *)(((char *)pSrcData + DataSize));

	CSnapshotBuilder Builder;
	Builder.Init();

	// unpack deleted stuff
	int *pDeleted = pData;
	if(pDelta->m_NumDeletedItems < 0)
		return -201;
	pData += pDelta->m_NumDeletedItems;
	if(pData > pEnd)
		return -101;

	// copy all non deleted stuff
	for(int i = 0; i < pFrom->NumItems(); i++)
	{
		const CSnapshotItem *pFromItem = pFrom->GetItem(i);
		const int ItemSize = pFrom->GetItemSize(i);
		bool Keep = true;
		for(int d = 0; d < pDelta->m_NumDeletedItems; d++)
		{
			if(pDeleted[d] == pFromItem->Key())
			{
				Keep = false;
				break;
			}
		}

		if(Keep)
		{
			void *pObj = Builder.NewItem(pFromItem->Type(), pFromItem->Id(), ItemSize);
			if(!pObj)
				return -301;

			// keep it
			mem_copy(pObj, pFromItem->Data(), ItemSize);
		}
	}

	// unpack updated stuff
	for(int i = 0; i < pDelta->m_NumUpdateItems; i++)
	{
		if(pData + 2 > pEnd)
			return -102;

		const int Type = *pData++;
		if(Type < 0 || Type > CSnapshot::MAX_TYPE)
			return -202;

		const int Id = *pData++;
		if(Id < 0 || Id > CSnapshot::MAX_ID)
			return -203;

		int ItemSize;
		if(Type < MAX_NETOBJSIZES && m_aItemSizes[Type])
			ItemSize = m_aItemSizes[Type];
		else
		{
			if(pData + 1 > pEnd)
				return -103;
			if(*pData < 0 || (size_t)*pData > INT_MAX / sizeof(int32_t))
				return -204;
			ItemSize = (*pData++) * sizeof(int32_t);
		}

		if(ItemSize < 0 || RangeCheck(pEnd, pData, ItemSize))
			return -205;

		const int Key = (Type << 16) | Id;

		// create the item if needed
		int *pNewData = Builder.GetItemData(Key);
		if(!pNewData)
			pNewData = (int *)Builder.NewItem(Type, Id, ItemSize);

		if(!pNewData)
			return -302;

		const int FromIndex = pFrom->GetItemIndex(Key);
		if(FromIndex != -1)
		{
			// we got an update so we need to apply the diff
			UndiffItem(pFrom->GetItem(FromIndex)->Data(), pData, pNewData, ItemSize / sizeof(int32_t), &m_aSnapshotDataRate[Type]);
		}
		else // no previous, just copy the pData
		{
			mem_copy(pNewData, pData, ItemSize);
			m_aSnapshotDataRate[Type] += ItemSize * 8;
		}
		m_aSnapshotDataUpdates[Type]++;

		pData += ItemSize / sizeof(int32_t);
	}

	// finish up
	return Builder.Finish(pTo);
}

// CSnapshotStorage

void CSnapshotStorage::Init()
{
	m_pFirst = 0;
	m_pLast = 0;
}

void CSnapshotStorage::PurgeAll()
{
	CHolder *pHolder = m_pFirst;

	while(pHolder)
	{
		CHolder *pNext = pHolder->m_pNext;
		free(pHolder);
		pHolder = pNext;
	}

	// no more snapshots in storage
	m_pFirst = 0;
	m_pLast = 0;
}

void CSnapshotStorage::PurgeUntil(int Tick)
{
	CHolder *pHolder = m_pFirst;

	while(pHolder)
	{
		CHolder *pNext = pHolder->m_pNext;
		if(pHolder->m_Tick >= Tick)
			return; // no more to remove
		free(pHolder);

		// did we come to the end of the list?
		if(!pNext)
			break;

		m_pFirst = pNext;
		pNext->m_pPrev = 0x0;

		pHolder = pNext;
	}

	// no more snapshots in storage
	m_pFirst = 0;
	m_pLast = 0;
}

void CSnapshotStorage::Add(int Tick, int64_t Tagtime, int DataSize, const void *pData, int AltDataSize, const void *pAltData)
{
	// allocate memory for holder + snapshot_data
	int TotalSize = sizeof(CHolder) + DataSize;

	if(AltDataSize > 0)
	{
		TotalSize += AltDataSize;
	}

	CHolder *pHolder = (CHolder *)malloc(TotalSize);

	// set data
	pHolder->m_Tick = Tick;
	pHolder->m_Tagtime = Tagtime;
	pHolder->m_SnapSize = DataSize;
	pHolder->m_pSnap = (CSnapshot *)(pHolder + 1);
	mem_copy(pHolder->m_pSnap, pData, DataSize);

	if(AltDataSize > 0) // create alternative if wanted
	{
		pHolder->m_pAltSnap = (CSnapshot *)(((char *)pHolder->m_pSnap) + DataSize);
		mem_copy(pHolder->m_pAltSnap, pAltData, AltDataSize);
		pHolder->m_AltSnapSize = AltDataSize;
	}
	else
	{
		pHolder->m_pAltSnap = 0;
		pHolder->m_AltSnapSize = 0;
	}

	// link
	pHolder->m_pNext = 0;
	pHolder->m_pPrev = m_pLast;
	if(m_pLast)
		m_pLast->m_pNext = pHolder;
	else
		m_pFirst = pHolder;
	m_pLast = pHolder;
}

int CSnapshotStorage::Get(int Tick, int64_t *pTagtime, const CSnapshot **ppData, const CSnapshot **ppAltData)
{
	CHolder *pHolder = m_pFirst;

	while(pHolder)
	{
		if(pHolder->m_Tick == Tick)
		{
			if(pTagtime)
				*pTagtime = pHolder->m_Tagtime;
			if(ppData)
				*ppData = pHolder->m_pSnap;
			if(ppAltData)
				*ppAltData = pHolder->m_pAltSnap;
			return pHolder->m_SnapSize;
		}

		pHolder = pHolder->m_pNext;
	}

	return -1;
}

// CSnapshotBuilder
CSnapshotBuilder::CSnapshotBuilder()
{
	m_NumExtendedItemTypes = 0;
}

void CSnapshotBuilder::Init(bool Sixup)
{
	m_DataSize = 0;
	m_NumItems = 0;
	m_Sixup = Sixup;

	for(int i = 0; i < m_NumExtendedItemTypes; i++)
	{
		AddExtendedItemType(i);
	}
}

CSnapshotItem *CSnapshotBuilder::GetItem(int Index)
{
	return (CSnapshotItem *)&(m_aData[m_aOffsets[Index]]);
}

int *CSnapshotBuilder::GetItemData(int Key)
{
	for(int i = 0; i < m_NumItems; i++)
	{
		if(GetItem(i)->Key() == Key)
			return GetItem(i)->Data();
	}
	return 0;
}

int CSnapshotBuilder::Finish(void *pSnapData)
{
	// flatten and make the snapshot
	CSnapshot *pSnap = (CSnapshot *)pSnapData;
	pSnap->m_DataSize = m_DataSize;
	pSnap->m_NumItems = m_NumItems;
	mem_copy(pSnap->Offsets(), m_aOffsets, pSnap->OffsetSize());
	mem_copy(pSnap->DataStart(), m_aData, m_DataSize);
	return pSnap->TotalSize();
}

int CSnapshotBuilder::GetTypeFromIndex(int Index)
{
	return CSnapshot::MAX_TYPE - Index;
}

void CSnapshotBuilder::AddExtendedItemType(int Index)
{
	dbg_assert(0 <= Index && Index < m_NumExtendedItemTypes, "index out of range");
	int TypeId = m_aExtendedItemTypes[Index];
	CUuid Uuid = g_UuidManager.GetUuid(TypeId);
	int *pUuidItem = (int *)NewItem(0, GetTypeFromIndex(Index), sizeof(Uuid)); // NETOBJTYPE_EX
	if(pUuidItem)
	{
		for(size_t i = 0; i < sizeof(CUuid) / sizeof(int32_t); i++)
			pUuidItem[i] = bytes_be_to_uint(&Uuid.m_aData[i * sizeof(int32_t)]);
	}
}

int CSnapshotBuilder::GetExtendedItemTypeIndex(int TypeId)
{
	for(int i = 0; i < m_NumExtendedItemTypes; i++)
	{
		if(m_aExtendedItemTypes[i] == TypeId)
		{
			return i;
		}
	}
	dbg_assert(m_NumExtendedItemTypes < MAX_EXTENDED_ITEM_TYPES, "too many extended item types");
	int Index = m_NumExtendedItemTypes;
	m_aExtendedItemTypes[Index] = TypeId;
	m_NumExtendedItemTypes++;
	return Index;
}

void *CSnapshotBuilder::NewItem(int Type, int Id, int Size)
{
	if(Id == -1)
	{
		return 0;
	}

	if(m_DataSize + sizeof(CSnapshotItem) + Size >= CSnapshot::MAX_SIZE ||
		m_NumItems + 1 >= CSnapshot::MAX_ITEMS)
	{
		dbg_assert(m_DataSize < CSnapshot::MAX_SIZE, "too much data");
		dbg_assert(m_NumItems < CSnapshot::MAX_ITEMS, "too many items");
		return 0;
	}

	bool Extended = false;
	if(Type >= OFFSET_UUID)
	{
		Extended = true;
		Type = GetTypeFromIndex(GetExtendedItemTypeIndex(Type));
	}

	CSnapshotItem *pObj = (CSnapshotItem *)(m_aData + m_DataSize);

	if(m_Sixup && !Extended)
	{
		if(Type >= 0)
			Type = Obj_SixToSeven(Type);
		else
			Type *= -1;

		if(Type < 0)
			return pObj;
	}
	else if(Type < 0)
		return nullptr;

	mem_zero(pObj, sizeof(CSnapshotItem) + Size);
	pObj->m_TypeAndId = (Type << 16) | Id;
	m_aOffsets[m_NumItems] = m_DataSize;
	m_DataSize += sizeof(CSnapshotItem) + Size;
	m_NumItems++;

	return pObj->Data();
}

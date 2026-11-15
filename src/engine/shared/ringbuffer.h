/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_RINGBUFFER_H
#define ENGINE_SHARED_RINGBUFFER_H

#include <cstdlib>
#include <functional>

class CRingBufferBase
{
public:
	class CItem
	{
	public:
		CItem *m_pPrev;
		CItem *m_pNext;
		int m_Free;
		int m_Size;
	};

protected:
	CItem *m_pBuffer;

	CItem *m_pProduce;
	CItem *m_pConsume;

	CItem *m_pFirst;
	CItem *m_pLast;

	int m_Size;
	int m_Flags;

	std::function<void(void *pCurrent)> m_PopCallback = nullptr;

	inline CItem *NextBlock(CItem *pItem)
	{
		if(pItem->m_pNext)
			return pItem->m_pNext;
		return m_pFirst;
	}

	inline CItem *PrevBlock(CItem *pItem)
	{
		if(pItem->m_pPrev)
			return pItem->m_pPrev;
		return m_pLast;
	}

	CItem *MergeBack(CItem *pItem);

	void *Allocate(int Size);

	inline void *Prev(void *pCurrent)
	{
		CItem *pItem = ((CItem *)pCurrent) - 1;
		while(true)
		{
			pItem = PrevBlock(pItem);
			if(pItem == m_pProduce)
				return nullptr;
			if(!pItem->m_Free)
				return pItem + 1;
		}
	}

	inline void *Next(void *pCurrent)
	{
		CItem *pItem = ((CItem *)pCurrent) - 1;
		while(true)
		{
			pItem = NextBlock(pItem);
			if(pItem == m_pProduce)
				return nullptr;
			if(!pItem->m_Free)
				return pItem + 1;
		}
	}

	inline void *First()
	{
		if(m_pConsume->m_Free)
			return nullptr;
		return (void *)(m_pConsume + 1);
	}

	inline void *Last()
	{
		return Prev(m_pProduce + 1);
	}

	void Init(void *pMemory, int Size, int Flags);
	int PopFirst();
	void SetPopCallback(std::function<void(void *pCurrent)> PopCallback);

public:
	enum
	{
		// Will start to destroy items to try to fit the next one
		FLAG_RECYCLE = 1
	};
	static constexpr int ITEM_SIZE = sizeof(CItem);

	void Clear();
};

template<typename T>
class CTypedRingBuffer : public CRingBufferBase
{
public:
	T *Allocate(int Size) { return (T *)CRingBufferBase::Allocate(Size); }
	int PopFirst() { return CRingBufferBase::PopFirst(); }
	void SetPopCallback(const std::function<void(T *pCurrent)> &PopCallback)
	{
		CRingBufferBase::SetPopCallback([PopCallback](void *pCurrent) {
			PopCallback((T *)pCurrent);
		});
	}

	T *Prev(T *pCurrent) { return (T *)CRingBufferBase::Prev(pCurrent); }
	T *Next(T *pCurrent) { return (T *)CRingBufferBase::Next(pCurrent); }
	T *First() { return (T *)CRingBufferBase::First(); }
	T *Last() { return (T *)CRingBufferBase::Last(); }
};

template<typename T, int TSIZE, int TFLAGS = 0>
class CStaticRingBuffer : public CTypedRingBuffer<T>
{
	unsigned char m_aBuffer[TSIZE];

public:
	CStaticRingBuffer() { Init(); }

	void Init() { CRingBufferBase::Init(m_aBuffer, TSIZE, TFLAGS); }
};

template<typename T>
class CDynamicRingBuffer : public CTypedRingBuffer<T>
{
	unsigned char *m_pBuffer = nullptr;

public:
	CDynamicRingBuffer(int Size, int Flags = 0) { Init(Size, Flags); }

	~CDynamicRingBuffer()
	{
		free(m_pBuffer);
	}

	void Init(int Size, int Flags)
	{
		free(m_pBuffer);
		m_pBuffer = static_cast<unsigned char *>(malloc(Size));
		CRingBufferBase::Init(m_pBuffer, Size, Flags);
	}
};

#endif

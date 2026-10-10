#include <base/lock.h>
#include <base/sphore.h>
#include <base/thread.h>

#include <gtest/gtest.h>

static void Nothing(void *pUser)
{
	(void)pUser;
}

TEST(Thread, Detach)
{
	void *pThread = thread_init(Nothing, nullptr, "detach");
	thread_detach(pThread);
}

static void SetToOne(void *pUser)
{
	*(int *)pUser = 1;
}

TEST(Thread, Wait)
{
	int Integer = 0;
	void *pThread = thread_init(SetToOne, &Integer, "wait");
	thread_wait(pThread);
	EXPECT_EQ(Integer, 1);
}

TEST(Thread, Yield)
{
	thread_yield();
}

TEST(Thread, Semaphore)
{
	SEMAPHORE Semaphore;
	sphore_init(&Semaphore);
	sphore_destroy(&Semaphore);
}

TEST(Thread, SemaphoreSingleThreaded)
{
	SEMAPHORE Semaphore;
	sphore_init(&Semaphore);
	sphore_signal(&Semaphore);
	sphore_signal(&Semaphore);
	sphore_wait(&Semaphore);
	sphore_wait(&Semaphore);
	sphore_destroy(&Semaphore);
}

TEST(Thread, SemaphoreWrapperSingleThreaded)
{
	CSemaphore Semaphore;
	EXPECT_EQ(Semaphore.GetApproximateValue(), 0);
	Semaphore.Signal();
	EXPECT_EQ(Semaphore.GetApproximateValue(), 1);
	Semaphore.Signal();
	EXPECT_EQ(Semaphore.GetApproximateValue(), 2);
	Semaphore.Wait();
	EXPECT_EQ(Semaphore.GetApproximateValue(), 1);
	Semaphore.Wait();
	EXPECT_EQ(Semaphore.GetApproximateValue(), 0);
}

static void SemaphoreThread(void *pUser)
{
	SEMAPHORE *pSemaphore = (SEMAPHORE *)pUser;
	sphore_wait(pSemaphore);
}

TEST(Thread, SemaphoreMultiThreaded)
{
	SEMAPHORE Semaphore;
	sphore_init(&Semaphore);
	sphore_signal(&Semaphore);
	void *pThread = thread_init(SemaphoreThread, &Semaphore, "semaphore");
	thread_wait(pThread);
	sphore_destroy(&Semaphore);
}

static void LockThread(void *pUser)
{
	CLock *pLock = (CLock *)pUser;
	pLock->lock();
	pLock->unlock();
}

TEST(Thread, Lock)
{
	CLock Lock;
	Lock.lock();
	void *pThread = thread_init(LockThread, &Lock, "lock");
	Lock.unlock();
	thread_wait(pThread);
}

#include <engine/external/readerwriterqueue/ReaderWriterQueue.h>
#include <memory>
#include <thread>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

TEST(Thread, ReaderWriterQueueSingleThreaded)
{
	moodycamel::ReaderWriterQueue<std::unique_ptr<int>> Queue(16);
	EXPECT_TRUE(Queue.try_enqueue(std::make_unique<int>(42)));
	std::unique_ptr<int> Val;
	EXPECT_TRUE(Queue.try_dequeue(Val));
	ASSERT_NE(Val, nullptr);
	EXPECT_EQ(*Val, 42);
	EXPECT_FALSE(Queue.try_dequeue(Val));
}

TEST(Thread, ReaderWriterQueueMultiThreadedSPSC)
{
	constexpr int Count = 50000;
	moodycamel::ReaderWriterQueue<int> Queue(1024);
	std::thread Producer([&]() {
		for(int i = 0; i < Count; ++i)
		{
			while(!Queue.try_enqueue(i))
			{
				std::this_thread::yield();
			}
		}
	});

	int Received = 0;
	while(Received < Count)
	{
		int Val = -1;
		if(Queue.try_dequeue(Val))
		{
			EXPECT_EQ(Val, Received);
			++Received;
		}
		else
		{
			std::this_thread::yield();
		}
	}
	Producer.join();
	EXPECT_EQ(Received, Count);
}

TEST(Thread, SeqlockVisualizerConcurrentReadWrite)
{
	constexpr int NumBars = 32;
	constexpr int Iterations = 50000;
	std::atomic<uint32_t> Seq{0};
	std::array<float, NumBars> SharedData{};
	SharedData.fill(0.0f);
	std::atomic<bool> Running{true};

	std::thread Writer([&]() {
		for(int i = 1; i <= Iterations; ++i)
		{
			Seq.fetch_add(1, std::memory_order_release);
			for(int j = 0; j < NumBars; ++j)
				SharedData[j] = static_cast<float>(i);
			Seq.fetch_add(1, std::memory_order_release);
		}
		Running.store(false, std::memory_order_release);
	});

	int SuccessfulReads = 0;
	while(Running.load(std::memory_order_acquire))
	{
		std::array<float, NumBars> LocalCopy{};
		uint32_t Seq1 = 0;
		uint32_t Seq2 = 0;
		do
		{
			Seq1 = Seq.load(std::memory_order_acquire);
			int Spins = 0;
			while(Seq1 & 1)
			{
				if(++Spins > 32)
				{
					std::this_thread::yield();
					break;
				}
#if defined(_MSC_VER)
				_mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
				__builtin_ia32_pause();
#else
				std::atomic_thread_fence(std::memory_order_relaxed);
#endif
				Seq1 = Seq.load(std::memory_order_acquire);
			}
			LocalCopy = SharedData;
			std::atomic_thread_fence(std::memory_order_acquire);
			Seq2 = Seq.load(std::memory_order_acquire);
		} while(Seq1 != Seq2);

		// Verify no torn reads
		const float First = LocalCopy[0];
		for(int j = 1; j < NumBars; ++j)
		{
			EXPECT_EQ(LocalCopy[j], First);
		}
		++SuccessfulReads;
	}

	Writer.join();
	EXPECT_GT(SuccessfulReads, 0);
}


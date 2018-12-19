#pragma once

#include <condition_variable>
#include <atomic>
#include <set>
#include <vector>

#ifndef INFINITE
#define INFINITE            0xFFFFFFFF  // Infinite timeou
#endif
#define WAIT_OBJECT_0       0
#define WAIT_TIMEOUT       -1

class CConditionEvent
{
public:
    CConditionEvent()
    {
        _count = 0;
    }
	void AddListener(std::shared_ptr<std::function<void()>> pFunc)
	{
		std::lock_guard<std::mutex> lock(_set_mutex);
		_callback_set.insert(pFunc);
	}

	void RemoveListener(std::shared_ptr<std::function<void()>> pFunc)
	{
		std::lock_guard<std::mutex> lock(_set_mutex);
		_callback_set.erase(pFunc);
	}

	virtual void SetEvent()
	{
		std::unique_lock<std::mutex> lock(_cv_mutex);
		SetCount(1);
		_cv.notify_all();
		lock.unlock();
		std::lock_guard<std::mutex> set_lock(_set_mutex);
		for (auto pFunc : _callback_set)
		{
			(*pFunc)();
		}
	}

	virtual void ResetEvent()
	{
		std::unique_lock<std::mutex> lock(_cv_mutex);
		SetCount(0);
	}

	virtual int Wait()
	{
		std::unique_lock<std::mutex> lock(_cv_mutex);
		_cv.wait(lock, [this] {return Pred(); });
		return 0;
	}

	virtual int WaitFor(unsigned int milliSeconds)
	{
		std::unique_lock<std::mutex> lock(_cv_mutex);
		bool success = false;
		if (milliSeconds == INFINITE)
		{
			success = true;
			_cv.wait(lock, [this] {return Pred(); });
		}
		else
		{
			success = _cv.wait_for(lock, std::chrono::milliseconds(milliSeconds), [this] {return Pred(); });
		}
		if (success)
		{
			return 0;
		}
		return -1;
	}

	virtual int WaitFor(std::shared_ptr<std::function<bool()>> pFunc, unsigned int milliSeconds)
	{
		std::unique_lock<std::mutex> lock(_cv_mutex);
		bool success = false;
		if (milliSeconds == INFINITE)
		{
			success = true;
			_cv.wait(lock, *pFunc);
		}
		else
		{
			success = _cv.wait_for(lock, std::chrono::milliseconds(milliSeconds), *pFunc);
		}
		if (success)
		{
			return 0;
		}
		return -1;
	}

	virtual bool Pred()
	{
		std::lock_guard<std::mutex> lock(_count_mutex);
		return _count > 0;
	}

	void MinusCount()
	{
		std::lock_guard<std::mutex> lock(_count_mutex);
		--_count;
	}

	void AddCount()
	{
		std::lock_guard<std::mutex> lock(_count_mutex);
		++_count;
	}

	void SetCount(long count)
	{
		std::lock_guard<std::mutex> lock(_count_mutex);
		_count = count;
	}

	~CConditionEvent()
	{
	}

protected:
	std::condition_variable	_cv;
	std::mutex _cv_mutex;

	std::mutex _count_mutex;
	long _count;

	std::mutex _set_mutex;
	std::set<std::shared_ptr<std::function<void()>> >	_callback_set;


};
#define QL_COMMON_API extern
QL_COMMON_API int WaitForSingleEvent(std::shared_ptr<CConditionEvent> pEvent,  unsigned int mill = INFINITE);
QL_COMMON_API int WaitForMultipleEvent(std::shared_ptr<CConditionEvent>* ppEvent, unsigned int count, unsigned mill = INFINITE);

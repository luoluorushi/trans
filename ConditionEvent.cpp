#include "cross_platform/ConditionEvent.h"
#pragma GCC visibility push(default)
int WaitForSingleEvent(std::shared_ptr<CConditionEvent> pEvent, unsigned int mill)
{
	return pEvent->WaitFor(mill);
}
int WaitForMultipleEvent(std::shared_ptr<CConditionEvent>* ppEvent, unsigned int count, unsigned int mill)
{
	if (count == 1)
	{
		return WaitForSingleEvent(*ppEvent, mill);
	}
	else
	{
		std::shared_ptr<CConditionEvent> pEvent = std::make_shared<CConditionEvent>();
		std::shared_ptr < std::function<void()>> pFunc = std::make_shared<std::function<void()>>([=]
		{
			pEvent->SetEvent();
		});
		for (int i = 0; i < count; ++i)
		{
			ppEvent[i]->AddListener(pFunc);
		}

		int wait_object = -1;

		std::shared_ptr<std::function<bool()>> pPred = std::make_shared<std::function<bool()>>([&]
		{
			for (int i = 0; i < count; ++i)
			{
				if (ppEvent[i]->Pred())
				{
					wait_object = i;
					return true;
				}
			}
				return false;
		});

		int waitResult = pEvent->WaitFor(pPred, mill);
        for (int i = 0; i < count; ++i)
        {
            ppEvent[i]->RemoveListener(pFunc);
        }
        if (waitResult == -1)
		{
			return waitResult;
		}
		else
		{
			return wait_object;
		}

	}
#pragma GCC visibility pop
}

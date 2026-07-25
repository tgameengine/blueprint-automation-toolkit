// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Core/EditorExecution.h"

#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

namespace BAT::EditorExecution
{
	void RunOnGameThread(TFunction<void()> Work)
	{
		if (IsInGameThread())
		{
			Work();
			return;
		}

		AsyncTask(ENamedThreads::GameThread, [LocalWork = MoveTemp(Work)]() mutable
		{
			LocalWork();
		});
	}

	bool RunOnGameThreadAndWaitVoid(TFunction<void()> Work, float TimeoutSeconds)
	{
		if (IsInGameThread())
		{
			Work();
			return true;
		}

		if (TimeoutSeconds <= 0.0f)
		{
			return false;
		}

		TAtomic<bool> bDone(false);
		RunOnGameThread([LocalWork = MoveTemp(Work), &bDone]() mutable
		{
			LocalWork();
			bDone.Store(true);
		});

		const double Start = FPlatformTime::Seconds();
		while (!bDone.Load())
		{
			FPlatformProcess::SleepNoStats(0.001f);
			if ((FPlatformTime::Seconds() - Start) > TimeoutSeconds)
			{
				return false;
			}
		}

		return true;
	}
}

// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Templates/Function.h"

namespace BAT::EditorExecution
{
	void RunOnGameThread(TFunction<void()> Work);
	bool RunOnGameThreadAndWaitVoid(TFunction<void()> Work, float TimeoutSeconds = 10.0f);

	template <typename T>
	TOptional<T> RunOnGameThreadAndWait(TFunction<T()> Work, float TimeoutSeconds = 10.0f)
	{
		if (IsInGameThread())
		{
			return TOptional<T>(Work());
		}

		if (TimeoutSeconds <= 0.0f)
		{
			return TOptional<T>();
		}

		TPromise<T> Promise;
		TFuture<T> Future = Promise.GetFuture();
		RunOnGameThread([LocalWork = MoveTemp(Work), LocalPromise = MoveTemp(Promise)]() mutable
		{
			LocalPromise.SetValue(LocalWork());
		});

		const bool bReady = Future.WaitFor(FTimespan::FromSeconds(TimeoutSeconds));
		if (!bReady)
		{
			return TOptional<T>();
		}

		return TOptional<T>(Future.Get());
	}
}

#include "Commands/CommandDispatcher.h"

FAutomationResult FCommandDispatcher::Dispatch(FAutomationCommand& Command, FAutomationContext& Context) const
{
	return Command.Execute(Context);
}

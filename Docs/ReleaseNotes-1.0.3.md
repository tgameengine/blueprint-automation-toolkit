# Blueprint Automation Toolkit 1.0.3

Version 1.0.3 is a focused Blueprint graph and Python execution reliability
update.

## Changes

- Executes encoded Python payloads with an explicit module-style globals
  dictionary so scripts that inspect `__name__` behave consistently.
- Creates Kismet array library calls with `UK2Node_CallArrayFunction`, preserving
  wildcard array pin behavior and Blueprint type propagation.
- Finalizes Spawn Actor node setup in the correct order and notifies the node
  when its class pin changes, ensuring class-dependent pins are refreshed.

## Validation

- Fab-style `BuildPlugin` packaging completed successfully on Unreal Engine
  5.5, 5.6, 5.7, and 5.8 for Win64.
- All release archives include the matching engine-specific descriptor,
  precompiled editor binary, plugin source, content, documentation, and
  resources.

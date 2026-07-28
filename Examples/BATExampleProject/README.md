# BAT Example Project

This is an executable host project for Blueprint Automation Toolkit. It depends
on the plugin but does not redistribute the plugin.

The project includes assets created and verified through real Codex prompts:

- `BAT_CodexShowcase`: a reviewable map with a stage, pillars, lights, and all
  three generated Blueprint actors.
- `BP_CodexTelemetryBeacon`: an Actor Blueprint with editable variables, a mesh
  component, a function graph, and a verified BeginPlay-to-PrintString link.
- `BP_CodexTelemetryBeacon_Configured`: a duplicated variant whose mesh was
  updated in place without changing the source Blueprint.
- `BP_CodexSplineBridge`: a spline-driven HISM Blueprint generated through
  `components_apply`.

The exact prompts, route traces, and condensed outputs are in
[`../../Docs/CodexExecutedExamples.md`](../../Docs/CodexExecutedExamples.md).

1. Install Blueprint Automation Toolkit for your Unreal Engine version.
2. Copy this project outside the plugin directory if you want to modify it.
3. If needed, update `EngineAssociation` in `BATExampleProject.uproject`.
4. Open the project and enable Blueprint Automation Toolkit when prompted.
5. The project opens `/Game/BAT_CodexExamples/Maps/BAT_CodexShowcase` by
   default; reopen it from the Content Browser if needed.
6. Configure the local server under **Editor Preferences > Plugins > Blueprint
   Automation Toolkit**.
7. Start with the server disabled, set a bearer token, then enable it for the
   current editor session.

The committed settings keep the server disabled, Safe Mode enabled, and exec
and Python disabled. No authentication token is stored in the project. The
plugin accepts loopback connections only.

See the main documentation for endpoint examples and the recommended
discover-inspect-mutate-compile-save workflow:

https://github.com/tgameengine/blueprint-automation-toolkit

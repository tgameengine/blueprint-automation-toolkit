# Fab Submission Checklist

## Pre-Submission

- [x] Version bumped to 1.0.0 in .uplugin
- [x] `IsBetaVersion` and `IsExperimentalVersion` set to `false`
- [x] `CreatedByURL`, `DocsURL`, `SupportURL` populated
- [x] Required engine plugins disclosed: PCG, GeometryProcessing, GeometryScripting
- [x] Plugin Browser icon included as `Resources/Icon128.png` (128x128 PNG)
- [x] Final source-only package contains `.uplugin`, `Source`, `Config`, `Content`, `Docs`, and `Resources`
- [x] Final submission package excludes generated `Binaries` and `Intermediate` folders
- [x] Every module declares `PlatformAllowList: Win64`
- [x] Every `.h` and `.cpp` file starts with the AkaSoft 2026 copyright notice
- [x] Plugin builds clean against UE 5.5, 5.6, 5.7, and 5.8 (Win64 Development Editor)
- [x] No compiler warnings in plugin source
- [x] All 49 automation tests pass in a clean UE 5.5 host project
- [x] Example project depends on BAT without redistributing the plugin

## Fab Listing Content

- [ ] **Title:** Blueprint Automation Toolkit
- [ ] **Short description:** Editor-only localhost automation bridge for AI agents to control Blueprints and Unreal Editor through a typed HTTP API.
- [x] **Long description:** (see below)
- [ ] **Category:** Code Plugins > Developer
- [ ] **Price:** $14.99
- [x] **Supported engine versions:** 5.5, 5.6, 5.7, 5.8
- [ ] **Supported platforms:** Editor only (Win64)
- [x] **Technical information:** See `FabTechnicalInformation.md`

### Long Description (copy-paste for Fab)

Blueprint Automation Toolkit is a secure, editor-only HTTP bridge that lets AI agents and external tools control Unreal Editor programmatically through a typed localhost API.

**What it does**

Instead of writing bespoke editor utilities for every task, point any HTTP client — an AI coding agent, a CI script, a custom tool — at your running editor and drive it through a small set of generic primitives:

• Read and write Blueprint graphs (nodes, pins, connections)
• Inspect and mutate any reflected UObject property
• Call any exposed UFunction
• Spawn and destroy actors in the editor world
• Resolve assets, Blueprints, animation references, and skeleton data
• Compile Blueprints and get structured diagnostics (warnings, errors)
• Save assets and control editor selection/focus
• Start and stop Play-In-Editor sessions

The API is intentionally small and reflective — it scales by data, not by adding endless one-off routes.

**Security first**

• Localhost-only: non-loopback requests are rejected
• Bearer-token authentication (configurable per project)
• Safe mode enabled by default — advanced capabilities are locked down
• Rate limiting and request body size limits on all endpoints
• No telemetry, no external network calls

**Extensible**

Other editor plugins can register custom automation commands at startup via the RegisterAutomationCommand API. Registered commands automatically get auth, rate limiting, and route binding — and appear in the discovery endpoint.

**Built for AI agents**

The canonical agent workflow is: discover → inspect → resolve → mutate → compile → save. Agents can call GET /engine/discover to learn what's available, then use a consistent set of JSON endpoints to accomplish any editor task.

**Requirements and dependencies**

• Editor-only Code Plugin (`EditorNoCommandlet`); it is not included in packaged games
• Requires the built-in PCG, GeometryProcessing, and GeometryScripting engine plugins
• No external SDKs, third-party DLLs, background services, package managers, or internet connection
• The local HTTP server runs inside Unreal Editor and is disabled by default

**What you get on Fab**

Pre-built binary plugin — install it for a supported engine version, enable it, and go. No compilation is needed for the packaged Win64 editor build. The same plugin is also available free on GitHub (source code, build from source) at github.com/tgameengine/blueprint-automation-toolkit.

**Key endpoints**

• POST /blueprint/graph/apply — Apply node/connection changes to a Blueprint graph
• GET /blueprint/graph/read — Read the full graph structure
• POST /blueprint/compile_save — Compile and save with structured diagnostics
• POST /object/set_property, GET /object/get_property — Reflected property access
• POST /object/call_function — Call any exposed UFunction
• POST /actor/spawn, POST /actor/destroy — Actor lifecycle
• GET /object/describe — Introspect any UObject
• POST /editor/select, POST /editor/focus — Editor UI control
• POST /pie/start, POST /pie/stop — Play-In-Editor control
• GET /engine/discover — Machine-readable capability handshake

## Required Assets

- [ ] Fab listing thumbnail
- [x] Plugin Browser icon (`Resources/Icon128.png`, 128x128 PNG)
- [ ] Screenshots (1920x1080, at least 3):
  - API in action (HTTP client calling endpoints)
  - Blueprint before/after automation
  - Extension API code sample
- [ ] Technical documentation link (GitHub README or Docs/)

## Post-Submission

- [ ] Monitor Epic review feedback
- [ ] Update `MarketplaceURL` in .uplugin with the Fab listing URL once live
- [ ] Announce on GitHub, UE forums, Reddit, X

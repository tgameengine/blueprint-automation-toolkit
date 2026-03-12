# UE Marketplace Submission Checklist

## Pre-Submission

- [ ] Version bumped to 1.0.0 in .uplugin
- [ ] `IsBetaVersion` and `IsExperimentalVersion` set to `false`
- [ ] `CreatedByURL`, `DocsURL`, `SupportURL` populated
- [ ] Plugin builds clean against UE 5.5 (Win64 Development Editor)
- [ ] No compiler warnings in plugin source
- [ ] All automation tests pass

## Marketplace Listing Content

- [ ] **Title:** Blueprint Automation Toolkit
- [ ] **Short description:** Editor-only localhost automation bridge for AI agents to control Blueprints and Unreal Editor through a typed HTTP API.
- [ ] **Long description:** (see below)
- [ ] **Category:** Code Plugins > Developer
- [ ] **Price:** $14.99
- [ ] **Supported engine versions:** 5.5 (add more as tested)
- [ ] **Supported platforms:** Editor only (Win64)

### Long Description (copy-paste for Marketplace)

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

**What you get on Marketplace**

Pre-built binary plugin — install from your Vault, enable, and go. No compilation needed. The same plugin is also available free on GitHub (source code, build from source) at github.com/tgameengine/blueprint-automation-toolkit.

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

- [ ] Icon (284x284 PNG)
- [ ] Screenshots (1920x1080, at least 3):
  - API in action (HTTP client calling endpoints)
  - Blueprint before/after automation
  - Extension API code sample
- [ ] Technical documentation link (GitHub README or Docs/)

## Post-Submission

- [ ] Monitor Epic review feedback
- [ ] Update `MarketplaceURL` in .uplugin once listing is live
- [ ] Announce on GitHub, UE forums, Reddit, X

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
- [ ] **Long description:** Expand from README — cover core API, security model, extension API
- [ ] **Category:** Code Plugins > Developer
- [ ] **Price:** $14.99
- [ ] **Supported engine versions:** 5.5 (add more as tested)
- [ ] **Supported platforms:** Editor only (Win64)

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

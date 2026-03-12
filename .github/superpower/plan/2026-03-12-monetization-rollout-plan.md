# Monetization Rollout — Implementation Plan

**Goal:** Prepare the Blueprint Automation Toolkit plugin for dual distribution — free on GitHub (proprietary + free use grant) and paid ($14.99) on UE Marketplace — with all necessary license, documentation, and metadata changes.

**Architecture:** No source code changes. Documentation, license, config, and distribution preparation only.

**Tech Stack:** Markdown, JSON (.uplugin), UE Marketplace submission pipeline, GitHub repo settings.

**Design doc:** `.github/superpower/brainstorm/2026-03-12-monetization-strategy-design.md`

---

## Task 1: Update LICENSE — Add Free Use Grant

**Step 1: Replace LICENSE content**
- File: `LICENSE`
- Replace entire file content with:

```text
Copyright (c) 2026 murataka
All rights reserved.

GRANT OF USE

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to use
the Software in their own projects, including commercial projects, subject to
the following conditions:

1. REDISTRIBUTION PROHIBITED — The Software may not be redistributed,
   sublicensed, repackaged, or sold as a standalone product or as part of a
   competing product. Users must obtain the Software exclusively from the
   official GitHub repository or the Unreal Engine Marketplace listing.

2. MODIFICATION FOR PERSONAL USE — Users may modify the Software for use in
   their own projects. Modified versions may not be distributed publicly.

3. ATTRIBUTION — This license notice shall be included in all copies or
   substantial portions of the Software.

4. NO TRADEMARK LICENSE — This license does not grant permission to use the
   trade names, trademarks, or service marks of the copyright holder.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM,
OUT OF, OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

**Step 2: Verify the license file**
- Command: `type Plugins\blueprint-automation-toolkit\LICENSE`
- Expected: Updated license text with "GRANT OF USE" heading and four numbered conditions.

---

## Task 2: Update .uplugin Metadata for Marketplace

**Step 1: Edit BlueprintAutomationToolkit.uplugin**
- File: `BlueprintAutomationToolkit.uplugin`
- Changes to make:
  1. Change `"VersionName"` from `"0.1.0"` to `"1.0.0"`
  2. Change `"Description"` to `"Editor-only localhost automation bridge for AI agents to control Blueprints and Unreal Editor through a typed HTTP API."`
  3. Set `"CreatedByURL"` to `"https://github.com/murataka/blueprint-automation-toolkit"` *(placeholder — update with real URL)*
  4. Set `"DocsURL"` to `"https://github.com/murataka/blueprint-automation-toolkit#readme"` *(placeholder — update with real URL)*
  5. Set `"SupportURL"` to `"https://github.com/murataka/blueprint-automation-toolkit/issues"` *(placeholder — update with real URL)*
  6. Change `"IsBetaVersion"` from `true` to `false`
  7. Change `"IsExperimentalVersion"` from `true` to `false`

- Full resulting file:

```json
{
  "FileVersion": 3,
  "Version": 1,
  "VersionName": "1.0.0",
  "FriendlyName": "Blueprint Automation Toolkit",
  "Description": "Editor-only localhost automation bridge for AI agents to control Blueprints and Unreal Editor through a typed HTTP API.",
  "Category": "Developer",
  "CreatedBy": "AkaSoft",
  "CreatedByURL": "https://github.com/murataka/blueprint-automation-toolkit",
  "DocsURL": "https://github.com/murataka/blueprint-automation-toolkit#readme",
  "MarketplaceURL": "",
  "SupportURL": "https://github.com/murataka/blueprint-automation-toolkit/issues",
  "CanContainContent": true,
  "IsBetaVersion": false,
  "IsExperimentalVersion": false,
  "Installed": false,
  "EnabledByDefault": true,
  "Plugins": [
    {
      "Name": "PCG",
      "Enabled": true
    },
    {
      "Name": "GeometryProcessing",
      "Enabled": true
    },
    {
      "Name": "GeometryScripting",
      "Enabled": true
    }
  ],
  "Modules": [
    {
      "Name": "BlueprintAutomationToolkit",
      "Type": "Editor",
      "LoadingPhase": "Default"
    }
  ]
}
```

**Step 2: Validate JSON**
- Command: `python -c "import json; json.load(open(r'Plugins\blueprint-automation-toolkit\BlueprintAutomationToolkit.uplugin')); print('OK')"` 
- Expected: `OK`

---

## Task 3: Update README — Add Distribution & License Sections

**Step 1: Insert new sections into README.md**
- File: `README.md`
- Location: After the opening description paragraph (line ~4, after "...feature-specific gameplay commands."), before "The preferred workflow is:"
- Insert the following:

```markdown

## Getting the Plugin

| Source | Cost | What you get |
|--------|------|--------------|
| **GitHub** | Free | Source code. Clone and build from source against your UE project. |
| **UE Marketplace** | $14.99 | Packaged binary. One-click install, Vault integration, engine version management. |

Both versions are identical in features. The Marketplace listing is a convenience option.

## License

This plugin is proprietary software. You may use it freely in your own projects (including commercial). Redistribution or repackaging as a standalone/competing product is prohibited. See [LICENSE](LICENSE) for full terms.

```

**Step 2: Verify README**
- Command: `type Plugins\blueprint-automation-toolkit\README.md`
- Expected: New "Getting the Plugin" table and "License" section visible between the intro and "The preferred workflow" sections.

---

## Task 4: Update MarketplaceNotes.md — Add Distribution & Pricing

**Step 1: Append to MarketplaceNotes.md**
- File: `Resources/Docs/MarketplaceNotes.md`
- Location: Append after the last line of existing content (after `RateLimitPerSecond=10`)
- Append:

```markdown

## Distribution model

- Plugin source is available on GitHub at no cost under a proprietary license with a free use grant.
- The Marketplace listing provides a packaged, pre-built binary for convenience at $14.99.
- Both versions are feature-identical. The Marketplace version ships under the standard UE Marketplace EULA.
- No feature gating, no time-limited trials, no telemetry.

## Pricing

- Marketplace price: **$14.99** (USD)
- No subscriptions, no recurring fees.
```

**Step 2: Verify file**
- Command: `type Plugins\blueprint-automation-toolkit\Resources\Docs\MarketplaceNotes.md`
- Expected: Original security content followed by new "Distribution model" and "Pricing" sections.

---

## Task 5: Create GitHub Repository Setup Checklist

**Step 1: Create GitHubSetup.md**
- File: `Resources/Docs/GitHubSetup.md` (new file)
- Content:

```markdown
# GitHub Repository Setup Checklist

## Repository Creation

- [ ] Create public repo (e.g., `murataka/blueprint-automation-toolkit`)
- [ ] Push plugin source (everything under the plugin root)
- [ ] Verify LICENSE file is visible at repo root
- [ ] Verify README.md renders correctly

## Repository Settings

- [ ] Add description: "Editor-only localhost automation bridge for AI agents to control Blueprints and Unreal Editor"
- [ ] Add topics: `unreal-engine`, `blueprint`, `automation`, `ai`, `editor-plugin`, `ue5`
- [ ] Enable Issues
- [ ] Enable Discussions (for community support)
- [ ] Disable wiki (README + Docs/ folder is sufficient)

## GitHub Sponsors

- [ ] Enable GitHub Sponsors on your profile
- [ ] Create sponsor tiers:
  - $5/mo — Supporter (name in README)
  - $25/mo — Backer (priority issue response)
  - $100/mo — Studio (direct support channel)

## Release Process

- [ ] Create GitHub Release for v1.0.0
- [ ] Attach pre-built binary ZIP for convenience (optional)
- [ ] Tag release matching .uplugin VersionName

## Community Files

- [ ] CONTRIBUTING.md (already exists)
- [ ] Issue templates (bug report, feature request)
- [ ] PR template
```

**Step 2: Verify file**
- Command: `type Plugins\blueprint-automation-toolkit\Resources\Docs\GitHubSetup.md`
- Expected: Checklist renders with all checkbox items.

---

## Task 6: Create UE Marketplace Submission Checklist

**Step 1: Create MarketplaceSubmission.md**
- File: `Resources/Docs/MarketplaceSubmission.md` (new file)
- Content:

```markdown
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
```

**Step 2: Verify file**
- Command: `type Plugins\blueprint-automation-toolkit\Resources\Docs\MarketplaceSubmission.md`
- Expected: Checklist with all items present.

---

## Task Summary

| Task | File | Action |
|------|------|--------|
| 1 | `LICENSE` | Edit — replace with proprietary + free use grant |
| 2 | `BlueprintAutomationToolkit.uplugin` | Edit — bump to 1.0.0, add URLs, remove beta flags |
| 3 | `README.md` | Edit — insert "Getting the Plugin" and "License" sections |
| 4 | `Resources/Docs/MarketplaceNotes.md` | Edit — append distribution model and pricing |
| 5 | `Resources/Docs/GitHubSetup.md` | Create — GitHub repo setup checklist |
| 6 | `Resources/Docs/MarketplaceSubmission.md` | Create — Marketplace submission checklist |

**No source code changes. No new routes. No commandlets.**

---

## Handoff

After all tasks are complete, update the `.uplugin` URLs with the real GitHub repository URL once it is created. Then follow the checklists in Tasks 5 and 6 to publish.

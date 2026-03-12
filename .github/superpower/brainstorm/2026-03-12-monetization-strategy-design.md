# Monetization Strategy — Blueprint Automation Toolkit

**Date:** 2026-03-12
**Status:** Approved

---

## 1. Licensing: Proprietary + Free Download

- Retain the current **proprietary license** ("All rights reserved").
- Add a **free download/use grant**: anyone may download and use the plugin in their projects at no cost.
- **No redistribution**: users must obtain the plugin from the official GitHub repository or the UE Marketplace. Forks repackaging it as a competing product are not permitted.
- **Marketplace version** ships under the standard UE Marketplace EULA (required by Epic).
- Full control is retained: license terms can be updated, commercial/custom licenses can be offered to studios, or the plugin can be relicensed if strategy changes.

## 2. Dual Distribution: Free on GitHub + Cheap on Marketplace

- **GitHub (free)**: Public repository with source. Developers clone, build from source, file issues, and contribute feedback. This is the community and ecosystem engine.
- **UE Marketplace (paid, $9.99–$19.99)**: Packaged build with one-click install, Vault integration, and engine version management. Epic takes 12% revenue share.
- **Same plugin, same features** — no feature gating between GitHub and Marketplace versions. The Marketplace price is a convenience/support fee.
- This sidesteps "competitors are free" concerns — the plugin *is* free on GitHub — while generating some revenue from developers who prefer Marketplace convenience.

## 3. Revenue Streams

| Stream | Description | Timing |
|--------|-------------|--------|
| **Marketplace sales** | Steady trickle from $9.99–$19.99 sales; compounds with volume | Immediate |
| **GitHub Sponsors** | Community and studio sponsorships for ongoing development | Immediate |
| **Enterprise licenses** | Annual paid license for studios wanting custom terms, priority support, or SLA | After adoption proves demand |
| **Paid support / consulting** | Integration help, custom automation workflows, priority bug fixes ($100–200/hr or retainers) | After adoption proves demand |
| **Premium companion tools** | Separate paid plugins or SaaS built on the BAT extension API (cloud dashboard, CI/CD integration, hosted plan execution) | Ecosystem maturity |

## 4. Rollout Sequence

| Phase | Milestone | Actions | Revenue |
|-------|-----------|---------|---------|
| **1 — Launch** | Day 1 | Publish on GitHub (proprietary, free download). List on UE Marketplace at ~$14.99. Set up GitHub Sponsors. | Marketplace sales + tips |
| **2 — Grow** | 500+ installs | Demo videos, UE forum posts, Reddit/X presence. Encourage extension plugins via `RegisterAutomationCommand` API. | Growing Marketplace volume |
| **3 — Monetize** | 1,000+ installs | Offer enterprise licenses and paid support packages to studios. | Enterprise licenses + support |
| **4 — Expand** | Ecosystem maturity | Build premium companion tools (cloud dashboard, CI integration). Consider SaaS layer. | Product revenue |

## 5. Key Decisions

- **Price point**: $9.99–$19.99 on Marketplace (cheap enough to be impulse, meaningful enough to cover listing effort).
- **No feature gating**: GitHub and Marketplace versions are identical.
- **License**: Proprietary with free use grant. No redistribution.
- **Phased monetization**: Start with Marketplace + Sponsors, add enterprise/consulting as adoption grows, premium tools last.

## 6. Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Competitors undercut on features | Extension API creates ecosystem moat; open GitHub builds community loyalty |
| Low Marketplace conversion (why pay when GitHub is free?) | Marketplace is bonus revenue, not primary strategy; price is low enough for impulse buys |
| Enterprise adoption is slow | Start with indie/community adoption; enterprise follows proven tools |
| Support burden scales with free users | Community-driven support (GitHub Issues/Discussions); paid support for priority |

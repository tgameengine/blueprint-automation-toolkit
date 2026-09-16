# Prompting Complex Character Animation Workflows

This guide provides a copy-ready prompt for building complex character
animations from visual references and finishing them in Unreal Editor through
Blueprint Automation Toolkit (BAT).

BAT does not generate reference images itself. An external image-generation
tool creates the approved visual references and motion data. BAT then acts as
the Unreal Editor implementation and verification bridge. Keep those stages
separate so a generated image cannot silently become an unreviewed animation.

## When to Use This Workflow

Use this workflow when an action has several physical phases, contact changes,
root motion, prop interaction, occluded limbs, or strict loop requirements.
Examples include combat combinations, vaults, climbing, takedowns, reloads,
multi-step traversal, and locomotion with direction changes.

For simple single-action clips, a smaller pose set may be sufficient. For a
complex action, plan 16–24 key poses and split the reference images into sheets
of no more than eight poses each.

## Information That Makes a Prompt Solvable

Define these items before asking an agent to create the animation:

1. The action's intent, duration, frame rate, playback type, and loop behavior.
2. A phase-by-phase timeline rather than only a high-level action name.
3. Every foot, hand, weapon, ledge, floor, or prop contact and when it releases.
4. Root translation, rotation, facing direction, and vertical-motion rules.
5. Balance, center-of-mass, momentum, joint-limit, and collision constraints.
6. The exact Skeleton, preview mesh, destination path, and overwrite policy.
7. Objective validation thresholds and the evidence required before saving.

A precise contact rule is more useful than a subjective request. For example:

> The left hand grips the ledge from frames 18 through 31. It may rotate around
> the grip point, but it may not translate until the release frame.

## Copy-Ready Master Prompt

Replace every bracketed placeholder before using this prompt.

```text
Use an image-generation tool to create a motion-reference package for a complex
animation for the UE5 Manny mannequin. After the reference and motion-data
stages are approved, use Blueprint Automation Toolkit (BAT) to implement and
verify the animation in Unreal Editor.

Do not modify Unreal Engine yet. Work in approval stages and stop after each
approval boundary.

ANIMATION OVERVIEW

Animation name: [NAME]
Action: [ONE-SENTENCE DESCRIPTION]
Character: UE5 Manny
Duration: [SECONDS]
Frame rate: 30 FPS
Playback type: [IN-PLACE / ROOT MOTION]
Looping: [YES / NO]
Starting facing direction: [DIRECTION]
Ending facing direction: [DIRECTION]
Style and intent: [HEAVY / AGILE / CAUTIOUS / EXHAUSTED / COMBAT-READY]
Environment or prop interaction: [DESCRIPTION OR NONE]

PHYSICAL REQUIREMENTS

- Preserve Manny's proportions and bone lengths.
- Keep the character's center of mass physically supported.
- Show clear anticipation, action, impact, recovery, and settling.
- Respect realistic hip, knee, ankle, spine, shoulder, elbow, and wrist limits.
- Avoid limb intersections, ground penetration, and sudden joint flips.
- Keep every planted hand and foot locked to its contact point.
- Specify which contacts may slide, pivot, rotate, or release.
- Preserve momentum and velocity direction between consecutive phases.
- Include pelvis rotation, torso counter-rotation, and controlled secondary motion.
- Do not add exaggerated motion unless explicitly requested.

ROOT-MOTION REQUIREMENTS

- Root translation: [RULES AND DISTANCE]
- Root rotation: [RULES AND TOTAL ROTATION]
- Vertical root movement: [RULES]
- If in-place, remove net horizontal displacement while preserving believable
  body mechanics.
- If looping, match the first and final local poses, velocities, root
  orientation, and active contact state.
- If using root motion, do not make the first and last world positions identical.

ACTION PHASES

Create the animation using the following phases:

1. [PHASE NAME]
   Time: [START-END]
   Description: [BODY ACTION]
   Supporting contacts: [LEFT FOOT / RIGHT FOOT / HAND / PROP]
   Pelvis behavior: [HEIGHT, ROTATION, AND WEIGHT SHIFT]
   Torso behavior: [LEAN AND TWIST]
   Important event: [CONTACT, RELEASE, IMPACT, OR OTHER EVENT]

2. [PHASE NAME]
   Time: [START-END]
   Description: [BODY ACTION]
   Supporting contacts: [CONTACTS]
   Pelvis behavior: [HEIGHT, ROTATION, AND WEIGHT SHIFT]
   Torso behavior: [LEAN AND TWIST]
   Important event: [CONTACT, RELEASE, IMPACT, OR OTHER EVENT]

[CONTINUE UNTIL EVERY PHYSICAL PHASE IS COVERED]

STAGE 1 - KEY-POSE PLAN

Propose 16-24 key poses before generating images.

For every proposed key pose, list:

- Exact timestamp and target animation frame.
- Phase name and pose purpose.
- Root position, orientation, and facing direction.
- Pelvis height and orientation.
- Approximate center-of-mass location.
- Planted and moving limbs.
- Hand, foot, environment, and prop constraints.
- Motion direction and expected velocity entering and leaving the pose.
- Whether it is an anticipation, contact, impact, passing, airborne, recovery,
  or settle pose.

Check that the pose plan covers every contact creation, contact release,
direction change, impact, and loop seam. Stop and wait for approval.

STAGE 2 - CLEAN REFERENCE GENERATION

After approval, generate clean reference sheets in synchronized groups of no
more than eight poses per sheet.

For every pose provide:

- Orthographic front view.
- Orthographic side view.
- Orthographic rear view.
- An orthographic three-quarter view where limb occlusion is significant.
- Identical camera, scale, framing, and ground height across the entire package.
- Transparent background.
- Exact pose, frame, phase, and view labels.
- Visible ground-contact and prop-contact markers.
- No perspective distortion, motion blur, cropped limbs, or hidden contact points.

Preserve the same character design and proportions in every image. Do not
generate data passes yet. Stop and wait for approval of all clean sheets.

STAGE 3 - DATA PASSES

After clean-sheet approval, produce synchronized passes for every approved
pose and view:

- Alpha silhouette.
- Normalized depth.
- Joint-ID colors.
- Bone-segment ID colors.
- Left/right limb identification.
- Contact-state colors.
- Root and pelvis local axes.
- Center-of-mass marker.
- Incoming and outgoing motion-direction vectors.
- Prop transforms and contact anchors, when applicable.

Use a fixed, documented color palette. Normalize masks and ID colors
deterministically after image generation. Do not allow antialiased edges to
introduce undocumented ID colors. Treat generated depth and ID passes as visual
references unless their pixel values have been normalized and verified.

Stop and wait for approval of the synchronized data passes.

STAGE 4 - MACHINE-READABLE MOTION

After data-pass approval:

- Derive root, pelvis, spine, head, limb, hand, foot, and prop transforms.
- Store transforms, contacts, confidence values, source observations, and
  constraints in machine-readable JSON.
- Use all synchronized camera views when resolving occluded joints.
- Mark uncertain joints and low-confidence observations instead of silently guessing.
- Interpolate the approved poses into [FRAME COUNT] playable frames.
- Use phase-aware interpolation rather than uniform blending.
- Preserve velocity through phase transitions.
- Apply acceleration limits, joint-angle limits, and fixed bone lengths.
- Enforce foot, hand, environment, and prop locking.
- Correct ground penetration and limb intersections.
- Validate center-of-mass support during every planted phase.
- Enforce pose, transform, velocity, and contact continuity at the loop seam.

Produce a validation summary for the motion data and stop if required
constraints cannot be satisfied.

STAGE 5 - UNREAL IMPLEMENTATION THROUGH BAT

Only after the motion data passes validation, connect to the currently open
Unreal Editor through BAT.

Start with:

- GET /engine/discover
- GET /health
- GET /ai/capabilities
- GET /openapi

Use only routes and permissions returned by the running editor. Resolve and
inspect the Skeleton, preview mesh, source assets, and destination before
editing. Do not invent an unavailable endpoint.

Create a new AnimSequence:

Destination: /Game/[DESTINATION]/[ANIMATION_NAME]
Skeleton: [EXACT SKELETON OBJECT PATH]
Preview mesh: [EXACT SKELETAL MESH OBJECT PATH]

- Do not overwrite an existing asset.
- Preserve a baseline of every source asset used.
- Use the requested forward-axis convention explicitly.
- Apply all playable frames and the closing loop sample where required.
- Compile or finalize animation data before saving.
- Inspect the resulting duration, frame rate, sampled key count, track count,
  Skeleton reference, and preview mesh reference.
- Save only when all acceptance checks pass.
- Do not enable BAT's optional execution or Python routes unless the user has
  explicitly authorized them and the native discovered API cannot perform the task.

STAGE 6 - VALIDATION AND VISUAL REVIEW

Before considering the animation complete:

- Inspect the complete animation at normal speed and quarter speed.
- Inspect it from front, side, rear, and three-quarter views.
- Check every contact creation, planted interval, pivot, and release individually.
- Report maximum planted-foot, planted-hand, and prop-contact translation error.
- Report loop position, rotation, velocity, and contact-state errors.
- Check ground penetration, joint popping, joint-limit violations, and limb intersections.
- Verify root-motion distance, vertical travel, and total rotation.
- Compare the resulting animation against every approved key pose.
- Run the available BAT asset inspection and validation routes.
- Save the validation report beside the reference package.

If visual inspection disagrees with numeric validation, treat the animation as
unfinished. Report the discrepancy and correct it before saving a final result.

Return:

- Created Unreal asset path.
- Reference-package and motion-JSON paths.
- Routes used and permissions required.
- Duration, frame rate, playable frames, samples, and animation track count.
- Skeleton and preview mesh references.
- Contact and loop-error measurements.
- Validation warnings and errors.
- Final save status and a statement confirming whether any existing asset changed.
```

## Root Motion and Loop Semantics

Do not use the same loop rule for in-place and root-motion clips:

- **In-place loop:** the first and final root transform should match, as should
  the local pose, velocity, and active contacts.
- **Root-motion loop:** the local pose, velocity, facing convention, and contacts
  should match, while the world-space root translation may advance by the
  intended cycle distance.
- **Turning loop:** compare orientation modulo the intended accumulated turn.
  A deliberate 360-degree advance can still be a continuous local loop.

State these semantics explicitly in the prompt so an agent does not remove
required motion while trying to close the loop.

## Contact Specification Pattern

For every important contact, document:

| Field | Example |
|---|---|
| Effector | `hand_l` |
| Target | Ledge anchor `Grip_A` |
| Start | Frame 18 |
| Fully planted | Frames 20–31 |
| Release | Frame 32 |
| Translation tolerance | 0.5 cm |
| Rotation rule | May pivot up to 12 degrees around the grip axis |
| Slip rule | No translation while fully planted |

This format makes foot locking, hand locking, weapon grips, and environmental
interaction measurable rather than subjective.

## Suggested Validation Thresholds

Choose thresholds appropriate to the character scale and action. A practical
starting point for Manny is:

- Planted foot translation: no more than 0.5 cm per planted interval.
- Planted hand or prop anchor translation: no more than 0.5 cm.
- Loop position difference: no more than 0.1 cm for in-place clips.
- Loop angular difference: no more than 0.1 degrees.
- Loop velocity difference: no more than 1% of the clip's peak velocity.
- Bone-length variation: zero, apart from numerical tolerance.
- Ground penetration: zero visible penetration; report any numeric penetration.

These are starting values, not BAT-wide guarantees. The prompt should override
them when the action, rig, scale, or downstream runtime requires a different
tolerance.

## BAT Safety and Capability Boundaries

- BAT is the Unreal implementation bridge, not the image generator or motion solver.
- Discover routes, permissions, request schemas, and limits from the running editor.
- Inspect before editing and verify by read-back rather than trusting HTTP success alone.
- Keep existing assets immutable unless the user explicitly authorizes an update.
- Prefer a new destination name for each iteration.
- Save only after structural, numeric, and visual acceptance checks pass.
- If the required animation-authoring surface is unavailable, report the gap.
  Do not silently broaden permissions or enable advanced execution.

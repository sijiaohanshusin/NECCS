---
description: "Plan and implement a new feature for the NECCS acoustic camera project using the Research-Plan-Implement workflow."
agent: "neccs-tech-lead"
tools: [read, search, edit, agent, todo, execute]
argument-hint: "Describe the feature you want to add"
---
Implement a new feature using the structured RPI workflow adapted for embedded development.

## Phase 1 — Research (GO/NO-GO)

Before writing any code, assess feasibility:

1. **Hardware constraints**: Does the MCU have enough Flash/RAM/peripherals?
2. **Memory budget**: Which memory region will new buffers go in? Is there space?
3. **Timing budget**: Will this impact the audio frame processing deadline (~5.3 ms)?
4. **Dependency analysis**: Which existing modules are affected?
5. **Risk assessment**: HIGH / MEDIUM / LOW based on subsystems touched

Verdict: **GO** (proceed to planning) or **NO-GO** (explain why and suggest alternatives).

## Phase 2 — Plan

If GO, create a design plan:

1. Run the standard T2 team discussion (consult required specialists)
2. Produce an ordered implementation plan with:
   - File manifest (CREATE/MODIFY for each file)
   - Memory placement decisions
   - API design (function signatures, error returns)
   - Integration points with existing code

## Phase 3 — Implement

Execute the plan through the standard T4-T7 workflow:

1. Implement changes file by file
2. Run code review (T5)
3. Run Keil build (T6) — must achieve 0 errors
4. Deliver with the standard T7 format

Throughout all phases, follow the constraints in `copilot-instructions.md`.

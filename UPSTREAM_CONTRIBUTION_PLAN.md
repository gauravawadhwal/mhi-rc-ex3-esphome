# Upstream Contribution Plan

Target repository: <https://github.com/hump-coder/mhi-rc-ex3-esphome>

## Recommendation

Contribute the work as two sequential pull requests:

1. A focused reliability PR for optimistic command synchronization without extra polling.
2. A capability-configuration PR for fan-speed mappings and supported HVAC modes.

Do not combine them. The first PR changes command and reconciliation behaviour; the
second changes the public YAML configuration and Home Assistant traits. Separate PRs
are easier to review, test, revert, and explain.

The second PR currently depends on helpers introduced by the first. Open it only after
the first PR is merged, or keep it as a local/fork branch until then.

## Proposed branch structure

```text
upstream/main
  └─ fix/optimistic-command-sync             PR 1

upstream/main after PR 1 merges
  └─ feat/configurable-climate-capabilities  PR 2
```

Use the fork remote for pushes and `hump-coder/mhi-rc-ex3-esphome:main` as the PR base.

The existing `fix/optimistic-sync-without-extra-polling` branch contains an unrelated
generated-cache ignore commit. For a clean PR, make a new branch from current upstream
`main` and cherry-pick only the optimistic-sync change. The existing mode branch can
remain as the tested development branch until PR 1 merges.

Each PR should contain two commits because `AGENTS.md` requires the example YAML to pin
the component revision introduced by the PR:

1. Code, tests/documentation, and behaviour change.
2. Update `mhi-rc-ex3.yaml` so its `ref` points to commit 1.

This repository historically uses merge commits, which preserve the pinned code commit
SHA. Mention the pin in the PR and ask the maintainer to update it if they choose a
squash merge, because a squash would replace the pinned SHA.

## PR 1: optimistic command synchronization

### Suggested title

`Improve optimistic command sync without extra polling`

### Scope

- Send only fields present in the ESPHome `ClimateCall`; use the protocol's `FF`
  unchanged marker for other fields.
- Publish requested state immediately through ESPHome, so Home Assistant updates as
  soon as a command is sent.
- Treat `RSSL0x` replies as command acknowledgements, not authoritative status.
- Track command fields during a short settling window and ignore a status response that
  briefly repeats the previous value.
- Accept a matching response as confirmation, or accept the controller's reported value
  after the settling window if the request was rejected or normalized.
- Avoid starting a scheduled status query while a command response is settling.
- Do not add an immediate post-command query. This avoids increasing serial traffic or
  aggravating the RC-EX3 panel's communications/lockout behaviour.
- Correct the stale polling description in `PROJECT.md`: the example uses a five-minute
  status interval, and operational-data requests are optional rather than unconditional.
- Document the distinction between status polling and operational-data polling.

Exclude the unrelated `.gitignore`/generated-cache change from this PR.

### Suggested PR body

> Home Assistant commands were followed by an immediate optimistic publish, but a stale
> RC-EX3 status frame could briefly restore the previous value. Sending a complete
> control packet also risked overwriting a newer wall-panel setting with cached state.
>
> This change sends partial control packets using `FF` for unchanged fields, publishes
> the requested fields immediately, and reconciles them with later authoritative status.
> A short settling window filters the observed stale response while still allowing the
> controller to correct rejected or normalized values afterward.
>
> It deliberately does not issue an extra status poll after a Home Assistant command.
> Scheduled polling remains responsible for confirmation and wall-panel discovery, so
> the fix does not increase bus traffic or the likelihood of the panel entering its
> communications state.

### Evidence and test matrix

Report the hardware honestly and narrowly:

- RC-EX3 controllers on an MHI KX6 installation.
- Three indoor units tested: two FDUT22 units and one FDUT56 unit.
- ESPHome 2026.6.5 on ESP32-C3 controllers.
- Status polling tested at one minute with `op_data_interval: 0`.

Manual checks to record before opening the PR:

- Power, operating mode, target temperature, Auto fan, and each manual fan speed update
  immediately in Home Assistant.
- A stale reply shortly after a command does not roll back the HA state.
- The next normal status response confirms or corrects the optimistic value.
- A wall-panel change appears in HA on the next scheduled status poll.
- A single-field HA change does not overwrite unrelated wall-panel fields.
- No additional status request appears after an HA command in debug logs.
- Run all three controllers for a meaningful period and report whether any panel lockout
  or “communicating with PC” state occurs.

Do not claim that the lockout is universally solved. The evidence supports “no extra
polling and no lockout observed in this tested configuration,” not a guarantee for every
MHI system.

### Polling defaults

Keep the upstream example's `update_interval: 5min` for now. One minute has worked well
on this KX6 installation and is a useful documented low-latency option, but changing the
global recommendation needs broader hardware evidence.

Recommend `op_data_interval: 0` as the safe starting point. Operational-data queries are
the requests associated with the panel's “communicating with PC” behaviour. Users who
want diagnostic sensors can opt in to a conservative interval with a clear warning.
This is more important for lockout avoidance than reducing the ordinary status interval.

## PR 2: configurable climate capabilities

### Suggested title

`Add configurable fan speeds and climate capabilities`

### Scope

- `fan_speed_count` (1–4): advertise only the manual speeds supported by the indoor
  unit; Auto fan remains available.
- `use_standard_fan_modes`: optionally map protocol speeds 1–3 to ESPHome
  Low/Medium/High so Home Assistant and HomeKit can treat them as ordered speeds.
- A capability option controlling whether `CLIMATE_MODE_HEAT_COOL` is advertised.
- Bidirectional mappings so commands and wall-panel status use the same configured
  representation.
- README, example YAML, and protocol documentation.

Before opening this PR, rename the current `auto_mode` option to
`supports_heat_cool_mode` (or a maintainer-preferred equivalent). `auto_mode` is
ambiguous because Auto fan speed is a separate supported feature.

Keep `use_standard_fan_modes` as an explicit opt-in. A possible reviewer suggestion is
an enum such as `fan_mode_labels: numbered|standard`; accept that if the maintainer
prefers an extensible schema, but the current boolean is adequate for the two mappings.

### Suggested PR body

> RC-EX3 installations do not all expose the same capabilities. Some indoor units have
> three manual fan speeds rather than four, and some multi-unit systems do not offer
> automatic heat/cool changeover. The component currently advertises all of these
> capabilities unconditionally.
>
> This adds backward-compatible YAML options to describe the installed unit. Users can
> limit the manual fan-speed count, suppress unsupported heat/cool changeover, and map
> three protocol speeds to ESPHome's standard Low/Medium/High modes. Standard modes also
> allow bridges such as HomeKit to expose ordered fan-speed controls instead of treating
> the numeric values as opaque custom modes.
>
> Existing configurations retain their current four numbered speeds and heat/cool mode
> unless the new options are explicitly enabled.

### Defaults and recommendations

Preserve existing behaviour by default:

| Option | Default | Reason |
|---|---:|---|
| `fan_speed_count` | `4` | Matches the component's existing four advertised speeds. |
| `use_standard_fan_modes` | `false` | Avoids breaking automations that select custom modes `1`–`4`. |
| `supports_heat_cool_mode` | `true` | Matches the currently advertised HVAC modes. |

Do not change these code defaults in the first upstream version. In the documentation,
recommend the following for a three-speed unit that does not support automatic
heat/cool changeover:

```yaml
climate:
  - platform: rc_ex3
    fan_speed_count: 3
    use_standard_fan_modes: true
    supports_heat_cool_mode: false
```

Explain that Home Assistant discovers the new traits automatically after firmware
reconnects. HomeKit may require resetting the bridged accessory because its service
layout is cached. Also explain that HomeKit's thermostat model does not expose Dry or
Fan Only as distinct target modes; that limitation is outside this component.

### Test matrix

Compile and, where hardware permits, inspect HA traits for:

| Configuration | Expected fan modes | Expected HVAC Auto |
|---|---|---|
| Options omitted | Auto, `1`, `2`, `3`, `4` | Present |
| Count 3, numbered | Auto, `1`, `2`, `3` | Present |
| Count 3, standard | Auto, Low, Medium, High | Configurable |
| Count 4, standard | Auto, Low, Medium, High, custom `4` | Configurable |
| Count 1 or 2 | Only the configured number of manual modes | Configurable |

Runtime checks should cover commands from HA and changes from the RC-EX3 wall panel.
For the three-speed standard configuration, verify HomeKit exposes a fan-speed control
after resetting the accessory.

## Review and publication sequence

1. Preserve the tested mode work on the fork before rewriting any branch history.
2. Fetch upstream and confirm `main` has not moved in a conflicting way.
3. Create the clean PR 1 branch from upstream `main`.
4. Apply only the optimistic-sync change, refine its documentation, compile all three
   device YAML files, and run the manual regression matrix.
5. Commit code/docs, update the example pin in a second commit, push to the fork, and
   open PR 1 as a draft.
6. Incorporate maintainer feedback and merge PR 1.
7. Create PR 2 from the new upstream `main`; do not carry PR 1 commits into its diff.
8. Rename `auto_mode`, add the configuration matrix documentation, and repeat compile
   and hardware checks.
9. Commit code/docs, update the example pin in a second commit, push, and open PR 2.
10. Keep personal device YAML files and `secrets.yaml` outside both PRs.

## Reviewer-facing posture

- Lead with the observed problem and measured behaviour, not HomeKit alone.
- Be explicit about what was tested and what remains system-dependent.
- Call the HomeKit improvement a consequence of using standard ESPHome traits, not a
  HomeKit-specific protocol implementation.
- Invite naming/schema feedback, especially for the capability flags.
- Avoid changing compatibility defaults while the feature is new.
- Offer logs or a short before/after screen recording if the maintainer wants evidence.


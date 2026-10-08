#!/usr/bin/env python3
"""Audit recorded task command interference, without inventing engine state.

DE action packets are orders, not simulation acknowledgments. In particular,
absence from a retry is NOT proof of boarding/death. Keep that uncertainty in
the output, retain exact packet offsets, and distinguish owner terminal STOPs.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct
import sys


COMMANDS = {"ORDER", "WORK", "MOVE", "STOP", "AI_ORDER", "DE_RETREAT",
            "BUILD", "WALL", "REPAIR", "GUARD", "FOLLOW", "PATROL",
            "DE_ATTACK_MOVE", "ATTACK_GROUND", "SPECIAL", "DELETE",
            "BACK_TO_WORK", "UNGARRISON", "DE_AUTOSCOUT", "DROP_RELIC"}


def inventory_writers(repository: Path) -> dict:
    """Enumerate source boundaries; classification is not runtime attribution."""
    from validate_naval_doctrine import rule_blocks
    direct = re.compile(r"\((up-target-objects|up-target-point|up-retask-gatherers|"
        r"up-assign-builders|up-reset-unit|up-retreat-now|up-retreat-to|"
        r"up-delete-idle-units|up-send-scout|up-ungarrison|up-garrison|"
        r"up-disband-group-type|up-reset-scouts|attack-now|delete-unit|delete-building)\b")
    delegated = re.compile(r"\((build|build-forward|build-wall|build-gate|up-build|up-build-line)\b")
    rows = []
    selectors = []
    for path in sorted(repository.glob("*.per")):
        source = path.read_text(encoding="utf-8-sig")
        for start, _, body, facts, actions in rule_blocks(source):
            line = source.count("\n", 0, start) + 1
            commands = direct.findall(actions)
            builders = delegated.findall(actions)
            if commands or builders:
                rows.append(dict(file=path.name, line=line, commands=commands,
                    builder_delegation=builders,
                    state_conditions=re.findall(r"\(goal ([^\n]+)", facts),
                    facts=facts, actions=actions,
                    reservation_filter_in_same_rule="object-data-group-flag" in body,
                    attribution="source capability, not proof of producing a replay command"))
            if re.search(r"\(up-(find-local|find-status-local|add-object-by-id|set-group)\b", actions):
                selectors.append(dict(file=path.name, line=line, facts=facts, actions=actions))
    return dict(command_rules=rows, selection_rules=selectors,
                counts_by_file=dict(Counter(row["file"] for row in rows)))


def object_array(raw: bytes, count: int, offsets: tuple[int, ...]) -> list[int]:
    """Only a length-verified layout can identify objects; never shift IDs."""
    candidates = [offset for offset in offsets if len(raw) == offset + count * 4]
    if count < 0 or len(candidates) != 1:
        raise ValueError(f"unrecognized object array: {len(raw)=}, {count=}, {offsets=}")
    return list(struct.unpack_from(f"<{count}I", raw, candidates[0]))


def decode_packet(kind: str, raw: bytes, parsed: dict) -> dict:
    result = dict(parsed)
    if kind == "DE_RETREAT":
        target, x, y, count, mode = struct.unpack_from("<IffII", raw)
        result.update(target_id=target, x=x, y=y, mode=mode,
                      object_ids=object_array(raw, count, (20,)))
    elif kind == "AI_ORDER":
        count, first, target, unknown, order = struct.unpack_from("<IIIII", raw)
        x, y = struct.unpack_from("<ff", raw, 20)
        if count == 1 and len(raw) == 40:
            members = [first]
        else:
            members = object_array(raw, count, (40,))
            if members and first != members[0]:
                raise ValueError("AI_ORDER leading object differs from explicit array")
        result = dict(player_id=parsed["player_id"], object_ids=members,
                      target_id=target, x=x, y=y, order_id=order,
                      unknown=unknown)
    elif kind in {"SPECIAL", "UNGARRISON"}:
        count = struct.unpack_from("<I", raw)[0]
        offsets = (28, 29) if kind == "SPECIAL" else (20, 21)
        result["object_ids"] = object_array(raw, count, offsets)
    elif kind == "WORK":
        target, x, y, count = struct.unpack_from("<IffH", raw)
        result.update(target_id=target, x=x, y=y,
                      object_ids=object_array(raw, count, (20,)))
    if result.get("target_id") == 0xFFFFFFFF:
        result["target_id"] = -1
    return result


def read_stream(source: Path, parser_root: Path):
    sys.path.insert(0, str(parser_root))
    from mgz import fast
    from mgz.fast import actions
    from mgz.fast.enums import Operation
    from analyze_replay import decode_chat

    original = actions.parse_action_71094
    original_fast = fast.parse_action_71094
    failures = Counter()

    def capture(kind, player, raw):
        parsed = original(kind, player, raw)
        if kind.name in COMMANDS:
            try:
                parsed = decode_packet(kind.name, raw, parsed)
            except (ValueError, struct.error) as error:
                failures[(kind.name, str(error))] += 1
                # The stock decoder is demonstrably wrong for these variants.
                # Do not silently fall back to its potentially corrupt IDs.
                parsed = dict(player_id=player, decode_error=str(error))
            parsed["raw_hex"] = raw.hex()
        return parsed

    fast.parse_action_71094 = actions.parse_action_71094 = capture
    events = []
    counts = Counter()
    milliseconds = sequence = 0
    size = source.stat().st_size
    try:
        with source.open("rb") as handle:
            header_length = struct.unpack("<I", handle.read(4))[0]
            handle.seek(header_length)
            fast.meta(handle)
            while handle.tell() < size:
                offset = handle.tell()
                operation, payload = fast.operation(handle)
                sequence += 1
                if operation == Operation.SYNC:
                    increment, _, sync = payload
                    milliseconds = max(milliseconds + increment, sync.get("current_time", 0))
                elif operation == Operation.ACTION:
                    kind, record = payload
                    counts[kind.name] += 1
                    if kind.name in COMMANDS:
                        events.append(dict(record, sequence=sequence, milliseconds=milliseconds,
                                           offset=offset, action=kind.name))
                elif operation == Operation.CHAT:
                    record = decode_chat(payload)
                    if record.get("message"):
                        events.append(dict(sequence=sequence, milliseconds=milliseconds,
                                           offset=offset, action="CHAT", **record))
    finally:
        actions.parse_action_71094 = original
        fast.parse_action_71094 = original_fast
    return events, dict(counts), [dict(action=k, error=e, count=n)
                                 for (k, e), n in failures.items()]


def paired_diagnostics(events: list[dict]) -> list[dict]:
    """Pair replay-visible RAW12 diagnostic IDs with their following values."""
    pending: dict[int, dict] = {}
    pairs = []
    for event in events:
        if event.get("action") != "CHAT":
            continue
        player = event.get("player")
        message = str(event.get("message", ""))
        match = re.fullmatch(r"RAW12 diag id: (-?\d+)", message)
        if match and isinstance(player, int):
            pending[player] = dict(event, diag_id=int(match.group(1)))
            continue
        match = re.fullmatch(r"RAW12 diag value: (-?\d+)", message)
        if match and isinstance(player, int) and player in pending:
            prior = pending.pop(player)
            pairs.append({
                "player": player,
                "diag_id": prior["diag_id"],
                "value": int(match.group(1)),
                "sequence": prior["sequence"],
                "milliseconds": prior["milliseconds"],
                "value_sequence": event["sequence"],
            })
    return pairs


def economic_retask_correlation(events: list[dict], window_ms: int = 30000) -> dict:
    """Correlate T53 Ctrl-retask samples with exact per-actor order-706 rates.

    This reconstructs command/diagnostic timing only. AI_ORDER packets are not
    proof that a simulated command executed, and a rate change is correlation,
    not by itself proof of the native assignment mechanism.
    """
    pre_names = {
        640: "writer", 641: "actor", 642: "old_action",
        643: "old_target", 644: "old_language_id", 645: "old_gather_type",
        646: "carry", 647: "new_target", 648: "new_target_type",
        649: "new_target_class", 650: "retask_game_time",
    }
    post_names = {
        651: "actor", 652: "new_action", 653: "observed_target",
        654: "new_language_id", 655: "new_gather_type",
        656: "observation_game_time", 657: "actor_observed",
    }
    episodes: list[dict] = []
    open_pre: dict[int, dict] = {}
    open_post: dict[int, dict] = {}
    for pair in paired_diagnostics(events):
        player, diag_id = pair["player"], pair["diag_id"]
        if diag_id == 640:
            episode = {
                "player": player,
                "pre_sequence": pair["sequence"],
                "retask_milliseconds": pair["milliseconds"],
                "writer": pair["value"],
                "post": None,
            }
            episodes.append(episode)
            open_pre[player] = episode
            continue
        if diag_id in pre_names and player in open_pre:
            episode = open_pre[player]
            episode[pre_names[diag_id]] = pair["value"]
            episode["pre_end_sequence"] = pair["value_sequence"]
            if diag_id == 650:
                open_pre.pop(player, None)
            continue
        if diag_id == 651:
            actor = pair["value"]
            episode = next((row for row in episodes
                            if row["player"] == player and row.get("actor") == actor
                            and row.get("post") is None), None)
            if episode is not None:
                episode["post"] = {
                    "actor": actor,
                    "observation_milliseconds": pair["milliseconds"],
                    "sequence": pair["sequence"],
                }
                open_post[player] = episode
            continue
        if diag_id in post_names and player in open_post:
            post = open_post[player]["post"]
            post[post_names[diag_id]] = pair["value"]
            post["end_sequence"] = pair["value_sequence"]
            if diag_id == 657:
                open_post.pop(player, None)

    order706: dict[tuple[int, int], list[dict]] = defaultdict(list)
    for event in events:
        if event.get("action") != "AI_ORDER" or event.get("order_id") != 706:
            continue
        player = event.get("player_id")
        if not isinstance(player, int):
            continue
        for actor in event.get("object_ids", []):
            order706[player, actor].append(event)

    replay_end = max((row.get("milliseconds", 0) for row in events), default=0)
    instrumented = set()
    for episode in episodes:
        actor = episode.get("actor")
        player = episode["player"]
        if not isinstance(actor, int):
            continue
        instrumented.add((player, actor))
        at = episode["retask_milliseconds"]
        start = max(0, at - window_ms)
        end = min(replay_end, at + window_ms)
        rows = order706.get((player, actor), [])
        pre = [row for row in rows if start <= row["milliseconds"] < at]
        post = [row for row in rows if at <= row["milliseconds"] <= end]
        later = [row for row in rows if row["milliseconds"] > end]
        pre_seconds = max((at - start) / 1000, 0.001)
        post_seconds = max((end - at) / 1000, 0.001)
        episode["order706"] = {
            "first_onset_milliseconds": rows[0]["milliseconds"] if rows else None,
            "total": len(rows),
            "pre_window_ms": [start, at],
            "pre_count": len(pre),
            "pre_rate_hz": round(len(pre) / pre_seconds, 4),
            "post_window_ms": [at, end],
            "post_count": len(post),
            "post_rate_hz": round(len(post) / post_seconds, 4),
            "later_recurrence_count": len(later),
            "first_later_recurrence_milliseconds": (
                later[0]["milliseconds"] if later else None
            ),
        }
        post_data = episode.get("post") or {}
        observed = post_data.get("actor_observed") == 1
        episode["role_transition_observed"] = (
            post_data.get("new_language_id") != episode.get("old_language_id")
            if observed and "new_language_id" in post_data
            and "old_language_id" in episode else None
        )
        episode["target_transition_observed"] = (
            post_data.get("observed_target") == episode.get("new_target")
            if observed and "observed_target" in post_data
            and "new_target" in episode else None
        )

    actor_rows = []
    for (player, actor), rows in order706.items():
        actor_rows.append({
            "player": player,
            "actor": actor,
            "count": len(rows),
            "first_milliseconds": rows[0]["milliseconds"],
            "last_milliseconds": rows[-1]["milliseconds"],
            "instrumented": (player, actor) in instrumented,
        })
    actor_rows.sort(key=lambda row: (-row["count"], row["player"], row["actor"]))
    return {
        "window_ms": window_ms,
        "writer_codes": {
            "1": "fisherman-to-farm",
            "2": "idle-villager-to-farm",
            "3": "free-economic-villager-to-farm",
            "4": "fisherman-to-gold-stone-or-tree",
        },
        "episodes": episodes,
        "order706_actors": actor_rows,
        "major_order706_actors_without_sample": [
            row for row in actor_rows if row["count"] >= 100 and not row["instrumented"]
        ],
        "limitations": [
            "Packet timing and telemetry are correlation, not simulation acknowledgement.",
            "A persistent Ctrl retask does not prove the internal assignment-conflict hypothesis.",
            "Major unsampled 706 actors remain outside the tested writer population.",
        ],
    }


def compact(event: dict) -> dict:
    return {key: event[key] for key in ("sequence", "milliseconds", "offset", "action",
            "target_id", "order_id", "x", "y", "message", "raw_hex") if key in event}


def boarding(event: dict, hull: int) -> bool:
    return (event["action"] == "SPECIAL" and event.get("order_id") == 5
            and event.get("target_id") == hull)


def source_hint(event: dict) -> str:
    if event["action"] == "DE_RETREAT":
        return "global recall; exact scripted/native caller unresolved in T7"
    if event["action"] == "AI_ORDER":
        return {705: "exploration owner; exact producer unresolved",
                706: "STOP writer; exact producer unresolved"}.get(
                    event.get("order_id"), "AI order producer unresolved")
    if event["action"] in {"BUILD", "WORK", "REPAIR", "BACK_TO_WORK"}:
        return "economy/worker controller or manual order; producer unresolved"
    return "scripted/native/manual producer unresolved"


def analyze(events: list[dict], hulls: set[tuple[int, int]], colors: dict[int, str],
            landing_evidence: tuple[dict, ...] = ()) -> dict:
    replay_end = dict(sequence=max(e["sequence"] for e in events) + 1,
                      milliseconds=max(e["milliseconds"] for e in events),
                      action="REPLAY_END_WITHOUT_RECORDED_TERMINAL")
    by_player = defaultdict(list)
    by_unit = defaultdict(list)
    loads = defaultdict(list)
    unloads = defaultdict(list)
    for event in events:
        player = event.get("player_id", event.get("player"))
        by_player[player].append(event)
        for unit in event.get("object_ids", []):
            by_unit[player, unit].append(event)
        if boarding(event, event.get("target_id", -1)):
            if (player, event["target_id"]) in hulls:
                loads[player, event["target_id"]].append(event)
        if event["action"] == "UNGARRISON":
            for unit in event.get("object_ids", []):
                unloads[player, unit].append(event)

    samples = []
    starts = defaultdict(list)
    terminals = defaultdict(list)
    for player, stream in by_player.items():
        chats = [e for e in stream if e["action"] == "CHAT"]
        for i, event in enumerate(chats):
            message = event["message"]
            kind = None
            if re.match(r"attack lift boarding target:", message):
                kind = "assault"
            elif re.match(r"migration (board target|scout board):", message):
                kind = "migration"
            if kind:
                starts[player].append((event, kind))
            if (re.match(r"RAW44B (attack load (partial|abort)|migration (full|partial|abort loaded|abort empty)) hull:", message)
                    or message.startswith("RAW44C assault ready hull:")):
                hull = int(message.rsplit(":", 1)[1])
                terminals[player, hull].append(event)
            elif message.startswith("attack lift ready:"):
                terminals[player, None].append(event)
            match = re.match(r"RAW44B (attack|migration) load remaining: (-?\d+)", message)
            if match:
                sample = dict(player=player, sequence=event["sequence"],
                              milliseconds=event["milliseconds"], kind=match[1])
                for other in chats[i + 1:i + 14]:
                    field = re.match(r"RAW44B " + match[1] + r" candidate (.*): (-?\d+)$", other["message"])
                    if not field:
                        break
                    sample[field[1]] = int(field[2])
                samples.append(sample)

    windows = []
    for (player, hull), stream in sorted(loads.items()):
        current = None
        for load in stream:
            candidates = [(chat, kind) for chat, kind in starts[player]
                          if abs(chat["milliseconds"] - load["milliseconds"]) <= 1000]
            begin = min(candidates, key=lambda pair: abs(pair[0]["sequence"] - load["sequence"]), default=None)
            next_unload = next((e for e in unloads[player, hull] if e["sequence"] > load["sequence"]), None)
            if (current is None or load["sequence"] >= current["end_sequence"]
                    or (begin and begin[0]["sequence"] != current.get("start_chat"))):
                if current is not None and load["sequence"] < current["end_sequence"]:
                    current["end_sequence"] = load["sequence"]
                    current["terminal"] = dict(sequence=load["sequence"],
                        milliseconds=load["milliseconds"], action="NEXT_RECORDED_TASK_START")
                # Unknown/recovery boarding cannot be assigned a fabricated owner.
                kind = begin[1] if begin else "unresolved/recovery/relic"
                bounds = [e for e in terminals[player, hull] + (terminals[player, None] if kind == "assault" else [])
                          if e["sequence"] > load["sequence"]]
                if next_unload:
                    bounds.append(next_unload)
                # Do not impose a 30/45s clock assumption: cached script clocks
                # can delay watchdogs. Retain the window until a recorded
                # terminal/unload/new start, or explicitly unresolved replay end.
                end = min(bounds, key=lambda e: e["sequence"], default=replay_end)
                if kind == "unresolved/recovery/relic":
                    terminal_kind = re.match(r"RAW44B (attack|migration) ", end.get("message", ""))
                    witness = next((s for s in samples if s["player"] == player
                                    and s.get("id") in load["object_ids"]
                                    and load["sequence"] <= s["sequence"]
                                    and s["milliseconds"] <= min(end["milliseconds"] + 1500,
                                                                  load["milliseconds"] + 35000)), None)
                    proven_kind = terminal_kind[1] if terminal_kind else (witness["kind"] if witness else None)
                    if end.get("message", "").startswith("RAW44C assault ready hull:"):
                        proven_kind = "attack"
                    if proven_kind:
                        kind = "assault" if proven_kind == "attack" else "migration"
                current = dict(player=player, color=colors[player], hull=hull,
                    owner=kind, expected_command="action-garrison to exact hull",
                    start=compact(load), start_chat=begin[0]["sequence"] if begin else None,
                    terminal=compact(end), end_sequence=end["sequence"], loads=[])
                windows.append(current)
            current["loads"].append(load)

    membership = defaultdict(list)
    aggregate = defaultdict(Counter)
    for index, window in enumerate(windows):
        player, hull = window["player"], window["hull"]
        window["id"] = index + 1
        initial = window["loads"][0]
        end = window["terminal"]
        manifests = sorted({u for load in window["loads"] for u in load["object_ids"] if u != hull})
        passengers = []
        full = (end.get("message", "").startswith("attack lift ready:")
                or end.get("message", "").startswith("RAW44C assault ready hull:")
                or end.get("message", "").startswith("RAW44B migration full hull:"))
        landed = set()
        for evidence in landing_evidence:
            check = evidence.get("membership_acceptance", {})
            minute, second = map(int, evidence["time"].split(":"))
            if (evidence["player"] == player and evidence["hull"] == hull
                    and end["milliseconds"] // 1000 == minute * 60 + second
                    and check.get("exact_boarded_only")):
                landed.update(check["dispatched"])
        for unit in manifests:
            attempts = [e for e in window["loads"] if unit in e["object_ids"]]
            first = attempts[0]
            relevant = [e for e in by_unit[player, unit]
                        if first["sequence"] < e["sequence"] < window["end_sequence"]
                        and e["milliseconds"] <= end["milliseconds"]]
            conflicting = [e for e in relevant if not boarding(e, hull)]
            conflict = conflicting[0] if conflicting else None
            own_samples = [s for s in samples if s["player"] == player and s.get("id") == unit
                           and first["sequence"] <= s["sequence"]
                           and s["milliseconds"] <= end["milliseconds"] + 1500]
            deleted = next((e for e in relevant if e["action"] == "DELETE"), None)
            ashore_after = bool(conflict and (any(e["sequence"] > conflict["sequence"] for e in attempts)
                               or any(s["sequence"] > conflict["sequence"] for s in own_samples)))
            if deleted:
                # A deletion request is still an order, not a simulation
                # acknowledgment that the passenger ceased to exist.
                classification = "unresolved_requested_deletion"
            elif conflict:
                classification = "overwritten_reserved" if ashore_after and window["owner"] != "unresolved/recovery/relic" else "unresolved_conflict_before_terminal"
            elif full and window["owner"] != "unresolved/recovery/relic":
                classification = "successful_reserved_corroborated_full_load"
            elif unit in landed and window["owner"] == "assault":
                classification = "successful_reserved_corroborated_partial_landing"
            else:
                classification = "unresolved"
            passenger = dict(unit=unit, first_garrison=compact(first), retry_count=len(attempts)-1,
                first_conflicting_command=compact(conflict) if conflict else None,
                likely_source=source_hint(conflict) if conflict else None,
                known_ashore_after_conflict=ashore_after,
                reservation_samples=own_samples,
                deletion_request=compact(deleted) if deleted else None,
                flag_at_exact_overwrite="not recorded",
                command_count_before_terminal=len(conflicting), classification=classification,
                original_task_result="full load corroborated" if full else end.get("message", "unresolved"),
                partial_landing_exact_member=unit in landed,
                overwrite_prevented_completion="not established; full load later" if full else "not established by orders alone")
            passengers.append(passenger)
            aggregate[player][classification] += 1
            membership[player, unit].append((window, passenger))
        window["passengers"] = passengers
        window["load_commands"] = [compact(e) | {"object_ids": e["object_ids"]} for e in window.pop("loads")]
        window.pop("start_chat")
        aggregate[player]["boarding_windows"] += 1
        aggregate[player]["load_commands"] += len(window["load_commands"])

    for totals in aggregate.values():
        # Neither absence from a retry nor a hull's cargo delta establishes
        # passenger death. Keep explicit zero PROVEN counts, not zero deaths.
        totals["died_proven"] = 0
        totals["genuinely_unavailable_proven"] = 0

    retreats = []
    for event in events:
        if event["action"] != "DE_RETREAT":
            continue
        player = event["player_id"]
        members = []
        for unit in event.get("object_ids", []):
            active = [(w, p) for w, p in membership[player, unit]
                      if p["first_garrison"]["sequence"] < event["sequence"] < w["end_sequence"]
                      and event["milliseconds"] <= w["terminal"]["milliseconds"]]
            previous = next((e for e in reversed(by_unit[player, unit]) if e["sequence"] < event["sequence"]), None)
            members.append(dict(unit=unit, boarding_windows=[w["id"] for w, _ in active],
                                owners=[w["owner"] for w, _ in active],
                                prior_command=compact(previous) if previous else None,
                                other_task_owner="not reconstructable from command packet alone"))
        retreats.append(compact(event) | dict(player=player, color=colors[player], members=members,
                                             likely_source=source_hint(event)))
        aggregate[player]["retreat_events"] += 1
    return dict(aggregate={colors[p]: dict(c) for p, c in sorted(aggregate.items())},
                boarding_windows=windows, retreats=retreats,
                limitations=["No simulation death events: died remains unknown, never inferred from missing orders.",
                  "Orders do not acknowledge execution. Full-load corroboration is not an exact boarding timestamp.",
                  "First conflicting packets are confirmed pre-boarding only with a later ashore retry/sample.",
                  "Unobserved release/cargo/manual intervention prevents universal task/caller attribution.",
                  "Command-linked hull coverage is not a census of idle/empty ships or physical obstructions.",
                  "Missing congestion logs do not establish movement or clearance; inspect unresolved phase/position evidence.",
                  "Missing explicit terminals remain open to a later unload/new start or replay end; no arbitrary time cap."])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("replay", type=Path)
    parser.add_argument("--parser-root", type=Path, required=True)
    parser.add_argument("--transport-audit", type=Path, required=True)
    parser.add_argument("--repository", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument('--writer-manifest', type=Path,
                        help='Exact T10 writer-trace-sites.json supplied with the deployed runtime')
    args = parser.parse_args()
    prior = json.loads(args.transport_audit.read_text(encoding="utf-8"))
    hulls = {(row["player"], row["hull"]) for row in prior["hull_phases"]}
    colors = {row["player"]: row["color"] for row in prior["hull_phases"]}
    events, counts, failures = read_stream(args.replay, args.parser_root)
    report = analyze(events, hulls, colors, tuple(prior.get("attack_terminals", [])))
    report["economic_retask_correlation"] = economic_retask_correlation(events)
    if args.writer_manifest:
        from audit_writer_trace import analyze_writer_trace
        manifest = json.loads(args.writer_manifest.read_text(encoding='utf-8'))
        report['writer_trace'] = analyze_writer_trace(events, manifest, report['boarding_windows'])
    with args.replay.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    with args.transport_audit.open("rb") as source:
        prior_digest = hashlib.file_digest(source, "sha256").hexdigest()
    report.update(source_sha256=digest, action_counts=counts, decoder_failures=failures,
                  transport_audit_path=str(args.transport_audit.resolve()),
                  transport_audit_sha256=prior_digest,
                  script_inventory=inventory_writers(args.repository),
                  transport_identity_basis="caller-supplied matching all-player telemetry/unload hull union; exact packet IDs redecoded; this input does not itself contain a replay hash")
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(dict(aggregate=report["aggregate"], decoder_failures=failures,
                          boarding_windows=len(report["boarding_windows"]),
                          retreat_events=len(report["retreats"])), indent=2))


if __name__ == "__main__":
    main()

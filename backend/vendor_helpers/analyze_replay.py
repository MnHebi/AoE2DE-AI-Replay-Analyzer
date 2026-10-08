#!/usr/bin/env python3
"""Read an AoE2 DE replay while preserving authoritative selected colors."""

from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import Counter, defaultdict
from collections.abc import Mapping, Sequence
from pathlib import Path


def timestamp(milliseconds: int) -> str:
    seconds = milliseconds // 1000
    return f"{seconds // 60:02d}:{seconds % 60:02d}"


def decode_chat(payload: bytes) -> dict[str, object]:
    text = payload.decode("utf-8", errors="ignore").replace("\x00", "")
    text = "".join(character for character in text if character.isprintable()).strip()
    try:
        message = json.loads(text)
    except (json.JSONDecodeError, TypeError):
        return {"message": text}
    if isinstance(message, dict):
        return {
            "player": message.get("player"),
            "channel": message.get("channel"),
            "taunt_number": message.get("tauntNumber"),
            "message": str(message.get("message", text)),
            "message_agp": message.get("messageAGP"),
        }
    return {"message": text}


def record_value(record: object, name: str, default: object = None) -> object:
    """Read a field from a Construct Container or an ordinary mapping."""
    if isinstance(record, Mapping):
        return record.get(name, default)
    return getattr(record, name, default)


def decode_de_string(value: object) -> str:
    """Decode a DE string field from either fast or full header output."""
    if isinstance(value, Mapping) and "value" in value:
        value = value["value"]
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="ignore")
    return "" if value is None else str(value)


def unavailable_players(fast_players: Sequence[object]) -> list[dict[str, object]]:
    """Retain non-visual fields while making unavailable colors explicit."""
    players: list[dict[str, object]] = []
    for player in fast_players:
        number = record_value(player, "number")
        if not isinstance(number, int) or number <= 0:
            continue
        players.append(
            {
                "number": number,
                "color_id": None,
                "selected_color_id": None,
                "internal_color_id": record_value(player, "color_id"),
                "selected_team_id": None,
                "resolved_team_id": record_value(player, "team_id"),
                "civilization_id": record_value(player, "civilization_id"),
                "ai_name": decode_de_string(record_value(player, "ai_name", b"")),
            }
        )
    return sorted(players, key=lambda item: int(item["number"]))


def validated_players(
    fast_players: Sequence[object], full_players: Sequence[object]
) -> list[dict[str, object]]:
    """Join DE player records by player number and validate selected colors."""

    def index_active(records: Sequence[object], number_field: str) -> dict[int, object]:
        indexed: dict[int, object] = {}
        for record in records:
            number = record_value(record, number_field)
            if not isinstance(number, int):
                raise ValueError(f"non-integer {number_field}: {number!r}")
            if number <= 0:
                continue
            if number in indexed:
                raise ValueError(f"duplicate active player number: {number}")
            indexed[number] = record
        return indexed

    fast_by_number = index_active(fast_players, "number")
    full_by_number = index_active(full_players, "player_number")
    if not fast_by_number:
        raise ValueError("DE header contains no active players")
    if set(fast_by_number) != set(full_by_number):
        raise ValueError(
            "fast/full active player mismatch: "
            f"fast={sorted(fast_by_number)} full={sorted(full_by_number)}"
        )

    players: list[dict[str, object]] = []
    for number in sorted(fast_by_number):
        fast_player = fast_by_number[number]
        full_player = full_by_number[number]
        internal_color = record_value(full_player, "color_id")
        selected_color = record_value(full_player, "selected_color")
        selected_team = record_value(full_player, "selected_team_id")
        resolved_team = record_value(full_player, "resolved_team_id")
        civilization = record_value(full_player, "civ_id")

        if internal_color != record_value(fast_player, "color_id"):
            raise ValueError(f"player {number} internal color mismatch")
        if resolved_team != record_value(fast_player, "team_id"):
            raise ValueError(f"player {number} resolved team mismatch")
        if civilization != record_value(fast_player, "civilization_id"):
            raise ValueError(f"player {number} civilization mismatch")
        if not isinstance(selected_color, int) or not 0 <= selected_color <= 7:
            raise ValueError(f"player {number} invalid selected color: {selected_color!r}")
        if not isinstance(selected_team, int) or not isinstance(resolved_team, int):
            raise ValueError(f"player {number} invalid selected/resolved team")

        players.append(
            {
                "number": number,
                # color_id remains the consumer-facing visible color for backward
                # compatibility. Never substitute the internal field here.
                "color_id": selected_color,
                "selected_color_id": selected_color,
                "internal_color_id": internal_color,
                "selected_team_id": selected_team,
                "resolved_team_id": resolved_team,
                "civilization_id": civilization,
                "ai_name": decode_de_string(record_value(full_player, "ai_name")),
            }
        )
    return players


def json_default(value: object) -> object:
    """Serialize parser containers and preserve otherwise opaque binary payloads."""
    if isinstance(value, (bytes, bytearray, memoryview)):
        return {"encoding": "hex", "value": bytes(value).hex()}
    if isinstance(value, Mapping):
        return dict(value)  # Construct containers are Mapping implementations.
    raise TypeError(f"cannot serialize replay value of type {type(value).__name__}")


def emit_report(report: dict[str, object], output: Path | None) -> None:
    rendered = json.dumps(report, indent=2, default=json_default, allow_nan=False)
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered + "\n", encoding="utf-8")
        print(output)
    else:
        print(rendered)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("parser_root", type=Path)
    parser.add_argument("replay", type=Path)
    parser.add_argument("--compact", action="store_true")
    parser.add_argument("--header-only", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    sys.path.insert(0, str(args.parser_root.resolve()))
    from mgz import fast  # pylint: disable=import-outside-toplevel
    from mgz.fast import header as fast_header  # pylint: disable=import-outside-toplevel
    from mgz.fast.enums import Action, Operation  # pylint: disable=import-outside-toplevel
    try:
        from mgz.header.de import de as full_de_header  # pylint: disable=import-outside-toplevel
    except ImportError:
        full_de_header = None

    # The fast parser exposes an internal color field and skips the adjacent
    # selected-color byte. Parse the isolated full DE lobby block as well, join
    # only by signed player number, and fail closed if the two views disagree.
    players: list[dict[str, object]] = []
    fast_players: list[object] = []
    replay_settings: dict[str, object] = {}
    header_parse_error: str | None = None
    visible_player_metadata_status = "unavailable"
    visible_player_metadata_error: str | None = None
    decompressed = None
    de_start = 0
    save_version = 0.0
    header_length = 0
    try:
        with args.replay.open("rb") as header_handle:
            header_length = struct.unpack("<I", header_handle.read(4))[0]
            header_handle.seek(0)
            decompressed = fast_header.decompress(header_handle)
            version, game_version, save_version, log_version = fast_header.parse_version(
                decompressed, header_handle
            )
            de_start = decompressed.tell()
            de_header = fast_header.parse_de(decompressed, version, save_version)
        fast_players = list(de_header.get("players", []))
        players = unavailable_players(fast_players)
        replay_settings = {
            "game_version": game_version,
            "save_version": save_version,
            "log_version": log_version,
            "difficulty_id": de_header.get("difficulty_id"),
            "population_limit": de_header.get("population_limit"),
            "speed": de_header.get("speed"),
            "treaty_length": de_header.get("treaty_length"),
            "shared_exploration": de_header.get("shared_exploration"),
            "rms_map_id": de_header.get("rms_map_id"),
            "rms_filename": de_header.get("rms_filename"),
        }
    except (AssertionError, OSError, RuntimeError, ValueError, struct.error) as error:
        header_parse_error = f"{type(error).__name__}: {error}"
        visible_player_metadata_error = (
            f"fast DE header unavailable: {type(error).__name__}: {error}"
        )

    if header_parse_error is None:
        if full_de_header is None:
            visible_player_metadata_error = (
                "parser does not provide mgz.header.de selected-color metadata"
            )
        else:
            try:
                decompressed.seek(de_start)
                full_de = full_de_header.parse_stream(
                    decompressed, save_version=save_version
                )
                players = validated_players(fast_players, list(full_de.players))
                visible_player_metadata_status = "selected-color-validated"
            except Exception as error:  # Parser-version compatibility boundary.
                players = unavailable_players(fast_players)
                visible_player_metadata_error = (
                    f"selected-color metadata unavailable: {type(error).__name__}: {error}"
                )

    if args.header_only:
        emit_report(
            {
                "file": str(args.replay),
                "header_length": header_length,
                "players": players,
                "visible_player_metadata_status": visible_player_metadata_status,
                "visible_player_metadata_error": visible_player_metadata_error,
                "replay_settings": replay_settings,
                "header_parse_error": header_parse_error,
            },
            args.output,
        )
        return

    operation_counts: Counter[str] = Counter()
    action_counts: Counter[str] = Counter()
    player_action_counts: dict[int, Counter[str]] = defaultdict(Counter)
    chats: list[dict[str, object]] = []
    relevant_actions: list[dict[str, object]] = []
    market_actions: list[dict[str, object]] = []
    build_actions: list[dict[str, object]] = []
    research_actions: list[dict[str, object]] = []
    make_actions: list[dict[str, object]] = []
    work_actions: list[dict[str, object]] = []
    ai_order_actions: list[dict[str, object]] = []
    order_actions: list[dict[str, object]] = []
    transport_actions: list[dict[str, object]] = []
    resign_actions: list[dict[str, object]] = []
    retreat_actions: list[dict[str, object]] = []
    error_actions: list[dict[str, object]] = []
    wall_actions: list[dict[str, object]] = []
    delete_actions: list[dict[str, object]] = []
    flare_actions: list[dict[str, object]] = []
    guard_actions: list[dict[str, object]] = []
    patrol_actions: list[dict[str, object]] = []
    player_market_counts: dict[int, Counter[str]] = defaultdict(Counter)
    player_build_counts: dict[int, Counter[str]] = defaultdict(Counter)
    player_make_counts: dict[int, Counter[str]] = defaultdict(Counter)
    minute_action_counts: dict[int, dict[int, Counter[str]]] = defaultdict(
        lambda: defaultdict(Counter)
    )
    errors: list[dict[str, object]] = []
    game_time = 0

    with args.replay.open("rb") as handle:
        file_size = args.replay.stat().st_size
        header_length = struct.unpack("<I", handle.read(4))[0]
        handle.seek(header_length)
        fast.meta(handle)

        while handle.tell() < file_size:
            offset = handle.tell()
            try:
                operation, payload = fast.operation(handle)
            except EOFError:
                break
            except (RuntimeError, ValueError, struct.error) as error:
                errors.append(
                    {
                        "offset": offset,
                        "next_offset": handle.tell(),
                        "time": timestamp(game_time),
                        "error": str(error),
                    }
                )
                if handle.tell() <= offset:
                    break
                continue

            operation_counts[operation.name] += 1
            if operation == Operation.SYNC:
                increment, _checksum, sync_payload = payload
                game_time += increment
                if sync_payload.get("current_time"):
                    game_time = max(game_time, sync_payload["current_time"])
                continue

            if operation == Operation.CHAT:
                decoded = decode_chat(payload)
                if decoded.get("message"):
                    chats.append({"time": timestamp(game_time), **decoded})
                continue

            if operation != Operation.ACTION:
                continue

            action, action_payload = payload
            action_counts[action.name] += 1
            player_id = action_payload.get("player_id")
            if player_id is not None:
                player_id = int(player_id)
                player_action_counts[player_id][action.name] += 1
                minute_action_counts[player_id][game_time // 60000][action.name] += 1

            action_record = {
                "time": timestamp(game_time),
                "action": action.name,
                **action_payload,
            }

            if action in {Action.BUY, Action.SELL}:
                market_actions.append(action_record)
                if player_id is not None:
                    resource_id = int(action_payload.get("resource_id", -1))
                    resource_name = {0: "food", 1: "wood", 2: "stone", 3: "gold"}.get(
                        resource_id, str(resource_id)
                    )
                    player_market_counts[player_id][f"{action.name.lower()}_{resource_name}"] += int(
                        action_payload.get("amount", 1)
                    )

            if action == Action.BUILD:
                build_actions.append(action_record)
                if player_id is not None:
                    player_build_counts[player_id][str(action_payload.get("building_id"))] += 1

            if action == Action.MAKE and player_id is not None:
                player_make_counts[player_id][str(action_payload.get("unit_id"))] += 1
                make_actions.append(action_record)

            if action == Action.WORK:
                work_actions.append(action_record)

            if action == Action.AI_ORDER:
                ai_order_actions.append(action_record)

            if action in {Action.ORDER, Action.MOVE, Action.STOP}:
                order_actions.append(action_record)

            if action in {Action.UNGARRISON, Action.SPECIAL}:
                transport_actions.append(action_record)

            if action == Action.RESEARCH:
                research_actions.append(action_record)

            if action == Action.RESIGN:
                resign_actions.append(action_record)

            if action == Action.DE_RETREAT:
                retreat_actions.append(action_record)

            if action == Action.ERROR:
                error_actions.append(action_record)

            if action == Action.WALL:
                wall_actions.append(action_record)

            if action == Action.DELETE:
                delete_actions.append(action_record)

            if action == Action.FLARE:
                flare_actions.append(action_record)

            if action == Action.GUARD:
                guard_actions.append(action_record)

            if action == Action.PATROL:
                patrol_actions.append(action_record)

            if action in {
                Action.BUILD,
                Action.BUY,
                Action.SELL,
                Action.RESEARCH,
                Action.RESIGN,
                Action.TRIBUTE,
                Action.DE_TRIBUTE,
                Action.GAME,
            }:
                relevant_actions.append(action_record)

    chat_terms = (
        "map type",
        "antiquity",
        "phase",
        "aging",
        "rush",
        "attack",
        "dock",
        "shipyard",
        "warboat",
        "sell",
        "buy",
        "gold-present",
        "present",
        "checking resources",
        "resource check",
        "depleted",
        "market",
        "roman",
        "briton",
        "nero",
        "migration",
        "transport",
        "lift",
        "route-screen",
        "retreat",
        "recall",
        "blocked",
        "controller",
        "home defend leash",
    )
    filtered_chats = [
        item for item in chats if any(term in str(item["message"]).lower() for term in chat_terms)
    ]

    market_summary: dict[str, dict[str, object]] = {}
    for item in market_actions:
        resource_id = int(item.get("resource_id", -1))
        resource_name = {0: "food", 1: "wood", 2: "stone", 3: "gold"}.get(
            resource_id, str(resource_id)
        )
        key = f"p{item.get('player_id')}_{str(item['action']).lower()}_{resource_name}"
        entry = market_summary.setdefault(
            key,
            {"count": 0, "amount": 0, "first": item["time"], "last": item["time"]},
        )
        entry["count"] = int(entry["count"]) + 1
        entry["amount"] = int(entry["amount"]) + int(item.get("amount", 1))
        entry["last"] = item["time"]

    chat_summary: dict[str, dict[str, object]] = {}
    for item in filtered_chats:
        key = f"p{item.get('player')}|{item['message']}"
        entry = chat_summary.setdefault(
            key, {"count": 0, "first": item["time"], "last": item["time"]}
        )
        entry["count"] = int(entry["count"]) + 1
        entry["last"] = item["time"]

    key_build_actions = [
        item for item in build_actions if item.get("building_id") in {45, 84, 1251}
    ]

    compact_minutes = {
        player: {
            minute: {
                name: counts.get(name, 0)
                for name in ("WORK", "AI_ORDER", "STOP", "BUILD", "SELL", "BUY")
                if counts.get(name, 0)
            }
            for minute, counts in sorted(minutes.items())
        }
        for player, minutes in minute_action_counts.items()
    }

    report = {
        "file": str(args.replay),
        "header_length": header_length,
        "players": players,
        "visible_player_metadata_status": visible_player_metadata_status,
        "visible_player_metadata_error": visible_player_metadata_error,
        "replay_settings": replay_settings,
        "header_parse_error": header_parse_error,
        "duration": timestamp(game_time),
        "operation_counts": operation_counts,
        "action_counts": action_counts,
        "player_action_counts": player_action_counts,
        "player_market_counts": player_market_counts,
        "player_build_counts": player_build_counts,
        "player_make_counts": player_make_counts,
        "minute_action_counts": compact_minutes,
        "chat_count": len(chats),
        "first_chats": chats[:30],
        "chat_summary": chat_summary,
        "market_summary": market_summary,
        "key_build_actions": key_build_actions,
        "research_actions": research_actions,
        "make_actions": make_actions,
        "work_actions": work_actions,
        "ai_order_actions": ai_order_actions,
        "order_actions": order_actions,
        "transport_actions": transport_actions,
        "resign_actions": resign_actions,
        "retreat_actions": retreat_actions,
        "error_actions": error_actions,
        "wall_actions": wall_actions,
        "delete_actions": delete_actions,
        "flare_actions": flare_actions,
        "guard_actions": guard_actions,
        "patrol_actions": patrol_actions,
        "parse_errors": errors,
    }
    if not args.compact:
        report["filtered_chats"] = filtered_chats
        report["market_actions"] = market_actions
        report["build_actions"] = build_actions
        report["chats"] = chats
        report["relevant_actions"] = relevant_actions
    emit_report(report, args.output)


if __name__ == "__main__":
    main()

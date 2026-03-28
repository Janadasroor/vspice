import json
import os
import re
from datetime import datetime, timezone


def _now_iso():
    return datetime.now(timezone.utc).isoformat()


def _project_root(project_path):
    if not project_path:
        return ""
    if os.path.isdir(project_path):
        return project_path
    return os.path.dirname(project_path)


def _memory_file_path(project_path):
    root = _project_root(project_path)
    if not root:
        return ""
    memory_dir = os.path.join(root, ".gemini")
    os.makedirs(memory_dir, exist_ok=True)
    return os.path.join(memory_dir, "memory.json")


def get_memory_file_path(project_path):
    return _memory_file_path(project_path)


def _default_memory(project_path):
    return {
        "version": 1,
        "project_path": project_path or "",
        "updated_at": _now_iso(),
        "user_preferences": [],
        "project_notes": [],
        "recent_topics": [],
        "recent_turns": [],
    }


def load_memory(project_path):
    memory_path = _memory_file_path(project_path)
    if not memory_path or not os.path.exists(memory_path):
        return _default_memory(project_path)
    try:
        with open(memory_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        if not isinstance(data, dict):
            return _default_memory(project_path)
        base = _default_memory(project_path)
        base.update(data)
        return base
    except Exception:
        return _default_memory(project_path)


def save_memory(project_path, memory):
    memory_path = _memory_file_path(project_path)
    if not memory_path:
        return
    memory["updated_at"] = _now_iso()
    with open(memory_path, "w", encoding="utf-8") as f:
        json.dump(memory, f, indent=2, ensure_ascii=True)


def _compact(text, limit=280):
    text = re.sub(r"\s+", " ", str(text or "")).strip()
    if len(text) <= limit:
        return text
    return text[: limit - 3].rstrip() + "..."


def _extract_explicit_items(prompt_text, patterns):
    found = []
    for pattern in patterns:
        for match in re.finditer(pattern, prompt_text, flags=re.IGNORECASE):
            value = _compact(match.group(1), 180)
            if value:
                found.append(value)
    return found


def update_memory(project_path, user_prompt, assistant_response):
    memory = load_memory(project_path)

    prompt_text = str(user_prompt or "").strip()
    response_text = str(assistant_response or "").strip()

    if prompt_text:
        topic = _compact(prompt_text, 120)
        if topic:
            memory["recent_topics"] = [t for t in memory.get("recent_topics", []) if t != topic]
            memory["recent_topics"].insert(0, topic)
            memory["recent_topics"] = memory["recent_topics"][:12]

        pref_patterns = [
            r"\bprefer\s+(.+?)(?:[.!?\n]|$)",
            r"\balways\s+(.+?)(?:[.!?\n]|$)",
        ]
        note_patterns = [
            r"\bremember\s+that\s+(.+?)(?:[.!?\n]|$)",
            r"\bnote\s+that\s+(.+?)(?:[.!?\n]|$)",
            r"\bsave\s+this\s+as\s+(.+?)(?:[.!?\n]|$)",
        ]

        for item in _extract_explicit_items(prompt_text, pref_patterns):
            if item not in memory["user_preferences"]:
                memory["user_preferences"].insert(0, item)
        memory["user_preferences"] = memory["user_preferences"][:12]

        for item in _extract_explicit_items(prompt_text, note_patterns):
            if item not in memory["project_notes"]:
                memory["project_notes"].insert(0, item)
        memory["project_notes"] = memory["project_notes"][:20]

        turn = {
            "timestamp": _now_iso(),
            "user": _compact(prompt_text, 300),
            "assistant": _compact(response_text, 400),
        }
        memory["recent_turns"].append(turn)
        memory["recent_turns"] = memory["recent_turns"][-20:]

    save_memory(project_path, memory)


def remember_fact(project_path, text, bucket="project_notes"):
    memory = load_memory(project_path)
    clean = _compact(text, 220)
    if not clean:
        return False, "Nothing to remember."
    if bucket not in {"project_notes", "user_preferences"}:
        bucket = "project_notes"
    items = list(memory.get(bucket, []))
    if clean in items:
        return True, f"Already stored in {bucket.replace('_', ' ')}."
    items.insert(0, clean)
    memory[bucket] = items[:20]
    save_memory(project_path, memory)
    return True, f"Stored in {bucket.replace('_', ' ')}."


def forget_fact(project_path, text):
    memory = load_memory(project_path)
    needle = _compact(text, 220).lower()
    if not needle:
        return False, "Nothing to forget."

    removed = []
    for bucket in ("user_preferences", "project_notes", "recent_topics"):
        kept = []
        for item in memory.get(bucket, []):
            if needle in str(item).lower():
                removed.append((bucket, item))
            else:
                kept.append(item)
        memory[bucket] = kept

    if removed:
        save_memory(project_path, memory)
        labels = ", ".join(item for _, item in removed[:5])
        return True, f"Removed {len(removed)} memory item(s): {labels}"
    return False, "No matching memory item was found."


def clear_memory(project_path):
    memory = _default_memory(project_path)
    save_memory(project_path, memory)
    return True


def render_memory_report(project_path):
    memory = load_memory(project_path)
    lines = []
    lines.append("Persistent memory is enabled for this project.")
    memory_path = get_memory_file_path(project_path)
    if memory_path:
        lines.append(f"Memory file: {memory_path}")

    prefs = memory.get("user_preferences", [])
    notes = memory.get("project_notes", [])
    topics = memory.get("recent_topics", [])
    turns = memory.get("recent_turns", [])

    lines.append(f"User preferences: {len(prefs)}")
    for item in prefs[:8]:
        lines.append(f"- {item}")

    lines.append(f"Project notes: {len(notes)}")
    for item in notes[:10]:
        lines.append(f"- {item}")

    lines.append(f"Recent topics: {len(topics)}")
    for item in topics[:8]:
        lines.append(f"- {item}")

    lines.append(f"Recent turns stored: {len(turns)}")
    return "\n".join(lines)


def format_memory_context(project_path, history_json=""):
    memory = load_memory(project_path)
    sections = []

    prefs = memory.get("user_preferences", [])
    if prefs:
        sections.append("User preferences:\n- " + "\n- ".join(prefs[:8]))

    notes = memory.get("project_notes", [])
    if notes:
        sections.append("Project notes:\n- " + "\n- ".join(notes[:10]))

    recent_topics = memory.get("recent_topics", [])
    if recent_topics:
        sections.append("Recent topics:\n- " + "\n- ".join(recent_topics[:8]))

    turns = memory.get("recent_turns", [])
    if turns:
        lines = []
        for turn in turns[-6:]:
            user_text = _compact(turn.get("user", ""), 160)
            assistant_text = _compact(turn.get("assistant", ""), 160)
            if user_text:
                lines.append(f"User: {user_text}")
            if assistant_text:
                lines.append(f"Assistant: {assistant_text}")
        if lines:
            sections.append("Persistent recent turns:\n" + "\n".join(lines))

    if history_json:
        try:
            history = json.loads(history_json)
            if isinstance(history, list):
                lines = []
                for item in history[-8:]:
                    if not isinstance(item, dict):
                        continue
                    role = str(item.get("role", "")).strip().lower()
                    text = _compact(item.get("text", ""), 180)
                    if not text:
                        continue
                    label = "User" if role == "user" else "Assistant"
                    lines.append(f"{label}: {text}")
                if lines:
                    sections.append("Current chat history:\n" + "\n".join(lines))
        except Exception:
            pass

    return "\n\n".join(sections).strip()

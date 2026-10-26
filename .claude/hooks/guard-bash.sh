#!/usr/bin/env bash
# PreToolUse guard for Bash. Enforces AGENTS.md hard rules 1 and 2.
# Exit 2 blocks the command and shows stderr to the agent.
cmd=$(jq -r '.tool_input.command // empty')

# Rule 1: no requests to the RetroAchievements server API (docs pages are fine).
if grep -qiE 'retroachievements\.org/(dorequest\.php|api/)' <<<"$cmd"; then
    echo "Blocked: this command talks to the RetroAchievements server API. Uploads/requests need explicit RA team approval (AGENTS.md rule 1)." >&2
    exit 2
fi

# Rule 2: never stage ROM, disc image or BIOS files.
if grep -qE '(^|[;&|[:space:]])git[[:space:]]+add' <<<"$cmd" &&
    grep -qiE '\.(bin|cue|chd|iso|img|pbp|ecm|sfc|smc|nes|gba|gbc?|n64|z64|v64|gen|smd)([[:space:]"'\'']|$)|scph' <<<"$cmd"; then
    echo "Blocked: staging what looks like a ROM/BIOS/disc image. Game data must never be committed (AGENTS.md rule 2)." >&2
    exit 2
fi

# Rule 2, broad adds: `git add .`, `git add -A` or `git commit -a` name no files, so check what
# git could pick up instead: untracked files that are not ignored, plus anything already staged.
if grep -qE '(^|[;&|[:space:]])git[[:space:]]+(add|commit)' <<<"$cmd"; then
    risky=$(cd "${CLAUDE_PROJECT_DIR:-.}" &&
        { git ls-files --others --exclude-standard; git diff --cached --name-only; } 2>/dev/null |
        grep -iE '\.(bin|cue|chd|iso|img|pbp|ecm|sfc|smc|nes|gba|gbc?|n64|z64|v64|gen|smd)$|(^|/)scph')
    if [ -n "$risky" ]; then
        echo "Blocked: these look like ROM/BIOS/disc images and are staged or not ignored (AGENTS.md rule 2):" >&2
        echo "$risky" >&2
        echo "Add them to .gitignore (patterns are case-sensitive) before running git add/commit." >&2
        exit 2
    fi
fi

exit 0

@AGENTS.md

## Claude Code specifics

- Hooks (`.claude/settings.json`):
  - `PreToolUse` on Bash → `.claude/hooks/guard-bash.sh` blocks commands that would send
    requests to the RA server API or `git add` ROM/BIOS-like files (hard rules 1 and 2).
  - `PostToolUse` on Edit/Write → `.claude/hooks/format-cpp.sh` runs `clang-format -i` on
    C/C++ files.
- Skills in `.claude/skills/` load automatically when relevant; open them explicitly when
  working on RA formats, the libretro front-end or packaging.
- The REA reverse-engineering MCP tools are available in this environment; they are useful for
  inspecting PS1 executables of games the user owns, not for this repo's own code.

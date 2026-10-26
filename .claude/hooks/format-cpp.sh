#!/usr/bin/env bash
# PostToolUse formatter: runs clang-format on C/C++ files after Edit/Write.
f=$(jq -r '.tool_response.filePath // .tool_input.file_path // empty')
case "$f" in
*.c | *.cc | *.cpp | *.cxx | *.h | *.hh | *.hpp | *.hxx) ;;
*) exit 0 ;;
esac
case "$f" in
*/third_party/*) exit 0 ;;
esac
command -v clang-format >/dev/null || exit 0
[ -f "$f" ] && clang-format -i --style=file "$f"
exit 0

"""Offline checks for tracked repository files; no third-party dependencies."""
from __future__ import annotations

import argparse
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
from urllib.parse import unquote, urlsplit


NEUTRAL_FILES = frozenset({
    "src/window_info.h",
    "src/windows_window_scanner.h",
    "src/windows_window_scanner.cpp",
    "src/platform/window_api_adapter.h",
    "src/platform/layout_executor.h",
    "src/platform/layout_executor.cpp",
})
NEUTRAL_PREFIXES = ("src/core/", "src/ui/")


def git(root: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(root), *args], encoding="utf-8")


def prose(text: str) -> str:
    """Exclude fenced examples before interpreting Markdown destinations."""
    return re.sub(r"(?ms)^\s*(`{3,}|~{3,})[^\n]*\n.*?^\s*\1\s*$", "", text)


def anchors(text: str) -> set[str]:
    found: set[str] = set()
    counts: dict[str, int] = {}
    for heading in re.findall(r"(?m)^ {0,3}#{1,6}\s+(.+?)\s*#*\s*$", prose(text)):
        slug = re.sub(r"[^\w\- ]", "", heading.lower()).replace(" ", "-")
        number = counts.get(slug, 0)
        found.add(slug if number == 0 else f"{slug}-{number}")
        counts[slug] = number + 1
    found.update(re.findall(r'<a\s+(?:id|name)=["\']([^"\']+)', text, re.I))
    return found


def markdown_links(text: str) -> list[str]:
    text = prose(text)
    # Inline links/images and full reference definitions. Inline code is not a link.
    text = re.sub(r"`+[^`\n]*`+", "", text)
    # Match each destination marker, including both targets in [![alt](a.png)](b.md).
    inline = re.findall(r"\]\(\s*(<[^>]+>|[^\s)]+)", text)
    references = re.findall(r"(?m)^ {0,3}\[[^\]\n]+\]:\s*(<[^>]+>|\S+)", text)
    return [item.strip("<>") for item in inline + references]


def link_errors(root: Path, files: set[str], source: str, text: str) -> list[str]:
    errors = []
    for destination in markdown_links(text):
        try:
            parts = urlsplit(destination)
        except ValueError:
            errors.append(f"{source}: malformed link: {destination}")
            continue
        if parts.scheme in ("https", "http", "mailto"):
            continue  # Remote access is deliberately outside this offline check.
        if parts.scheme or parts.netloc or "\\" in parts.path:
            errors.append(f"{source}: non-portable local link: {destination}")
            continue
        path = unquote(parts.path)
        target = (root / path.lstrip("/") if path.startswith("/")
                  else root / PurePosixPath(source).parent / path) if path else root / source
        target = target.resolve()
        try:
            relative = target.relative_to(root.resolve()).as_posix()
        except ValueError:
            errors.append(f"{source}: link escapes repository: {destination}")
            continue
        # Exact tracked spelling makes Windows checks catch Linux case errors too.
        # Normalize '..' lexically: resolve() on Windows can change actual casing.
        logical = PurePosixPath(path.lstrip("/")) if path.startswith("/") else PurePosixPath(source).parent / path
        if not path:
            logical = PurePosixPath(source)
        pieces = []
        for piece in logical.parts:
            if piece == "..":
                if pieces:
                    pieces.pop()
            elif piece != ".":
                pieces.append(piece)
        relative = "/".join(pieces)
        is_directory = any(name.startswith(relative.rstrip("/") + "/") for name in files)
        if relative not in files and not is_directory:
            errors.append(f"{source}: missing/untracked local link: {destination}")
        elif parts.fragment and relative.lower().endswith(".md"):
            if unquote(parts.fragment) not in anchors(target.read_text(encoding="utf-8")):
                errors.append(f"{source}: missing heading: {destination}")
    return errors


def neutral_source(path: str) -> bool:
    return path.startswith(NEUTRAL_PREFIXES) or path in NEUTRAL_FILES


def boundary_errors(path: str, text: str) -> list[str]:
    if not neutral_source(path) or not path.endswith((".h", ".hpp", ".cpp")):
        return []
    # Raw strings can contain comment markers and native-looking examples.
    code = re.sub(r'(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"',
                  '""', text, flags=re.S)
    code = re.sub(r"/\*.*?\*/|//[^\n]*", "", code, flags=re.S)
    native_include = re.search(r'^\s*#\s*include\s*[<"](?:windows\.h|windowsx\.h|dwmapi\.h|X11/)', code, re.I | re.M)
    # Exclude string literals before looking for native type tokens.
    code = re.sub(r'"(?:\\.|[^"\\])*"', '""', code)
    native_type = re.search(r"\b(?:HWND|HDC|HANDLE|DWORD|WPARAM|LPARAM|LRESULT|XEvent)\b", code)
    return [f"{path}: native window API leaked into neutral/UI code"] if native_include or native_type else []


def artifact_errors(path: str) -> list[str]:
    parts = PurePosixPath(path).parts
    blocked = any(part in {"__pycache__", ".venv", ".vs", ".idea"} for part in parts)
    blocked |= parts[0] == "plans" or parts[0] == "build" or parts[0].startswith(("build-", "cmake-build-"))
    blocked |= path.endswith((".pyc", ".user")) or parts[-1] in {"CMakeUserPresets.json", "aqtinstall.log", ".env"}
    return [f"{path}: generated/private/temporary file is tracked"] if blocked else []


def check_files(root: Path, paths: list[str]) -> list[str]:
    errors = []
    files = set(paths)
    missing_targets = sorted(NEUTRAL_FILES - files)
    if missing_targets:
        errors.append("boundary check targets missing: " + ", ".join(missing_targets))
    for path in paths:
        errors.extend(artifact_errors(path))
        file = root / path
        if not file.is_file():
            errors.append(f"{path}: tracked file is absent")
            continue
        if file.suffix.lower() not in {".md", ".cpp", ".h", ".hpp", ".py", ".ps1", ".yml", ".yaml", ".json", ".txt"}:
            continue
        try:
            text = file.read_text(encoding="utf-8")
        except UnicodeError:
            errors.append(f"{path}: text is not UTF-8")
            continue
        errors.extend(boundary_errors(path, text))
        if file.suffix.lower() == ".md":
            errors.extend(link_errors(root, files, path, text))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", help="Check whitespace for base...HEAD (CI passes the PR base SHA).")
    args = parser.parse_args()
    root = Path(git(Path.cwd(), "rev-parse", "--show-toplevel").strip())
    paths = [path for path in git(root, "ls-files", "-z").split("\0") if path]
    errors = check_files(root, paths)
    commands = [["diff", "--check"], ["diff", "--cached", "--check"]]
    if args.base and set(args.base) != {"0"}:
        # resolve first; untrusted branch data never becomes a shell expression.
        base = git(root, "rev-parse", "--verify", "--end-of-options", args.base + "^{commit}").strip()
        commands.append(["diff", "--check", base + "...HEAD"])
    else:
        commands.append(["show", "--format=", "--check", "HEAD"])
    for command in commands:
        result = subprocess.run(["git", "-C", str(root), *command], check=False)
        if result.returncode:
            errors.append("git " + " ".join(command) + " failed")
    for error in errors:
        print(error, file=sys.stderr)
    if not errors:
        print(f"Repository checks passed ({len(paths)} tracked files).")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())

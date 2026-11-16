"""Conservative source scans used without invoking the C compiler."""

import re

from .symbols import IDENTIFIER


C_TOKENS = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|' + IDENTIFIER, re.S)


def matching_end(text, start, opening, closing):
    depth = 0
    for index in range(start, len(text)):
        if text[index] == opening:
            depth += 1
        elif text[index] == closing:
            depth -= 1
            if depth == 0:
                return index + 1
    return len(text)


def declaration_names(statement):
    """Extract variables from a top-level declaration, excluding externs/types."""
    if re.match(r"\s*(?:extern|typedef)\b", statement):
        return {}
    while True:
        attribute = re.search(r"\b__attribute__\s*\(", statement)
        if attribute is None:
            break
        end = matching_end(statement, attribute.end() - 1, "(", ")")
        statement = statement[:attribute.start()] + statement[end:]
    static = bool(re.search(r"\bstatic\b", statement))
    parts, start, stack = [], 0, []
    for index, char in enumerate(statement):
        if char in "([{":
            stack.append(char)
        elif char in ")]}":
            if stack:
                stack.pop()
        elif char == "," and not stack:
            parts.append(statement[start:index])
            start = index + 1
    parts.append(statement[start:])
    names = {}
    for position, part in enumerate(parts):
        declarator = part.split("=", 1)[0].strip()
        pointer = re.search(r"\(\s*\*\s*(?:const\s+)?(" + IDENTIFIER + r")\s*\)", declarator)
        if pointer and "(" not in declarator[:pointer.start()]:
            names[pointer[1]] = static
            continue
        name = re.search(r"\b(" + IDENTIFIER + r")\s*(?:\[[^\]]*\]\s*)*$", declarator)
        if name and (position > 0 or name.start() > 0):
            names[name[1]] = static
    return names


def storage_definitions(source):
    """Find unconditional file-scope storage; functions and local variables skip.

    Conditional declarations are left unresolved rather than assuming which
    build branch is enabled. The normal linker still checks actual definitions.
    """
    source = C_TOKENS.sub(lambda match: " " if match[0].startswith(("/", '"', "'")) else match[0], source)
    lines, conditional, continuation = [], 0, False
    for line in source.splitlines():
        directive = re.match(r"\s*#\s*(\w+)", line)
        if directive:
            command = directive[1]
            if command in {"if", "ifdef", "ifndef"}:
                conditional += 1
            elif command == "endif":
                conditional = max(0, conditional - 1)
        skip = bool(directive or continuation or conditional)
        continuation = bool((directive or continuation) and line.rstrip().endswith("\\"))
        lines.append("" if skip else line)
    text = "\n".join(lines)
    definitions, start, index, braces = {}, 0, 0, 0
    while index < len(text):
        char = text[index]
        if char == "{":
            prefix = text[start:index].strip()
            if braces == 0 and "=" not in prefix and prefix.endswith(")"):
                index = matching_end(text, index, "{", "}")
                start = index
                continue
            braces += 1
        elif char == "}":
            braces -= 1
        elif char == ";" and braces == 0:
            definitions.update(declaration_names(text[start:index].strip()))
            start = index + 1
        index += 1
    return definitions

Import("env")

import os
import re

def die(msg):
    raise Exception(msg)

def get_sections_ld_path():
    # nájde framework balík
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    if not framework_dir or not os.path.isdir(framework_dir):
        die("Neviem nájsť balík framework-arduinoespressif32 cez PlatformIO.")

    board_config = env.BoardConfig()
    mcu = (board_config.get("build.mcu") or "").lower()

    # ESP32-P4 používa v Arduino libs názov esp32p4_es
    if mcu == "esp32p4":
        arch_dir = "esp32p4"
    else:
        # fallback: skús priamo mcu (esp32, esp32s3, esp32c6, ...)
        arch_dir = mcu if mcu else "esp32"

    candidate = os.path.join(
        framework_dir,
        "tools",
        "esp32-arduino-libs",
        arch_dir,
        "ld",
        "sections.ld"
    )

    if os.path.isfile(candidate):
        return candidate

    # posledná záchrana: prehľadaj esp32-arduino-libs a nájdi prvé sections.ld
    libs_root = os.path.join(framework_dir, "tools", "esp32-arduino-libs")
    if os.path.isdir(libs_root):
        for root, dirs, files in os.walk(libs_root):
            if "sections.ld" in files and os.path.basename(root) == "ld":
                return os.path.join(root, "sections.ld")

    die("Nenašiel som sections.ld v tools/esp32-arduino-libs/*/ld/sections.ld")

def patch_text(content: str):
    changed = False

    # 0) CATCH ALL orphan text sections (aby nevznikali separátne BIN segmenty)
    irom0_pat = re.compile(
        r"(?m)^(?P<indent>\s*)\*\(\.irom0\.text\)\s*/\*\s*catch stray ICACHE_RODATA_ATTR\s*\*/\s*$"
    )
    m = irom0_pat.search(content)
    if m:
        indent = m.group("indent")
        block = (
            f"{indent}/* CATCH ALL orphan text sections so they don't become separate BIN segments */\n"
            f"{indent}*(.literal .literal.*)\n"
            f"{indent}*(.text .text.*)\n"
        )

        # už to tam je? (kontrola v okolí pred irom0)
        window = content[max(0, m.start() - 800):m.start()]
        if "CATCH ALL orphan text sections" not in window and not re.search(
            r"(?m)^\s*\*\(\.text\s+\.text\.\*\)\s*$", window
        ):
            content = content[:m.start()] + block + content[m.start():]
            changed = True

    # 1) panic wildcard fix: feed_wdts* + panic_print_char*
    # (aby sa chytili aj .part.0 / varianty)
    new = re.sub(r"(\.text\.esp_panic_handler_feed_wdts)(?!\*)", r"\1*", content)
    if new != content:
        content = new
        changed = True

    new = re.sub(r"(\.text\.panic_print_char)(?!\*)", r"\1*", content)
    if new != content:
        content = new
        changed = True

    # 2) fix pre "segment length must be multiple of 4"
    # Vloží . = ALIGN(4); tesne pred _text_end, ak tam už nie je.
    # (idempotentné – keď už je, nepridá)
    pat = re.compile(r"(?m)^(?P<indent>\s*)_text_end\s*=\s*ABSOLUTE\s*\(\s*\.\s*\)\s*;\s*$")
    m = pat.search(content)
    if m:
        start = m.start()
        # pozri 2 riadky nad tým, či už tam nie je ALIGN(4)
        before = content[max(0, start - 400):start]
        if not re.search(r"(?m)^\s*\.\s*=\s*ALIGN\s*\(\s*4\s*\)\s*;\s*$", before):
            insert = f"{m.group('indent')}. = ALIGN(4);\n"
            content = content[:start] + insert + content[start:]
            changed = True

    return content, changed

def main():
    # Patch má zmysel iba pre espressif32
    if env.get("PIOPLATFORM") != "espressif32":
        return

    path = get_sections_ld_path()

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        orig = f.read()

    patched, changed = patch_text(orig)

    if not changed:
        print(f"[sections.ld patch] OK (už patchnuté): {path}")
        return

    # zapíš naspäť priamo do balíka
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(patched)

    print(f"[sections.ld patch] PATCH applied in-place: {path}")

main()

Import("env")

from pathlib import Path

# Compile the boot GIF into .rodata so flashing firmware.bin is enough —
# no SD copy and no LittleFS uploadfs step.
#
# Cardputer factory app partition is 0x5E0000 (~6.0 MiB). A multi‑MB GIF
# overflows that partition and bricks boot after upload. Refuse oversized embeds.

EMBED_ENVS = {"m5stack-cardputer", "m5stack-sticks3"}
# App code is ~4.5 MiB today; leave ~0.5 MiB slack → GIF hard cap ~1.6 MiB.
MAX_BOOTIMAGE_BYTES = 1600 * 1024

proj = Path(env.subst("$PROJECT_DIR"))
gif = proj / "tools" / "build" / "embedded_resources" / "bootimage.gif"
asm_out = proj / "src" / "root" / "ui" / "bootimage_embed.S"
pioenv = env.subst("${PIOENV}")

should_embed = pioenv in EMBED_ENVS and gif.is_file()
skip_reason = None

if should_embed:
    size = gif.stat().st_size
    if size > MAX_BOOTIMAGE_BYTES:
        should_embed = False
        skip_reason = (
            f"[bootimage] REFUSING to embed {gif.name} ({size} bytes) — "
            f"max is {MAX_BOOTIMAGE_BYTES} bytes. Oversized embeds overflow "
            f"the factory app partition and the device will not boot. "
            f"Use a small 240x135 GIF or theme boot_img on SD instead."
        )
        print(skip_reason)

if should_embed:
    # Absolute path so GNU as .incbin does not depend on assembler cwd.
    # Stamp size/mtime so a replaced GIF rebuilds even when the incbin path is unchanged.
    incbin_path = gif.resolve().as_posix()
    stamp = f"{gif.stat().st_size} {gif.stat().st_mtime_ns}"
    asm = (
        f"    # bootimage {stamp}\n"
        "    .section .rodata.bootimage,\"a\"\n"
        "    .global bootimage_gif\n"
        "    .global bootimage_gif_end\n"
        "    .balign 4\n"
        "bootimage_gif:\n"
        f"    .incbin \"{incbin_path}\"\n"
        "bootimage_gif_end:\n"
    )
    env.Append(CPPDEFINES=["HAS_EMBEDDED_BOOTIMAGE"])
    print(f"[bootimage] embedding {gif} ({gif.stat().st_size} bytes) for {pioenv}")
else:
    asm = "# no embedded boot image for this env\n"
    if skip_reason is None and pioenv in EMBED_ENVS and not gif.is_file():
        print(f"[bootimage] no {gif.name}; using text splash")

asm_out.parent.mkdir(parents=True, exist_ok=True)
if not asm_out.exists() or asm_out.read_text() != asm:
    asm_out.write_text(asm)

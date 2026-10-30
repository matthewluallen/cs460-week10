"""Check the effective PlatformIO profile after inheritance and build_unflags.

This is a POST extra script, so env/projenv contain the actual build definitions.
It checks profile selection, not whether a security control works at runtime.
The pure check is also importable by the host tests without PlatformIO installed.
"""

EXPECTED = {
    "m5stamplc": "teaching",
    "m5stamplc_secure": "secure",
    "rtu_serial_only": "teaching",
}
PROFILE_FLAGS = ("ENABLE_CWE_DEMOS", "SECURE_BUILD")
APPLICATION_FLAGS = ("ENABLE_PLC_VM", "ENABLE_PLC_LOADER")


def check_profile(environment, definitions):
    """Return the selected profile or reject contradictory/missing definitions."""
    macros = {}
    for definition in definitions:
        if isinstance(definition, (tuple, list)):
            name = str(definition[0])
            value = definition[1] if len(definition) > 1 else None
        else:
            name, separator, value = str(definition).partition("=")
            if not separator:
                value = None
        if name in PROFILE_FLAGS + APPLICATION_FLAGS:
            # These switches use #ifdef. Defining one as 0 still enables that
            # branch, so reject the misleading form rather than treating it as off.
            if value is not None and str(value) != "1":
                raise ValueError(f"{environment}: {name} must be absent or defined as 1")
            macros[name] = value

    teaching = "ENABLE_CWE_DEMOS" in macros
    secure = "SECURE_BUILD" in macros
    if teaching == secure:
        raise ValueError(
            f"{environment}: select exactly one of ENABLE_CWE_DEMOS and SECURE_BUILD"
        )
    profile = "teaching" if teaching else "secure"
    if environment in EXPECTED and profile != EXPECTED[environment]:
        raise ValueError(f"{environment}: expected {EXPECTED[environment]} profile, got {profile}")
    for name in APPLICATION_FLAGS:
        if name not in macros:
            raise ValueError(f"{environment}: both profiles require {name} for project loading")
    return profile


if "Import" in globals():
    Import("env", "projenv")
    environment = env.subst("$PIOENV")
    global_profile = check_profile(environment, env.get("CPPDEFINES", []))
    source_profile = check_profile(environment, projenv.get("CPPDEFINES", []))
    if global_profile != source_profile:
        raise ValueError(f"{environment}: project and global security profiles disagree")
    print(
        f"[profile] {environment}: {source_profile}; project loader enabled "
        "(build configuration check, not security behavior validation)"
    )

#!/usr/bin/env python3
"""firmware_sbom.py — SBOM of the firmware image the twin is RUNNING (CycloneDX 1.5 + SPDX 2.3).

Two sources, kept apart so the student can see which claim comes from where:
  * DECLARED  the build manifest sbom/firmware/platformio.ini (platform + lib_deps of the env that
              built the image)
  * FOUND     the image itself: the app descriptor (project, version, ESP-IDF version, ELF SHA-256)
              and strings that show a bundled component is present (mbedTLS, lwIP, FreeRTOS ...)

It also says whether the running image's fingerprint (app ELF SHA-256) matches a build in this
checkout — a local PlatformIO build or the committed prebuilt — because an SBOM of a manifest only
describes the running device if the device runs what that manifest built.

    python3 sbom/firmware_sbom.py --image qemu/twin/stamplc-flash-8MB.bin --out sbom/out
Standard library only. Called by sbom/scan-running.sh.
"""
import argparse, configparser, datetime, glob, hashlib, json, os, re, sys, uuid

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "qemu"))
import fw_identity as FW  # noqa: E402

# lib_deps spec -> source repository (package identifier only; used for the purl)
REPO = {"arduinojson": "bblanchon/ArduinoJson", "pubsubclient": "knolleary/pubsubclient",
        "m5stamplc": "m5stack/M5StamPLC", "m5unified": "m5stack/M5Unified", "m5gfx": "m5stack/M5GFX",
        "adafruit bme680 library": "adafruit/Adafruit_BME680",
        "adafruit unified sensor": "adafruit/Adafruit_Sensor", "adafruit busio": "adafruit/Adafruit_BusIO",
        "ethernet_generic": "khoih-prog/Ethernet_Generic"}
# components ESP-IDF bundles: (name, evidence regex in the image, CPE vendor:product or None)
BUNDLED = [("mbedTLS", rb"mbedtls_[a-z_]+", "arm:mbed_tls"),
           ("lwIP", rb"components/lwip/", "lwip_project:lwip"),
           ("FreeRTOS kernel", rb"FreeRTOS", None),
           ("wpa_supplicant", rb"wpa_supplicant|esp_supplicant", None),
           ("esp-tls", rb"esp-tls|esp_tls", None)]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def read_ini(path, env):
    cp = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(";",),
                                   comment_prefixes=(";", "#"))
    cp.read(path)

    def get(e, key, seen=()):
        sec = "env:" + e
        if not cp.has_section(sec):
            return None
        if cp.has_option(sec, key):
            return cp.get(sec, key)
        if cp.has_option(sec, "extends"):
            base = cp.get(sec, "extends").strip().replace("env:", "")
            if base not in seen:
                return get(base, key, seen + (e,))
        return None
    platform = (get(env, "platform") or "").strip()
    libs = []
    for line in (get(env, "lib_deps") or "").splitlines():
        line = line.strip()
        if not line:
            continue
        spec, _, ver = line.partition("@")
        owner, _, name = spec.rpartition("/")
        libs.append({"spec": line, "owner": owner or None, "name": name.strip(), "version": ver.strip()})
    return platform, libs


def build_match(app_elf_sha):
    """Which build in this checkout produced the running image (by app ELF SHA-256)?"""
    for elf in sorted(glob.glob(os.path.join(ROOT, "sbom", "firmware", ".pio", "build", "*", "firmware.elf"))):
        if sha256(elf) == app_elf_sha:
            env = os.path.basename(os.path.dirname(elf))
            return {"matched": True, "env": env, "how": "local PlatformIO build sbom/firmware/.pio/build/%s "
                    "(this checkout's source tree)" % env}
    pre = os.path.join(ROOT, "sbom", "firmware", "prebuilt", "firmware.bin")
    if os.path.isfile(pre):
        with open(pre, "rb") as f:
            d = FW.app_desc(f, 0)
        if d and d["elf_sha256"] == app_elf_sha:
            return {"matched": True, "env": "m5stamplc", "how": "the committed prebuilt sbom/firmware/prebuilt/"
                    "firmware.bin (built from sbom/firmware, env m5stamplc)"}
    return {"matched": False, "env": None,
            "how": "no build in this checkout has this fingerprint: the DECLARED part below is the "
                   "repo's manifest, which may not be what this image was built from"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--ini", default=os.path.join(ROOT, "sbom", "firmware", "platformio.ini"))
    ap.add_argument("--env", default=None, help="platformio env (default: from the build match / profile)")
    ap.add_argument("--self-report", default=None, help="JSON from plc> diag json (elf_sha256)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    ident = FW.identify(a.image)
    app = ident.get("app") or {}
    if not app:
        sys.exit("no ESP app descriptor in the selected slot of %s" % a.image)
    label, n = FW.extract_app(a.image, os.path.join(a.out, "app.bin"))
    with open(os.path.join(a.out, "app.bin"), "rb") as f:
        appbytes = f.read()
    match = build_match(app["elf_sha256"])
    env = a.env or match["env"] or {"secure": "m5stamplc_secure"}.get(ident["profile"], "m5stamplc")
    platform, libs = read_ini(a.ini, env)

    self_rep = None
    if a.self_report and os.path.isfile(a.self_report):
        try:
            self_rep = json.load(open(a.self_report))
        except ValueError:
            self_rep = None
    running_check = None
    if self_rep and self_rep.get("elf_sha256"):
        running_check = {"firmware_says": self_rep["elf_sha256"],
                         "image_slot_says": app["elf_sha256"],
                         "agree": self_rep["elf_sha256"] == app["elf_sha256"]}

    idf_full = app.get("idf_version", "")
    m = re.match(r"v?(\d+\.\d+(?:\.\d+)?)", idf_full)
    idf = m.group(1) if m else idf_full
    pm = re.search(r"/download/(\d+)\.(\d+)\.(\d+)/", platform)
    core = None
    if pm:
        core = "%d.%d.%d" % (int(pm.group(2)), int(pm.group(3)) // 10, int(pm.group(3)) % 10) \
            if len(pm.group(3)) == 2 else None
    core_in_image = bool(core and re.search(re.escape(core.encode()) + rb"\x00", appbytes))

    comps = []

    def add(name, version, ctype, src, evidence, purl=None, cpe=None, scope="required", props=None):
        c = {"bom-ref": "comp-%d" % (len(comps) + 1), "type": ctype, "name": name, "scope": scope,
             "properties": [{"name": "sbom:source", "value": src}, {"name": "sbom:evidence", "value": evidence}]}
        if version:
            c["version"] = version
        if purl:
            c["purl"] = purl
        if cpe:
            c["cpe"] = cpe
        for k, v in (props or {}).items():
            c["properties"].append({"name": k, "value": str(v)})
        comps.append(c)

    add("ESP-IDF", idf, "framework", "FOUND",
        "app descriptor idf_version = %s" % idf_full,
        purl="pkg:github/espressif/esp-idf@v%s" % idf, cpe="cpe:2.3:a:espressif:esp-idf:%s:*:*:*:*:*:*:*" % idf,
        props={"idf_version_full": idf_full})
    if core:
        add("Arduino core for ESP32", core, "framework", "DECLARED+FOUND" if core_in_image else "DECLARED",
            "platformio.ini platform %s%s" % (platform.rsplit("/download/", 1)[-1].split("/")[0],
                                              "; string '%s' in the image" % core if core_in_image else ""),
            purl="pkg:github/espressif/arduino-esp32@%s" % core)
    if platform:
        add("pioarduino platform-espressif32", pm and ".".join(pm.groups()) or "", "application", "DECLARED",
            "platformio.ini [env:%s] platform" % env, scope="excluded",
            props={"note": "build-time tool: compiles the image, does not run on the chip"})
    for lib in libs:
        key = lib["name"].lower()
        repo = REPO.get(key)
        add(lib["name"], lib["version"], "library", "DECLARED",
            "platformio.ini [env:%s] lib_deps: %s" % (env, lib["spec"]),
            purl=("pkg:github/%s@%s" % (repo, lib["version"])) if repo else
                 ("pkg:generic/%s@%s" % (lib["name"].replace(" ", "-"), lib["version"])))
    for name, rx, cpe in BUNDLED:
        hit = re.search(rx, appbytes)
        if not hit:
            continue
        add(name, None, "library", "FOUND",
            "string %r at app offset 0x%x; version not stated in the image (bundled with ESP-IDF v%s)"
            % (hit.group(0).decode("latin1"), hit.start(), idf),
            props={"version_note": "unknown: the image carries no version string; a scanner cannot "
                                   "match CVEs without one (absent-from-scan is not absent-from-device)",
                   "cpe_if_version_known": ("cpe:2.3:a:%s:<version>" % cpe) if cpe else "n/a"})

    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    fw_name = "stamplc-teaching-firmware" if ident["profile"] != "secure" else "stamplc-secure-firmware"
    meta_props = {"profile": ident["profile"], "image": os.path.basename(a.image), "boot_slot": label,
                  "flash_sha256": ident.get("flash_sha256"), "app_elf_sha256": app["elf_sha256"],
                  "app_project": app.get("project"), "app_version": app.get("version"),
                  "built": "%s %s" % (app.get("date"), app.get("time")),
                  "build_match": match["how"], "platformio_env": env}
    if running_check:
        meta_props["running_check"] = ("agree" if running_check["agree"] else "DISAGREE") + \
            " (plc> diag json elf_sha256 vs the image's boot slot)"
    cdx = {"bomFormat": "CycloneDX", "specVersion": "1.5", "serialNumber": "urn:uuid:" + str(uuid.uuid4()),
           "version": 1,
           "metadata": {"timestamp": now, "tools": {"components": [{"type": "application",
                                                                       "name": "sbom/firmware_sbom.py"}]},
                        "component": {"bom-ref": "firmware", "type": "firmware", "name": fw_name,
                                      "version": app.get("version") or "unknown",
                                      "hashes": [{"alg": "SHA-256", "content": hashlib.sha256(appbytes).hexdigest()}],
                                      "properties": [{"name": k, "value": str(v)} for k, v in meta_props.items()]}},
           "components": comps,
           "dependencies": [{"ref": "firmware", "dependsOn": [c["bom-ref"] for c in comps if c["scope"] != "excluded"]}]}
    with open(os.path.join(a.out, "firmware.cdx.json"), "w") as f:
        json.dump(cdx, f, indent=1)

    def spdx_id(c):
        return "SPDXRef-" + re.sub(r"[^A-Za-z0-9.-]", "-", c["bom-ref"])
    pkgs = [{"SPDXID": "SPDXRef-firmware", "name": fw_name, "versionInfo": app.get("version") or "NOASSERTION",
             "downloadLocation": "NOASSERTION", "filesAnalyzed": False, "primaryPackagePurpose": "FIRMWARE",
             "checksums": [{"algorithm": "SHA256", "checksumValue": hashlib.sha256(appbytes).hexdigest()}],
             "comment": "; ".join("%s=%s" % kv for kv in meta_props.items())}]
    rels = [{"spdxElementId": "SPDXRef-DOCUMENT", "relationshipType": "DESCRIBES", "relatedSpdxElement": "SPDXRef-firmware"}]
    for c in comps:
        refs = []
        if c.get("purl"):
            refs.append({"referenceCategory": "PACKAGE-MANAGER", "referenceType": "purl", "referenceLocator": c["purl"]})
        if c.get("cpe"):
            refs.append({"referenceCategory": "SECURITY", "referenceType": "cpe23Type", "referenceLocator": c["cpe"]})
        pkgs.append({"SPDXID": spdx_id(c), "name": c["name"], "versionInfo": c.get("version") or "NOASSERTION",
                     "downloadLocation": "NOASSERTION", "filesAnalyzed": False, "externalRefs": refs,
                     "comment": "; ".join("%s=%s" % (p["name"], p["value"]) for p in c["properties"])})
        rels.append({"spdxElementId": "SPDXRef-firmware",
                     "relationshipType": "BUILD_TOOL_OF" if c["scope"] == "excluded" else "CONTAINS",
                     "relatedSpdxElement": spdx_id(c)})
        if c["scope"] == "excluded":   # BUILD_TOOL_OF reads tool -> artifact
            rels[-1] = {"spdxElementId": spdx_id(c), "relationshipType": "BUILD_TOOL_OF",
                        "relatedSpdxElement": "SPDXRef-firmware"}
    spdx = {"spdxVersion": "SPDX-2.3", "dataLicense": "CC0-1.0", "SPDXID": "SPDXRef-DOCUMENT",
            "name": fw_name + "-running-image",
            "documentNamespace": "https://example.invalid/spdx/%s-%s" % (fw_name, uuid.uuid4()),
            "creationInfo": {"created": now, "creators": ["Tool: sbom/firmware_sbom.py"]},
            "packages": pkgs, "relationships": rels}
    with open(os.path.join(a.out, "firmware.spdx.json"), "w") as f:
        json.dump(spdx, f, indent=1)

    ident_out = {"image": os.path.basename(a.image), "profile": ident["profile"], "boot_slot": label,
                 "selected_how": ident["selected_how"], "flash_sha256": ident.get("flash_sha256"),
                 "app": app, "app_sha256": hashlib.sha256(appbytes).hexdigest(), "app_bytes": n,
                 "build_match": match, "running_check": running_check, "platformio_env": env}
    with open(os.path.join(a.out, "running-identity.json"), "w") as f:
        json.dump(ident_out, f, indent=1)

    print("image        : %s (%s profile), boot slot %s (%s)" % (os.path.basename(a.image), ident["profile"],
                                                                label, ident["selected_how"]))
    print("fingerprint  : app ELF SHA-256 %s" % app["elf_sha256"])
    if running_check:
        print("running check: plc> diag json says %s -> %s" % (running_check["firmware_says"][:16],
              "AGREES with the image" if running_check["agree"] else
              "DISAGREES: the code executing is not the boot slot (rebooted? OTA pending?)"))
    else:
        print("running check: no plc> diag json self-report (console not reachable) - image only")
    print("build match  : %s" % match["how"])
    print("ESP-IDF      : %s   (found in the image)" % idf_full)
    print("components   : %d declared/found -> firmware.cdx.json, firmware.spdx.json" % len(comps))
    for c in comps:
        print("   %-34s %-12s %s" % (c["name"], c.get("version") or "(no version)",
                                      next(p["value"] for p in c["properties"] if p["name"] == "sbom:source")))
    return 0


if __name__ == "__main__":
    sys.exit(main())

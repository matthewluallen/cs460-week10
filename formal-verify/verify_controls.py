#!/usr/bin/env python3
"""Formally verify the StampLC security controls with the Z3 SMT solver (Level 3).

For each control we write a small *model*: the attacker's degrees of freedom as SMT
variables, the control's rule as a constraint, and the **violation** we never want to be
reachable. Z3 then answers directly:

  * UNSAT  → the violation is unreachable for ALL inputs  → the control is PROVEN.
  * SAT    → Z3 returns a concrete counterexample (the exact date / length / mode that
             breaks it)                                    → the control is VIOLATED.

Run it across the three build levels to see security *earned, then proven*:

  level 1  vuln     the teaching build — every control VIOLATED (Z3 hands you the exploit input)
  level 2  secure   the hardened build — the per-weakness controls PROVEN; supply-chain
                    attestation still VIOLATED (HMAC alone does not establish origin)
  level 3  verified the assured build — every control PROVEN, including build provenance

    python formal-verify/verify_controls.py                 # matrix across all three levels
    python formal-verify/verify_controls.py --profile verified --assert-proven   # CI gate
    python formal-verify/verify_controls.py --control y2038 --profile vuln        # one cell

IMPORTANT (a cyber-informed-engineering point, not a disclaimer): this proves properties of a
*model* of each control. The proof is only as good as the model's fidelity to the firmware. The
model here is kept deliberately close to the C in sbom/firmware/src/ (same bound, same gate,
same truncation), and Week 12 asks you to argue where model and implementation could diverge —
that gap is exactly where a compensating control belongs. See pipeline-assurance.md.
"""
import argparse, sys
import z3

PROFILES = ["vuln", "secure", "verified"]
PASS, FAIL = "PROVEN", "VIOLATED"

def _verdict(s, cex_vars, fmt=None):
    r = s.check()
    if r == z3.unsat:
        return PASS, "violation unreachable for all inputs"
    if r == z3.sat:
        m = s.model()
        if fmt:
            return FAIL, fmt(m)
        return FAIL, "counterexample: " + ", ".join(f"{v}={m[v]}" for v in cex_vars)
    return "UNKNOWN", str(r)

# ---- control models -------------------------------------------------------
# Each takes the profile and returns (verdict, detail). The control constraint changes with
# the profile; the violation predicate is the same across profiles (that is the point).

def ota_signature(profile):
    """CWE-347/494 · ATT&CK T0857 (System Firmware) · D3FEND Message Authentication.
    Secure/verified: accept an OTA image iff its HMAC verifies. Violation: an unsigned image accepted."""
    accepted, hmac_ok = z3.Bools("accepted hmac_ok")
    s = z3.Solver()
    if profile == "vuln":
        s.add(accepted == True)                      # OTA accepts anything
    else:
        s.add(accepted == hmac_ok)                   # accept iff the MAC verifies
    s.add(accepted, z3.Not(hmac_ok))                 # violation: an unsigned/forged image was accepted
    return _verdict(s, [accepted, hmac_ok])

def y2038_time(profile):
    """CWE-197/190 · ATT&CK T0830 (AiTM) + T0837 · D3FEND clock integrity.
    Store a post-2038 epoch and read it back. Violation: the readback != the real time (wrap)."""
    e = z3.BitVec("epoch_s", 64)
    s = z3.Solver()
    lo = 2**31                                       # 2038-01-19, the signed-32-bit boundary
    s.add(z3.UGE(e, lo), z3.ULE(e, lo + 20 * 365 * 24 * 3600))   # a real post-2038 window
    stored = e if profile != "vuln" else z3.SignExt(32, z3.Extract(31, 0, e))  # 64-bit vs int32 truncation
    s.add(stored != e)                               # violation: stored wall time differs from the real epoch
    def fmt(m):
        import datetime
        secs = m[e].as_long()
        y = datetime.datetime(1970, 1, 1) + datetime.timedelta(seconds=secs)
        return f"counterexample: epoch_s={secs} ({y:%Y-%m-%d}) truncates/wraps on read"
    return _verdict(s, [e], fmt)

def force_overflow(profile):
    """CWE-787/120 · ATT&CK T0835 (Manipulate I/O Image) · D3FEND bounds checking.
    Copy a tag label into a 32-byte buffer adjacent to the I/O force image. Violation: the
    effective write exceeds the buffer (and so lands in the force words)."""
    L = z3.BitVec("copy_len", 32)
    BUF = z3.BitVecVal(32, 32)
    s = z3.Solver()
    s.add(z3.ULE(L, z3.BitVecVal(4096, 32)))         # attacker-chosen label length
    eff = L if profile == "vuln" else z3.If(z3.UGT(L, BUF), BUF, L)   # unbounded vs clamped copy
    s.add(z3.UGT(eff, BUF))                           # violation: write runs past the 32-byte buffer
    return _verdict(s, [L])

def modbus_authz(profile):
    """CWE-306 · ATT&CK T0855 (Unauthorized Command) / T0831 (Manipulation of Control).
    Secure: a coil/register write executes only in REMOTE mode (2). Violation: a write executes outside REMOTE."""
    mode = z3.Int("mode")                            # 0 RUN, 1 PROGRAM, 2 REMOTE
    wrote = z3.Bool("write_executed")
    s = z3.Solver()
    s.add(mode >= 0, mode <= 2)
    if profile == "vuln":
        s.add(wrote == True)                         # writes always land
    else:
        s.add(z3.Implies(wrote, mode == 2))          # gated to REMOTE
    s.add(wrote, mode != 2)                          # violation: a write executed outside REMOTE
    return _verdict(s, [mode])

def login_lockout(profile):
    """CWE-307 · ATT&CK T0859 (Valid Accounts) · D3FEND credential-hardening.
    Secure: after 5 failures inside the window, even a valid credential is refused. Violation:
    a login is accepted during the lockout."""
    fails = z3.Int("fails")
    window = z3.Bool("within_window")
    accepted = z3.Bool("accepted")
    valid = z3.Bool("cred_valid")
    s = z3.Solver()
    s.add(fails >= 0)
    if profile == "vuln":
        s.add(accepted == valid)                     # no lockout at all
    else:
        s.add(accepted == z3.And(valid, z3.Not(z3.And(fails >= 5, window))))
    s.add(accepted, valid, fails >= 5, window)       # violation: accepted while locked out
    return _verdict(s, [fails])

def supply_chain_attestation(profile):
    """CWE-494/506 · ATT&CK T0862 (Supply Chain Compromise) · SLSA / in-toto provenance.
    This is the Level-3 differentiator. Level 2 (secure) verifies the artifact HMAC, which proves
    integrity against tampering-in-flight but NOT origin. Level 3 additionally requires verified
    build provenance. Violation: an artifact is deployed without verified provenance."""
    deployed = z3.Bool("deployed")
    hash_ok = z3.Bool("hmac_matches")
    prov_ok = z3.Bool("provenance_verified")
    s = z3.Solver()
    if profile == "vuln":
        s.add(deployed == True)                                  # deploy anything
    elif profile == "secure":
        s.add(z3.Implies(deployed, hash_ok))                     # HMAC only (key is a repo constant)
    else:  # verified
        s.add(z3.Implies(deployed, z3.And(hash_ok, prov_ok)))    # integrity AND attested origin
    s.add(deployed, z3.Not(prov_ok))                             # violation: deployed w/o verified provenance
    return _verdict(s, [deployed, hash_ok, prov_ok])

CONTROLS = [
    ("ota_signature", "CWE-347/494", "T0857", ota_signature),
    ("y2038",         "CWE-197/190", "T0830", y2038_time),
    ("force_overflow","CWE-787/120", "T0835", force_overflow),
    ("modbus_authz",  "CWE-306",     "T0855", modbus_authz),
    ("login_lockout", "CWE-307",     "T0859", login_lockout),
    ("attestation",   "CWE-494/506", "T0862", supply_chain_attestation),
]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", choices=PROFILES + ["all"], default="all")
    ap.add_argument("--control", help="run one control by name")
    ap.add_argument("--assert-proven", action="store_true",
                    help="exit nonzero if any run of the chosen profile is not PROVEN (CI gate)")
    args = ap.parse_args()
    profiles = PROFILES if args.profile == "all" else [args.profile]
    controls = [c for c in CONTROLS if (not args.control or c[0] == args.control)]
    if not controls:
        sys.exit(f"unknown control: {args.control} — choose from {[c[0] for c in CONTROLS]}")

    print("== StampLC security controls · Z3 formal verification ==")
    print("   PROVEN = the violation is unreachable for all inputs; VIOLATED = Z3 found an exploit input.\n")
    any_unexpected = False
    for prof in profiles:
        print(f"-- level: {prof} --")
        for name, cwe, mitre, fn in controls:
            verdict, detail = fn(prof)
            mark = "✓" if verdict == PASS else "✗"
            print(f"  {mark} {name:16} [{cwe} · {mitre}]  {verdict}")
            if verdict == FAIL:
                print(f"        {detail}")
            # a VIOLATED result is "unexpected" only when we asked to assert the profile proven
            if args.assert_proven and verdict != PASS:
                any_unexpected = True
        print()
    if args.assert_proven:
        if any_unexpected:
            print("ASSERTION FAILED: not every control is PROVEN for the chosen profile.")
            return 1
        print("All controls PROVEN for the chosen profile.")
    return 0

if __name__ == "__main__":
    sys.exit(main())

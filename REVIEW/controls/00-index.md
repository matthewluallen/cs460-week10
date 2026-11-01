# Controls index (Stage 4)

The six barriers the Stage-3 bowties named, each a control card: the primary mitigation, the compensating
(defense-in-depth) controls, single-points-of-failure, the residual-risk statement, and the mapping to
D3FEND / IEC 62443 (FR + SL) / NIST 800-53 (800-82 overlay) / CIE.

| Card | Barrier | Guards (chains) | Lab's primary control | Standing weakness |
|---|---|---|---|---|
| CC1 | Input integrity gate | trust-sensor, signal-journey link 2 | E3 plausibility gate (sensor only) | not applied to the physical input path |
| CC2 | Read-back integrity gate | signal-journey link 3 (altered program) | Part 2 RUN-enable on matching read-back | trusts the controller's own self-report |
| CC3 | Signed firmware + audited write path | trust-firmware, trust-memory-overflow | HMAC + bounds-check + audited-only writes | HMAC key is a repo constant; no secure boot |
| CC4 | Network auth + segmentation | trust-network-write | secure REMOTE-mode write gate | not client auth; flat LAN; no zones/conduits |
| CC5 | Authenticated time + plausibility | trust-time, trust-sensor | monotonic audit clock + 64-bit store; E3 gate | no authenticated time; detect/limit only |
| CC6 | Un-rewritable protection layer | signal-journey link 6 (the terminal barrier) | **none** — the E-stop is only an *input* | the lab's honest core lesson (TRITON/CIE) |

**Theme (the compensating-controls finding):** the lab implements a primary control for most risks but
**names its compensating controls in prose rather than demonstrating them**, and the terminal barrier
(CC6, an un-rewritable protection) is deliberately *absent* — which is the course's central CIE point and
should be stated as such, not left as a gap.

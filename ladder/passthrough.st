(* Simplest PLC program: one input drives one output.
   Input  : DI 0     (your SnapCircuits switch, or the remote `force di0` trigger)
   Output : COIL 0   (Modbus coil 0 — this is what energizes physical relay0,
                      which switches your SnapCircuits LED / lamp)

   Output syntax: both sample programs write the output as COIL[n] — the Modbus
   coil image. "relay0" is the physical thing COIL[0] drives; they are not two
   different commands. (The write to that coil is unauthenticated — see
   latch_with_estop.st and Part 5.)

   This is the "prebuilt block." You change ONE rung — see ladder/README.md. *)

PROGRAM Passthrough
  VAR
    push_button : BOOL;   (* DI 0  *)
    lamp        : BOOL;   (* Relay 0 *)
  END_VAR

  push_button := DI[0];

  (* ---- RUNG 1: direct pass-through (ENABLED) ---- *)
  lamp := push_button;

  (* ---- RUNG 2: invert (DISABLED) ----
     Un-comment the next line (and comment out RUNG 1) to make the
     lamp turn ON when the button is RELEASED. *)
  (* lamp := NOT push_button; *)

  COIL[0] := lamp;   (* Modbus coil 0 → energizes relay0 *)
END_PROGRAM

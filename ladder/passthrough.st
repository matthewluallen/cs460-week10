(* Simplest PLC program: one input drives one output.
   Input  : DI 0    (your SnapCircuits switch, or the remote `force di0` trigger)
   Output : Relay 0 (your SnapCircuits LED / lamp)

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

  Relay[0] := lamp;
END_PROGRAM

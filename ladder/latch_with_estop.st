(* Demo Ladder Logic for M5 StampLC OpenPLC
   Demonstrates simple latching relay with E-stop safety

   Inputs:
   - DI 0: Set input (latch relay ON)
   - DI 1: Reset input (unlatch relay OFF)
   - DI 2: E-Stop button (force relay OFF immediately)

   Outputs:
   - Relay 0: Main output (pump/motor control)
   - Relay 1: E-stop status lamp
*)

PROGRAM DemoLatchLogic
  VAR
    set_input : BOOL;         (* DI 0 *)
    reset_input : BOOL;       (* DI 1 *)
    estop_button : BOOL;      (* DI 2 *)
    relay_output : BOOL;      (* Relay 0 *)
    estop_lamp : BOOL;        (* Relay 1 *)
    relay_state : BOOL;       (* Internal state *)
  END_VAR

  (* Read inputs from Modbus *)
  set_input := DI[0];
  reset_input := DI[1];
  estop_button := DI[2];

  (* CWE-306 Teaching Point: E-Stop has NO authentication check *)
  (* In a real industrial system, E-stop should always work *)
  (* But here we show how unauthenticated writes can be dangerous *)

  IF estop_button THEN
    (* E-stop pressed: immediately OFF (no auth, no delay) *)
    relay_state := FALSE;
    estop_lamp := TRUE;   (* Light the E-stop indicator *)
  ELSE
    estop_lamp := FALSE;

    (* Normal latch/unlatch logic (SR flip-flop) *)
    IF set_input AND NOT relay_state THEN
      relay_state := TRUE;
    END_IF;

    IF reset_input AND relay_state THEN
      relay_state := FALSE;
    END_IF;
  END_IF;

  (* Write outputs to Modbus relays *)
  relay_output := relay_state;

  (* Map to Modbus coils *)
  COIL[0] := relay_state;
  COIL[1] := estop_lamp;

END_PROGRAM

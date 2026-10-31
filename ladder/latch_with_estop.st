(* Motor start/stop with a fail-safe E-stop -- the readable twin of the factory program
   web/public/projects/motor-start-stop-estop.csco (open it in the Logic Studio to see the
   same logic as ladder rungs; the Studio's own ST export of it is equivalent).

   Inputs (board prints IN1-IN8; firmware di0-di7):
   - IN1 (di0, %IX0.0) start_pb : Start push button, N.O. momentary, 1 = pressed
   - IN2 (di1, %IX0.1) stop_pb  : Stop push button,  N.O. momentary, 1 = pressed
                                   (the snap-kit parts have no N.C. push button, so a cut
                                   Stop wire can NOT stop the motor -- the E-stop is the
                                   input that fails safe)
   - IN3 (di2, %IX0.2) estop_ok : E-stop slide switch wired N.C.
                                   1 = healthy, 0 = pressed, wire cut or loop supply lost
   - IN4 (di3, %IX0.3) reset_pb : Fault reset push button, N.O. momentary

   Outputs (board prints RELAY 1-4; firmware relay0-relay3):
   - RELAY 1 (relay0, %QX0.0) motor      : seals in on a NEW Start press
   - RELAY 2 (relay1, %QX0.1) run_light  : follows the motor
   - RELAY 4 (relay3, %QX0.3) fault_lamp : the latched E-stop fault

   Rules: E-stop trip drops the motor and latches the fault. Restoring the E-stop does
   NOT restart the motor and does NOT clear the fault. Reset clears the fault only while
   estop_ok = 1, and the motor stays off until Start is pressed again. A held or stuck
   Start/Reset button does nothing (both act on a press, not a level).
*)

PROGRAM MotorStartStopEstop
  VAR
    start_pb       AT %IX0.0 : BOOL;
    stop_pb        AT %IX0.1 : BOOL;
    estop_ok       AT %IX0.2 : BOOL;
    reset_pb       AT %IX0.3 : BOOL;
    motor          AT %QX0.0 : BOOL;
    run_light      AT %QX0.1 : BOOL;
    fault_lamp     AT %QX0.3 : BOOL;   (* the fault latch itself *)
    reset_trig     : R_TRIG;
    reset_edge     : BOOL;             (* one scan on a new Reset press *)
    start_released : BOOL;             (* Start was released last scan *)
  END_VAR

  reset_trig(CLK := reset_pb);
  reset_edge := reset_trig.Q;

  (* Fault latch: a tripped (or cut) E-stop sets it; only a Reset press while the E-stop is
     healthy clears it. *)
  fault_lamp := NOT estop_ok OR (fault_lamp AND NOT reset_edge);

  (* Motor: a NEW Start press seals in; Stop, the E-stop or the fault latch drops it. *)
  motor := ((start_pb AND start_released) OR motor)
           AND NOT stop_pb AND estop_ok AND NOT fault_lamp;

  start_released := NOT start_pb;
  run_light := motor;

  (* CWE-306 teaching point: this E-stop is only as trustworthy as the input image. A
     console `force estop_ok 1`, or any unauthenticated write that reaches the I/O image,
     makes the PLC believe a tripped E-stop is healthy. Real E-stops also open the motor
     supply directly (a second, hard-wired channel) and safety PLCs refuse forces on
     safety I/O. *)
END_PROGRAM

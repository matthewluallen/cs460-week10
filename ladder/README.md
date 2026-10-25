# Part 4 — Ladder logic (the prebuilt block)

A PLC runs a tiny program on a loop: read inputs → evaluate logic → write outputs. You
won't write one from scratch. You start from a **complete, working program** and make
**one** change, so the focus stays on *what the logic does to the physical world*.

Two programs are provided:

- [`passthrough.st`](passthrough.st) — the simplest: **DI 0 → Relay 0**. One input, one
  output.
- [`latch_with_estop.st`](latch_with_estop.st) — a step up: a latching relay (set/reset)
  with an **E-stop** that forces the output off. Note the comment in it: the E-stop has
  **no authentication** — anyone who can write that coil can trip it (we come back to this
  as a security point).

## Your task — pick ONE

**(a) Toggle a rung.** In `passthrough.st`, switch from direct pass-through to *inverted*
by enabling RUNG 2 and disabling RUNG 1 (it's one line each, marked in the file). Predict
what the lamp will do before you run it.

**or (b) Add one simple function.** Give `passthrough.st` a second input so the output
needs **both**: an AND gate.

```
VAR  enable_sw : BOOL;  END_VAR     (* DI 1 *)
enable_sw := DI[1];
lamp := push_button AND enable_sw;  (* output only when BOTH are on *)
```

## How it gets onto the device

You don't flash over USB. In **Part 5** you open the class **front end**, pick your
program, and **Apply** — it loads onto your team's StampLC over the network. Then you
toggle the input and watch the output. The editor shows the same program as ladder and
as ST, so you can see the rung you changed either way.

## See it as a ladder — Logic Studio

The repo ships a browser ladder / Structured-Text editor, **TBD Logic Studio**, at
`web/public/logic_studio.html`. Open it to author — or just *see* — your change as a
ladder diagram and as ST side by side, and compile it to the device IR. Serve the folder
and open it on the forwarded port:

```bash
python3 -m http.server -d web/public 8000   # then open logic_studio.html on port 8000
```

Logic Studio also carries a **Bus monitor** whose lane-timeline is the same view you used
in Part 1's `i2c-analyzer.html`, so you can watch a rung drive the `0x59` relay write on
the bus — the ladder editor and the bus analyzer are two halves of one picture. Authoring
here is optional and offline; your change reaches your team's PLC in **Part 5** through the
class front end, not over USB.

Keep your finding small: which change you made, what you **predicted**, what the device
**did**, and whether they matched.

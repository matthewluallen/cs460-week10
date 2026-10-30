/* CS460 Logic/Protocol Studio — shared contextual-help content (single source of truth).
   Loaded by logic_studio.html and protocol_studio.html before the TBDHelp component,
   which reads window.TBD_TOPICS. Each topic is CIE-structured:
   {id,title,plain,engineering,cyber,check,sources,aliases?}. Offline; no network or learner identity.
   Edit topics HERE only — do not re-inline per page. */
window.TBD_TOPICS = [
 {
  "id": "substation",
  "title": "Substation and single-line diagram",
  "plain": "A substation connects parts of an electrical network and often changes voltage. A single-line drawing summarizes a three-phase power system with one line per circuit.",
  "engineering": "Follow source → transformer → bus → breaker → feeder. A nominal voltage is a design value; a measured value can differ. This example is a synthetic balanced AC model, not a surveyed utility.",
  "cyber": "Power connections and communication connections are different paths. Interrupting telemetry does not necessarily interrupt power or local protection.",
  "check": "Locate the solid electrical path and dashed communication path. Identify which states are modeled and which were reported by the RTU.",
  "sources": [
   {
    "title": "EIA · delivery from transmission through distribution",
    "url": "https://www.eia.gov/energyexplained/electricity/delivery-to-consumers.php"
   },
   {
    "title": "DOE · United States Electricity Industry Primer",
    "url": "https://www.energy.gov/sites/prod/files/2015/12/f28/united-states-electricity-industry-primer.pdf"
   }
  ]
 },
 {
  "id": "transformer",
  "title": "Transformer · T1",
  "plain": "A transformer transfers AC electrical energy between circuits and changes the voltage level.",
  "engineering": "115 / 12.47 kV describes nominal primary and secondary voltage. 10 MVA is an apparent-power rating, not a claim that the load uses 10 MW. This model includes impedance, but not switching transients or winding temperatures.",
  "cyber": "A displayed voltage comes through sensors, controller registers and a network. Changing the display does not prove the physical voltage changed.",
  "check": "Trace bus voltage from the model's transducer input to the controller register, protocol response and display.",
  "sources": [
   {
    "title": "EIA · delivery from transmission through distribution",
    "url": "https://www.eia.gov/energyexplained/electricity/delivery-to-consumers.php"
   },
   {
    "title": "DOE · United States Electricity Industry Primer",
    "url": "https://www.energy.gov/sites/prod/files/2015/12/f28/united-states-electricity-industry-primer.pdf"
   }
  ]
 },
 {
  "id": "bus",
  "title": "MV bus · medium-voltage connection",
  "plain": "MV means medium voltage. A bus (or busbar) is a common electrical connection serving several circuits. Here it is a 12.47 kV distribution bus; 1 kV is 1,000 volts. Voltage-class conventions vary, so the actual nominal voltage is shown.",
  "engineering": "This bus is nominally 12.47 kV. Opening the feeder breaker can leave the upstream bus energized. Zero feeder current does not prove the bus is de-energized.",
  "cyber": "Check units and scaling before comparing tools. FUXA shows holding register 10 divided by 500 in kV; DNP3 AI2 × 0.002 gives kV in this lesson.",
  "check": "Compare bus voltage and feeder current with the breaker open. Explain why both readings can be correct.",
  "sources": [
   {
    "title": "DOE · United States Electricity Industry Primer",
    "url": "https://www.energy.gov/sites/prod/files/2015/12/f28/united-states-electricity-industry-primer.pdf"
   },
   {
    "title": "EIA · delivery from transmission through distribution",
    "url": "https://www.eia.gov/energyexplained/electricity/delivery-to-consumers.php"
   }
  ]
 },
 {
  "id": "breaker",
  "title": "Circuit breaker · 52",
  "plain": "A circuit breaker opens or closes an electrical path. Device number 52 identifies an AC circuit breaker.",
  "engineering": "A close request is intent. Mechanical position is separate feedback. This model assumes 100 ms closing and 60 ms opening; these are teaching parameters, not verified vendor timings.",
  "cyber": "An accepted network command does not prove that equipment moved. Verify the resulting contact feedback and process consequence.",
  "check": "Compare panel request, RTU close output, model position and reported 52a/52b. Use only the isolated virtual panel for this lesson.",
  "sources": [
   {
    "title": "SEL · Improving Breaker Failure Protection, auxiliary contact behavior",
    "url": "https://cdn.selinc.com/assets/Literature/Publications/Technical%20Papers/6491_ImprovingBreaker_MT_20110912_Web2.pdf"
   },
   {
    "title": "DOE · United States Electricity Industry Primer",
    "url": "https://www.energy.gov/sites/prod/files/2015/12/f28/united-states-electricity-industry-primer.pdf"
   }
  ]
 },
 {
  "id": "contacts",
  "title": "52a and 52b · breaker position contacts",
  "plain": "Auxiliary contacts report breaker position. Here52a is asserted when closed and 52b when open.",
  "engineering": "These are two distinct indications. During motion or a fault, an inconsistent pair needs investigation. Do not silently replace missing contacts with a guessed position.",
  "cyber": "Feedback can be stale, delayed or inconsistent with a command. A fresh packet also does not prove the sensor itself is truthful.",
  "check": "Trace DI2 and DI3 → coil mirrors102/103 → FUXA addresses 103/104 → DNP3 BI2/BI3. Observe the pair before and after a virtual close.",
  "sources": [
   {
    "title": "SEL · Improving Breaker Failure Protection, auxiliary contact behavior",
    "url": "https://cdn.selinc.com/assets/Literature/Publications/Technical%20Papers/6491_ImprovingBreaker_MT_20110912_Web2.pdf"
   }
  ]
 },
 {
  "id": "feeder",
  "title": "Feeder, load and current · A / MW",
  "plain": "A feeder carries power to downstream loads. Current measures electrical flow in amperes; power measures the rate of energy transfer.",
  "engineering": "1 MW is 1,000 kW. The normal synthetic load is 900 kW at 0.95 power factor. Higher demand raises current; an open breaker stops feeder current in this model.",
  "cyber": "A load request, current reading and breaker state are different evidence. Compare them instead of treating any one value as the whole process.",
  "check": "Raise virtual demand from 0.9 to 1.8 MW. Observe current and protection, then distinguish the trip cause from its reported time.",
  "sources": [
   {
    "title": "EIA · delivery from transmission through distribution",
    "url": "https://www.eia.gov/energyexplained/electricity/delivery-to-consumers.php"
   },
   {
    "title": "EIA · measuring power and energy",
    "url": "https://www.eia.gov/energyexplained/electricity/measuring-electricity.php"
   }
  ]
 },
 {
  "id": "transducer",
  "title": "CT, VT and transducer scaling",
  "plain": "A current transformer (CT) and voltage transformer (VT) provide lower-level signals representing primary electrical measurements. A transducer converts a measurement into a signal a controller can read.",
  "engineering": "This lesson maps primary bus voltage to CH1 millivolts and current to CH2 millivolts. These are modeled signals; no real high-voltage instrument is attached. The tour’s CT/VT forms and blue secondary routes are illustrative; CT saturation, wiring faults and meter accuracy are not independently simulated.",
  "cyber": "Raw register values need a device/lesson map, units and scaling. Correct packet decoding alone cannot establish physical meaning.",
  "check": "Holding 10 ÷500 = kV. Holding 12 ÷50 = A. Check the point map instead of applying these scales to another device.",
  "sources": [
   {
    "title": "DOE Electrical Science · instrument transformers (1992 fundamentals)",
    "url": "https://www.energy.gov/sites/default/files/2026-04/DOE-HDBK-1011-92_VOL4.pdf"
   },
   {
    "title": "DOE · United States Electricity Industry Primer",
    "url": "https://www.energy.gov/sites/prod/files/2015/12/f28/united-states-electricity-industry-primer.pdf"
   },
   {
    "title": "EIA · electrical quantities and terminology",
    "url": "https://www.eia.gov/tools/glossary/?id=electricity"
   }
  ]
 },
 {
  "id": "protection",
  "title": "Protection IED · 51 and trip latch",
  "plain": "An intelligent electronic device (IED) can protect equipment. Function 51 is time-delayed overcurrent protection. A trip latch remembers that protection operated.",
  "engineering": "The separate model trips after current exceeds 65 A for 500 ms. It is a definite-time teaching model, not a commercial relay or full protection study. Reset requires the request and breaker to be open. The tour’s separate relay panel and orange trip/DC routes explain equipment roles; no vendor relay firmware is running there.",
  "cyber": "Local protection can operate while supervisory polling is paused. Report delay is not protection delay. The PLC in this lesson does not implement the protection algorithm.",
  "check": "Pause lesson DNP3 polls, apply a virtual overload, then resume. Compare model trip time with when the master learns about it.",
  "sources": [
   {
    "title": "SEL · Improving Breaker Failure Protection, auxiliary contact behavior",
    "url": "https://cdn.selinc.com/assets/Literature/Publications/Technical%20Papers/6491_ImprovingBreaker_MT_20110912_Web2.pdf"
   },
   {
    "title": "NIST SP 800-82 Rev. 3 · operational technology architecture and security",
    "url": "https://csrc.nist.gov/pubs/sp/800/82/r3/final"
   },
   {
    "title": "SEL · local protection, breaker monitoring and SCADA communications",
    "url": "https://selinc.com/products/351/"
   }
  ]
 },
 {
  "id": "rtu",
  "title": "RTU · remote terminal unit; PLC · programmable logic controller",
  "plain": "A remote terminal unit (RTU) gathers field readings for a control center. A programmable logic controller (PLC) runs control logic. QEMU emulates a computer/controller platform.",
  "engineering": "The actual M5StamPLC firmware serves as this educational RTU in QEMU. The protection IED and electrical plant are separate models. This is not Siemens or Rockwell device firmware.",
  "cyber": "Firmware, protocol settings, network namespace and point map identify the target. A familiar device label is insufficient evidence of implementation fidelity.",
  "check": "Inspect the admitted firmware/program identity and register map. Separate controller execution from the modeled environment feeding it.",
  "sources": [
   {
    "title": "NIST SP 800-82 Rev. 3 · operational technology architecture and security",
    "url": "https://csrc.nist.gov/pubs/sp/800/82/r3/final"
   },
   {
    "title": "Siemens · S7-1200 Easy Book, PLC and control concepts",
    "url": "https://cache.industry.siemens.com/dl/files/145/39710145/att_5787/v1/s71200_easy_book_en-US_en-US.pdf"
   }
  ]
 },
 {
  "id": "permissive",
  "title": "Interlock, permissive and close intent",
  "plain": "A permissive is a condition that must be satisfied before an action is allowed. An interlock prevents an action when a required condition is absent.",
  "engineering": "The reused Logic Studio program needs a panel request and protection permissive before QX0 requests closing. QX0 is the first output, represented by raw coil 1 here.",
  "cyber": "Distinguish an operator request, program decision, forced output and physical feedback. Permission to reach a port is not permission to operate equipment.",
  "check": "Compare DI0 request, DI1 permissive and QX0 output. After a trip, release the request and reset protection before retrying.",
  "sources": [
   {
    "title": "Siemens · S7-1200 Easy Book, PLC and control concepts",
    "url": "https://cache.industry.siemens.com/dl/files/145/39710145/att_5787/v1/s71200_easy_book_en-US_en-US.pdf"
   },
   {
    "title": "NIST SP 800-82 Rev. 3 · operational technology architecture and security",
    "url": "https://csrc.nist.gov/pubs/sp/800/82/r3/final"
   }
  ]
 },
 {
  "id": "dnp3",
  "title": "DNP3 · Distributed Network Protocol",
  "plain": "DNP3 means Distributed Network Protocol, version 3. It carries measurements, status and control messages. A master asks for information; an outstation reports field data. It is used in electric utilities and also in water, transportation, and oil/gas systems.",
  "engineering": "Binary inputs represent states; analog inputs represent numbers. Point indexes need a device map. The live lesson uses a bounded static Class 0 READ subset.",
  "cyber": "Inspect source/destination, function, point index and reply. CRC detects transmission errors; it does not authenticate an operator. The recorded SELECT/OPERATE lesson has capabilities beyond the current live subset.",
  "check": "Select a request and its response in Protocol Studio. Relate BI2/BI3 to position and AI2/AI3 to the lesson's transducer map.",
  "sources": [
   {
    "title": "DNP Users Group · protocol primer",
    "url": "https://www.dnp.org/Portals/0/AboutUs/DNP3%20Primer%20Rev%20A.pdf"
   },
   {
    "title": "DNP Users Group · incoming data, object headers and qualifier layout",
    "url": "https://www.dnp.org/Portals/0/Public%20Documents/DNP3%20AN2013-004b%20Validation%20of%20Incoming%20DNP3%20Data.pdf"
   }
  ]
 },
 {
  "id": "modbus",
  "title": "Modbus registers and coils",
  "plain": "Modbus exposes numbered data tables. A coil is a single bit; a holding register is a 16-bit value. Those numbers do not contain their own engineering meaning.",
  "engineering": "FUXA uses one-based configured addresses; wire offsets here begin at zero. The same number in different tables refers to different data.",
  "cyber": "Read and write function codes differ. Check table, offset, count, unit identifier, endpoint and namespace before drawing conclusions about a transaction.",
  "check": "Find FUXA address 26 in the project, then locate function 3 / raw holding 25 in Wireshark. It is the heartbeat counter.",
  "sources": [
   {
    "title": "Modbus Organization · application protocol V1.1b3 and implementation guides",
    "url": "https://www.modbus.org/modbus-specifications"
   }
  ]
 },
 {
  "id": "hmi",
  "title": "HMI · human-machine interface; FUXA",
  "plain": "A human-machine interface (HMI) helps an operator see a process and sometimes send requests. FUXA is the actual HMI application used here.",
  "engineering": "A widget is connected to a tag with a data type, address and scale. The diagram is an explanation; current process readings require successful acquisition.",
  "cyber": "The seeded display has no write buttons, but native editor/API access can change the project and issue controls. That access stays inside the private training boundary.",
  "check": "Open Point map, choose a widget, and locate its tag in the native editor. Compare with packet evidence. The supervised page hides readings when acquisition cannot be verified.",
  "sources": [
   {
    "title": "FUXA · upstream documentation and editor guides",
    "url": "https://github.com/frangoteam/FUXA/wiki"
   },
   {
    "title": "NIST SP 800-82 Rev. 3 · operational technology architecture and security",
    "url": "https://csrc.nist.gov/pubs/sp/800/82/r3/final"
   }
  ]
 },
 {
  "id": "quality",
  "title": "Fresh, stale, unknown and recorded data",
  "plain": "Fresh means a recent observation passed the stated checks. Stale means it is too old. Unknown means the needed evidence is missing. Recorded data belongs to an earlier run.",
  "engineering": "None of these labels proves calibration or physical truth. A static correct measurement can stay unchanged, so a separate advancing heartbeat helps detect a stalled controller.",
  "cyber": "Cached pages, old values and HTTP200 responses can survive a data-path failure. Check acquisition timestamps, source identity and liveness independently.",
  "check": "Read the evidence-source label first. The FUXA supervisor requires all nine points within 5 seconds and an advancing heartbeat; acquisition loss covers the native display.",
  "sources": [
   {
    "title": "DNP Users Group · protocol primer",
    "url": "https://www.dnp.org/Portals/0/AboutUs/DNP3%20Primer%20Rev%20A.pdf"
   },
   {
    "title": "NIST SP 800-82 Rev. 3 · operational technology architecture and security",
    "url": "https://csrc.nist.gov/pubs/sp/800/82/r3/final"
   }
  ]
 },
 {
  "id": "timing",
  "title": "Scan, poll, latency and model clocks",
  "plain": "A scan is one controller execution cycle. A poll is one request for data. Latency is delay between events. They run on different clocks.",
  "engineering": "A 50 ms configured scan does not mean a 1-second HMI poll sees every transition. Clock authority depends on the selected runtime: the substation model follows controller boot-clock deltas; the cooling model follows host monotonic time. Recorded sensor playback has its own cursor and preserves original source timing separately. Pausing playback does not pause a controller or physical equipment.",
  "cyber": "Packet timestamps can measure network observations, not automatically the exact instant of a mechanical event. Separate request RTT, observation age and protection delay.",
  "check": "Compare configured scan, poll period, RTT and model event timestamps. State which clock supports each conclusion.",
  "sources": [
   {
    "title": "Siemens · S7-1200 Easy Book, PLC and control concepts",
    "url": "https://cache.industry.siemens.com/dl/files/145/39710145/att_5787/v1/s71200_easy_book_en-US_en-US.pdf"
   },
   {
    "title": "DNP Users Group · protocol primer",
    "url": "https://www.dnp.org/Portals/0/AboutUs/DNP3%20Primer%20Rev%20A.pdf"
   },
   {
    "title": "QEMU · system emulation overview",
    "url": "https://www.qemu.org/docs/master/system/introduction.html"
   }
  ]
 },
 {
  "id": "core",
  "title": "CORE, zones and network namespaces",
  "plain": "CORE builds the modeled network. A Linux network namespace gives a node its own interfaces, routes and network view. A zone groups assets with a shared network role.",
  "engineering": "Supervisory and station zones separate operator tools from field equipment in this recipe. CORE assigns and configures the actual modeled IP addresses.",
  "cyber": "The same IP can occur in different isolated ranges. Identify a node using its range instance and namespace, not IP alone. Tailscale or SSH transport is separate from scenario identity.",
  "check": "Use the live range inventory to trace HMI → firewall → RTU. Compare it with the packet source/destination addresses.",
  "sources": [
   {
    "title": "CORE · architecture and network namespaces",
    "url": "https://coreemu.github.io/core/architecture.html"
   }
  ]
 },
 {
  "id": "firewall",
  "title": "Firewall and allow rule",
  "plain": "A firewall evaluates network traffic against rules. This range's boundary node permits specified flows between its two zones.",
  "engineering": "Blocking HMI traffic can hide observations while local control continues. Network access and process safety are different questions.",
  "cyber": "The recipe explicitly permits HMI→RTU TCP502 and master→RTU TCP 20000. Stateful return traffic is allowed. Port permission alone does not authorize an individual command.",
  "check": "Locate the allow rule and its counters. Compare an allowed acquisition with a controlled loss test inside the owned virtual range.",
  "sources": [
   {
    "title": "NIST SP 800-82 Rev. 3 · operational technology architecture and security",
    "url": "https://csrc.nist.gov/pubs/sp/800/82/r3/final"
   },
   {
    "title": "Netfilter · nftables rules, chains and counters",
    "url": "https://wiki.nftables.org/wiki-nftables/index.php/Quick_reference-nftables_in_10_minutes"
   }
  ]
 },
 {
  "id": "observers",
  "title": "Wireshark, noVNC and Zeek",
  "plain": "Wireshark displays captured packets. noVNC provides browser access to its remote desktop. Zeek interprets traffic into structured records.",
  "engineering": "These tools observe communications; they do not directly measure breaker movement or electrical power. Correlate their evidence with controller and process observations.",
  "cyber": "Packet capture and protocol parsing do not by themselves prove intrusion detection. Check capture placement, packet loss, connection identifiers and decoder support.",
  "check": "Match one DNP3 exchange or FUXA Modbus read across the UI, packet capture and Zeek records. This observer is private instructor infrastructure.",
  "sources": [
   {
    "title": "Wireshark · user guide, packet analysis and capture limits",
    "url": "https://www.wireshark.org/docs/wsug_html_chunked/ChapterIntroduction.html"
   },
   {
    "title": "Zeek 8 LTS · connection records, timestamps and UIDs",
    "url": "https://docs.zeek.org/en/lts/logs/conn.html"
   }
  ]
 },
 {
  "id": "mqtt",
  "title": "MQTT publisher, broker and subscriber",
  "plain": "A publisher sends messages to named topics. A broker routes them to clients whose subscriptions match. A subscriber receives those messages.",
  "engineering": "A payload needs application-defined units and meaning. A delivered command is not proof that a pump moved or a sensor is accurate.",
  "cyber": "Inspect publish/subscribe permissions, client identity and topic patterns. A broker is a routing service, not automatically an authority for process truth.",
  "check": "Follow the recorded tank message from publisher through broker to HMI. Recorded MQTT is not evidence of a live sensor connection.",
  "sources": [
   {
    "title": "OASIS · MQTT 3.1.1 standard",
    "url": "https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html"
   }
  ]
 },
 {
  "id": "qos",
  "title": "MQTT QoS, retained messages and Will",
  "plain": "Quality of Service (QoS) describes message delivery behavior. A retained message can be sent to a later subscriber. A Will is a message the broker can publish after an unexpected client disconnect.",
  "engineering": "A retained reading may be old. Delivery assurance is not a guarantee of current measurement or exactly-once physical actuation.",
  "cyber": "Packet identifiers belong to a connection's exchange. Inspect acknowledgments and payload timestamps; do not infer a Will fired merely because it was configured.",
  "check": "Compare the selected message's QoS, retain flag and acknowledgments. State which behavior is actually present in the capture.",
  "sources": [
   {
    "title": "OASIS · MQTT 3.1.1 standard",
    "url": "https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html"
   }
  ]
 },
 {
  "id": "logic",
  "title": "Logic Studio program and I/O",
  "plain": "Logic Studio authors the controller's program. Inputs (I) carry observations and requests; outputs (Q) carry the program's decisions. X means a bit and W means a word in this notation.",
  "engineering": "Author mode edits a project; Monitor mode reads controller replies. The instruction set spans contacts and coils, timers (TON/TOF), counters (CTUD), edge detectors (R_TRIG/F_TRIG), compare and XOR, scaled-integer word math (ADD/SUB/MUL/DIV, MOVE, LIMIT, SEL) and a fixed-point PID loop — all compiled to the same device IR and shown in ladder and structured text together. Bits are %IX/%QX/%MX; words are %IW (inputs), %MW (scratch) and %QW (analog outputs). Editing locally does not change the running controller until the explicit download/admission path succeeds.",
  "cyber": "Source, compiled program and running identity should agree. A software output can differ from physical feedback, especially under a force or fault. Altering the logic or its parameters maps to ATT&CK for ICS Modify Program (T0889) and Modify Parameter (T0836); the ladder↔text view and the readback-vs-workspace check are how you confirm the installed logic is the logic you authored, not just that it parses.",
  "check": "Verify the program, inspect IX0/IX1 and QX0, then trace the corresponding controller image and network values.",
  "sources": [
   {
    "title": "Siemens · S7-1200 Easy Book, PLC and control concepts",
    "url": "https://cache.industry.siemens.com/dl/files/145/39710145/att_5787/v1/s71200_easy_book_en-US_en-US.pdf"
   }
  ]
 },
 {
  "id": "operating-model",
  "title": "Controller operating model · Author/Monitor view, PROGRAM/RUN/REMOTE",
  "plain": "Two different switches sit above the program. The Studio VIEW chooses what you are doing here — ✎ Author edits the project offline, ◉ Monitor watches the live controller. The controller MODE is the device's own state — PROGRAM accepts a new program, RUN executes it, REMOTE lets a master command it. Editing in Author never changes a running controller by itself.",
  "engineering": "Going live is gated, not a single button. The controller must be connected, in PROGRAM to accept a download, and the loaded program's readback must match the workspace before it reports run-ready; only then can RUN or REMOTE start. A latched fault (for example a scan overrun under load) blocks RUN until it is cleared and readiness is re-read. The intended sequence is connect → PROGRAM → download + readback match → run-ready → RUN/REMOTE.",
  "cyber": "These transitions are exactly where an attacker changes what the plant runs. Installing new logic requires PROGRAM mode, so Program Download (T0843) and the Modify Program (T0889) it delivers show up as a mode change plus a download, not a silent edit; driving outputs from a master is Manipulation of Control (T0831) through REMOTE. The readback-must-match gate is the integrity check — it proves the running program is the one that was authored — so a mismatch, or a controller reporting RUN without a matching readback, is the signal worth alarming on, not the download packet alone.",
  "check": "From Author, change a rung and try to go live: watch the readiness gate refuse RUN until the download readback matches. Switch to Monitor and compare the live image with your workspace. Then reason: which mode must the controller be in for an attacker to install new logic, and what independent evidence would tell an operator the running program is not the authored one?",
  "sources": [
   {
    "title": "MITRE ATT&CK for ICS · Program Download (T0843)",
    "url": "https://attack.mitre.org/techniques/T0843/"
   },
   {
    "title": "Top 20 Secure PLC Coding Practices",
    "url": "https://plc-security.com/"
   }
  ]
 },
 {
  "id": "pid",
  "title": "PID loop · proportional, integral and derivative control",
  "plain": "A proportional-integral-derivative (PID) controller continuously adjusts an output to reduce the difference between a setpoint and measured value.",
  "engineering": "P reacts to present error, I accumulates past error, D reacts to change. In this Studio the PID runs on the same device IR as the ladder: a fixed-point block whose gains are scaled integers (entered ×100, so KP 1.50 is 150), with the integral and previous error held per instance, conditional-integration anti-windup, and the output clamped to [OUTMIN, OUTMAX]. The measured value PV comes from a word input (%IW); the control value CV drives an analog output (%QW); the scan period sets the loop rate.",
  "cyber": "A PID loop is a parameter-rich control surface. Changing the setpoint, a gain, or the measured PV can drive the process to an unsafe state while every packet stays syntactically valid — ATT&CK for ICS Modify Parameter (T0836) and Manipulation of Control (T0831); rewriting the loop in the program itself is Modify Program (T0889) via Program Download (T0843). Anti-windup and the output clamp are engineered software limits, not a security boundary: cyber-informed engineering asks which independent physical interlock — not this same PLC's logic — still bounds the consequence.",
  "check": "Open the reservoir-pid example in Logic Studio. Read SP, PV, the three gains and the output limits off the PID block, and confirm CV lands in the expected %QW. Change the setpoint or a gain, then reason about the physical consequence before the network one, and ask whether any barrier independent of this controller would still bound the outcome.",
  "sources": [
   {
    "title": "Siemens · PID control function manual, control principles",
    "url": "https://cache.industry.siemens.com/dl/files/036/108210036/att_896030/v1/s71500_pid_control_function_manual_enUS_en-US.pdf"
   }
  ]
 },
 {
  "id": "control-logic",
  "title": "Control-logic instructions · timers, counters, edges, math",
  "plain": "Beyond plain contacts and coils, a PLC program expresses sequence, timing and calculation with function blocks: timers, counters, edge detectors, comparisons, word math and a PID loop. Logic Studio compiles them all to the same device bytecode and shows each one in ladder and structured text side by side.",
  "engineering": "Timers (TON on-delay, TOF off-delay) hold an output for a preset time; the up/down counter (CTUD) tallies events against a preset; edge detectors (R_TRIG/F_TRIG) fire for a single scan on a transition; compare (GT/LT/GE/LE/EQ) and XOR build decisions; word math (ADD/SUB/MUL/DIV, MOVE, LIMIT, SEL) works on scaled integers in %IW/%MW/%QW; PID closes a loop. The defining property is state carried between scans — a timer's elapsed time, a counter's value, an edge's remembered input, a PID's integral — so behaviour depends on history, not only on this scan's inputs.",
  "cyber": "Logic is an attack surface in its own right. Changing a preset, gain or comparison threshold is Modify Parameter (T0836); replacing the rungs is Modify Program (T0889) delivered by Program Download (T0843); forcing the bit that drives a timer or counter is Manipulation of Control (T0831). Because these blocks hold state across scans, a brief manipulation can leave a lasting effect — a latched output, a wound-up integral, a skipped or spoofed count — that outlives the packet that caused it. Cyber-informed engineering asks what each block is protecting and whether a barrier independent of this PLC still bounds the consequence if the logic is subverted.",
  "check": "Insert a timer and a counter, watch the ladder↔text pairing highlight together, and trace each preset and output into the point map. Then reason: if an attacker changed just one preset, or forced its input for a single scan, what physical state would persist afterwards — and what, independent of this controller, would still limit it?",
  "sources": [
   {
    "title": "MITRE ATT&CK for ICS · Techniques",
    "url": "https://attack.mitre.org/matrices/ics/"
   },
   {
    "title": "INL / DOE · Cyber-Informed Engineering",
    "url": "https://inl.gov/cie/"
   }
  ]
 },
 {
  "id": "registers",
  "title": "Register table and point map",
  "plain": "A register table shows controller values at numbered locations. A point map states each location's name, type, unit and meaning for a particular firmware and lesson.",
  "engineering": "Digital bits, signed values, scaling and physical channels differ. Lesson roles can reuse generic firmware points without changing their global names.",
  "cyber": "An incorrect offset or scale can make a correctly decoded packet misleading. Verify map version, firmware identity and whether a table is read-only or writable.",
  "check": "Choose one displayed value, find its raw address/type/scale, and independently decode a matching wire response.",
  "sources": [
   {
    "title": "Modbus Organization · application protocol V1.1b3 and implementation guides",
    "url": "https://www.modbus.org/modbus-specifications"
   },
   {
    "title": "DNP Users Group · protocol primer",
    "url": "https://www.dnp.org/Portals/0/AboutUs/DNP3%20Primer%20Rev%20A.pdf"
   }
  ]
 },
 {
  "id": "byte-order",
  "title": "Little-endian / big-endian · byte order",
  "aliases": [
   "little indian",
   "endianness",
   "hex",
   "LSB",
   "MSB"
  ],
  "plain": "A byte contains 8 bits. A number larger than 255 needs more than one byte. Little-endian sends or stores the least significant byte first; big-endian puts the most significant byte first. Endianness describes byte order, not the order of individual bits.",
  "engineering": "A measurement must be decoded with the correct type, byte order, scale and unit. The same two bytes can otherwise become a plausible but very wrong pressure or voltage.",
  "cyber": "For decimal 4660 (hexadecimal 0x1234), little-endian bytes are 34 12 and big-endian bytes are 12 34. DNP3 uses little-endian for the address and integer fields shown here. Modbus uses big-endian within a 16-bit register; word order across multiple registers depends on the device map. A protocol can have fields with different rules.",
  "check": "In the recorded DNP3 READ, destination bytes 0A 00 mean 10 in little-endian. Reversing the interpretation gives 2560. Use the explorer below, then select the destination field in the packet.",
  "sources": [
   {
    "title": "DNP Users Group · incoming data, object headers and qualifier layout",
    "url": "https://www.dnp.org/Portals/0/Public%20Documents/DNP3%20AN2013-004b%20Validation%20of%20Incoming%20DNP3%20Data.pdf"
   },
   {
    "title": "Modbus Organization · application protocol V1.1b3 and implementation guides",
    "url": "https://www.modbus.org/modbus-specifications"
   },
   {
    "title": "OASIS · MQTT 3.1.1 standard",
    "url": "https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html"
   }
  ]
 },
 {
  "id": "acronyms",
  "title": "Acronyms · OT, SCADA, RTU, PLC, HMI, IED, DNP3, MQTT",
  "plain": "OT = operational technology: systems that monitor or control physical processes. SCADA = supervisory control and data acquisition: tools for supervising distributed equipment.",
  "engineering": "RTU = remote terminal unit. PLC = programmable logic controller. HMI = human-machine interface. IED = intelligent electronic device. CT = current transformer. VT = voltage transformer. MV = medium voltage. These describe roles, instruments or voltage classes; they are not interchangeable devices.",
  "cyber": "DNP3 = Distributed Network Protocol, version 3. MQTT is the protocol name used by OASIS; it historically came from MQ Telemetry Transport. TCP = Transmission Control Protocol. IP = Internet Protocol. CRC = cyclic redundancy check, an error check rather than authentication.",
  "check": "Identify a device role (RTU), an interface (HMI), a protocol (DNP3) and an electrical component (transformer). Explain one connection between them.",
  "sources": [
   {
    "title": "NIST SP 800-82 Rev. 3 · operational technology architecture and security",
    "url": "https://csrc.nist.gov/pubs/sp/800/82/r3/final"
   },
   {
    "title": "DNP Users Group · protocol primer",
    "url": "https://www.dnp.org/Portals/0/AboutUs/DNP3%20Primer%20Rev%20A.pdf"
   },
   {
    "title": "OASIS · MQTT 3.1.1 standard",
    "url": "https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html"
   }
  ]
 },
 {
  "id": "electrical-units",
  "title": "Electrical units · V, A, W, VA, AC and power factor",
  "plain": "Voltage (V) is electrical potential difference. Current (A) is electrical flow. Real power (W) is the rate of energy transfer that performs work or produces heat. k means 1,000 and M means 1,000,000.",
  "engineering": "AC means alternating current. DC means direct current. In an AC system, apparent power (VA) combines real and reactive power effects. Power factor is real power divided by apparent power. Impedance describes how a circuit opposes AC current, including resistance and reactance.",
  "cyber": "Do not compare MVA capacity with MW demand as if they were identical. A corrupted scale or unit can mislead an operator even when packet bytes decode correctly.",
  "check": "The transformer rating is 10 MVA; normal demand is 0.9 MW at 0.95 power factor. Explain why its nameplate capacity is not the current load.",
  "sources": [
   {
    "title": "EIA · electrical quantities and terminology",
    "url": "https://www.eia.gov/tools/glossary/?id=electricity"
   },
   {
    "title": "EIA · measuring power and energy",
    "url": "https://www.eia.gov/energyexplained/electricity/measuring-electricity.php"
   }
  ]
 },
 {
  "id": "grid-architecture",
  "title": "How the grid is built · DOE and EIA resources",
  "plain": "Generation produces electrical energy, transmission moves it over longer distances, and distribution delivers it to customers. Substations connect these stages and may transform voltage, switch circuits, measure conditions and protect equipment.",
  "engineering": "A useful starting path is generation → step-up transformer → transmission → step-down substation → distribution feeder → customers. Real networks have branches, interconnections and local generation; a single chain is an introduction, not every possible topology.",
  "cyber": "Overlay a separate information path: sensors and protection → station RTU/gateway → communication network → control center/HMI. Add dependencies on fuel, water, communications and auxiliary power. Do not confuse a network route with an electrical connection.",
  "check": "Start with EIA’s delivery overview. Use the DOE primer’s supply-chain figure and substation/transformer sections, then compare DOE’s cross-sector diagrams with our proposed gas–power–water dependencies.",
  "sources": [
   {
    "title": "EIA · How electricity is delivered",
    "url": "https://www.eia.gov/energyexplained/electricity/delivery-to-consumers.php"
   },
   {
    "title": "DOE · United States Electricity Industry Primer (2015)",
    "url": "https://www.energy.gov/sites/prod/files/2015/12/f28/united-states-electricity-industry-primer.pdf"
   },
   {
    "title": "DOE CESER · supply-chain and cross-sector diagrams",
    "url": "https://www.energy.gov/ceser/state-energy-security-plan-sesp-resources"
   }
  ]
 },
 {
  "id": "gas-station",
  "title": "Gas station example · pressure regulator, flow and linepack",
  "plain": "A proposed gas pressure-regulating station would reduce supply pressure, measure gas flow and pressure, and report valve and shutdown state to an RTU. It is a planned contrasting DNP3 lesson, not a running range.",
  "engineering": "Gas is compressible. Pipe inventory, called linepack, changes as pressure and stored mass change. Pressure loss, regulator action, demand and valve travel need a gas model. Gauge pressure is relative to atmosphere; absolute pressure includes it. Flow units need defined temperature/pressure reference conditions.",
  "cyber": "Reuse DNP3 point types and evidence tools, but give the points gas-specific units and meanings. Distinguish actual pressure loss from a stale report, a shifted sensor value or a communication outage.",
  "check": "Planned: compare a demand step with a telemetry interruption. The model must conserve mass and show pressure/flow consequences before this example is accepted. Water hydraulics must not be relabeled as natural-gas physics.",
  "sources": [
   {
    "title": "DNP Users Group · applications and protocol primer",
    "url": "https://www.dnp.org/Portals/0/AboutUs/DNP3%20Primer%20Rev%20A.pdf"
   },
   {
    "title": "pandapipes · gas-network calculation candidate",
    "url": "https://pandapipes.readthedocs.io/en/latest/"
   },
   {
    "title": "pandapipes · pressure, flow and reference units",
    "url": "https://pandapipes.readthedocs.io/en/latest/about/units.html"
   }
  ]
 },
 {
  "id": "operations",
  "title": "Operations desk · SOC, process and evidence",
  "plain": "SOC means security operations center: analysts investigate security evidence. A control-system operator watches equipment and service delivery. This desk joins those perspectives around the same selected asset.",
  "engineering": "The 3D view is an illustrative layout. Its breaker status indicator follows current model position; it does not establish physical dimensions, field calibration or electrical safety. Compare process-model state with actual RTU and HMI observations.",
  "cyber": "Animated markers represent newly observed Zeek exchanges, not attacks or every packet. The asset identity includes its range and CORE namespace. Console snapshots are fixed read-only diagnostics. Detection and interactive-shell adapters remain separate capabilities.",
  "check": "Select the RTU in the network map, find it in3D, then inspect its registers and source. Run the12-second virtual firewall experiment: distinguish lost DNP3 visibility from HMI acquisition and process change. Read the rollback receipt.",
  "sources": [
   {
    "title": "NIST SP 800-82 Rev. 3 · operational technology architecture and security",
    "url": "https://csrc.nist.gov/pubs/sp/800/82/r3/final"
   },
   {
    "title": "Zeek 8 LTS · connection records, timestamps and UIDs",
    "url": "https://docs.zeek.org/en/lts/logs/conn.html"
   }
  ]
 },
 {
  "id": "caldera",
  "title": "CALDERA, Sandcat and C2",
  "plain": "CALDERA coordinates an exercise. Sandcat is the agent that executes a task. C2 means command and control: the conversation that delivers tasks and returns results. Here they run in two separate Docker containers.",
  "engineering": "The fixed diagnostic reads holding register offset 25 twice using Modbus function 3. An advancing heartbeat shows the firmware is progressing. It does not measure sensor freshness, actuator position or process safety.",
  "cyber": "CORE assigns the container network addresses. The agent mounts only its own range data, with no host-root or Docker socket mount. Containers still share the Linux kernel. A Zeek notice identifies a read outside the operational client baseline; it is not proof of malicious intent. The C2 path and controller-read path are separate firewall rules.",
  "check": "Run a heartbeat diagnostic. Match its source port and Modbus transaction ID to the field capture and Zeek UID. Deny agent reads and repeat. Explain why a returned failure result can coexist with fresh HMI data.",
  "sources": [
   {
    "title": "MITRE CALDERA · agents, abilities and operations",
    "url": "https://caldera.readthedocs.io/en/latest/Getting-started.html"
   }
  ]
 },
 {
  "id": "policy-editor",
  "title": "Firewall policy · preview, trial and recovery",
  "plain": "An allow rule permits a named source to connect to a destination port. TCP and UDP are transport protocols; a port selects a service. Default drop silently discards other forwarded packets.",
  "engineering": "Keep HMI acquisition and local controller operation separate from the diagnostic path. This workshop protects the HMI and agent management routes. Devices on the same network segment can communicate without crossing the firewall.",
  "cyber": "Preview resolves names to this CORE instance’s namespaces and addresses. A revision identifies the policy; packet counters are evidence, not part of that revision. Established replies are tied to each allowed flow, so removing a flow affects existing sessions. Application timeouts and retries determine visible recovery.",
  "check": "Predict what removing master → rtu TCP 20000 will change. Preview the diff, apply a short trial, compare DNP3 evidence with Modbus acquisition and controller RUN, then verify the baseline readback. The worker restores on expiry or graceful stop; a hard-killed worker/host requires journal-based recovery.",
  "sources": [
   {
    "title": "Netfilter · nftables rules, chains and counters",
    "url": "https://wiki.nftables.org/wiki-nftables/index.php/Quick_reference-nftables_in_10_minutes"
   },
   {
    "title": "NIST SP 800-82 Rev. 3 · operational technology architecture and security",
    "url": "https://csrc.nist.gov/pubs/sp/800/82/r3/final"
   }
  ]
 },
 {
  "id": "infrastructure",
  "title": "Physical and cyber infrastructure",
  "plain": "The physical view follows electricity through recognizable equipment. The cyber view follows observations and commands through computers, a firewall and the field controller. Select a labeled component to see the same asset in its inspector.",
  "engineering": "Transformer bushings carry conductors through the grounded tank; radiator fins represent cooling surfaces. Insulators support live bus conductors. A circuit breaker interrupts current internally: its front status indicator changes with the modeled position; it is not an exposed knife disconnect. Three conductors illustrate a balanced three-phase model, not independent phase calculations. The control-house cutaway shows the RTU role. Geometry, cooling and clearances are illustrative.",
  "cyber": "Device enclosures represent CORE network roles. The Sandcat agent and CALDERA coordinator run in separate containers; their drawn positions do not establish physical separation. Recipe links are topology, not permission or captured traffic. Only new observed exchanges move. Unknown or expired process observations must stay unknown even when the equipment remains visible.",
  "check": "Select the transformer, then Show related field RTU. Compare its cyber connections with the physical power path. During a DNP3 policy trial, explain why network visibility can fail while controller RUN, independent HMI acquisition and modeled delivered power continue. Use Overview, Top view and Focus selection, or select every asset by keyboard.",
  "sources": [
   {
    "title": "CORE · architecture and network namespaces",
    "url": "https://coreemu.github.io/core/architecture.html"
   },
   {
    "title": "EIA · delivery from transmission through distribution",
    "url": "https://www.eia.gov/energyexplained/electricity/delivery-to-consumers.php"
   },
   {
    "title": "NIST SP 800-82 Rev. 3 · operational technology architecture and security",
    "url": "https://csrc.nist.gov/pubs/sp/800/82/r3/final"
   }
  ]
 },
 {
  "id": "switch-span",
  "title": "Ethernet switch, hub, TAP and SPAN",
  "plain": "A switch learns which port reaches each Ethernet address. A hub floods frames across its shared segment. SPAN means Switched Port Analyzer: a switch copies selected traffic to a monitoring port. A TAP is a separate traffic access device.",
  "engineering": "CORE uses Linux bridges and virtual interfaces to emulate these networks. CORE owns the IP addresses inside endpoint namespaces. The current station recipe uses a hub, while the supervisory and C2 networks use switches; there is no configured SPAN session.",
  "cyber": "An observer sees only traffic delivered to its capture interface. Promiscuous mode does not make a learning switch copy other ports. Packets blocked at the firewall before the field segment will be absent here. A future SPAN destination needs transmit isolation, explicit source ports and directions, loss counters and acceptance captures.",
  "check": "Select the supervisory switch and field hub. Read their actual bridge identities, then compare an allowed and denied Sandcat read in the field observer. Explain why absence at the observer does not locate the drop by itself.",
  "sources": [
   {
    "title": "CORE · architecture and network namespaces",
    "url": "https://coreemu.github.io/core/architecture.html"
   },
   {
    "title": "Wireshark · user guide, packet analysis and capture limits",
    "url": "https://www.wireshark.org/docs/wsug_html_chunked/ChapterIntroduction.html"
   },
   {
    "title": "Cisco · SPAN source, destination and capture limitations",
    "url": "https://www.cisco.com/c/en/us/td/docs/switches/lan/c9000/mgmt/management-configuration-guide/span.html"
   }
  ]
 },
 {
  "id": "incident-timeline",
  "title": "Incident timeline and evidence clocks",
  "plain": "A timeline puts observations in time order. It helps you compare a rule change, a network exchange and a process response. Nearby events may be related, but timing alone does not prove why something happened.",
  "engineering": "The controller, process model and host have different clocks. Model event rows use their first host observation for placement and retain the original model milliseconds in the evidence. Do not subtract model milliseconds from UTC time or claim sub-millisecond ordering from this view.",
  "cyber": "Firewall rows come from mutation/readback/restore journals. CALDERA rows identify real agent operations. Zeek rows preserve source UIDs and timestamps. Controller and process changes come from durable observations. Bounded retention, polling gaps and a field-only capture point limit what can be concluded.",
  "check": "Run a diagnostic, block its read path, run it again, then compare the result with Zeek and the controller. Select an event to examine ±15 host seconds, export the evidence, and state what supports your explanation and what remains unknown.",
  "sources": [
   {
    "title": "Zeek 8 LTS · connection records, timestamps and UIDs",
    "url": "https://docs.zeek.org/en/lts/logs/conn.html"
   },
   {
    "title": "NIST SP 800-82 Rev. 3 · operational technology architecture and security",
    "url": "https://csrc.nist.gov/pubs/sp/800/82/r3/final"
   }
  ]
 },
 {
  "id": "observer-placement",
  "title": "Capture point, analyzer and remote display",
  "plain": "The capture point is where traffic enters an analyzer. The computer showing its screen may be elsewhere. Here the field hub connects to the observer’s eth0, while your browser receives a remote desktop over a separate management path.",
  "engineering": "Wireshark, Zeek and dumpcap enter the observer CORE network namespace. Xvfb, the window manager and noVNC services run on the Linux range runtime; the Wireshark window uses a local Unix X11 socket. The drawing separates logical analysis from plant equipment, not actual geographic sites.",
  "cyber": "Only traffic reaching the field hub is in this capture scope. The hub connection is not a configured SPAN destination and does not enforce receive-only behavior. These observer processes share the range runtime filesystem; a shared classroom host needs per-learner analysis isolation and authenticated display access. A packaged single-student runtime provides an outer container, not a separate observer container.",
  "check": "Select Observer namespace and compare its capture namespace with the host display namespace. Find a permitted flow in Zeek, then explain why an upstream firewall denial and browser display traffic may be absent.",
  "sources": [
   {
    "title": "CORE · architecture and network namespaces",
    "url": "https://coreemu.github.io/core/architecture.html"
   },
   {
    "title": "Wireshark · user guide, packet analysis and capture limits",
    "url": "https://www.wireshark.org/docs/wsug_html_chunked/ChapterIntroduction.html"
   },
   {
    "title": "Zeek 8 LTS · connection records, timestamps and UIDs",
    "url": "https://docs.zeek.org/en/lts/logs/conn.html"
   },
   {
    "title": "Cisco · SPAN source, destination and capture limitations",
    "url": "https://www.cisco.com/c/en/us/td/docs/switches/lan/c9000/mgmt/management-configuration-guide/span.html"
   }
  ]
 },
 {
  "id": "dnp3-objects",
  "title": "DNP3 objects · group, variation, qualifier and index",
  "aliases": [
   "g30v1",
   "g1v2",
   "object header",
   "range",
   "point count"
  ],
  "plain": "An object is an encoded point value or another DNP3 data structure. Group tells you its category; variation selects a format within that group; qualifier tells you how many objects are included and how their point indexes are identified.",
  "engineering": "Example: g30v1 means analog input, 32-bit signed integer plus quality flags. Qualifier 0x00 with start 2 and stop 4 selects indexes 2, 3, 4 inclusive. Qualifier 0x17 carries a one-byte count and a one-byte index before each object, so a message can identify nonconsecutive points. Qualifier 0x06 means all objects of the specified type; g60v1 with 0x06 requests Class 0 static data. Group 1 is binary input snapshots; Group 2 is binary input events. Variation 1 does not have the same meaning in every group.",
  "cyber": "Qualifier is message layout, not data quality. A valid object header does not authenticate the sender or identify an engineering unit. The device point map supplies the meaning of each index. Binary input 2 and analog input 2 are different points. Unknown variations must remain unsupported rather than be guessed.",
  "check": "Select an object header and its qualifier in Decoded fields. Identify the highlighted bytes, the included indexes, and the bytes used for each value. Then use the lesson point map to connect a point to a sensor or breaker contact.",
  "sources": [
   {
    "title": "DNP Users Group · protocol primer",
    "url": "https://www.dnp.org/Portals/0/AboutUs/DNP3%20Primer%20Rev%20A.pdf"
   },
   {
    "title": "DNP Users Group · incoming data, object headers and qualifier layout",
    "url": "https://www.dnp.org/Portals/0/Public%20Documents/DNP3%20AN2013-004b%20Validation%20of%20Incoming%20DNP3%20Data.pdf"
   },
   {
    "title": "Step Function · DNP3 object group/variation reference",
    "url": "https://docs.stepfunc.io/dnp3/1.6.0/java/io/stepfunc/dnp3/Variation.html"
   }
  ]
 },
 {
  "id": "dnp3-layers",
  "title": "DNP3 layers · link, transport, application and sequence",
  "aliases": [
   "FIR",
   "FIN",
   "CON",
   "UNS",
   "link address",
   "sequence"
  ],
  "plain": "DNP3 wraps point data in several headers. The link header identifies DNP3 endpoints. Transport information joins segments. The application header describes the request or response. TCP/IP provides another surrounding delivery layer in this lesson.",
  "engineering": "FIR means first; FIN means final. Transport has a six-bit sequence; application control has a separate four-bit sequence. CON asks for an application confirmation and UNS marks unsolicited traffic. A DNP3 link address is not an IP address. Values wrap because sequence fields have a fixed number of bits.",
  "cyber": "Sequence numbers help associate exchanges, not authenticate a client. A complete TCP connection does not mean every DNP3 application fragment was captured. The current teaching decoder handles single transport/application fragments and does not reassemble streams.",
  "check": "Find a response that requests confirmation and the corresponding CONFIRM. Compare direction and application sequence. Keep its DNP3 addresses separate from the IP endpoints.",
  "sources": [
   {
    "title": "DNP Users Group · protocol primer",
    "url": "https://www.dnp.org/Portals/0/AboutUs/DNP3%20Primer%20Rev%20A.pdf"
   },
   {
    "title": "DNP Users Group · incoming data, object headers and qualifier layout",
    "url": "https://www.dnp.org/Portals/0/Public%20Documents/DNP3%20AN2013-004b%20Validation%20of%20Incoming%20DNP3%20Data.pdf"
   },
   {
    "title": "Wireshark · user guide, packet analysis and capture limits",
    "url": "https://www.wireshark.org/docs/wsug_html_chunked/ChapterIntroduction.html"
   }
  ]
 },
 {
  "id": "dnp3-quality",
  "title": "DNP3 quality and IIN · what the device is reporting",
  "aliases": [
   "ONLINE",
   "RESTART",
   "COMM_LOST",
   "internal indications",
   "flags"
  ],
  "plain": "Quality flags qualify an individual point value. IIN means Internal Indications and reports broader outstation or protocol conditions. Neither is the qualifier byte that describes the object layout.",
  "engineering": "A point may report ONLINE, restart or communication loss; other flags depend on the point type. IIN can indicate queued events, a device restart, need for time or an unsupported request. A binary state is a value bit, not a quality bit.",
  "cyber": "A fresh packet can carry stale or bad-quality data. A valid CRC only checks the bytes and does not make the reported measurement truthful. No IIN flags set does not establish that the physical plant is healthy. The studio preserves the ONLINE bit separately. Its summary conservatively withholds forced, restart, communication-loss, range/reference-error and other flagged values; inspect the original flags and value. This is a classroom display rule, not a universal DNP3 quality policy.",
  "check": "Inspect a binary state, its flag byte and the message IIN separately. Explain which evidence supports a value, device condition and capture integrity.",
  "sources": [
   {
    "title": "DNP Users Group · incoming data, object headers and qualifier layout",
    "url": "https://www.dnp.org/Portals/0/Public%20Documents/DNP3%20AN2013-004b%20Validation%20of%20Incoming%20DNP3%20Data.pdf"
   },
   {
    "title": "Step Function · DNP3 object group/variation reference",
    "url": "https://docs.stepfunc.io/dnp3/1.6.0/java/io/stepfunc/dnp3/Variation.html"
   },
   {
    "title": "DNP Users Group · protocol primer",
    "url": "https://www.dnp.org/Portals/0/AboutUs/DNP3%20Primer%20Rev%20A.pdf"
   },
   {
    "title": "Step Function DNP3 implementation · individual quality flags",
    "url": "https://docs.stepfunc.io/dnp3/1.1.0/java/io/stepfunc/dnp3/Flag.html"
   }
  ]
 },
 {
  "id": "dnp3-control",
  "title": "DNP3 controls · SELECT, OPERATE and CROB",
  "aliases": [
   "control relay output block",
   "direct operate",
   "pulse",
   "latch",
   "command status"
  ],
  "plain": "SELECT asks an outstation to check and select a control. OPERATE requests its execution. DIRECT OPERATE requests execution without a prior SELECT. CROB means Control Relay Output Block, the encoded binary output command.",
  "engineering": "A CROB includes an action, operation count, on/off times and command status. Pulse durations are requests; they are not measured breaker travel times. Output command status and physical position contacts describe different stages.",
  "cyber": "A successful command response is not independent proof that equipment moved. Inspect the client, requested action, matching response and later input feedback. This live substation lesson uses local virtual inputs and read-only DNP3 polling; recorded commands do not establish live control support.",
  "check": "In the recorded lesson, compare SELECT, OPERATE, response status and the most recent binary input report. State what the capture proves and what physical feedback is still missing.",
  "sources": [
   {
    "title": "DNP Users Group · protocol primer",
    "url": "https://www.dnp.org/Portals/0/AboutUs/DNP3%20Primer%20Rev%20A.pdf"
   },
   {
    "title": "DNP Users Group · incoming data, object headers and qualifier layout",
    "url": "https://www.dnp.org/Portals/0/Public%20Documents/DNP3%20AN2013-004b%20Validation%20of%20Incoming%20DNP3%20Data.pdf"
   },
   {
    "title": "Step Function · DNP3 object group/variation reference",
    "url": "https://docs.stepfunc.io/dnp3/1.6.0/java/io/stepfunc/dnp3/Variation.html"
   }
  ]
 },
 {
  "id": "mqtt-packets",
  "title": "MQTT packet fields · connection, flags and acknowledgements",
  "aliases": [
   "CONNACK",
   "PUBACK",
   "PUBREC",
   "PUBREL",
   "PUBCOMP",
   "SUBACK",
   "DUP",
   "Clean Session",
   "Session Present",
   "Keep Alive"
  ],
  "plain": "MQTT carries application messages between clients through a broker. A fixed header gives the packet type and remaining length; the variable header and payload depend on that packet type. CONNECT starts the session and CONNACK reports whether the broker accepted it.",
  "engineering": "Keep Alive is a maximum client control-packet interval, not a sensor sampling period. Client ID identifies session state; it is separate from credentials. Clean Session and Session Present concern stored session state. Retained publications concern stored last messages. These mechanisms are different.",
  "cyber": "QoS and PUBACK describe message delivery at a protocol hop, not physical actuation. Packet identifiers are connection-local. A declared Will is not evidence that the broker delivered it. Password display redaction does not remove credential bytes from an imported capture.",
  "check": "Follow CONNECT to CONNACK, then PUBLISH to its acknowledgements. Compare packet identifiers on the publisher/broker and broker/subscriber connections. Find the meaning and official section for each field.",
  "sources": [
   {
    "title": "OASIS · MQTT 3.1.1 standard",
    "url": "https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html"
   },
   {
    "title": "Wireshark · user guide, packet analysis and capture limits",
    "url": "https://www.wireshark.org/docs/wsug_html_chunked/ChapterIntroduction.html"
   }
  ]
 },
 {
  "id": "mqtt-topics",
  "title": "MQTT topic names, filters and application payloads",
  "aliases": [
   "publisher",
   "subscriber",
   "wildcard",
   "UTF-8",
   "JSON",
   "+",
   "#"
  ],
  "plain": "A topic name is the case-sensitive label on a published message. A topic filter is a subscription pattern that chooses matching names. The payload is the application content carried under that name.",
  "engineering": "plant/+/telemetry matches one complete middle level, such as tank1. plant/# matches the parent and its descendants. + and # belong in filters, not publication names. A payload may be JSON, text or binary; units and meaning come from the device contract.",
  "cyber": "Receiving a matching topic does not imply permission to publish commands. A broad subscription increases what can be observed only if broker policy allows it. Topics beginning with $ have special wildcard matching rules. A plausible payload is not proof of sensor freshness.",
  "check": "Compare the recorded subscription filter with each PUBLISH topic. Identify who published it, which broker forwarded it, and which fields are application-defined rather than MQTT-defined.",
  "sources": [
   {
    "title": "OASIS · MQTT 3.1.1 standard",
    "url": "https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html"
   }
  ]
 },
 {
  "id": "capture-basics",
  "title": "Packet inspection · bytes, CRC, PCAP and evidence limits",
  "aliases": [
   "hex",
   "hexadecimal",
   "offset",
   "packet capture",
   "TCP",
   "IPv4",
   "checksum",
   "reassembly"
  ],
  "plain": "A byte contains eight bits. Hexadecimal writes one byte as two base-16 digits, such as 0A for decimal 10. An offset counts a position from the beginning, starting at zero. PCAP is a packet-capture file format.",
  "engineering": "Wire byte offsets in this studio are relative to the selected protocol frame. A DNP3 CRC is a corruption check; it is different from a cryptographic signature. A packet timestamp belongs to the capture clock and is not automatically a sensor sample or controller event time.",
  "cyber": "Capture location limits what you can see. Missing packets, truncated records, unsupported formats, encryption and stream fragmentation can prevent decoding. This bounded decoder requires one complete supported message per TCP payload. Wireshark can provide broader reassembly and independent interpretation.",
  "check": "Select a decoded field and find its original bytes. Compare with Wireshark at the same capture point. Identify one conclusion the bytes support and one they cannot establish.",
  "sources": [
   {
    "title": "Wireshark · user guide, packet analysis and capture limits",
    "url": "https://www.wireshark.org/docs/wsug_html_chunked/ChapterIntroduction.html"
   },
   {
    "title": "DNP Users Group · incoming data, object headers and qualifier layout",
    "url": "https://www.dnp.org/Portals/0/Public%20Documents/DNP3%20AN2013-004b%20Validation%20of%20Incoming%20DNP3%20Data.pdf"
   },
   {
    "title": "Zeek 8 LTS · connection records, timestamps and UIDs",
    "url": "https://docs.zeek.org/en/lts/logs/conn.html"
   }
  ]
 },
 {
  "id": "isolator",
  "title": "Disconnect switch · 89",
  "plain": "An air-break disconnect provides an isolating gap when opened under the appropriate operating conditions. This illustration remains closed.",
  "engineering": "The disconnect sits between the incoming gantry and T1 in this representative layout. It is not remotely operable in the lesson. A visible gap is useful evidence, but it is not a complete clearance procedure. This tour does not teach authorized switching steps.",
  "cyber": "Do not infer isolation from a screen icon or a communication failure. Field verification and utility procedures are separate work.",
  "check": "Find three blade contacts on insulator posts and a linked operating handle. This is separate from the enclosed circuit breaker.",
  "sources": [
   {
    "title": "OSHA · illustrated substation equipment glossary",
    "url": "https://www.osha.gov/etools/electric-power/illustrated-glossary/sub-station"
   },
   {
    "title": "DOE · United States Electricity Industry Primer",
    "url": "https://www.energy.gov/sites/prod/files/2015/12/f28/united-states-electricity-industry-primer.pdf"
   }
  ]
 },
 {
  "id": "arrester",
  "title": "Surge arresters",
  "plain": "Surge arresters limit transient overvoltage by providing a surge-current path toward earth near protected equipment.",
  "engineering": "Their upper connections branch from the high-voltage conductors; lower leads bond to ground. They are shunt devices. The range has no lightning impulse, arrester degradation or insulation-coordination model. Color does not establish arrester health.",
  "cyber": "A quiet telemetry screen cannot prove surge protection is healthy. Inspection and device-specific tests provide different evidence.",
  "check": "Find the three ribbed columns beside T1 and their ground leads. These are not switches, fuses or normal load-current paths.",
  "sources": [
   {
    "title": "OSHA · illustrated substation equipment glossary",
    "url": "https://www.osha.gov/etools/electric-power/illustrated-glossary/sub-station"
   },
   {
    "title": "DOE · United States Electricity Industry Primer",
    "url": "https://www.energy.gov/sites/prod/files/2015/12/f28/united-states-electricity-industry-primer.pdf"
   }
  ]
 },
 {
  "id": "dc",
  "title": "Station DC battery & charger",
  "plain": "DC means direct current. A battery and charger can supply protection and breaker trip circuits during an AC disturbance.",
  "engineering": "The conceptual control-power route is separate from the high-energy feeder and from communication links. This substation view does not model station-battery depletion. The platform’s separate facility UPS exercise is a different model.",
  "cyber": "A healthy network link does not prove a trip coil has control power. What independent DC measurement would you ask a technician to obtain?",
  "check": "Find the battery cells, links, DC panel and charger in the cutaway house. No charge level is invented for this illustration.",
  "sources": [
   {
    "title": "OSHA · illustrated substation equipment glossary",
    "url": "https://www.osha.gov/etools/electric-power/illustrated-glossary/sub-station"
   },
   {
    "title": "DOE · United States Electricity Industry Primer",
    "url": "https://www.energy.gov/sites/prod/files/2015/12/f28/united-states-electricity-industry-primer.pdf"
   }
  ]
 },
 {
  "id": "ground",
  "title": "Grounding grid cutaway",
  "plain": "Grounding bonds equipment to a conductive network in the earth. Its design controls fault-current paths and hazardous potential differences.",
  "engineering": "Copper-colored bonds connect equipment to ground. They are not a normal phase path and do not carry DNP3 traffic. Grid geometry, soil resistivity, touch/step voltage and arc hazards are not calculated. Scene distances are not safe working clearances.",
  "cyber": "Use the distinction between cyber evidence and physical verification. A packet capture cannot certify that a ground bond is intact.",
  "check": "This raised cutaway exposes conductors normally buried beneath the yard. The real grid is not an exposed walking surface.",
  "sources": [
   {
    "title": "OSHA · illustrated substation equipment glossary",
    "url": "https://www.osha.gov/etools/electric-power/illustrated-glossary/sub-station"
   },
   {
    "title": "DOE · United States Electricity Industry Primer",
    "url": "https://www.energy.gov/sites/prod/files/2015/12/f28/united-states-electricity-industry-primer.pdf"
   }
  ]
 },
 {
  "id": "weather-boundary",
  "title": "Outside conditions and local control",
  "plain": "A boundary condition describes the surroundings of a model. An outdoor weather report can influence modeled room temperature without being the local sensor reading.",
  "engineering": "The lumped cooling model balances IT heat, cooling and heat exchanged with outside air. Its thermal capacity and power ratings are teaching assumptions. A weather observation has its own source time and geography; it does not measure the real room or a physical fan.",
  "cyber": "A stale public report, a lost local sensor and an unavailable controller are different failures. Keep the original weather time, modeled sample time and packet time separate. An explicitly held boundary is a prediction, not a new measurement.",
  "check": "Select the outside-air and local-sensor objects. Compare their timestamps and values, then use a native Modbus response to identify which temperature the controller actually received. Explain what would distinguish a feed outage from local control loss.",
  "sources": [
   {
    "title": "NWS API · observations, caching and delayed data",
    "url": "https://www.weather.gov/documentation/services-web-api"
   },
   {
    "title": "Modbus Organization · application protocol specification",
    "url": "https://www.modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf"
   }
  ]
 },
 {
  "id": "city-experiment",
  "title": "Connected district simulation",
  "plain": "Stations connect wind farms and customer circuits. Select equipment to change its modeled connection. W means watts of delivered power; °C is temperature in degrees Celsius.",
  "engineering": "Advance moves the simulation clock by one second, up to 60 seconds. A data-center transfer switch selects a supply path; an open switch alone does not show whether its UPS still serves equipment. This example uses simplified customer thermal models, not a qualified chiller plant.",
  "cyber": "These actions affect numerical models only. They do not send protocol commands or prove a controller, CORE network or physical device responded. Revision numbers reject conflicting edits; recipe owner names are not authenticated user identities.",
  "check": "Disconnect north, compare north and south household power, then advance time. Save and reopen the experiment and compare the same results. A prediction failure means unavailable, not zero. Check browser downloads before replacing work.",
  "sources": [
   {
    "title": "pandapower power-flow documentation",
    "url": "https://pandapower.readthedocs.io/en/latest/powerflow.html"
   }
  ]
 },
 {
  "id": "ncir-temperature",
  "title": "NCIR sensor and object temperature",
  "plain": "Sensor temperature describes the sensing package. Object temperature is the infrared estimate of what it sees. These are different measurements; neither automatically represents the whole room or machine.",
  "engineering": "Both display in degrees Celsius. Controller bindings carry signed centi-Celsius: divide by 100, so 2875 means 28.75°C. Field of view, surface emissivity, reflections and package heating affect interpretation. Our driver reads the two values sequentially; the displayed age starts at read completion, not a measured physical conversion timestamp.",
  "cyber": "A checksum detects transmission errors; it does not authenticate the device or prove calibration. Read refreshes cached observations. Apply changes an explicit input assignment, not the program or RUN mode. Invalid or stale input must follow the controller’s declared fault behavior. For a critical cooling function, consider an independent temperature limit and safe fallback rather than trusting one networked reading.",
  "check": "Compare both values and their age/quality. Trace the selected field through its binding and register scaling into ladder/ST. For a qualified fault exercise, check that a bad checksum or sensor-error flag removes validity instead of producing a healthy zero. NCIR emulated fault exercises are not yet qualified. Keep physical measurements distinct from simulated consequences.",
  "sources": [
   {
    "title": "M5Stack · NCIR U028",
    "url": "https://docs.m5stack.com/en/unit/Unit_NCIR"
   },
   {
    "title": "Melexis · MLX90614 datasheet",
    "url": "https://www.melexis.com/en/documents/documentation/datasheets/datasheet-mlx90614"
   }
  ]
 }
];

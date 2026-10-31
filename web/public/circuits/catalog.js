/* CircuitCatalog — the Part 1 "Wire & run" predesigned circuits as DATA.
 *
 * Each circuit lists its parts (snap-kit modules placed on the 40-unit base grid), conductor strips,
 * flying leads (with hand-routed waypoints that follow the routing rules: perpendicular exits, nested-L
 * harness order, no lead under a part, crossings only over strips and captioned), the netlist, the
 * Studio project it pairs with, the signal path for the strip under the drawing, and teaching notes.
 *
 * The netlist is the source of truth: tools/circuit-catalog-check.js rebuilds connectivity from the
 * drawing (studs, strips, leads, terminals) and fails if it differs from `nets`; the schematic view is
 * generated from `nets`, so bench and schematic cannot drift apart.
 *
 * Conditions (`on`) use silkscreen names: IN1..IN8 (input bit observed), RELAY1..RELAY4 (relay bit
 * observed), '!RELAY2' (de-energised), 'any' (any load or input active). Nothing is ever "on" unless the
 * target is connected and reporting: idle means idle.
 *
 * Board names are 1-based (IN1, RELAY 1); firmware names are 0-based (di0, relay0). Both are shown.
 */
(function(root){
'use strict';

/* Phone → controller point mapping, as the firmware ships it TODAY (mqtt_sensors.cpp / plc_core.cpp).
 * The firmware engineer is redesigning this (per-sensor freshness, declared slots, quality word): update
 * this table only — the phone block, the readout, the network-link label and the signal path read it. */
const PHONE_MAP=[
  {id:'motion', name:'Motion',          scalar:'|a|',     unit:'m/s²', point:'%IW2', reg:51, scale:100, scaleNote:'|a| × 100', topic:'accel',  key:'mag',  ios:true,  spoof:true,  thr:{set:1300,clear:1100,op:'>',text:'|a|×100 > 1300 sets · < 1100 clears'}},
  {id:'orient', name:'Orientation',     scalar:'tilt β',  unit:'°',    point:'%IW3', reg:52, scale:1,   scaleNote:'β in degrees', topic:'orient', key:'beta', ios:true,  spoof:true,  thr:{set:30,clear:25,op:'>',text:'β > 30° sets · < 25° clears'}},
  {id:'light',  name:'Ambient light',   scalar:'lux',     unit:'lux',  point:'%IW1', reg:50, scale:1,   scaleNote:'lux',      topic:'light',  key:'lux',  ios:false, spoof:true,  note:'Android Chrome only', thr:{set:20,clear:40,op:'<',text:'lux < 20 sets · > 40 clears (dark alarm)'}},
  {id:'sound',  name:'Microphone level',scalar:'level',   unit:'0–100',point:null,   reg:null,scale:1,  topic:'sound',  key:'level',ios:true,  spoof:true,  note:'firmware does not subscribe to phone/+/sound yet'},
  {id:'geo',    name:'Geolocation',     scalar:'lat / lon',unit:'',    point:null,   reg:null,          topic:'geo',    key:null,   ios:true,  spoof:false, note:'no scalar defined yet; no spoof control'},
  {id:'battery',name:'Battery',         scalar:'%',       unit:'%',    point:null,   reg:null,          topic:'battery',key:'level',ios:false, spoof:false, note:'no Battery API on iOS Safari; not ingested'},
  {id:'btn1',   name:'BTN 1',           scalar:'momentary',unit:'',    point:'%IX6', bit:6,            topic:'btn1',   key:null,   ios:true,  spoof:false, note:'OR-ed with the IN7 screw; never a Stop or E-stop'},
  {id:'btn2',   name:'BTN 2',           scalar:'momentary',unit:'',    point:'%IX7', bit:7,            topic:'btn2',   key:null,   ios:true,  spoof:false, note:'OR-ed with the IN8 screw; never a Stop or E-stop'}
];
const PHONE_SCREEN={r:'relay0',g:'relay1',b:'relay2',topic:'stamplc/display/rgb',retained:true,period:'0.4 s'};

const CTL={x:44,y:300};
/* standard field-supply leads: + lead over the top (it must cross the − strip once: + and − are adjacent
 * screws, so the cyclic order forces one crossing — put it over a strip, away from terminals, captioned). */
function plusLead(x,y,landY){return {id:'Lplus',fn:'plus',from:{term:'+'},via:[[90,y],[x,y]],to:{stud:[x,landY]},tag:'+',on:'field',ma:'field',dir:1};}
function minusLead(){return {id:'Lminus',fn:'minus',from:{term:'-'},via:[[128,140]],to:{stud:[700,140]},tag:'−',on:'loads',ma:'loads',dir:-1};}
const S=(a,b,fn,o)=>Object.assign({a,b,fn},o||{});

const CIRCUITS=[
/* ------------------------------------------------------------------ a */
{id:'a',key:'pushbutton-led',name:'Pushbutton → LED',
  title:'Pushbutton → LED',subtitle:'S1 on IN2 (di1) → your rung → RELAY 2 (relay1) → H1',
  project:{file:'pushbutton',name:'Pushbutton → green LED',reads:['%IX1 (IN2)'],writes:['%QX1 (RELAY 2)'],
    note:'One rung: NO contact %IX1 → coil %QX1. Load it (PROGRAM → Load → RUN) before you press.'},
  ctl:CTL,used:{IN2:1,RELAY2:1},
  parts:[
    {id:'S1',type:'push',at:[740,220],span:4,cap:'blue',glyph:'',control:true,mode:'momentary',in:'IN2',rest:false,
      label:{x:930,y:214,t:'S1  PUSH',s:'NO · momentary · → IN2 (di1)'},aria:'S1 push button, normally open, wired from + to IN2 (di1). Hold to press.'},
    {id:'R1',type:'resistor',at:[980,780],span:2,value:'100 Ω'},
    {id:'H1',type:'led',at:[1060,780],span:3,hue:'green',glyph:'1',on:'RELAY2',ma:23,
      label:{x:960,y:834,t:'H1  LED',s:'green · RELAY 2 N.O (relay1)'}}
  ],
  strips:[S([900,220],[900,500],'plus',{on:'RELAY2',ma:23,dir:1}),S([900,500],[900,740],'plus',{on:'RELAY2',ma:23,dir:1}),
    S([700,140],[980,140],'minus',{on:'RELAY2',ma:23,dir:-1}),S([980,140],[1180,140],'minus',{on:'RELAY2',ma:23,dir:-1}),
    S([1180,140],[1180,420],'minus',{on:'RELAY2',ma:23,dir:-1}),S([1180,420],[1180,700],'minus',{on:'RELAY2',ma:23,dir:-1}),S([1180,700],[1180,780],'minus',{on:'RELAY2',ma:23,dir:-1})],
  leads:[plusLead(900,104,220),minusLead(),
    {id:'Lin2',fn:'signal',from:{term:'IN2'},via:[[242,220]],to:{stud:[740,220]},tag:'IN2',on:'IN2',ma:1.25,dir:-1,trace:1},
    {id:'Lcom2',fn:'plus',from:{term:'RELAY2.COM'},via:[[221,740]],to:{stud:[900,740]},tag:'+',on:'RELAY2',ma:23,dir:-1,trace:1},
    {id:'Lno2',fn:'load',from:{term:'RELAY2.NO'},via:[[189,780]],to:{stud:[980,780]},tag:'LED',on:'RELAY2',ma:23,dir:1,trace:1}],
  jumper:true,
  consumers:[{on:'IN2',ma:1.25,input:1},{on:'RELAY2',ma:23}],
  nets:{'FP+':['U1.+','S1.b','U1.RELAY2.COM'],'DI_IN2':['S1.a','U1.IN2'],'FP-':['U1.-','U1.C','H1.k'],'Q_RLY2':['U1.RELAY2.NO','R1.a'],'L_H1':['R1.b','H1.a']},
  crossings:[{x:914,y:121,t:'+ lead passes over the − strip · no stud = no joint'}],
  notes:[{x:620,y:292,w:262,title:'How this loop works',lines:['The button does not power the LED.','The controller reads IN2, solves','your rung, then closes RELAY 2:','read → solve → write.','R1 (100 Ω) keeps H1 near 23 mA','at the 4.45 V field supply.','C is tied to − (sourcing wiring),','so pressing S1 puts + on IN2.']}],
  legend:{x:1262,y:640,fns:['plus','minus','signal','load','flow']},
  path:{kind:'wired',sw:'S1',in:2,relay:2,contact:'NO',load:'H1',loadName:'H1 LED',ma:23,rung:'NO contact %IX1 → coil %QX1'},
  teach:{idea:'The scan cycle: the controller sits between the button and the LED (read → solve → write).',
    predict:'Order three events — the 0x59 input read changes, the 0x59 output write changes, H1 lights — and give the longest delay you expect between the first two, using the scan time.',
    run:'Hold S1. Watch the path strip, then the console (io). Repeat with a forced input on a USB board and compare the hop tags.',
    reconcile:'Compare the read → write gap with the scan time. With force, which hops turned BYPASSED, and where did the force act?',
    mustNot:['that the button is electrically connected to the LED','that act = 1 proves the contact closed (it is the expander pin readback)','that a forced input exercised the input wiring'],
    consequence:'2015 Ukraine distribution grid: breakers opened by commands sent through the operators’ own software. The controller obeys whatever reaches its command path (T0855, T0835).'}
},
/* ------------------------------------------------------------------ b */
{id:'b',key:'no-vs-nc',name:'NO vs NC · two LEDs',
  title:'NO vs NC · two LEDs',subtitle:'one relay, two contacts: RELAY 2 N.O → green H1 · RELAY 2 N.C → amber H2',
  project:{file:'pushbutton',name:'Pushbutton → green LED',reads:['%IX1 (IN2)'],writes:['%QX1 (RELAY 2)'],
    note:'Same one-rung program as circuit a. The NO/NC difference here is in the relay contacts, not in the ladder. The two-led-nonc example shows the ladder kind (an NC contact on a second relay).'},
  ctl:CTL,used:{IN2:1,RELAY2:1},
  parts:[
    {id:'S1',type:'push',at:[740,220],span:4,cap:'blue',control:true,mode:'momentary',in:'IN2',rest:false,
      label:{x:930,y:214,t:'S1  PUSH',s:'NO · momentary · → IN2 (di1)'},aria:'S1 push button, normally open, wired from + to IN2 (di1). Hold to press.'},
    {id:'R2',type:'resistor',at:[980,740],span:2,value:'100 Ω'},
    {id:'H2',type:'led',at:[1060,740],span:3,hue:'amber',glyph:'0',on:'!RELAY2',ma:22,
      label:{x:960,y:794,t:'H2  LED',s:'amber · RELAY 2 N.C (relay1)'}},
    {id:'R1',type:'resistor',at:[980,860],span:2,value:'100 Ω'},
    {id:'H1',type:'led',at:[1060,860],span:3,hue:'green',glyph:'1',on:'RELAY2',ma:23,
      label:{x:960,y:914,t:'H1  LED',s:'green · RELAY 2 N.O (relay1)'}}
  ],
  strips:[S([900,220],[900,500],'plus',{on:'any',ma:23,dir:1}),S([900,500],[900,780],'plus',{on:'any',ma:23,dir:1}),S([900,780],[900,820],'plus',{on:'any',ma:23,dir:1}),
    S([700,140],[980,140],'minus',{on:'any',ma:23,dir:-1}),S([980,140],[1180,140],'minus',{on:'any',ma:23,dir:-1}),
    S([1180,140],[1180,420],'minus',{on:'any',ma:23,dir:-1}),S([1180,420],[1180,700],'minus',{on:'any',ma:23,dir:-1}),
    S([1180,700],[1180,740],'minus',{on:'any',ma:23,dir:-1}),S([1180,740],[1180,860],'minus',{on:'RELAY2',ma:23,dir:-1})],
  leads:[plusLead(900,104,220),minusLead(),
    {id:'Lin2',fn:'signal',from:{term:'IN2'},via:[[242,220]],to:{stud:[740,220]},tag:'IN2',on:'IN2',ma:1.25,dir:-1,trace:1},
    {id:'Lnc2',fn:'load',from:{term:'RELAY2.NC'},via:[[253,740]],to:{stud:[980,740]},tag:'N.C',on:'!RELAY2',ma:22,dir:1},
    {id:'Lcom2',fn:'plus',from:{term:'RELAY2.COM'},via:[[221,820]],to:{stud:[900,820]},tag:'+',on:'any',ma:23,dir:-1,trace:1},
    {id:'Lno2',fn:'load',from:{term:'RELAY2.NO'},via:[[189,860]],to:{stud:[980,860]},tag:'N.O',on:'RELAY2',ma:23,dir:1,trace:1}],
  jumper:true,
  consumers:[{on:'IN2',ma:1.25,input:1},{on:'RELAY2',ma:23},{on:'!RELAY2',ma:22}],
  nets:{'FP+':['U1.+','S1.b','U1.RELAY2.COM'],'DI_IN2':['S1.a','U1.IN2'],'FP-':['U1.-','U1.C','H1.k','H2.k'],
    'Q_NO':['U1.RELAY2.NO','R1.a'],'L_H1':['R1.b','H1.a'],'Q_NC':['U1.RELAY2.NC','R2.a'],'L_H2':['R2.b','H2.a']},
  crossings:[{x:914,y:121,t:'+ lead passes over the − strip · no stud = no joint'}],
  notes:[{x:620,y:292,w:262,title:'One coil, two contacts',lines:['H1 on RELAY 2 N.O: lit while the','coil is energised (relay1 = 1).','H2 on RELAY 2 N.C: lit while it is','not — in PROGRAM mode, after a','fault, at every power-up.','The ladder’s NC contact (examine-','if-open) is a different NO/NC again.'],foot:'Never both: break-before-make.'}],
  legend:{x:1262,y:640,fns:['plus','minus','signal','load','flow']},
  path:{kind:'wired',sw:'S1',in:2,relay:2,contact:'NO',load:'H1',loadName:'H1 green',ma:23,alt:{contact:'NC',load:'H2',loadName:'H2 amber'},rung:'NO contact %IX1 → coil %QX1'},
  teach:{idea:'“NO / NC” means three different things: the field device, the ladder instruction and the relay output contact. Here it is the relay contact: one coil, two contacts that are never closed together.',
    predict:'Fill in a released/pressed table for H1 and H2. Then predict both LEDs when the controller is in PROGRAM (outputs off).',
    run:'Press and release S1 in RUN; then switch to PROGRAM and look again.',
    reconcile:'Which LED is the “something is wrong or stopped” indicator, and why does that make N.C the fail-safe choice for a warning light? Load two-led-nonc and say which NO/NC layer changed.',
    mustNot:['that the amber LED proves the button circuit is healthy','that “NC” in the ladder and “N.C” on the relay are the same thing','that two outputs changing together means they are wired together'],
    consequence:'Maroochy water breach, 2000: commands sent to pumping stations while alarm reporting was suppressed, so the control room saw “normal”. Signals that read “fine” when cut hide the event (T0878).'}
},
/* ------------------------------------------------------------------ c */
{id:'c',key:'motor-estop',name:'Motor start/stop + E-stop',
  title:'Motor start / stop + fail-safe E-stop',subtitle:'START IN1 · STOP IN2 · E-STOP IN3 (NC, ON = healthy) · RESET IN4 → RELAY 1 motor on its own 3 V pack',
  project:{file:null,name:'your Part 1 motor latch',reads:['%IX0 START','%IX1 STOP','%IX2 E-STOP (1 = healthy)','%IX3 RESET'],writes:['%QX0 motor (RELAY 1)','%QX1 run light (RELAY 2)','%QX3 fault lamp (RELAY 4)'],
    note:'Build it in Logic: seal-in on %QX0, permissives %IX1 and %IX2, a latched fault (%MX) on %IX2 = 0, reset on %IX3. ladder/latch_with_estop.st still trips on %IX2 = 1 (energise-to-trip); the fail-safe version trips on 0.'},
  ctl:CTL,used:{IN1:1,IN2:1,IN3:1,IN4:1,RELAY1:1,RELAY2:1,RELAY4:1},
  restDi:0x04,
  parts:[
    {id:'S1',type:'push',at:[740,180],span:4,cap:'green',glyph:'I',control:true,mode:'momentary',in:'IN1',rest:false,
      label:{x:930,y:176,t:'S1  START',s:'NO · momentary · → IN1 (di0)'},aria:'S1 START push button, normally open, to IN1 (di0). Hold to press.'},
    {id:'S2',type:'push',at:[740,260],span:4,cap:'black',glyph:'O',control:true,mode:'momentary',in:'IN2',rest:false,
      label:{x:930,y:256,t:'S2  STOP',s:'NO · momentary · → IN2 (di1)'},aria:'S2 STOP push button, normally open, to IN2 (di1). Hold to press.'},
    {id:'S3',type:'slide',at:[740,340],span:4,estop:true,control:true,mode:'maintained',in:'IN3',rest:true,
      label:{x:930,y:336,t:'S3  E-STOP',s:'slide · ON = healthy · → IN3 (di2)',s2:'logic trips on IN3 = 0 *',warn:1},aria:'S3 E-STOP slide switch, closed while healthy, to IN3 (di2). Toggle to trip or reset.'},
    {id:'S4',type:'push',at:[740,420],span:4,cap:'blue',glyph:'R',control:true,mode:'momentary',in:'IN4',rest:false,
      label:{x:930,y:416,t:'S4  RESET',s:'NO · momentary · → IN4 (di3)'},aria:'S4 RESET push button, normally open, to IN4 (di3). Hold to press.'},
    {id:'R2',type:'resistor',at:[980,740],span:2,value:'100 Ω'},
    {id:'H2',type:'led',at:[1060,740],span:3,hue:'red',glyph:'!',on:'RELAY4',ma:23,label:{x:960,y:794,t:'H2  FAULT',s:'RELAY 4 (relay3)',inline:1}},
    {id:'R1',type:'resistor',at:[980,860],span:2,value:'100 Ω'},
    {id:'H1',type:'led',at:[1060,860],span:3,hue:'green',glyph:'▶',on:'RELAY2',ma:23,label:{x:960,y:912,t:'H1  RUN',s:'RELAY 2 (relay1)',inline:1}},
    {id:'B1',type:'battery',at:[1300,580],span:4,cells:2,label:{x:1256,y:540,t:'B1  MOTOR PACK',s:'2×AA · 3 V',inline:1}},
    {id:'D1',type:'diode',at:[1300,700],span:4,label:{x:1284,y:654,t:'D1  FREEWHEEL DIODE',small:1},sub2:{x:1318,y:764,t:'band (k) → M+'}},
    {id:'M1',type:'motor',at:[1300,820],span:4,on:'RELAY1',ma:320,label:{x:1290,y:900,t:'M1  MOTOR',s:'3 V DC',inline:1}}
  ],
  fence:{x:1222,y:462,w:324,h:490,title:'MOTOR LOOP · OWN 3 V PACK',lines:['no copper path to the controller’s + / −;','the RELAY 1 (relay0) contact is the only link']},
  strips:[S([900,180],[900,260],'plus',{on:'IN2|IN3|IN4|RELAY2|RELAY4',ma:'field',dir:1}),S([900,260],[900,340],'plus',{on:'IN3|IN4|RELAY2|RELAY4',ma:'field',dir:1}),S([900,340],[900,420],'plus',{on:'IN4|RELAY2|RELAY4',ma:'field',dir:1}),
    S([900,420],[900,700],'plus',{on:'RELAY2|RELAY4',ma:23,dir:1}),S([900,700],[900,820],'plus',{on:'RELAY2',ma:23,dir:1}),
    S([700,140],[980,140],'minus',{on:'RELAY2|RELAY4',ma:23,dir:-1}),S([980,140],[1180,140],'minus',{on:'RELAY2|RELAY4',ma:23,dir:-1}),
    S([1180,140],[1180,420],'minus',{on:'RELAY2|RELAY4',ma:23,dir:-1}),S([1180,420],[1180,700],'minus',{on:'RELAY2|RELAY4',ma:23,dir:-1}),
    S([1180,700],[1180,740],'minus',{on:'RELAY2|RELAY4',ma:23,dir:-1}),S([1180,740],[1180,860],'minus',{on:'RELAY2',ma:23,dir:-1}),
    S([1460,580],[1460,700],'minus',{on:'RELAY1',ma:320,dir:-1,tracer:1}),S([1460,700],[1460,820],'minus',{on:'RELAY1',ma:320,dir:-1,tracer:1}),
    S([1300,700],[1300,820],'load',{on:'RELAY1',ma:0,dir:1,tracer:1,noflow:1})],
  leads:[plusLead(900,104,180),minusLead(),
    {id:'Lin1',fn:'signal',from:{term:'IN1'},via:[[204,180]],to:{stud:[740,180]},tag:'IN1',on:'IN1',ma:1.25,dir:-1,trace:1},
    {id:'Lin2',fn:'signal',from:{term:'IN2'},via:[[242,198],[684,198],[684,260]],to:{stud:[740,260]},tag:'IN2',on:'IN2',ma:1.25,dir:-1},
    {id:'Lin3',fn:'signal',from:{term:'IN3'},via:[[280,216],[652,216],[652,340]],to:{stud:[740,340]},tag:'IN3',on:'IN3',ma:1.25,dir:-1},
    {id:'Lin4',fn:'signal',from:{term:'IN4'},via:[[318,234],[620,234],[620,420]],to:{stud:[740,420]},tag:'IN4',on:'IN4',ma:1.25,dir:-1},
    {id:'Lcom4',fn:'plus',from:{term:'RELAY4.COM'},via:[[457,724],[850,724],[850,700]],to:{stud:[900,700]},tag:'+',on:'RELAY4',ma:23,dir:-1,r:12},
    {id:'Lno4',fn:'load',from:{term:'RELAY4.NO'},via:[[425,740]],to:{stud:[980,740]},tag:'FLT',on:'RELAY4',ma:23,dir:1},
    {id:'Lcom2',fn:'plus',from:{term:'RELAY2.COM'},via:[[221,756],[840,756],[840,820]],to:{stud:[900,820]},tag:'+',on:'RELAY2',ma:23,dir:-1},
    {id:'Lno2',fn:'load',from:{term:'RELAY2.NO'},via:[[189,772],[820,772],[820,860]],to:{stud:[980,860]},tag:'RUN',on:'RELAY2',ma:23,dir:1},
    {id:'Lcom1',fn:'plus',tracer:1,from:{term:'RELAY1.COM'},via:[[103,930],[1240,930],[1240,580]],to:{stud:[1300,580]},tag:'B+',on:'RELAY1',ma:320,dir:-1,trace:1},
    {id:'Lno1',fn:'load',tracer:1,from:{term:'RELAY1.NO'},via:[[71,946],[1264,946],[1264,820]],to:{stud:[1300,820]},tag:'M+',on:'RELAY1',ma:320,dir:1,trace:1}],
  jumper:true,
  consumers:[{on:'IN1',ma:1.25,input:1},{on:'IN2',ma:1.25,input:1},{on:'IN3',ma:1.25,input:1},{on:'IN4',ma:1.25,input:1},{on:'RELAY2',ma:23},{on:'RELAY4',ma:23}],
  nets:{'FP+':['U1.+','S1.b','S2.b','S3.b','S4.b','U1.RELAY2.COM','U1.RELAY4.COM'],
    'DI_START':['S1.a','U1.IN1'],'DI_STOP':['S2.a','U1.IN2'],'DI_ESTOP':['S3.a','U1.IN3'],'DI_RESET':['S4.a','U1.IN4'],
    'FP-':['U1.-','U1.C','H1.k','H2.k'],'Q_RUN':['U1.RELAY2.NO','R1.a'],'L_RUN':['R1.b','H1.a'],'Q_FLT':['U1.RELAY4.NO','R2.a'],'L_FLT':['R2.b','H2.a'],
    'B+':['B1.p','U1.RELAY1.COM'],'M+':['U1.RELAY1.NO','M1.p','D1.k'],'B-':['B1.n','M1.n','D1.a']},
  crossings:[{x:914,y:121,t:'+ lead passes over the − strip · no stud = no joint'}],
  notes:[{x:624,y:476,w:252,accent:1,title:'* Fail-safe wiring',lines:['S3 is closed while healthy: IN3 = 1.','A press, a cut wire or a dead supply','all read 0; the logic trips and','latches the fault (no auto-restart).','S2 STOP is NO (the kit has no NC push','button), so a cut S2 wire goes unseen.'],foot:'Real plants also cut motor power.'}],
  legend:{x:150,y:800,fns:['plus','minus','signal','load','tracer','flow']},
  path:{kind:'wired',sw:'S1',in:1,relay:1,contact:'NO',load:'M1',loadName:'M1 motor',ma:320,rung:'seal-in latch → %QX0'},
  teach:{idea:'Seal-in memory, permissives, de-energise-to-trip (E-stop ON = healthy = 1), no automatic restart, a latched fault with a deliberate reset — and a control circuit (controller) that only closes a contact in a separate power circuit (the motor’s own pack).',
    predict:'(1) relay0 after a 0.5 s START pulse. (2) After the E-stop, which stops first: the motor or relay0’s bus write? (3) After releasing the E-stop with no START, what do the motor and the fault lamp do?',
    run:'START pulse → E-STOP off → E-STOP on → RESET → START. Watch IN3, RELAY 1 and RELAY 4.',
    reconcile:'Name the evidence for “the motor stopped” versus “the controller commanded stop”. Which would an operator’s HMI show you?',
    mustNot:['that the run light means the motor turns (it shows the command)','that act = 1 means a contact moved','that this snap-kit E-stop is safety-rated'],
    consequence:'TRITON, 2017: attackers reached a plant’s safety controllers; the plant tripped safe because protection failed safe. Protection must not be something the control logic can rewrite (T0880, T0837).'}
},
/* ------------------------------------------------------------------ d */
{id:'d',key:'light-alarm',name:'Light sensor → alarm',
  title:'Light sensor → alarm (analog CH1)',subtitle:'photoresistor divider → IO expansion CH1 → %IW0 (mV) → compare rung → RELAY 3 (relay2) → buzzer + LED',
  project:{file:'light-alarm',name:'Light sensor → alarm',reads:['%IW0 light_mv = CH1 mV (process binding)'],writes:['%QX2 light_alarm → RELAY 3 (relay2)'],
    note:'Ready to load: CH1 → %IW0; the alarm sets above 3000 mV and holds until CH1 falls below 2400 mV (a deadband, so a hovering hand does not chatter the buzzer). Load it, RUN, then cover RP1.'},
  ctl:CTL,used:{RELAY3:1},expansion:{x:518,y:330,w:100,h:322,used:{'CH1.IN+':1,'CH1.ISO':1}},
  analogPending:true,
  parts:[
    {id:'RP1',type:'ldr',at:[820,140],span:4,dir:'v',label:{x:776,y:200,t:'RP1  LIGHT',s:'photoresistor',s2:'≈1 kΩ bright · ≈10 kΩ room',anchor:'end'}},
    {id:'R3',type:'resistor',at:[820,300],span:2,value:'10 kΩ'},
    {id:'BZ1',type:'buzzer',at:[980,740],span:3,on:'RELAY3',ma:30,label:{x:1208,y:736,t:'BZ1  BUZZER',s:'active · RELAY 3 N.O (relay2)'}},
    {id:'R4',type:'resistor',at:[980,860],span:2,value:'100 Ω'},
    {id:'H1',type:'led',at:[1060,860],span:3,hue:'red',glyph:'!',on:'RELAY3',ma:23,label:{x:960,y:914,t:'H1  ALARM',s:'red LED · RELAY 3 N.O (relay2)'}}
  ],
  strips:[S([900,300],[900,580],'plus',{on:'RELAY3',ma:53,dir:1}),S([900,580],[900,700],'plus',{on:'RELAY3',ma:53,dir:1}),
    S([700,140],[820,140],'minus',{on:'RELAY3',ma:53,dir:-1}),S([820,140],[1100,140],'minus',{on:'RELAY3',ma:53,dir:-1}),S([1100,140],[1180,140],'minus',{on:'RELAY3',ma:53,dir:-1}),
    S([1180,140],[1180,420],'minus',{on:'RELAY3',ma:53,dir:-1}),S([1180,420],[1180,460],'minus',{on:'RELAY3',ma:53,dir:-1}),S([1180,460],[1180,740],'minus',{on:'RELAY3',ma:53,dir:-1}),S([1180,740],[1180,860],'minus',{on:'RELAY3',ma:23,dir:-1}),
    S([820,300],[820,380],'signal',{on:'never'}),
    S([980,740],[980,860],'load',{on:'RELAY3',ma:23,dir:1}),S([1100,740],[1180,740],'minus',{on:'RELAY3',ma:30,dir:1})],
  leads:[plusLead(900,94,300),minusLead(),
    {id:'Lain',fn:'signal',from:{term:'CH1.IN+'},via:[],to:{stud:[820,380]},tag:'AIN',on:'never'},
    {id:'Liso',fn:'minus',from:{term:'CH1.ISO'},via:[],to:{stud:[1180,460]},tag:'−',on:'never'},
    {id:'Lcom3',fn:'plus',from:{term:'RELAY3.COM'},via:[[339,724],[850,724],[850,700]],to:{stud:[900,700]},tag:'+',on:'RELAY3',ma:53,dir:-1,r:12,trace:1},
    {id:'Lno3',fn:'load',from:{term:'RELAY3.NO'},via:[[307,740]],to:{stud:[980,740]},tag:'ALM',on:'RELAY3',ma:53,dir:1,trace:1}],
  jumper:false,
  consumers:[{on:'RELAY3',ma:53}],
  nets:{'FP+':['U1.+','R3.b','U1.RELAY3.COM'],'AIN':['R3.a','RP1.b','X1.CH1.IN+'],'FP-':['U1.-','RP1.a','X1.CH1.ISO','BZ1.n','H1.k'],
    'Q_ALM':['U1.RELAY3.NO','BZ1.p','R4.a'],'L_ALM':['R4.b','H1.a']},
  crossings:[{x:914,y:118,t:'+ lead passes over the − strip · no stud = no joint'}],
  notes:[{x:648,y:510,w:234,title:'Light → volts → millivolts',lines:['V = 4.45 V · RP1 / (RP1 + 10 kΩ)','bright ≈ 0.4 V · room ≈ 2.2 V','covered ≈ 4 V or more.','X1 digitises CH1 → %IW0 (mV)','over I²C @0x20; a deadband','compare rung drives RELAY 3.'],foot:'ISO GND on − gives up isolation.'}],
  legend:{x:150,y:792,fns:['plus','minus','signal','load','flow']},
  path:{kind:'analog',sensor:'RP1',iw:0,relay:3,contact:'NO',load:'BZ1',loadName:'BZ1 + H1',ma:53,rung:'%IW0 > 3000 mV · < 2400 mV clears'},
  teach:{idea:'The analog signal chain (resistance → volts → I²C → mV → threshold) and alarm hygiene: a deadband stops chatter, an on-delay ignores a passing hand, a range check catches a broken wire.',
    predict:'Write down %IW0 in room light and covered. How many alarm transitions do you expect in 10 s of hovering near the threshold, with and without the deadband?',
    run:'Physical board with the expansion fitted: cover, hover, uncover; then unplug the 10 kΩ lead. On the twin, use circuit e for now.',
    reconcile:'Count RELAY 3 transitions. What did the cut wire read as, and which rung caught it?',
    mustNot:['that mV equals lux (the divider is uncalibrated and nonlinear)','that a steady reading means a healthy sensor','that an alarm that never fires proves it is bright'],
    consequence:'US water treatment, 2021 (CISA AA21-042A): a chemical setpoint raised more than 100-fold over remote access. Thresholds and setpoints are attack targets (T0836, T0838).'}
},
/* ------------------------------------------------------------------ e */
{id:'e',key:'phone-alarm',name:'Phone sensor → alarm',
  title:'Phone sensor → alarm (network source)',subtitle:'any phone sensor → MQTT → point map → the same compare rung → RELAY 3 (relay2) → buzzer + LED',
  project:{file:null,name:'a phone alarm rung (build it in Logic)',reads:['the selected phone point (see the phone block)'],writes:['%QX2 (RELAY 3)'],
    note:'Same rung shape as circuit d, but compare the phone’s point (e.g. tilt β on %IW3 > 30, clear < 25) and drive %QX2. Phone buttons may only be a Start/request input — never Stop or E-stop.'},
  ctl:{x:70,y:300},used:{RELAY3:1},phone:{x:880,y:108,w:340,h:548,broker:{x:596,y:112,w:240,h:72}},
  parts:[
    {id:'BZ1',type:'buzzer',at:[980,740],span:3,on:'RELAY3',ma:30,label:{x:1208,y:736,t:'BZ1  BUZZER',s:'active · RELAY 3 N.O (relay2)'}},
    {id:'R4',type:'resistor',at:[980,860],span:2,value:'100 Ω'},
    {id:'H1',type:'led',at:[1060,860],span:3,hue:'red',glyph:'!',on:'RELAY3',ma:23,label:{x:960,y:914,t:'H1  ALARM',s:'red LED · RELAY 3 N.O (relay2)'}}
  ],
  strips:[S([900,700],[900,820],'plus',{on:'RELAY3',ma:53,dir:-1}),
    S([980,740],[980,860],'load',{on:'RELAY3',ma:23,dir:1}),S([1100,740],[1180,740],'minus',{on:'RELAY3',ma:30,dir:1}),S([1180,740],[1180,860],'minus',{on:'RELAY3',ma:30,dir:1})],
  leads:[
    {id:'Lplus',fn:'plus',from:{term:'+'},via:[[116,240],[42,240],[42,934],[900,934]],to:{stud:[900,820]},tag:'+',on:'RELAY3',ma:53,dir:1},
    {id:'Lminus',fn:'minus',from:{term:'-'},via:[[154,222],[26,222],[26,950],[1180,950]],to:{stud:[1180,860]},tag:'−',on:'RELAY3',ma:53,dir:-1},
    {id:'Lcom3',fn:'plus',from:{term:'RELAY3.COM'},via:[[365,724],[850,724],[850,700]],to:{stud:[900,700]},tag:'+',on:'RELAY3',ma:53,dir:-1,r:12,trace:1},
    {id:'Lno3',fn:'load',from:{term:'RELAY3.NO'},via:[[333,740]],to:{stud:[980,740]},tag:'ALM',on:'RELAY3',ma:53,dir:1,trace:1}],
  jumper:false,
  consumers:[{on:'RELAY3',ma:53}],
  nets:{'FP+':['U1.+','U1.RELAY3.COM'],'FP-':['U1.-','BZ1.n','H1.k'],'Q_ALM':['U1.RELAY3.NO','BZ1.p','R4.a'],'L_ALM':['R4.b','H1.a']},
  crossings:[{x:612,y:778,t:'ALM lead passes over the + strip'},{x:612,y:795,t:'no stud = no joint',cont:1}],
  notes:[{x:1240,y:440,w:304,title:'No copper between phone and controller',lines:['The value reaches your logic as a','network message: phone → broker →','the controller’s MQTT client → point','map. It never passes a terminal, an','opto or the input bus. Transmit OFF','changes no wire: the point just stops','updating (ages out after 8 s today).'],foot:'authentication: none'}],
  legend:{x:150,y:792,fns:['plus','minus','load','net','flow']},
  path:{kind:'phone',relay:3,contact:'NO',load:'BZ1',loadName:'BZ1 + H1',ma:53},
  teach:{idea:'Provenance: same rung shape as d, different evidence chain. A network value can be spoofed, withheld, frozen or impersonated — and the logic cannot tell where a number came from.',
    predict:'(a) Which live views will show the input change: the bus, the network, or both? (b) What will the alarm do 10 s after Transmit OFF? (c) Can anything on the controller tell a spoofed value from a real one?',
    run:'Scan the QR code on your phone, pick a sensor here (tilt works on every phone), then: real reading → spoof → Transmit OFF → back on.',
    reconcile:'List what the controller observed (a message on a topic, when it arrived) and what you inferred (that a phone measured it). Compare with circuit d.',
    mustNot:['that the value is a measurement (it is a claim)','that the QR session key authenticates the data','that “no alarm” means “safe” (it may mean “no data”)'],
    consequence:'Stuxnet (discovered 2010): machines were driven while operators saw normal-looking values. Your spoof slider does the same to a PLC input; Transmit OFF is the denial-of-view version (T0856, T0832, T0815).'}
}
];

const api={PHONE_MAP,PHONE_SCREEN,CIRCUITS,byId:id=>CIRCUITS.find(c=>c.id===id)};
if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.CircuitCatalog=api;
})(typeof window!=='undefined'?window:globalThis);

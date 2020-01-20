/* Stand-alone MIDI loop station
 *  for arduino DUE.
 *  MIDI events recordable: note on/off, control change, pitch bend. Clock is supported, but not recorded.
 *  Maximum recording time: MAX_STEPS * timeQuantum.
 *  - a push button/foot switch on RECPin starts and stops recording the midi sequence. The sequence 
 *    starts being recorded when the first keypress is detected. If you press it again, overdubbing 
 *    starts. Press it again to stop overubbing.
 *  - keeping pressed for more than 3 seconds RECpin push button/foot switch reset your current track MIDI 
 *    sequence.
 *  - Press the relative "trackPin[]" push button/foot switch to select a track. Press it again to MUTE the track.
 *    Press it again to unmute. A LED (trackLEDPin[]) will lit up indicating which track is active. A maximum number 
 *    of "MAX_TRACKS" are handled. 
 *  - You can clear the whole MIDI sequence by pressing the 3 trackPin[] buttons at the same time.
 *  - a push button/foot switch on panicPin send a "all notes off" message to all the channels.
 *  - a push button/foot switch on undoPin undo your latest record/overdub.
 *  - a rotative encoder (optAPin & optBpin) increases or decreases BPM, thus modifying tempo. If a sequence has been 
 *    recorded, it will be rescaled to the new BPM value.
 *  - if an external clock is received, BPMs are set accordingly. If a sequence has been recorded, it will be 
 *    rescaled to the new BPM value. You can deactivate the external clock receive by keeping pressed Panic button
 *    on startup/reset.
 *  - you can record a maximum of MAX_POLY notes on each time step, and a maximum of 
 *    MAX_STEPS notes.
 *  - Activate the track via dedicated button (trackPin[]).
 *  
 *  
 *  by barito 2019
 *  last update: 03 july 2019
 */

#include <MIDI.h>

#define MAX_STEPS 2300
#define MAX_POLY 10
#define MAX_TRACKS 5
#define DISABLE_THRU
#define BPM_MAX 250
#define BPM_MIN 50
#define METRONOME_NOTE 64
#define METRONOME_CHANNEL 10
#define TF 6

MIDI_CREATE_DEFAULT_INSTANCE();

//foot switches
const byte RECPin = 2;
//const byte mutePin = 3;
//other switches
const byte pushRotaryPin = 23; //metronome
const byte panicPin = 24;
const byte undoPin = 25;
const byte trackPin[MAX_TRACKS] = {34, 32, 31, 30, 36};
//optical encoders
const byte optAPin = 4;
const byte optBPin = 5;
//LEDs
const byte OVDBLEDPin = 6;
const byte RECLEDPin = 7;
const byte trackLEDPin[MAX_TRACKS] = {35, 33, 29, 28, 37};

bool RECState;
//bool muteState;
bool panicState;
bool undoState;
bool pushRotaryState;
bool optA_state;
bool trackState[MAX_TRACKS];
bool muteTrack[2*MAX_TRACKS];
bool blankTrack[MAX_TRACKS];
int lastStep = MAX_STEPS;
int actStep = 0;
bool waitingFirstNote;
bool waitingFirstSeq;
bool RECORDING;
bool OVERDUBBING;
bool PLAY;
bool extClockEnable;
byte beatsPerLoop[TF] = {8, 16, 32, 6, 12, 24}; //2x 4/4 measures, 4x 4/4 measures, 8x 4/4 measures; 2x 3/4 measures, 4x 3/4 measures, 8x 3/4 measures, 
byte timeFrame;
const int defaultTimeQuantum = 10000;//us
unsigned int timeQuantum = defaultTimeQuantum; 
unsigned long trigTime;
unsigned long TIME;
unsigned long beatTIME;
unsigned long pressRECTime;
unsigned long startLoopTime;
unsigned long endLoopTime;
unsigned long loopLenght;
unsigned long firstClockTick;
unsigned long lastClockTick;
byte clockTick;
bool metronomeActive;
unsigned int BPM;
unsigned int beatLenght;
const int debounceTime = 100;
const int rotaryDebounceTime = 20;
byte currentTrack;
bool traceStuckNote[127];
bool traceMIDIchannel[16];
byte MIDItrack[MAX_POLY][MAX_STEPS]; //number of the track
byte MIDIdata1[MAX_POLY][MAX_STEPS];
byte MIDIdata2[MAX_POLY][MAX_STEPS];
byte MIDIchannel[MAX_POLY][MAX_STEPS];

void setup() {
pinMode(RECPin, INPUT_PULLUP);
//pinMode(mutePin, INPUT_PULLUP);
pinMode(pushRotaryPin, INPUT_PULLUP);
pinMode(optAPin,INPUT_PULLUP);
pinMode(optBPin,INPUT_PULLUP);
pinMode(panicPin,INPUT_PULLUP);
pinMode(undoPin,INPUT_PULLUP);
for(int i=0; i<MAX_TRACKS; i++){pinMode(trackPin[i],INPUT_PULLUP);}
pinMode(RECLEDPin, OUTPUT);
pinMode(OVDBLEDPin, OUTPUT);
for(int j=0; j<MAX_TRACKS; j++){pinMode(trackLEDPin[j], OUTPUT);}
//intialize MIDI...
MIDI.setHandleNoteOn(Handle_Note_On);
MIDI.setHandleNoteOff(Handle_Note_Off);
MIDI.setHandleControlChange(Handle_CC);
MIDI.setHandlePitchBend(Handle_PB);
//MIDI.setHandleStart(Handle_Start);
MIDI.setHandleClock(Handle_Clock);
MIDI.begin(MIDI_CHANNEL_OMNI);// start MIDI and listen to ALL MIDI channels
#ifdef DISABLE_THRU
MIDI.turnThruOff();
#endif
RESETMIDI();
PANIC();
}

void loop() {
MIDI.read();        //calls MIDI.handles
Trace_Step();
Metronome_Play();
REC_Switch();
Metronome_Switch();
Undo_Switch();
Track_Switch();
Panic_Switch();
Opt_Encoder();
}

void RESETMIDI(){
RECORDING = false;
OVERDUBBING = false;
trigTime = millis();
//clean ALL MIDI notes tracking
for(int i = 0; i<MAX_STEPS; i++){
  for(int j = 0; j<MAX_POLY; j++){
     MIDIdata1[j][i] = 0; //pitch or CC number
     MIDIdata2[j][i] = 0; //velocity or CC value
   }
}
waitingFirstSeq = true; 
waitingFirstNote = true;
lastStep = MAX_STEPS;
timeQuantum = defaultTimeQuantum;
PLAY = true;
FULL_LED_Blink(5);
digitalWrite(trackLEDPin[0], HIGH);
currentTrack = 0;
metronomeActive = true;
loopLenght = 0;
BPM = 100;
beatLenght = 60000/BPM;
timeFrame = 1; //4x 4/4 measures as default
RECState = digitalRead(RECPin);
undoState = digitalRead(undoPin);
optA_state = digitalRead(optAPin);
panicState = digitalRead(panicPin);
if(panicState == LOW) {extClockEnable = false;} //keep the panic button pressed on startup or reset to disable external clock
else {extClockEnable = true;}
//intialize MIDI stuck notes...
for(int m = 0; m < 127; m++){traceStuckNote[m] = false;}
//intialize MIDI channels used...
for(int n = 0; n < 16; n++){traceMIDIchannel[n] = false;}
//initialize tracks
for(int o = 0; o < MAX_TRACKS; o++){
  muteTrack[o] = false;
  muteTrack[o + MAX_TRACKS] = false; 
  blankTrack[o] = true;
}
}

void Handle_Note_On(byte channel, byte pitch, byte velocity){ //questo handle non vede velocità = 0, per questo è necessario l'handle noteoff
if(RECORDING || OVERDUBBING){ //record note parameters
  if(waitingFirstNote == true) {
    waitingFirstNote = false;
    startLoopTime = millis();
    actStep = 0;
  }
  for(int i=0; i<MAX_POLY; i++){
    if(MIDIdata1[i][actStep] == 0 && actStep <= lastStep){
      MIDIdata1[i][actStep] = pitch;
      MIDIdata2[i][actStep] = velocity;
      MIDItrack[i][actStep] = currentTrack + MAX_TRACKS; // "+ MAX_TRACKS" is for undo traceing
      blankTrack[currentTrack] = false;
      traceStuckNote[pitch] = true;
      traceMIDIchannel[channel-1] = true;
      MIDIchannel[i][actStep] = channel;
      return;
    }
  }
}
}

void Handle_Note_Off(byte channel, byte pitch, byte velocity){
if(RECORDING || OVERDUBBING){ //record note parameters
  for(int i=0; i<MAX_POLY; i++){
    if(MIDIdata1[i][actStep] == 0 && actStep <= lastStep){
      MIDIdata1[i][actStep] = pitch;
      MIDIdata2[i][actStep] = 0;
      MIDItrack[i][actStep] = currentTrack + MAX_TRACKS; // "+ MAX_TRACKS" is for undo traceing
      blankTrack[currentTrack] = false;
      traceStuckNote[pitch] = false;
      MIDIchannel[i][actStep] = channel;
      return;
    }
  }
}
}

void Handle_CC(byte channel, byte number, byte value){
if(RECORDING || OVERDUBBING){ //record CC parameters
  for(int i=0; i<MAX_POLY; i++){
    if(MIDIdata1[i][actStep] == 0 && actStep <= lastStep){
      MIDIdata1[i][actStep] = number;
      MIDIdata2[i][actStep] = value;
      MIDItrack[i][actStep] = currentTrack + MAX_TRACKS; // "+ MAX_TRACKS" is for undo traceing
      blankTrack[currentTrack] = false;
      MIDIchannel[i][actStep] = channel+16; //this is to distinguish notes from control changes and pitch bends
      return;
    }
  }
}
}

void Handle_PB(byte channel, int bend){
if(RECORDING || OVERDUBBING){ //record CC parameters
  for(int i=0; i<MAX_POLY; i++){
    if(MIDIdata1[i][actStep] == 0 && actStep <= lastStep){
      MIDIdata1[i][actStep] = bend >> 6; // >>6 = /64
      MIDItrack[i][actStep] = currentTrack + MAX_TRACKS; // "+ MAX_TRACKS" is for undo traceing
      blankTrack[currentTrack] = false;
      MIDIchannel[i][actStep] = channel+32; //this is to distinguish notes from control changes and pitch bends
      return;
    }
  }
}
}

void Handle_Clock(){
if(extClockEnable == true){
  clockTick++;
  if (clockTick == 1) {
    firstClockTick = millis();
    //beatTIME = millis(); //IT WAS for sync, but kills the metronome
  }
  else if (clockTick == 5){//5-1 = 4; 24 = one beat
    lastClockTick = millis();
    clockTick = 0;
    //GET the beatlenght and BPM from clock and rescale the loop (compute quantum)
    beatLenght = (lastClockTick-firstClockTick)*6; //this variable is necessary for the metronome
    BPM = 60000/ beatLenght;
    if(waitingFirstSeq == false /*&& RECORDING == false && OVERDUBBING == false*/){
     Compute_Quantum();
    }
  }
}
}

//used elsewhere too...
void Compute_Quantum(){
timeQuantum = defaultTimeQuantum * beatLenght * beatsPerLoop[timeFrame]  / loopLenght;
} 

/*void Handle_Start(){
if(RECORDING == false && OVERDUBBING == false){
  actStep = lastStep;
  PLAY = true;
}}*/

void Trace_Step(){
if (micros()-TIME >= timeQuantum){
    TIME = micros();    
    actStep++;
    //if we reach the end of the phrase...
    if (actStep >= lastStep && waitingFirstNote == false){
      if(RECORDING){
        waitingFirstSeq = false; //enter overdubbing
        RECORDING = false;
        digitalWrite(RECLEDPin, LOW);
      }
      actStep = 0; //don't touch ;)
      beatTIME = millis(); //this syncronize and stops the execution of the first metronome beat
      Kill_Stuck_Notes();
    }
    if(PLAY){
      Play_Seq();
    }
}
}

void Play_Seq(){
for(int i=0; i<MAX_POLY; i++){
  if(MIDIdata1[i][actStep]!= 0 && muteTrack[MIDItrack[i][actStep]] == false){ //pitch, CC number or bend, not muted
    if(MIDIchannel[i][actStep] <= 16){
      if(MIDIdata2[i][actStep]>0){
        MIDI.sendNoteOn(MIDIdata1[i][actStep], MIDIdata2[i][actStep], MIDIchannel[i][actStep]);
        traceStuckNote[MIDIdata1[i][actStep]] = true;
        traceMIDIchannel[MIDIchannel[i][actStep]] = true;
      }
      else{
        MIDI.sendNoteOff(MIDIdata1[i][actStep], 0, MIDIchannel[i][actStep]);
        traceStuckNote[MIDIdata1[i][actStep]] = false;
      }
    }
    else if(MIDIchannel[i][actStep] <= 32){
      MIDI.sendControlChange(MIDIdata1[i][actStep], MIDIdata2[i][actStep], MIDIchannel[i][actStep] - 16);
    }
    else /*if(MIDIchannel[i][actStep] <= 48)*/ {
      MIDI.sendPitchBend(MIDIdata1[i][actStep]<<6, MIDIchannel[i][actStep] - 32);
    }
  }
}
}

void Kill_Stuck_Notes(){
for(int i = 0; i < 127; i++){//whole pitch range
  if(traceStuckNote[i] == true){
    for(int j = 0; j < 16; j++){//whole channel range
      if(traceMIDIchannel[j] == true){
        MIDI.sendNoteOff(i, 0, j+1);
      }
    }
  }
}
}

void REC_Switch(){
//check on trigger...
if (digitalRead(RECPin) !=  RECState && millis()-trigTime > debounceTime){
    RECState = !RECState;
    trigTime = millis();
     if (RECState == LOW){
        if(waitingFirstSeq == true){//first recording
            RECORDING = !RECORDING;
            digitalWrite(RECLEDPin, RECORDING);
            OVERDUBBING = false;
            digitalWrite(OVDBLEDPin, LOW);
            if(RECORDING == true){} //do nothing
            else { //if(RECORDING == false){
              if(waitingFirstNote == false){//this keeps into account the eventuality of no notes recorded: no notes = still waiting for the first recording = no overdub yet
                  lastStep = actStep;
                  waitingFirstSeq = false; //enter overdubbing
                  endLoopTime = millis();
                  loopLenght = endLoopTime - startLoopTime; //used elsewhere ...
                  beatLenght = loopLenght / beatsPerLoop[timeFrame];
                  //Compute_Quantum(); you don't want to adjust quantum here ...
              }
            }
        }
        else {
            OVERDUBBING = !OVERDUBBING; 
            digitalWrite(OVDBLEDPin, OVERDUBBING);
            RECORDING = false;
            digitalWrite(RECLEDPin, LOW);
            if(OVERDUBBING == true){
              NoMore_Latest_MIDI();}
            else {}
        }
      }
}
//always check...
if(RECState == HIGH){pressRECTime = millis();}
else /*if(RECState == LOW)*/{
  if(millis()-pressRECTime > 3200) {
    Clean_Track();
    Kill_Stuck_Notes();
    pressRECTime = millis();
    if(blankTrack[0] == true && blankTrack[1] == true && blankTrack[2] == true){
      RESETMIDI();
      PANIC();
    }
  }
}
}

void Clean_Track(){
for(int i = 0; i<MAX_STEPS; i++){
   for(int j = 0; j<MAX_POLY; j++){
    if(MIDItrack[j][i] == currentTrack || MIDItrack[j][i] == currentTrack + MAX_TRACKS){
      //MIDI.sendNoteOff(MIDIdata1[j][actStep], 0, MIDIchannel[j][actStep]);
      MIDIdata1[j][i] = 0;
      MIDIdata2[j][i] = 0;//needed to avoid unwanted noteOffs on second recording
   }
}
}
blankTrack[currentTrack] = true;
muteTrack[currentTrack] = false;
muteTrack[currentTrack + MAX_TRACKS] = false;
single_LED_Blink(trackLEDPin[currentTrack], 3);
}

void NoMore_Latest_MIDI(){
for(int i = 0; i<MAX_STEPS; i++){
   for(int j = 0; j<MAX_POLY; j++){
      if(MIDItrack[j][i] >= MAX_TRACKS) {
        MIDItrack[j][i] = MIDItrack[j][i] - MAX_TRACKS;
      }
   }
}
}

void Track_Switch(){
for(int i=0; i<MAX_TRACKS; i++){
  if (digitalRead(trackPin[i]) !=  trackState[i] && millis()-trigTime > debounceTime){
      trackState[i] = !trackState[i];
      trigTime = millis();
      if(trackState[0]==LOW && trackState[1]==LOW && trackState[2]==LOW){RESETMIDI();}
      else if(trackState[0]==LOW && trackState[1]==LOW){PANIC(); Kill_Stuck_Notes();}
      else if (trackState[i] == LOW){
        if(currentTrack!=i){
           currentTrack = i;
           switch (i){
              case 0:  
              digitalWrite(trackLEDPin[0], HIGH);
              digitalWrite(trackLEDPin[1], LOW);
              digitalWrite(trackLEDPin[2], LOW);
              digitalWrite(trackLEDPin[3], LOW);
              break;
              case 1:
              digitalWrite(trackLEDPin[1], HIGH);
              digitalWrite(trackLEDPin[0], LOW);
              digitalWrite(trackLEDPin[2], LOW);
              digitalWrite(trackLEDPin[3], LOW);
              digitalWrite(trackLEDPin[4], LOW);
              break;
              case 2:
              digitalWrite(trackLEDPin[2], HIGH);
              digitalWrite(trackLEDPin[0], LOW);
              digitalWrite(trackLEDPin[1], LOW);
              digitalWrite(trackLEDPin[3], LOW);
              digitalWrite(trackLEDPin[4], LOW);
              break;
              case 3:
              digitalWrite(trackLEDPin[3], HIGH);
              digitalWrite(trackLEDPin[0], LOW);
              digitalWrite(trackLEDPin[1], LOW);
              digitalWrite(trackLEDPin[2], LOW);
              digitalWrite(trackLEDPin[4], LOW);
              break;
              case 4:
              digitalWrite(trackLEDPin[4], HIGH);
              digitalWrite(trackLEDPin[0], LOW);
              digitalWrite(trackLEDPin[1], LOW);
              digitalWrite(trackLEDPin[2], LOW);
              digitalWrite(trackLEDPin[3], LOW);
              break;
          }
        }
        else{ //if the track button is depressed the second time
          muteTrack[i] = ! muteTrack[i];
          muteTrack[i + MAX_TRACKS] = !muteTrack[i + MAX_TRACKS];
        }
      }
  }
}
}

void PANIC(){
//full panic for older synths
for(int i = 0; i<MAX_STEPS; i++){
   for(int j = 0; j<MAX_POLY; j++){
     if(MIDIdata1[j][i]!=0 && MIDIdata2[j][i]!=0 && MIDIchannel[j][i]<= 16){
      for(int k = 1; k<=16; k++){
        MIDI.sendNoteOff(MIDIdata1[j][i], 0, k);}
     }
   }
}
//smallest, code effective control change panic for newer synths
  for(int i = 1; i<=16; i++){
    MIDI.sendControlChange(123, 0, i);
  }
}

void Metronome_Switch(){
if (digitalRead(pushRotaryPin) !=  pushRotaryState && millis()-trigTime > debounceTime){
    pushRotaryState = !pushRotaryState;
    trigTime = millis();
    if(pushRotaryState == LOW) {
      metronomeActive = !metronomeActive;
    }
}
}

void Metronome_Play(){
if(metronomeActive == true) {
  if (millis()-beatTIME >= beatLenght){
      beatTIME = millis();  
      MIDI.sendNoteOn(METRONOME_NOTE, 70, METRONOME_CHANNEL); // note, velocity, channel
  }
}
}

//rotary encoder handling...
void Opt_Encoder(){
if (millis()-trigTime > rotaryDebounceTime && digitalRead(optAPin) != optA_state){
  optA_state=!optA_state;
  trigTime = millis();
  if (optA_state == HIGH){
      if(digitalRead(optBPin) == HIGH){
        BPM++;
        if(BPM > BPM_MAX){BPM = BPM_MAX;}
      }
      else{
        BPM--;
        if(BPM < BPM_MIN) {BPM = BPM_MIN;}
      }
      beatLenght = 60000/ BPM;
      if(waitingFirstSeq == false /*&& RECORDING == false && OVERDUBBING == false*/){
        Compute_Quantum();
        actStep = lastStep;
        beatTIME = millis(); //this syncronize and stops the execution of the first metronome beat
      }
  }
}
}

void Panic_Switch(){
if (digitalRead(panicPin) !=  panicState && millis()-trigTime > debounceTime){
    panicState = !panicState;
    trigTime = millis();
    if (panicState == LOW){
        PANIC();
        Kill_Stuck_Notes();
    }
}
}

void Undo_Switch(){
if (digitalRead(undoPin) !=  undoState && millis()-trigTime > debounceTime){
    undoState = !undoState;
    trigTime = millis();
    if (undoState == LOW){
        UNDO();
        Kill_Stuck_Notes();
      /*//the following was for timeFrame cycling testing
        timeFrame++;
        if(timeFrame >= TF){timeFrame = 0;}
        beatLenght = loopLenght / beatsPerLoop[timeFrame];
        BPM = 60000/beatLenght;
        actStep = 0;
        beatTIME = millis(); //this syncronize and stops the execution of the first metronome beat
       */
    }
}
}

void UNDO(){
//if(MIDIdata1[0][1] != 0){//do not affect the very first recording (not working)
for(int i = 0; i<MAX_STEPS; i++){
  for(int j = 0; j<MAX_POLY; j++){
     if(MIDItrack[j][i] >= MAX_TRACKS){
         MIDIdata1[j][i] = 0;
         MIDIdata2[j][i] = 0;
     }
   }
}
//}
}

void FULL_LED_Blink(byte count){
for(int i=0; i<count; i++){
  digitalWrite(RECLEDPin, HIGH); 
  digitalWrite(OVDBLEDPin, HIGH);
  for (int j=0; j<MAX_TRACKS; j++){
    digitalWrite(trackLEDPin[j], HIGH);
  }
  delay(200);
  digitalWrite(RECLEDPin, LOW); 
  digitalWrite(OVDBLEDPin, LOW);
  for (int k=0; k<MAX_TRACKS; k++){
    digitalWrite(trackLEDPin[k], LOW);
  }
  delay(200);
}}

void single_LED_Blink(int pin, byte count){
for(int i=0; i<count; i++){
  digitalWrite(pin, LOW); 
  delay(100);
  digitalWrite(pin, HIGH);
  delay(100);
}}

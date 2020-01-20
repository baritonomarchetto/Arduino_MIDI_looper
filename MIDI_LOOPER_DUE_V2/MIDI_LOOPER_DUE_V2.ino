/* differenze dalla versione precedente:
 *  -il led verde indica PLAY
 *  - il led giallo indica RECORD
 *  - temere premuto REC per 3 secondi sulla prima track, resetta
 *  - tenere premuto REC per 3 secondi su qualsiasi altra traccia pulisce la traccia
 *  - tempo massimo di registrazione: 71 minuti circa
 *  - numero massimo di note: 10.000 in layers da massimo 500
 *  - il fu bottone di panic ora attiva/disattiva il MIDI echo dei parametri supportati
 *  
 * 
 *  
 *  by barito 2019
 *  last update: 20 jan 2020
 */

#include <MIDI.h>

#define MAX_TRACKS 5
#define MAX_NOTES 500    //max notes per sequence
#define MAX_SEQUENCES 20 //MAX_NOTES*MAX_SEQUENCES < 10000
#define DISABLE_THRU
#define METRONOME_NOTE 64
#define METRONOME_CHANNEL 10
#define SEND_INT_CLOCK       //uncomment or delete this line if you dont want internal clock to be sent out
#define EXT_CLOCK            //uncomment or delete this line if you dont want clock to be received

MIDI_CREATE_DEFAULT_INSTANCE();

//foot switches
const byte RECPin = 2;
const byte startPin = 3;
const byte clockTrigPin = 22;
//other switches
const byte pushRotaryPin = 23; //metronome on/off
const byte ECHOPin = 24;
const byte undoPin = 25;
const byte trackPin[MAX_TRACKS] = {34, 32, 31, 30, 36};
//optical encoders
const byte optAPin = 4;
const byte optBPin = 5;
//LEDs
const byte RECLEDPin = 6;
const byte PLAYledPin = 7;
const byte trackLEDPin[MAX_TRACKS] = {35, 33, 29, 28, 37};

bool LEDdummyState[MAX_TRACKS] = {LOW, LOW, LOW, LOW, LOW};
bool RECState;
bool startState;
bool ECHOState;
bool undoState;
bool pushRotaryState;
bool optA_state;
bool clockTrigState;
bool trackState[MAX_TRACKS];
bool muteTrack[2*MAX_TRACKS];
unsigned long TblinkTime[MAX_NOTES];
bool waitingFirstNote[MAX_SEQUENCES];
bool waitingFirstSeq;
bool RECORDING;
bool PLAY;
unsigned long TIME;
unsigned long beatTIME;
unsigned long pressRECTime;
unsigned long startLoopTime;
unsigned long endLoopTime;
unsigned long loopLenght;
unsigned long beatLenght;
unsigned long prevBeatLenght;
unsigned long trigTime;
unsigned long clockLenght;
unsigned long clockTime;
unsigned long firstClockTick;
unsigned long firstTrigTick;
byte clockTick;
byte trigCount;
byte incomingClock;
bool metronomeActive = true;
bool MIDI_ECHO = 1;
const int debounceTime = 100;
const int rotaryDebounceTime = 20;
int timeFrame;
int currentTrack;
int currentSequence;
byte MIDIdata1[MAX_NOTES][MAX_SEQUENCES];
byte MIDIdata2[MAX_NOTES][MAX_SEQUENCES];
byte MIDIchannel[MAX_NOTES][MAX_SEQUENCES];
byte noteTrack[MAX_NOTES][MAX_SEQUENCES];
unsigned long noteTime[MAX_NOTES][MAX_SEQUENCES];
int noteSeqCounter[MAX_SEQUENCES];
int noteNumber[MAX_SEQUENCES];

void setup() {
pinMode(RECPin, INPUT_PULLUP);
pinMode(startPin, INPUT_PULLUP);
pinMode(pushRotaryPin, INPUT_PULLUP);
pinMode(optAPin,INPUT_PULLUP);
pinMode(optBPin,INPUT_PULLUP);
pinMode(ECHOPin,INPUT_PULLUP);
pinMode(undoPin,INPUT_PULLUP);
pinMode(clockTrigPin,INPUT_PULLUP);
for(int i=0; i<MAX_TRACKS; i++){pinMode(trackPin[i],INPUT_PULLUP);}
pinMode(RECLEDPin, OUTPUT);
pinMode(PLAYledPin, OUTPUT);
for(int j=0; j<MAX_TRACKS; j++){pinMode(trackLEDPin[j], OUTPUT);}
//intialize MIDI...
MIDI.setHandleNoteOn(Handle_Note_On);
MIDI.setHandleNoteOff(Handle_Note_Off);
MIDI.setHandleControlChange(Handle_CC);
MIDI.setHandlePitchBend(Handle_PB);
MIDI.setHandleStart(Handle_Start);
//MIDI.setHandleStop(Handle_Stop);
#ifdef EXT_CLOCK
MIDI.setHandleClock(Handle_Clock);
#endif
MIDI.begin(MIDI_CHANNEL_OMNI);// start MIDI and listen to ALL MIDI channels
#ifdef DISABLE_THRU
MIDI.turnThruOff();
#endif
RESETMIDI();
}

void loop() {
MIDI.read();        //calls MIDI.handles
#ifdef SEND_INT_CLOCK 
SendClock();
#endif
if(PLAY){
  SequenceNotes();
  REC_Switch();
  Metronome_Switch();
  Metronome_PLAY();
  Opt_Encoder();
  Undo_Switch();
}
TrackMutedLED();
Track_Switch();
Play_Switch();
ECHO_Switch();
clockTrigSwitch();
}

void RESETMIDI(){
RECORDING = false;
trigTime = millis();
//clean ALL MIDI notes tracking
for(int i = 0; i<MAX_NOTES; i++){
  for(int j = 0; j<MAX_SEQUENCES; j++){
    //MIDIdata1[i][j] = 0; //pitch or CC number
    //MIDIdata2[i][j] = 0; //velocity or CC value
    //MIDIchannel[i][j] = 0; //MIDI channel. Channels start from 1 because of the library
    noteTime[i][j] = 4294967295; //MAX unsigned long
  }
}
for(int k = 0; k<MAX_SEQUENCES; k++){
  noteSeqCounter[k] = 0;
  waitingFirstNote[k] = true;
  noteNumber[k]= 0;
}
waitingFirstSeq = true; 
PLAY = true;
FULL_PANIC();
FULL_LED_Blink(5);
digitalWrite(PLAYledPin, HIGH);
digitalWrite(trackLEDPin[0], HIGH);
currentTrack = 0;
currentSequence = 0;
loopLenght = 4294967295; //max unsigned long
beatLenght = 500000; //us
prevBeatLenght = beatLenght;
timeFrame = 16; //beats*bar (4-> 1 beat, 8-> 2 beats, 16-> 4 beats)
RECState = digitalRead(RECPin);
undoState = digitalRead(undoPin);
optA_state = digitalRead(optAPin);
ECHOState = digitalRead(ECHOPin);
startState = digitalRead(startPin);
clockTrigState = digitalRead(clockTrigPin);
//initialize tracks
for(int l = 0; l < MAX_TRACKS; l++){
  muteTrack[l] = false;
}
incomingClock = 0; //0 - no clock, 1 - MIDI clock, 2 - trigger clock
trigCount = 0;
}

////////////////////////////////
//START - MIDI IN message
void Handle_Start(){
PLAY = true;
digitalWrite(PLAYledPin, HIGH);
for(int i = 0; i < MAX_SEQUENCES; i++){
   noteSeqCounter[i] = 0;
}
}

////////////////////////////////
//STOP - MIDI IN message
void Handle_Stop(){
PLAY = false;
digitalWrite(PLAYledPin, LOW);
FULL_PANIC();
}

/////////////////////////////////
//CLOCK
void Handle_Clock(){
if(incomingClock < 2){ //if clock is not coming from the trigger input...
  //using namespace midi;
  if(incomingClock == 1){MIDI.sendRealTime(MIDI_NAMESPACE::Clock);}//echo the clock message
  //if(START == false){//COMPUTE TEMPO ONLY AT REST (TO REDUCE CPU LOAD)
  clockTick++;
  if (clockTick == 1){
    firstClockTick = micros();
  }
  else if (clockTick > 24){//25-1 = 24 = 1 beat
    clockTick = 0;
    //SET the beatlenght from clock
    incomingClock = 1; //0 - no clock, 1 - MIDI clock, 2 - trigger clock
    beatLenght = micros()-firstClockTick;
    if(beatLenght != prevBeatLenght && waitingFirstSeq == false){
      //Compute_Note_Times();
      prevBeatLenght = beatLenght;
    }
  }
}
}

////////////////////////////////////////
//compute note lenghts - NOT WORKING: notes computed from MIDI clock are too fast, while computed from trig are tto slow (1/2 the awaited speed...)
void Compute_Note_Times(){
double multiplier = (double)beatLenght/(double)prevBeatLenght;
loopLenght = loopLenght*multiplier;
for(int i = 0; i<MAX_NOTES; i++){
  for(int j = 0; j<MAX_SEQUENCES; j++){
    if(noteTime[i][j]<4294967295){
      noteTime[i][j] = noteTime[i][j]*multiplier;
    }
  }
}
}

void clockTrigSwitch(){
if (digitalRead(clockTrigPin) != clockTrigState /*&& millis()-trigTime > debounceTime*/){
  clockTrigState =! clockTrigState;
  //trigTime = micros();
  if (clockTrigState == HIGH){ 
    trigCount++;
    if (trigCount == 1){
      firstTrigTick = micros();
    }
    else if(trigCount > 1){
      trigCount = 0;
      incomingClock = 2; //0 - no clock, 1 - MIDI clock, 2 - trigger clock
      beatLenght = micros()-firstTrigTick;
      if(beatLenght != prevBeatLenght && waitingFirstSeq == false){
        //Compute_Note_Times();
        prevBeatLenght = beatLenght;
      }
    }
  }
}
}

////////////////////////////////////////////////////
//when a note-on is received, record the note variables
void Handle_Note_On(byte channel, byte pitch, byte velocity){ //questo handle non vede velocità = 0, per questo è necessario l'handle noteoff
if(MIDI_ECHO){
MIDI.sendNoteOn(pitch, velocity, channel);//echo the message
}
if(RECORDING && muteTrack[currentTrack] == false){ //record note parameters
  if(waitingFirstNote[0] == true) {//the very first sequence must start with a noteOn
    waitingFirstNote[0] = false;
    startLoopTime = micros();
  }
  else if(waitingFirstNote[currentSequence] == true) {
    waitingFirstNote[currentSequence] = false;
  }
  if(noteNumber[currentSequence] < MAX_NOTES && currentSequence < MAX_SEQUENCES){
    MIDIdata1[noteNumber[currentSequence]][currentSequence] = pitch;
    MIDIdata2[noteNumber[currentSequence]][currentSequence] = velocity;
    MIDIchannel[noteNumber[currentSequence]][currentSequence] = channel;
    noteTime[noteNumber[currentSequence]][currentSequence] = micros() - startLoopTime;
    noteTrack[noteNumber[currentSequence]][currentSequence] = currentTrack;
    noteNumber[currentSequence]++;
  }
}
}

////////////////////////////////////////////////////
//when a note-oFF is received, record the note variables
void Handle_Note_Off(byte channel, byte pitch, byte velocity){
if(MIDI_ECHO){
MIDI.sendNoteOn(pitch, 0, channel);//echo the message
//MIDI.sendNoteOff(pitch, 0, channel);//echo the message 
}
if(RECORDING && waitingFirstNote[0] == false && muteTrack[currentTrack] == false){ //record note parameters
  if(waitingFirstNote[currentSequence] == true) {
    waitingFirstNote[currentSequence] = false;
  }
  if(noteNumber[currentSequence] < MAX_NOTES && currentSequence < MAX_SEQUENCES){
    MIDIdata1[noteNumber[currentSequence]][currentSequence] = pitch;
    MIDIdata2[noteNumber[currentSequence]][currentSequence] = 0;
    MIDIchannel[noteNumber[currentSequence]][currentSequence] = channel;
    noteTime[noteNumber[currentSequence]][currentSequence] = micros() - startLoopTime;
    noteTrack[noteNumber[currentSequence]][currentSequence] = currentTrack;
    noteNumber[currentSequence]++;
  }
}
}

////////////////////////////////////////////////////
//when a control change message is received, record the variables
void Handle_CC(byte channel, byte number, byte value){
if(MIDI_ECHO){
MIDI.sendControlChange(number, value, channel); //echo the message
}
if(RECORDING && waitingFirstNote[0] == false && muteTrack[currentTrack] == false){ //record CC parameters
  if(waitingFirstNote[currentSequence] == true) {
    waitingFirstNote[currentSequence] = false;
  }
  if(noteNumber[currentSequence] < MAX_NOTES && currentSequence < MAX_SEQUENCES){
    MIDIdata1[noteNumber[currentSequence]][currentSequence] = number;
    MIDIdata2[noteNumber[currentSequence]][currentSequence] = value;
    MIDIchannel[noteNumber[currentSequence]][currentSequence] = channel+16; //+16 to distinguish notes from control changes
    noteTime[noteNumber[currentSequence]][currentSequence] = micros() - startLoopTime;
    noteTrack[noteNumber[currentSequence]][currentSequence] = currentTrack;
    noteNumber[currentSequence]++;
  }
}
}

////////////////////////////////////////////////////
//when a pitch bend message is received, record the variables
void Handle_PB(byte channel, int bend){
if(MIDI_ECHO){
MIDI.sendPitchBend(bend, channel); //echo the message
}
if(RECORDING  && waitingFirstNote[0] == false && muteTrack[currentTrack] == false){ //record PB parameters
  if(waitingFirstNote[currentSequence] == true) {
    waitingFirstNote[currentSequence] = false;
  }
  if(noteNumber[currentSequence] < MAX_NOTES && currentSequence < MAX_SEQUENCES){
    MIDIdata1[noteNumber[currentSequence]][currentSequence] = bend >> 6; // >>6 = /64
    MIDIchannel[noteNumber[currentSequence]][currentSequence] = channel+32; //+32 to distinguish notes and CCs from pitch bends
    noteTime[noteNumber[currentSequence]][currentSequence] = micros() - startLoopTime;
    noteTrack[noteNumber[currentSequence]][currentSequence] = currentTrack;
    noteNumber[currentSequence]++;
  }
}
}

/////////////////////////////////
//Send internal clock if no external clock incoming (if an internal clock is received, it is immediately echoed)
void SendClock(){
if(incomingClock != 1){
  if(micros()-clockTime >= clockLenght){
    clockTime = micros();
    MIDI.sendRealTime(MIDI_NAMESPACE::Clock);
  }
}
}

void SequenceNotes(){
for(int i = 0; i<MAX_SEQUENCES; i++){
  if(micros() -  startLoopTime >= noteTime[noteSeqCounter[i]][i]){
    if(i != currentSequence){ //this kills the REC ECHO
      if(muteTrack[noteTrack[noteSeqCounter[i]][i]] == false){
        if(MIDIchannel[noteSeqCounter[i]][i]<=16){ //note on/off
          MIDI.sendNoteOn(MIDIdata1[noteSeqCounter[i]][i], MIDIdata2[noteSeqCounter[i]][i], MIDIchannel[noteSeqCounter[i]][i]);
        }
        else if(MIDIchannel[noteSeqCounter[i]][i]<=32){ //control change
          MIDI.sendControlChange(MIDIdata1[noteSeqCounter[i]][i], MIDIdata2[noteSeqCounter[i]][i], MIDIchannel[noteSeqCounter[i]][i]-16);
        }
        else{//<= 48 - pitch bend
          MIDI.sendPitchBend(MIDIdata1[noteSeqCounter[i]][i]<<6, MIDIchannel[noteSeqCounter[i]][i]-32);
        }
      }
    }
    noteSeqCounter[i]++;
  }
}
if(micros() - startLoopTime >= loopLenght){//restart the loop
  for(int j = 0; j<MAX_SEQUENCES; j++){
    noteSeqCounter[j] = 0;
  }
  startLoopTime = micros();
  //if(incomingClock == 0){
    beatTIME = micros();
  //}
  if(waitingFirstNote[currentSequence] == false){
     currentSequence++;
     if(currentSequence >= MAX_SEQUENCES){currentSequence = MAX_SEQUENCES;}
     /*if(currentSequence >= MAX_SEQUENCES){currentSequence = 0;} this part of the code allows the recovery of unused sequences. You must modify the undo function too to re-introduce this
     if(waitingFirstNote[currentSequence] == true){return;}
     else { 
      for(int k = 0; k<MAX_SEQUENCES; k++){
        if(waitingFirstNote[k] == true){
          currentSequence = k;
          return;
        }
      }
     }*/
  }
}
}

void REC_Switch(){
//check on trigger...
if(digitalRead(RECPin) !=  RECState && millis()-trigTime > debounceTime){
   RECState = !RECState;
   trigTime = millis();
   if(RECState == LOW){
     RECORDING = !RECORDING;
     digitalWrite(RECLEDPin, RECORDING);
         if(RECORDING){} //do nothing
         else { //if(RECORDING == false){
           if(waitingFirstSeq){
             loopLenght = micros() - startLoopTime;
             /*if(incomingClock == 0){
              beatLenght = loopLenght/timeFrame;
             }*/
             startLoopTime = micros(); //restart the loop cycle
             if(waitingFirstNote[0] == false){
                waitingFirstSeq = false;
             }
             currentSequence++;
             for(int i = 0; i < MAX_SEQUENCES; i++){
                noteSeqCounter[i] = 0;
             }
           }
         }
    }
}
//always check...
if(RECState == HIGH){pressRECTime = millis();}
else /*if(RECState == LOW)*/{
  if(millis()-pressRECTime > 3200) {
    pressRECTime = millis();
    if(currentTrack == 0) {RESETMIDI();}
    else{
      for(int i = 0; i<MAX_NOTES; i++){
        for(int j = 0; j<MAX_SEQUENCES; j++){
          if(noteTrack[i][j] == currentTrack){
            //MIDIdata1[i][j] = 0; //pitch or CC number
            //MIDIdata2[i][j] = 0; //velocity or CC value
            //MIDIchannel[i][j] = 0; //MIDI channel. Channels start from 1 because of the library
            noteTime[i][j] = 4294967295; //MAX unsigned long
          }
        }
      }
    }
  }
}
}

/*void TFrame_Switch(){
if (digitalRead(tframePin) !=  tframeState && millis()-trigTime > debounceTime){
    tframeState = !tframeState;
    trigTime = millis();
    if (tframeState == LOW && incomingClock == 0){
       timeFrame = timeFrame*2;
       if(timeFrame > 32){timeFrame = 4;}
       beatLenght = loopLenght/timeFrame;
       prevBeatLenght = beatLenght;
    }
}
}*/

void ECHO_Switch(){
if (digitalRead(ECHOPin) !=  ECHOState && millis()-trigTime > debounceTime){
    ECHOState = !ECHOState;
    trigTime = millis();
    if (ECHOState == LOW){
       MIDI_ECHO = !MIDI_ECHO;
    }
}
}

void Undo_Switch(){
if (digitalRead(undoPin) !=  undoState && millis()-trigTime > debounceTime){
    undoState = !undoState;
    trigTime = millis();
    if(undoState == LOW){
      currentSequence--;
      if(currentSequence == 0){RESETMIDI();}
      else{
        Sequence_PANIC();
        for(int i = 0; i<MAX_NOTES; i++){
          //MIDIdata1[i][currentSequence] = 0; //pitch or CC number
          //MIDIdata2[i][currentSequence] = 0; //velocity or CC value
          //MIDIchannel[i][currentSequence] = 0; //MIDI channel. Channels start from 1 because of the library
          noteTime[i][currentSequence] = 4294967295; //max unsigned long
          waitingFirstNote[currentSequence] = true;
          noteNumber[currentSequence] = 0;
        }
      }
   }
}
}

void Play_Switch(){
if (digitalRead(startPin) !=  startState && millis()-trigTime > debounceTime){
    startState = !startState;
    trigTime = millis();
    if(startState == LOW){
      PLAY=!PLAY;
      if(PLAY){
        startLoopTime = micros(); 
        for(int i = 0; i < MAX_SEQUENCES; i++){
           noteSeqCounter[i] = 0;
        }
      }
      else{ //STOP
        Slim_PANIC();
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
      else if(trackState[0]==LOW && trackState[1]==LOW){FULL_PANIC();}
      else if (trackState[i] == LOW){
        if(currentTrack!=i){
          currentTrack = i;
        }
        else{ //if the track button is depressed the second time
          muteTrack[i] = ! muteTrack[i];
          Track_PANIC();
        }
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
      }
  }
}

void TrackMutedLED(){
for(int i = 0; i < MAX_TRACKS; i++){
  if(muteTrack[i] == true && millis() - TblinkTime[i] > 200){
    TblinkTime[i] = millis();
    LEDdummyState[i] = !LEDdummyState[i];
    digitalWrite(trackLEDPin[i], LEDdummyState[i]);
  }
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

void Metronome_PLAY(){
if(metronomeActive == true) {
  if (micros()-beatTIME >= beatLenght){
      beatTIME = micros(); 
      MIDI.sendNoteOn(METRONOME_NOTE, 70, METRONOME_CHANNEL); // note, velocity, channel
  }
}
}

//rotary encoder handling...
void Opt_Encoder(){
if(incomingClock == 0){
  if (digitalRead(optAPin) != optA_state && millis()-trigTime > rotaryDebounceTime){
    optA_state=!optA_state;
    trigTime = millis();
    if (optA_state == HIGH){
        if(digitalRead(optBPin) == HIGH){
          beatLenght = beatLenght - 10000; //SPEED UP
          //if(beatLenght < 240000){beatLenght = 240000;} //us
        }
        else{
          beatLenght = beatLenght + 10000; //SLOW DOWN
          //if(beatLenght > 1500000) {beatLenght = 1500000;} //us
        }
        clockLenght = beatLenght/24;
    }
  }
}
}

void Sequence_PANIC(){
for(int k = 0; k<MAX_NOTES; k++){
    if(noteTrack[k][currentSequence] == currentTrack){
      MIDI.sendNoteOn(MIDIdata1[k][currentSequence], 0, MIDIchannel[k][currentSequence]);
    }
  }
}

void Track_PANIC(){
for(int k = 0; k<MAX_NOTES; k++){
  for(int j = 0; j<MAX_SEQUENCES; j++){
    if(noteTrack[k][j] == currentTrack){
      MIDI.sendNoteOn(MIDIdata1[k][j], 0, MIDIchannel[k][j]);
    }
  }
}
}

void Slim_PANIC(){
for(int k = 0; k<MAX_NOTES; k++){
  for(int j = 0; j<MAX_SEQUENCES; j++){
      MIDI.sendNoteOn(MIDIdata1[k][j], 0, MIDIchannel[k][j]);
    }
  }
}

void FULL_PANIC(){
//full panic for older synths
for(int i = 0; i<127; i++){ //whole MIDI notes range
   for(int j = 0; j<16; j++){ //whole MIDI channels range
     MIDI.sendNoteOn(i, 0, j);
   }
}
}

void MIDI_PANIC(){
//smallest, code effective control change panic for newer synths
for(int i = 1; i<=16; i++){
  MIDI.sendControlChange(123, 0, i);
}
}

void FULL_LED_Blink(byte count){
for(int i=0; i<count; i++){
  digitalWrite(RECLEDPin, HIGH); 
  digitalWrite(PLAYledPin, HIGH);
  for (int j=0; j<MAX_TRACKS; j++){
    digitalWrite(trackLEDPin[j], HIGH);
  }
  delay(200);
  digitalWrite(RECLEDPin, LOW); 
  digitalWrite(PLAYledPin, LOW);
  for (int k=0; k<MAX_TRACKS; k++){
    digitalWrite(trackLEDPin[k], LOW);
  }
  delay(200);
}}

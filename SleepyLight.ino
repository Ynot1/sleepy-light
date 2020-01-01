// https://github.com/Aircoookie/Espalexa for analog values...


/* Alexa controlled Dowdow Arduino Clone for assisting sleep . 
 *  
 *  Based on https://github.com/multiple Wemo switch by ard????
 *  and on https://github.com/yknivag/Arduino-Dodow-Clone/bob/master/DowDowClone
 *   
 *  The purpose of this device is to progessively slow breathing,
 *  the user breathes in as the light gets brighter and out as
 *  it dims.  Each breath gets slightly longer until the
 *  target respiration rate is achieved.
 *  
 *  The commercial Dowdow device slows the breathing from 11 breaths
 *  per minute to 6 breaths per minute over a period of 8 minutes, it also has a 20 min mode.
 *  
 *  This clone allows the duration of the light sequence to be set via either:
 *  a voice command to Alexa "Switch on 8 minute Light sequence" (or 10, or 20)
 *  via a push button connected to a processor pin. I used the RXD pin of the ESP-01
 *  a short press will wink the lED once and start the 8 min cycle, two pushs for 10 min and 3 for 20 min
 *  this same push button can control the brightness up/down via a long push
 *  
 *  
 *  Breathing is reduced over "DEFAULT_REDUCING_TIME_PC"% of the selected run time and the
 *  remaining time is constant at the target rate.
 *  
 *  
 *  Wiring
 *  
 *  An LED (with appropriate resistor) should be connected to the pin
 *  set in "Led_Pin" (this must be a PWM output, labelled as ~ on most
 *  Arduino boards).  If using more than one LED for brightness then connect 
 *  this pin to a transistor or mosfet and connect the LEDs to that.
 *  A push button switch will take the pin Push_Button_Pin low for local control inputs
 */

 
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <functional>
#include "switch.h"
#include "UpnpBroadcastResponder.h"
#include "CallbackFunction.h"
#include "SimpleTimer.h"

// Site specfic variables
const char* ssid = "A_Virtual_Information";
const char* password = "BananaRock";

boolean connectWifi();
boolean wifiConnected = false;

UpnpBroadcastResponder upnpBroadcastResponder;

Switch *Start8minSequence = NULL;
Switch *Start20minSequence = NULL;
//Switch *StopSequence = NULL;
Switch *Start10minSequence = NULL;

//due to a bug in the way Alexa discovers devices, only 4 switches can be reliably detected 

bool min8triggeredflag = LOW;
bool min20triggeredflag = LOW;
bool min10triggeredflag = HIGH;
bool stoptriggeredflag = HIGH;

bool UDPActiveSemaphore = false;
bool UDPRestartTried = false; 
bool DebugMessages = true;
bool RunLedsSemaphore = false;

bool ButtonState = false;
bool PrevButtonState = false;

float total_duration = 0;
float total_duration_run = 0;
float steady_duration =0;
float steady_time_run = 0;
float initial_bpm = 0; 
float target_bpm = 0;
float duration = 0;
float target_duration_ms =0;
int max_brightness = 0;
int this_duration =0;
int TimeSetting = 0; //used to count button pushes
int FlashLEDCount = 0; // used to indicate back to user which runtime is selected via push button
 
const int Led_Pin = 2;  // GPIO2 pin - on board LED of some of the ESP-01 variants.
const int Push_Button_Pin = 3;  // RXD pin on an ESP-01

// the timer object
SimpleTimer timer; // 
int UDPmessageTimer = 0; // Timer id for checking UDP message frequency
int CheckStopWorksTimer = 0; // Timer id for proving the stop function will work - **remove this **
int ShortPushTimer = 0; // Timer id for short push button action
int LongPushTimer = 0; // Timer id for long push button action
int TimeSettingHoldOffTimer = 0; //Holdoff time to ensure user is finished selecting run time

//Set up sleep light options

#define DEFAULT_INITIAL_FREQUENCY 11  //breaths per minute to start from
#define DEFAULT_TARGET_FREQUENCY 6    //breaths per minute to end at

#define DURATION_8MIN 8               //the breathing will be slowed over this number of minutes
#define DURATION_10MIN 10             //the breathing will be slowed over this number of minutes
#define DURATION_20MIN 20             //the breathing will be slowed over this number of minutes
#define DEFAULT_REDUCING_TIME_PC 80   //the device will continue at the target rate for this number of seconds

#define INHALE_PC 20                   //% of each breath for inhale
#define INHALEHOLD_PC 32               //% of each breath for inhale hold
#define EXHALE_PC 38                   //% of each breath for exhale
#define EXHALEHOLD_PC 10               //% of each breath for exhale hold

// popular theory on Google suggests 4:7:8 as ideal ratio for the first 3 parameters above
// translates to 22%, 36%, 42%
// I suggest (no science in this, just a guess) the exhalehold time should be about 1/3 the inhalehold time 
// translates to 20%, 32%, 38%, 10%

#if defined(__AVR__)
  #define PWM_MAX 255                 //Arduino has 8-bit PWM
#elif defined(ESP8266)
  #define PWM_MAX 1023                //ESP8266 has 10-bit PWM
#endif

#define GAP_BRIGHTNESS_PC 30          //Percentage of PWN_RANGE value for dimmest setting (gap between breaths)
//i suspect this doesnt work. still goes out competely at 30% setting.

void setup()
{

  pinMode(Led_Pin, OUTPUT); 

  Serial.begin(9600);


    Serial.println("Booting...");

  // Initialise wifi connection
  wifiConnected = connectWifi();

  if (wifiConnected) {
    Serial.println("flashing slow to indicate wifi connected...");
    //flash slow a few times to indicate wifi connected OK. Only works for some variants of the ESP-01 board
    digitalWrite(Led_Pin, LOW);
    delay(1000);
    digitalWrite(Led_Pin, HIGH);
    delay(1000);
    digitalWrite(Led_Pin, LOW);
    delay(1000);
    digitalWrite(Led_Pin, HIGH);
    delay(1000);
    digitalWrite(Led_Pin, LOW);
    delay(1000);
    digitalWrite(Led_Pin, HIGH);

    Serial.println("starting upnp responder");
    upnpBroadcastResponder.beginUdpMulticast();

    // Define your switches here. Max 10 - I can only get 5 to load reliably, hence the commented out ones below
    // Format: Alexa invocation name, local port no, on callback, off callback, Alexa talley indicator 
    // Make sure youset the same port numbers in the switch.cppmodule
    Start8minSequence  = new Switch("8 minute Sleep Sequence", 85, min8on, min8off, min8triggeredflag);
    Start20minSequence = new Switch("20 minute Sleep Sequence", 86, min20on, min20off, min20triggeredflag);
    //StopSequence       = new Switch("Stop Sleep Sequence", 87, stopon, stopoff, stoptriggeredflag);
    Start10minSequence = new Switch("10 minute Sleep Sequence", 88, min10on, min10off, min10triggeredflag);

    //I THINK SWITCH PORT NUMBERS HAVE TO BE UNIQUE IN THE LAN AS WELL !!!!....
    
    Serial.println("Adding switches upnp broadcast responder");
    upnpBroadcastResponder.addDevice(*Start8minSequence); 
    upnpBroadcastResponder.addDevice(*Start20minSequence);  
    //upnpBroadcastResponder.addDevice(*StopSequence);  
    upnpBroadcastResponder.addDevice(*Start10minSequence);
  }

  digitalWrite(Led_Pin, HIGH); // turn off LED

   Serial.println("Making RX into an INPUT"); // used to detect local user control

   pinMode(Push_Button_Pin, FUNCTION_3);
   pinMode(Push_Button_Pin, INPUT);
  
UDPmessageTimer = timer.setInterval((1000*60*30), UDPmessageTimerTask);// 30 mins
//UDPmessageTimer = timer.setInterval((1000*60*30), UDPmessageTimerTask);// 30 secs for debug/testing puposes

ShortPushTimer = timer.setInterval((300), ShortPushTask);// 300ms
LongPushTimer = timer.setInterval((2000), LongPushTask);// 2 secs
TimeSettingHoldOffTimer = timer.setInterval((5000), TimeSettingHoldOffTask);// 5 secs
 
UDPActiveSemaphore = LOW;

} //void setup end

void loop()
{
  delay(50); // Main loop timer - 
  
  // this is where the "polling" for the timers occurs
  timer.run();

  // Handle the UDP Activity watchdog
  // This will have to be removed for situations with no Alexa control
  
    if (UDPActiveSemaphore == HIGH) { // semaphore flag to indicate normal activity would have been set in either switch.cpp or UpnpBroadcastResponder functions

       timer.restartTimer(UDPmessageTimer);
      if (DebugMessages == true)  { 
        Serial.println("All Good - Dumping UDPmessage timer ...");
       }
       UDPActiveSemaphore = LOW;
       UDPRestartTried = LOW; // reset the flag to reboot at the next fail of the UDPSemaphore
    }
  // Handle the user Push Button

  ButtonState = digitalRead(Push_Button_Pin); 
  delay(100);  
  if (ButtonState == LOW) {

   
      if (PrevButtonState == HIGH){ 
        Serial.println("ButtonState has just closed (Logic LOW )");
      }
  }       
      
  if (ButtonState == HIGH) {
        timer.enable(ShortPushTimer);
        //timer.enable(LongPushTimer);
        timer.restartTimer(ShortPushTimer);
        timer.restartTimer(LongPushTimer);
       
       if (PrevButtonState == LOW){ 
        Serial.println("ButtonState State has just opened (Logic HIGH)");
        
      }
  }      


   PrevButtonState = ButtonState;   // remember state for next pass
   
 
  if (wifiConnected) {

  upnpBroadcastResponder.serverLoop();


   Start8minSequence->serverLoop();
   Start20minSequence->serverLoop();
   //StopSequence->serverLoop();
   Start10minSequence->serverLoop();

  }
  // Handle the run light sequence semaphore detection
  
  if (RunLedsSemaphore == HIGH) {
               
       if (DebugMessages == true)  { 
          Serial.println("Calling executeLEDsequence");
        }
        executeLEDsequence();
         
    // this code here, as a delay of >5 sec in the routines that run
    // as each control activates makes alexa think the devices are unresponsive
    // in this way, the alexa response is sent quickly, and the slower functions
    // is done outside that loop

    RunLedsSemaphore = LOW;
  }
    //Shut everything off

    
       min8triggeredflag = LOW;
       min20triggeredflag = LOW;
       min10triggeredflag = LOW;
       stoptriggeredflag = LOW;  
           
      analogWrite(Led_Pin, 0);
      digitalWrite(Led_Pin, HIGH);
  
} // Void loop end

//Enhanced Fade Function

void EnhancedFadeLed(int pin, int duration_of_fade, int low_value, int high_value)  {

  int period_time = duration_of_fade / 6;
  int Inhale_duration = period_time * (INHALE_PC / 10.0);
  int InhaleHold_duration = period_time * (INHALEHOLD_PC / 10.0);
  int Exhale_duration = period_time * (EXHALE_PC / 10.0);
  int ExhaleHold_duration = period_time * (EXHALEHOLD_PC / 10.0);
 
  int inhale_delay_time = (Inhale_duration * 2)/ (high_value - low_value);
  int exhale_delay_time = (Exhale_duration * 2)/ (high_value - low_value);

  //Inhale
    if (DebugMessages == true) {

    Serial.print("Inhale_duration: ");
    Serial.print(Inhale_duration);
    Serial.println("ms.");
  }
 for (int j=(high_value - 1); j>low_value; j--) {
    analogWrite(pin, j);
    delay(inhale_delay_time);
  }
  //Inhale Hold
  if (DebugMessages == true) {

    Serial.print("InhaleHold_duration: ");
    Serial.print(InhaleHold_duration);
    Serial.println("ms.");

  }
  delay(InhaleHold_duration);
  
  //Exhale
     if (DebugMessages == true) {

    Serial.print("Exhale_duration: ");
    Serial.print(Exhale_duration);
    Serial.println("ms.");

  }  
  for (int i=(low_value - 1); i<high_value; i++) {
    analogWrite(pin, i);
    delay(exhale_delay_time);
  }
  
  //Exhale Hold
    if (DebugMessages == true) {

    Serial.print("ExhaleHold_duration: ");
    Serial.print(ExhaleHold_duration);
    Serial.println("ms.");
    Serial.println("");
  }
  delay(ExhaleHold_duration);
}

// Execute Function

void executeLEDsequence(){

  //Set up some maths to calculate how quickly to change the delay in each fade.

  float reducing_duration = duration * (DEFAULT_REDUCING_TIME_PC / 100.0);
  steady_duration = duration - reducing_duration;
  steady_time_run = 0; 
  
  float initial_duration_ms = (60 / initial_bpm) * 1000;
  target_duration_ms = (60 / target_bpm) * 1000;

  float average_duration_ms = (initial_duration_ms + target_duration_ms) / 2;
  float number_of_reducing_breaths = round (reducing_duration / (average_duration_ms / 1000));
  int reducing_breath_count = 1;
  float change_in_duration_ms = round((target_duration_ms - initial_duration_ms) / number_of_reducing_breaths);

  int min_brightness = max_brightness * (GAP_BRIGHTNESS_PC / 100.0);


  //If the DebugMessages flag is enabled, output data on the console
 if (DebugMessages == true) {
    total_duration = 0;
    Serial.begin(9600);
    Serial.println("DowDow Arduino Clone");
    Serial.println("====================");
    Serial.println();
    
    Serial.println("Initial Settings");
    Serial.println("----------------");
    Serial.println();
    Serial.print("Initial Frequency (bpm): ");
    Serial.println(initial_bpm);
    Serial.print("Target Frequency (bpm): ");
    Serial.println(target_bpm);
    Serial.print("Duration (minutes): ");
    Serial.println(duration / 60);
    Serial.print("Duration (seconds): ");
    Serial.println(duration);
    Serial.println();
    Serial.println("Calculated Settings");
    Serial.println("-------------------");
    Serial.println();
    float reduction_to_stable_fraction = DEFAULT_REDUCING_TIME_PC / 100.0;
    Serial.print("Reduction to Stable Ratio: ");
    Serial.println(reduction_to_stable_fraction);
    Serial.print("Reducing duration (s): ");
    Serial.println(reducing_duration);
    Serial.print("Steady duration (s): ");
    Serial.println(steady_duration);
    Serial.print("Initial breath duration (ms): ");
    Serial.println(initial_duration_ms);
    Serial.print("Target breath duration (ms): ");
    Serial.println(target_duration_ms);
    Serial.print("Average breath duration (ms): ");
    Serial.println(average_duration_ms);
    Serial.print("Total number of reducing breaths required: ");
    Serial.println(number_of_reducing_breaths);
    Serial.print("Breath duration delta (ms): ");
    Serial.println(change_in_duration_ms);
    float min_brightness_fraction = (GAP_BRIGHTNESS_PC / 100.0);
    Serial.print("Min to Max Brightness Ratio: ");
    Serial.println(min_brightness_fraction);
    Serial.print("Max Brightness: ");
    Serial.println(max_brightness);
    Serial.print("Min Brightness: ");
    Serial.println(min_brightness);
    Serial.println();
    Serial.print("Extra time duration (s): ");
    Serial.println(steady_duration);
    Serial.println();
    Serial.println("Running now");
    Serial.println("-----------");
    Serial.println();
 }

  //We're done seting up, lets start running

  this_duration = initial_duration_ms;
  
  while (this_duration <= target_duration_ms) {
    //If the DebugMessages flag is enabled, output data on the console
    if (DebugMessages == true) {
      Serial.print("Reducing Breath ");
      Serial.print(reducing_breath_count);
      Serial.print(" of ");
      Serial.println(number_of_reducing_breaths);
      Serial.print("This breath cycle duration: ");
      Serial.print(this_duration);
      Serial.println("ms.");
      total_duration = total_duration + this_duration;
    }

      EnhancedFadeLed(Led_Pin, this_duration, min_brightness, max_brightness);
      this_duration = this_duration + change_in_duration_ms;
      reducing_breath_count ++;

       //tried putting a call to the serverLoop here to cause a status update to alexa
       // this put a variable delay in the light sequence
       //probably waiting for an alaexa responce
       // having alexastatus out of step is a lesser evil

  }

  //If the DebugMessages flag is enabled, output data on the console
  if (DebugMessages == true) {
    Serial.println();
    Serial.println("Run period stats");
    Serial.println("----------------");
    Serial.println();
    Serial.print("Total reducing period was: ");
    Serial.print(total_duration);
    Serial.print(" ms = ");
    Serial.print(total_duration/1000);
    Serial.print(" s = ");
    Serial.print(total_duration/60000);
    Serial.println(" minutes.");
  }

  //Run the extra piece of stable rate breathing at the end...
  
  while (steady_time_run<steady_duration) {
    EnhancedFadeLed(Led_Pin, target_duration_ms, min_brightness, max_brightness);
    steady_time_run = steady_time_run + (target_duration_ms / 1000);
    if (DebugMessages == true) {
      Serial.print("This breath cycle duration: ");
      Serial.print(this_duration);
      Serial.println("ms.");
      total_duration = total_duration + this_duration;
    }

   }

  //If the DebugMessages flag is enabled, output data on the console
  if (DebugMessages == true){
    Serial.print("Steady time run: ");
    Serial.print(steady_time_run);
    Serial.print(" s = ");
    Serial.print(steady_time_run / 60);
    Serial.println(" minutes.");
    Serial.println();
    total_duration_run = (total_duration/1000) + steady_time_run;
    Serial.print("Total running time: ");
    Serial.print(total_duration_run);
    Serial.print(" s = ");
    Serial.print(total_duration_run / 60);
    Serial.println(" minutes.");

    Serial.println("Finished SleepLight cycle ...");
  }
    //Shut everything off

    
      min8triggeredflag = LOW;
      min20triggeredflag = LOW;
      min10triggeredflag = LOW;
      stoptriggeredflag = LOW; 
     
  analogWrite(Led_Pin, 0);
  digitalWrite(Led_Pin, HIGH);

} // end of executeLEDsequence

void TimeSettingHoldOffTask() {
  if (DebugMessages == true)  { 
      Serial.println("TimeSettingHoldOffTask Tripped");
      Serial.println("TimeSetting:");
      Serial.println(TimeSetting); 
  }
      timer.disable(TimeSettingHoldOffTimer);

      if (TimeSetting > 0) {
        Serial.println("Invoking Light Sequence from Push Button");
        switch (TimeSetting) {
      case 1:
            min8triggeredflag = HIGH; // set the call back indicator to ON to show 8 min sequence in progress.
            min20triggeredflag = LOW;
            min10triggeredflag = LOW;
            stoptriggeredflag = LOW;
    
            initial_bpm = DEFAULT_INITIAL_FREQUENCY; 
            target_bpm = DEFAULT_TARGET_FREQUENCY;
            duration = DURATION_8MIN * 60.0;
            max_brightness = PWM_MAX;  
        
            RunLedsSemaphore = HIGH; //set the flag so void runloop will call the function to run the LEDS.
            TimeSetting = 0;
            
      break;
      case 2:
            min8triggeredflag = LOW; // set the call back indicator to ON to show 8 min sequence in progress.
            min20triggeredflag = LOW;
            min10triggeredflag = HIGH;
            stoptriggeredflag = LOW;
    
            initial_bpm = DEFAULT_INITIAL_FREQUENCY; 
            target_bpm = DEFAULT_TARGET_FREQUENCY;
            duration = DURATION_10MIN * 60.0;
            max_brightness = PWM_MAX;  
        
            RunLedsSemaphore = HIGH; //set the flag so void runloop will call the function to run the LEDS.
            TimeSetting = 0;         
      break;
      case 3:
            min8triggeredflag = LOW; // set the call back indicator to ON to show 8 min sequence in progress.
            min20triggeredflag = HIGH;
            min10triggeredflag = LOW;
            stoptriggeredflag = LOW;
    
            initial_bpm = DEFAULT_INITIAL_FREQUENCY; 
            target_bpm = DEFAULT_TARGET_FREQUENCY;
            duration = DURATION_20MIN * 60.0;
            max_brightness = PWM_MAX;  
        
            RunLedsSemaphore = HIGH; //set the flag so void runloop will call the function to run the LEDS.
            TimeSetting = 0; 
      break;
      
      }
    }
}


void ShortPushTask() {
  if (DebugMessages == true)  { 
      Serial.println("ShortPush Timer Tripped"); 
    }
    // disables the specified timer
    timer.disable(ShortPushTimer);

    TimeSetting = TimeSetting +1;

    if (TimeSetting > 3) {
      TimeSetting = 0;
    }

    if (TimeSetting > 0) {
         for (FlashLEDCount=0; FlashLEDCount < TimeSetting; FlashLEDCount++){
          Serial.println("looping FlashLEDCount");
          digitalWrite(Led_Pin, LOW);
          delay(500);
          digitalWrite(Led_Pin, HIGH);
          delay(500);
   } 
 }
    
    Serial.println("TimeSetting:");
    Serial.println(TimeSetting);
    timer.enable(TimeSettingHoldOffTimer);
    timer.restartTimer(TimeSettingHoldOffTimer);
}


void LongPushTask() {
  if (DebugMessages == true)  { 
      Serial.println("LongPush Timer Tripped"); 
    }
    // disables the specified timer
    timer.disable(LongPushTimer);
}

void UDPmessageTimerTask() {

  if (UDPRestartTried == HIGH) { // we have been here before, time for a restart this time...

   if (DebugMessages == true)  { 
      Serial.println("here if the second attempt to restart comms - Regular UDP messages havent occured as expected, so reboot"); 
    }
     ESP.reset();     
    
  }
  
 if (DebugMessages == true)  { 
    Serial.println("here if the first attempt to restart comms - Regular UDP messages havent occured as expected, so restart wifi"); 
  }

   WiFi.persistent(false);      
    WiFi.disconnect();          
    WiFi.persistent(true);
   
   wifiConnected = connectWifi();

   UDPRestartTried = HIGH; // set the flag to reboot the next time we enter here
   timer.restartTimer(UDPmessageTimer);
  if (DebugMessages == true)  { 
    Serial.println("setting UDPRestartTried flag & Dumping UDPmessage timer ...");
   }
   UDPActiveSemaphore = LOW;
       
}

bool min8on() {
 if (DebugMessages == true)  { 
    Serial.println("Request to trigger 8 min sequence received ...");
  }

    min8triggeredflag = HIGH; // set the call back indicator to ON to show 8 min sequence in progress.
      min20triggeredflag = LOW;
      min10triggeredflag = LOW;
      stoptriggeredflag = LOW;
    
    initial_bpm = DEFAULT_INITIAL_FREQUENCY; 
    target_bpm = DEFAULT_TARGET_FREQUENCY;
    duration = DURATION_8MIN * 60.0;
    max_brightness = PWM_MAX;  

   RunLedsSemaphore = HIGH; //set the flag so void runloop will call the function to run the LEDS.
   
  return min8triggeredflag;
}

bool min8off() {

  if (DebugMessages == true)  { 
    Serial.println("Request to trigger 8 min sequence received ...");
  }

    min8triggeredflag = LOW; // set the call back indicator to ON to show 8 min sequence in progress.
      min20triggeredflag = LOW;
      min10triggeredflag = LOW;
      stoptriggeredflag = LOW;
    
    initial_bpm = DEFAULT_INITIAL_FREQUENCY; 
    target_bpm = DEFAULT_TARGET_FREQUENCY;
    duration = DURATION_8MIN * 60.0;
    max_brightness = PWM_MAX;  

   RunLedsSemaphore = HIGH; //set the flag so void runloop will call the function to run the LEDS.
   
  return min8triggeredflag;
}

bool min20on() {
 if (DebugMessages == true)  { 
    Serial.println("Request to trigger 20 min sequence received ...");
  }

      min20triggeredflag = HIGH; // set the call back indicator to ON to show 20 min sequence in progress.
      min8triggeredflag = LOW;
      min10triggeredflag = LOW;
      stoptriggeredflag = LOW;
      
    initial_bpm = DEFAULT_INITIAL_FREQUENCY; 
    target_bpm = DEFAULT_TARGET_FREQUENCY;
    duration = DURATION_20MIN * 60.0;
    max_brightness = PWM_MAX;  

    RunLedsSemaphore = HIGH; //set the flag so void runloop will call the function to run the LEDS.
      
  return min20triggeredflag;
}

bool min20off() {
if (DebugMessages == true)  { 
    Serial.println("Request to trigger 20 min sequence received ...");
  }

      min20triggeredflag = HIGH; // set the call back indicator to ON to show 20 min sequence in progress.
      min8triggeredflag = LOW;
      min10triggeredflag = LOW;
      stoptriggeredflag = LOW;
      
    initial_bpm = DEFAULT_INITIAL_FREQUENCY; 
    target_bpm = DEFAULT_TARGET_FREQUENCY;
    duration = DURATION_20MIN * 60.0;
    max_brightness = PWM_MAX;  

    RunLedsSemaphore = HIGH; //set the flag so void runloop will call the function to run the LEDS.
      
  return min20triggeredflag;
}

bool min10on() {
 if (DebugMessages == true)  { 
    Serial.println("Request to trigger 10 min sequence received ...");
  }

    min10triggeredflag = HIGH; // set the call back indicator to ON to show 10 min sequence in progress.
    min8triggeredflag = LOW;
    min20triggeredflag = LOW;
    stoptriggeredflag = LOW;
    
    initial_bpm = DEFAULT_INITIAL_FREQUENCY; 
    target_bpm = DEFAULT_TARGET_FREQUENCY;
    duration = DURATION_10MIN * 60.0;
    max_brightness = PWM_MAX;  

    RunLedsSemaphore = HIGH; //set the flag so void runloop will call the function to run the LEDS.
      
  return min10triggeredflag;
}

bool min10off() {

if (DebugMessages == true)  { 
    Serial.println("Request to trigger 10 min sequence received ...");
  }

    min10triggeredflag = HIGH; // set the call back indicator to ON to show 10 min sequence in progress.
    min8triggeredflag = LOW;
    min20triggeredflag = LOW;
    stoptriggeredflag = LOW;
    
    initial_bpm = DEFAULT_INITIAL_FREQUENCY; 
    target_bpm = DEFAULT_TARGET_FREQUENCY;
    duration = DURATION_10MIN * 60.0;
    max_brightness = PWM_MAX;  

    RunLedsSemaphore = HIGH; //set the flag so void runloop will call the function to run the LEDS.
      
  return min10triggeredflag;
}
bool stopon() {
 if (DebugMessages == true)  { 
    Serial.println("Request to trigger stop sequence recieved ...");
    Serial.println("set duration run timers to terminal count");
  }
      stoptriggeredflag = HIGH; // set the call back indicator to ON to show stop sequence in progress.
      min8triggeredflag = LOW;
      min20triggeredflag = LOW;
      min10triggeredflag = LOW;
      
      //set duration run timers to terminal count 
      steady_time_run = steady_duration;
      this_duration = target_duration_ms;
      
  return stoptriggeredflag;
}

bool stopoff() {
if (DebugMessages == true)  { 
    Serial.println("Request to trigger stop sequence recieved ...");
    Serial.println("set duration run timers to terminal count");
  }
      stoptriggeredflag = HIGH; // set the call back indicator to ON to show stop sequence in progress.
      min8triggeredflag = LOW;
      min20triggeredflag = LOW;
      min10triggeredflag = LOW;
      
      //set duration run timers to terminal count 
      steady_time_run = steady_duration;
      this_duration = target_duration_ms;
      
  return stoptriggeredflag;
}

// connect to wifi – returns true if successful or false if not
boolean connectWifi() {
  boolean state = true;
  int i = 0;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");
  Serial.println("Connecting to WiFi Network");

  // Wait for connection
  Serial.print("Connecting ...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(2000);
    Serial.print(".");
    if (i > 10) {
      state = false;
      break;
    }
    i++;
  }

  if (state) {
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
  else {
    Serial.println("");
    Serial.println("Connection failed. Bugger");

    

  }

  return state;
}







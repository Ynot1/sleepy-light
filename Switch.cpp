#include "Switch.h"
#include "CallbackFunction.h"


extern bool min8triggeredflag ;
extern bool min20triggeredflag ;
extern bool stoptriggeredflag ;
extern bool min10triggeredflag ;

extern bool UDPActiveSemaphore ;
extern bool DebugMessages ;

    
//<<constructor>>
Switch::Switch(){
   if (DebugMessages == true) { 
    Serial.println("default constructor called");
   }
}
//Switch::Switch(String alexaInvokeName,unsigned int port){
Switch::Switch(String alexaInvokeName, unsigned int port, CallbackFunction oncb, CallbackFunction offcb, bool AlexaIndicator){
    uint32_t chipId = ESP.getChipId();
    char uuid[64];
    // make this string unique in the LAN
    //sprintf_P(uuid, PSTR("38323636-4558-4dda-9188-cda0e6%02x%02x%02x"), // as released in GitHub
    //sprintf_P(uuid, PSTR("78323644-4668-3dda-9688-cda0e6%02x%02x%02x"), // as used by KittyFeeder
    sprintf_P(uuid, PSTR("75783621-4338-2dda-4588-cda0e6%02x%02x%02x"), // as used by SleepyLight
    
          (uint16_t) ((chipId >> 16) & 0xff),
          (uint16_t) ((chipId >>  8) & 0xff),
          (uint16_t)   chipId        & 0xff);
    
    serial = String(uuid);
    persistent_uuid = "Socket-1_0-" + serial+"-"+ String(port);
        
    device_name = alexaInvokeName;
    localPort = port;
    onCallback = oncb;
    offCallback = offcb;
     
    startWebServer();
}


 
//<<destructor>>
Switch::~Switch(){/*nothing to destruct*/}

void Switch::serverLoop(){
    if (server != NULL) {
        server->handleClient();
        delay(1);
    }
}

void Switch::startWebServer(){
  server = new ESP8266WebServer(localPort);

  server->on("/", [&]() {
    handleRoot();
  });
 

  server->on("/setup.xml", [&]() {
    handleSetupXml();
  });

  server->on("/upnp/control/basicevent1", [&]() {
    handleUpnpControl();
  });

  server->on("/eventservice.xml", [&]() {
    handleEventservice();
  });

  //server->onNotFound(handleNotFound);
  server->begin();

  if (DebugMessages == true) { 
    Serial.println("WebServer started on port: ");
  
  Serial.println(localPort);
  }
}
 
void Switch::handleEventservice(){
  if (DebugMessages == true) { 
    Serial.println(" ########## Responding to eventservice.xml ... ########\n");
  }
  
  String eventservice_xml = "<scpd xmlns=\"urn:Belkin:service-1-0\">"
        "<actionList>"
          "<action>"
            "<name>SetBinaryState</name>"
            "<argumentList>"
              "<argument>"
                "<retval/>"
                "<name>BinaryState</name>"
                "<relatedStateVariable>BinaryState</relatedStateVariable>"
                "<direction>in</direction>"
                "</argument>"
            "</argumentList>"
          "</action>"
          "<action>"
            "<name>GetBinaryState</name>"
            "<argumentList>"
              "<argument>"
                "<retval/>"
                "<name>BinaryState</name>"
                "<relatedStateVariable>BinaryState</relatedStateVariable>"
                "<direction>out</direction>"
                "</argument>"
            "</argumentList>"
          "</action>"
      "</actionList>"
        "<serviceStateTable>"
          "<stateVariable sendEvents=\"yes\">"
            "<name>BinaryState</name>"
            "<dataType>Boolean</dataType>"
            "<defaultValue>0</defaultValue>"
           "</stateVariable>"
           "<stateVariable sendEvents=\"yes\">"
              "<name>level</name>"
              "<dataType>string</dataType>"
              "<defaultValue>0</defaultValue>"
           "</stateVariable>"
        "</serviceStateTable>"
        "</scpd>\r\n"
        "\r\n";
          
    server->send(200, "text/plain", eventservice_xml.c_str());
}



void Switch::sendRelayState() {
  String body = 
      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>\r\n"
      "<u:GetBinaryStateResponse xmlns:u=\"urn:Belkin:service:basicevent:1\">\r\n"
      "<BinaryState>";
      
  body += (switchStatus ? "1" : "0");
  
  body += "</BinaryState>\r\n"
      "</u:GetBinaryStateResponse>\r\n"
      "</s:Body> </s:Envelope>\r\n";
  
  server->send(200, "text/xml", body.c_str());
  if (DebugMessages == true) { 

  Serial.print("SendRelayState is Sending :");
  Serial.println(switchStatus);
  }
}
 
void Switch::handleUpnpControl(){
  if (DebugMessages == true) { 
    Serial.println("########## Responding to  /upnp/control/basicevent1 . ########"); // Alexa does this every few , about 4minutes     
  }

  UDPActiveSemaphore = HIGH; // set the flag to be acted upon in main loop

      
  //for (int x=0; x <= HTTP.args(); x++) {
  //  Serial.println(HTTP.arg(x));
  //}

  String request = server->arg(0);      
//  Serial.print("request:");
//  Serial.println(request);
  

  if(request.indexOf("SetBinaryState") >= 0) {
    if(request.indexOf("<BinaryState>1</BinaryState>") >= 0) {
        if (DebugMessages == true) { 
          Serial.println("Got Turn on request");
        }
        switchStatus = onCallback();
  if (DebugMessages == true) { 
    Serial.print("SetBinaryState is Sending :");
  Serial.println(switchStatus);
    }
  
        sendRelayState();
    }

    if(request.indexOf("<BinaryState>0</BinaryState>") >= 0) {
        if (DebugMessages == true) { 
          Serial.println("Got Turn off request");
        }
        switchStatus = offCallback();
        
        sendRelayState();
    }
  }

  if(request.indexOf("GetBinaryState") >= 0) {
    if (DebugMessages == true) { 
      Serial.println("Got binary state request");
          Serial.println("its localPort is:");
    Serial.println(localPort);
    }
     


      switch (localPort) {
      case 85:
            switchStatus = min8triggeredflag;
      break;
      case 86:
            switchStatus = min20triggeredflag;            
      break;
      case 87:
            switchStatus = stoptriggeredflag;
      break;
      case 88:
            switchStatus = min10triggeredflag;
      break;
    
  }
       
    sendRelayState();
  }
  
  server->send(200, "text/plain", "");
}

void Switch::handleRoot(){
  server->send(200, "text/plain", "You should tell Alexa to discover devices");
}

void Switch::handleSetupXml(){
 // Serial.println(" ########## Responding to setup.xml ... ########\n");
  
  IPAddress localIP = WiFi.localIP();
  char s[16];
  sprintf(s, "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);


  String setup_xml = "<?xml version=\"1.0\"?>"
            "<root>"
             "<device>"
                "<deviceType>urn:Belkin:device:controllee:1</deviceType>"
                "<friendlyName>"+ device_name +"</friendlyName>"
                "<manufacturer>Belkin International Inc.</manufacturer>"
                "<modelName>Socket</modelName>"
                "<modelNumber>3.1415</modelNumber>"
                "<modelDescription>Belkin Plugin Socket 1.0</modelDescription>\r\n"
                "<UDN>uuid:"+ persistent_uuid +"</UDN>"
                // Make this sting unique
                //original as used in GitHub # "<serialNumber>221517K0101769</serialNumber>"
                // as used by KittyFeeder "<serialNumber>271517K0101001</serialNumber>"
                "<serialNumber>271517K0106721</serialNumber>" // As used by SleepyTimer
                "<binaryState>0</binaryState>"
                "<serviceList>"
                  "<service>"
                      "<serviceType>urn:Belkin:service:basicevent:1</serviceType>"
                      "<serviceId>urn:Belkin:serviceId:basicevent1</serviceId>"
                      "<controlURL>/upnp/control/basicevent1</controlURL>"
                      "<eventSubURL>/upnp/event/basicevent1</eventSubURL>"
                      "<SCPDURL>/eventservice.xml</SCPDURL>"
                  "</service>"
              "</serviceList>" 
              "</device>"
            "</root>\r\n"
            "\r\n";
 
        
    server->send(200, "text/xml", setup_xml.c_str());
    
   //Serial.print("Sending :");
   //Serial.println(setup_xml);
}

String Switch::getAlexaInvokeName() {
    return device_name;
}



void Switch::respondToSearch(IPAddress& senderIP, unsigned int senderPort) {
      if (DebugMessages == true) { 
  
    Serial.println(localPort);
  Serial.println("");
  Serial.print("Sending response to ");
  Serial.println(senderIP);
  Serial.print("Port : ");
  Serial.println(senderPort);

      }
  IPAddress localIP = WiFi.localIP();
  char s[16];
  sprintf(s, "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);

  String response = 
       "HTTP/1.1 200 OK\r\n"
       "CACHE-CONTROL: max-age=86400\r\n"
       "DATE: Sat, 26 Nov 2016 04:56:29 GMT\r\n"
       "EXT:\r\n"
       "LOCATION: http://" + String(s) + ":" + String(localPort) + "/setup.xml\r\n"
       "OPT: \"http://schemas.upnp.org/upnp/1/0/\"; ns=01\r\n"
       // Make this string unique
       //"01-NLS: b9200ebb-736d-4b93-bf03-835149d13983\r\n" // As released in GitHub
       //"01-NLS: a8200ebb-735e-4b91-bf09-835149d13001\r\n" // As used by KittyFeeder
       "01-NLS: c4200ebb-865e-4b92-bf08-835149d13010\r\n" // As used by SleepyTime
       "SERVER: Unspecified, UPnP/1.0, Unspecified\r\n"
       "ST: urn:Belkin:device:**\r\n"
      // "USN: uuid:" + persistent_uuid + "::urn:Belkin:device:**\r\n"
       "USN: uuid:" + persistent_uuid + "::upnp:rootdevice\r\n"
       "X-User-Agent: redsonic\r\n\r\n";

  UDP.beginPacket(senderIP, senderPort);
  UDP.write(response.c_str());
  UDP.endPacket();                    
if (DebugMessages == true) { 
   Serial.println("Response sent !");
}
}

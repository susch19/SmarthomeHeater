#include <vector>
#include <algorithm>
#include <sstream>
#include <Heater.hpp>
using namespace std;



Heater heater("Firmware V2 " __DATE__ );

void setup()
{
  Serial.begin(115200);
  pinMode(Heater::HEATERPIN, OUTPUT);
  digitalWrite(Heater::HEATERPIN, HIGH);
  
  heater.setup(true);
  // mesh.setContainsRoot(true);
  // mesh.setDebugMsgTypes(ERROR | COMMUNICATION);
  // mesh.initOTA(typeOfNode);
  // mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT, WIFI_STA, 6); //,
  // mesh.onReceive(&receivedCallback);
  // mesh.onNewConnection(&newConnectionCallback);

  // // userScheduler.addTask(clockTask);
  // // clockTask.enable();
  // userScheduler.addTask(taskUpdateTemp);
  // taskUpdateTemp.enable();
}

void loop()
{
  heater.loop();
}


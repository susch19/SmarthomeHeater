
#include <vector>
#include <algorithm>
#include <sstream>
#include <Heater.hpp>
using namespace std;

Heater heater;
void setup()
{
  Serial.begin(115200);
  pinMode(Heater::HEATERPIN, OUTPUT);
  digitalWrite(Heater::HEATERPIN, HIGH);
  heater.setup("heater", 21, true);
  heater.maxLogSize = 512;
}

void loop()
{
  heater.loop();
  // Serial.printf("DRAM free: %6d bytes\r\n", ESP.getFreeHeap());
}

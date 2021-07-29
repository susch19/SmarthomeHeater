
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
  heater.setup("heater", 15, true);

}

void loop()
{
  heater.loop();
}


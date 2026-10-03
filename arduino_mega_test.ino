void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
}

void loop() {
  static int s = 0, r = 0, t = 50;
  s = (s + 1) % 200;
  r = (r + 50) % 8000;
  t = 80 + (sin(millis() / 1000.0) * 10);
  
  Serial1.printf("SPEED:%d,RPM:%d,TEMP:%d\n", s, r, (int)t);
  Serial.printf("Sent -> SPEED:%d,RPM:%d,TEMP:%d\n", s, r, (int)t);
  
  delay(100);
}

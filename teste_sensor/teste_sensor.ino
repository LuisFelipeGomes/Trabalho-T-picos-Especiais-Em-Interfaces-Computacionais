// Teste isolado HC-SR04 - sem RF
// Testa pinos A0, A1, A2, A3, D2, D3 automaticamente

struct Config { int trig; int echo; const char* nome; };

Config configs[] = {
  {A2, A3, "TRIG=A2 ECHO=A3"},
  {A3, A2, "TRIG=A3 ECHO=A2"},
  {A0, A1, "TRIG=A0 ECHO=A1"},
  {2,  3,  "TRIG=D2 ECHO=D3"},
  {3,  2,  "TRIG=D3 ECHO=D2"},
};

float medir(int trig, int echo) {
  pinMode(trig, OUTPUT);
  pinMode(echo, INPUT);
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 30000);
  if (dur == 0) return 9999.0;
  return dur / 58.0;
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== TESTE HC-SR04 ===");
}

void loop() {
  for (auto& c : configs) {
    float d = medir(c.trig, c.echo);
    Serial.print(c.nome);
    Serial.print(" -> ");
    Serial.print(d);
    Serial.println(" cm");
  }
  Serial.println("---");
  delay(1000);
}

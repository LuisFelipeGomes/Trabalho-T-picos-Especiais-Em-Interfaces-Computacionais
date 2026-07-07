#include <SPI.h>
#include "RF24.h"

// Sensor Ultrassonico HC-SR04 - ENTRADA
#define TRIG_PIN A2
#define ECHO_PIN A3

// NRF24
#define CE_PIN 7
#define CSN_PIN 8

// Tipos de mensagens para o MACAW
#define MSG_RTS   1
#define MSG_CTS   2
#define MSG_DADOS 3
#define MSG_ACK   4

#define ID_ENTRADA 47

// Distancia abaixo da qual consideramos que tem gente passando no sensor
#define LIMIAR_CM 15.0

// Struct Pacote - sem a variavel de controle (verif)
struct Pacote {
  uint8_t tipo;
  uint8_t id;
  float distancia;
};

Pacote dados;
RF24 radio(CE_PIN, CSN_PIN);
uint64_t address = 0x3030303030LL;


void setup() {
  Serial.begin(115200);
  Serial.println("=== TRANSMISSOR ENTRADA (HC-SR04) ===");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  randomSeed(analogRead(A2));

  if (!radio.begin()) {
    Serial.println("NRF24 nao encontrado!");
    while (1);
  }

  radio.setPALevel(RF24_PA_LOW);
  radio.setChannel(12);
  radio.setPayloadSize(sizeof(Pacote));
  radio.setAutoAck(false);
  radio.setCRCLength(RF24_CRC_DISABLED);
  radio.setDataRate(RF24_250KBPS);
  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address);
  radio.stopListening();

  delay(100);
  Serial.println("Radio iniciado!");
}

// ── Le o HC-SR04 e devolve a distancia em cm ──────────────────────────
float medirDistanciaCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duracao = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duracao == 0) return 9999.0;     // sem eco = nada na frente
  return duracao / 58.0;
}

void loop() {
  float distancia = medirDistanciaCm();

  dados.tipo = MSG_DADOS;
  dados.id = ID_ENTRADA;
  dados.distancia = distancia;


  Pacote rts;
  rts.tipo = MSG_RTS;
  rts.id = dados.id;
  radio.stopListening();

  delayMicroseconds(200);

  radio.flush_tx();
  radio.flush_rx();

  Serial.println("\nEnviando RTS...");

  bool okRTS = radio.write(&rts, sizeof(rts));

  if (!okRTS) {
    Serial.println("Falha RTS");
    delay(random(300, 1000));
    return;
  }


  // Esperando CTS
  radio.startListening();
  delayMicroseconds(200);
  // nao faz flush_rx aqui: o CTS pode ter chegado durante o write do RTS

  bool recebeuCTS = false;
  unsigned long inicio = millis();

  while (millis() - inicio < 1000) {
    if (radio.available()) {
      Pacote resposta;
      radio.read(&resposta, sizeof(resposta));

      if (resposta.tipo == MSG_CTS &&
          resposta.id == dados.id) {
        recebeuCTS = true;

        break;
      }
    }
  }

  if (!recebeuCTS) {
    Serial.println("Timeout CTS");
    radio.stopListening();
    delay(random(300, 1000));
    return;
  }

  Serial.println("CTS recebido");


  // Envia os Dados
  radio.stopListening();
  delayMicroseconds(200);
  radio.flush_tx();
  Serial.println("Enviando dados...");

  bool okDados = radio.write(&dados, sizeof(dados));

  if (!okDados) {
    Serial.println("Falha envio dados");
    delay(random(300, 1000));
    return;
  }


  // Esperando o ACK
  radio.startListening();
  delayMicroseconds(200);

  bool recebeuACK = false;
  inicio = millis();

  while (millis() - inicio < 1000) {
    if (radio.available()) {
      Pacote resposta;
      radio.read(&resposta, sizeof(resposta));

      if (resposta.tipo == MSG_ACK &&
          resposta.id == dados.id) {
        recebeuACK = true;
        break;
      }
    }
  }

  radio.stopListening();

  if (recebeuACK) {
    Serial.println("ACK recebido!");
    Serial.print("Distancia ENTRADA: ");
    Serial.print(distancia);
    Serial.println(" cm");

  } else {
    Serial.println("Timeout ACK");
  }

  // Leitura rapida (gente passa rapido), bem mais frequente que o sensor de solo
  delay(200 + random(0, 150));
}

#include <SPI.h>
#include "RF24.h"

// NRF24
#define CE_PIN 7
#define CSN_PIN 8

RF24 radio(CE_PIN, CSN_PIN);

// Tipos de mensagens para o MACAW
#define MSG_RTS   1
#define MSG_CTS   2
#define MSG_DADOS 3
#define MSG_ACK   4

// IDs das placas
#define ID_RECEPTOR 5
#define ID_ENTRADA  15
#define ID_SAIDA    47

// Distancia abaixo da qual consideramos que tem gente passando no sensor
#define LIMIAR_CM 15.0

// Struct Pacote - sem a variavel de controle (verif)
struct Pacote {
  uint8_t tipo;
  uint8_t id;
  float distancia;
};

Pacote pacote;
uint64_t address = 0x3030303030LL;

// ── Contadores esperados pelo app.py / index.html ──────────────────
// {"entradas":N,"saidas":N,"ocupacao":N,"distA":N,"distB":N,"pktOK":N,"pktErr":N}
long entradas = 0;
long saidas   = 0;
long pktOK    = 0;   // handshakes (CTS+ACK) que deram certo
long pktErr   = 0;   // falhas ao enviar CTS ou ACK

float distA = 9999.0;   // ultima distancia lida pelo no ENTRADA
float distB = 9999.0;   // ultima distancia lida pelo no SAIDA

// ── Variaveis de controle (debounce) para nao contar a mesma pessoa
//    em mais de uma leitura enquanto ela ainda esta na frente do sensor
bool presenteEntrada = false;
bool presenteSaida   = false;

// Buffer para ler comandos vindos do backend (ex: "RESET")
String cmdBuffer = "";


void enviaStatus() {
  long ocupacao = entradas - saidas;
  if (ocupacao < 0) ocupacao = 0;

  Serial.print("{\"entradas\":");
  Serial.print(entradas);
  Serial.print(",\"saidas\":");
  Serial.print(saidas);
  Serial.print(",\"ocupacao\":");
  Serial.print(ocupacao);
  Serial.print(",\"distA\":");
  Serial.print(distA, 1);
  Serial.print(",\"distB\":");
  Serial.print(distB, 1);
  Serial.print(",\"pktOK\":");
  Serial.print(pktOK);
  Serial.print(",\"pktErr\":");
  Serial.print(pktErr);
  Serial.println("}");
}

void enviaEvento(const char* tipo, long total) {
  Serial.print("{\"evento\":\"");
  Serial.print(tipo);
  Serial.print("\",\"total\":");
  Serial.print(total);
  Serial.println("}");
}

// ── Le comandos vindos do backend Python via Serial (ex: RESET) ─────
void verificaComandoSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      cmdBuffer.trim();
      if (cmdBuffer == "RESET") {
        entradas = 0;
        saidas = 0;
        pktOK = 0;
        pktErr = 0;
        Serial.println("{\"status\":\"contadores zerados\"}");
        enviaStatus();
      }
      cmdBuffer = "";
    } else {
      cmdBuffer += c;
    }
  }
}


void setup() {
  Serial.begin(115200);
  Serial.println("{\"status\":\"RECEPTOR CENTRAL (ID 5) iniciado\"}");

  if (!radio.begin()) {
    Serial.println("{\"erro\":\"NRF24 nao encontrado\"}");
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
  radio.startListening();
  delay(100);
}


void loop() {

  verificaComandoSerial();

  if (radio.available()) {
    radio.read(&pacote, sizeof(pacote));

    if (pacote.tipo == MSG_RTS) {

      Pacote cts;
      cts.tipo = MSG_CTS;
      cts.id = pacote.id;

      radio.stopListening();
      delayMicroseconds(200);
      radio.flush_tx();

      bool okCTS = radio.write(&cts, sizeof(cts));
      if (!okCTS) {
        pktErr++;
      }

      delayMicroseconds(200);
      radio.startListening();
      // nao limpa rx aqui: o DATA pode ter chegado enquanto mandava CTS
    }

    else if (pacote.tipo == MSG_DADOS) {

      // ── ENTRADA: incrementa "entradas" quando alguem entra no campo
      //    de visao do sensor (so uma vez por passagem, via debounce) ──
      if (pacote.id == ID_ENTRADA) {
        distA = pacote.distancia;

        if (pacote.distancia < LIMIAR_CM) {
          if (!presenteEntrada) {
            entradas++;
            enviaEvento("entrada", entradas);
          }
          presenteEntrada = true;
        } else {
          presenteEntrada = false;
        }
      }

      // ── SAIDA: incrementa "saidas" da mesma forma ──────────────────
      else if (pacote.id == ID_SAIDA) {
        distB = pacote.distancia;

        if (pacote.distancia < LIMIAR_CM) {
          if (!presenteSaida) {
            saidas++;
            enviaEvento("saida", saidas);
          }
          presenteSaida = true;
        } else {
          presenteSaida = false;
        }
      }

      Pacote ack;
      ack.tipo = MSG_ACK;
      ack.id = pacote.id;

      radio.stopListening();
      delayMicroseconds(200);
      radio.flush_tx();

      bool okACK = radio.write(&ack, sizeof(ack));

      if (okACK) {
        pktOK++;
      } else {
        pktErr++;
      }

      delayMicroseconds(200);
      radio.startListening();
      radio.flush_rx();

      // Status completo a cada transacao, no formato que o app.py espera
      enviaStatus();
    }
  }
}
